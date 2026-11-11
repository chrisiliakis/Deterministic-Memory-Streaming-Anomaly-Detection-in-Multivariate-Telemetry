#pragma once

#include "stream_guard/core/slab_pool.hpp"
#include "stream_guard/core/types.hpp"
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <random>

namespace stream_guard {

template <size_t Dim, size_t MaxLeaves = 256>
class RandomCutTree {
    static_assert(Dim > 0 && MaxLeaves >= 4);

public:
    static constexpr size_t kMaxNodes = 2 * MaxLeaves;
    using Point = std::array<float, Dim>;

    struct Node {
        Point box_min{};
        Point box_max{};
        float cut_value{0.0f};
        uint32_t parent{kNullIdx};
        uint32_t left{kNullIdx};
        uint32_t right{kNullIdx};
        uint16_t mass{0};
        uint16_t cut_dim{0};

        [[nodiscard]] constexpr bool is_leaf() const noexcept {
            return left == kNullIdx && right == kNullIdx;
        }
    };

private:
    FixedSlabPool<Node, kMaxNodes> pool_{};
    uint32_t root_idx_{kNullIdx};

public:
    constexpr RandomCutTree() noexcept = default;

    constexpr void reset() noexcept {
        pool_.reset();
        root_idx_ = kNullIdx;
    }

    template <typename RNG>
    [[nodiscard]] uint32_t insert_point(const Point& pt, RNG& rng) noexcept {
        if (root_idx_ == kNullIdx) {
            const uint32_t leaf_idx = pool_.allocate();
            assert(leaf_idx != kNullIdx);
            Node& leaf = pool_[leaf_idx];
            leaf.box_min = pt;
            leaf.box_max = pt;
            leaf.mass = 1;
            root_idx_ = leaf_idx;
            return leaf_idx;
        }

        uint32_t curr_idx = root_idx_;
        while (true) {
            Node& curr = pool_[curr_idx];

            Point merged_min{};
            Point merged_max{};
            std::array<float, Dim> spans{};
            float span_sum = 0.0f;

            for (size_t i = 0; i < Dim; ++i) {
                merged_min[i] = std::min(curr.box_min[i], pt[i]);
                merged_max[i] = std::max(curr.box_max[i], pt[i]);
                spans[i] = merged_max[i] - merged_min[i];
                span_sum += spans[i];
            }

            uint16_t chosen_dim = 0;
            float cut_val = pt[0];
            bool separates = false;

            if (span_sum <= 1e-12f) {
                separates = curr.is_leaf();
            } else {
                std::uniform_real_distribution<float> dist(0.0f, span_sum);
                const float r = dist(rng);
                float accum = 0.0f;
                for (size_t i = 0; i < Dim; ++i) {
                    if (r <= accum + spans[i] || i == Dim - 1) {
                        chosen_dim = static_cast<uint16_t>(i);
                        const float offset = std::clamp(r - accum, 0.0f, spans[i]);
                        cut_val = merged_min[i] + offset;
                        break;
                    }
                    accum += spans[i];
                }
                separates = curr.is_leaf() ||
                            (cut_val < curr.box_min[chosen_dim]) ||
                            (cut_val > curr.box_max[chosen_dim]);
            }

            if (separates) {
                const uint32_t leaf_idx = pool_.allocate();
                const uint32_t parent_idx = pool_.allocate();
                assert(leaf_idx != kNullIdx && parent_idx != kNullIdx);

                Node& leaf = pool_[leaf_idx];
                leaf.box_min = pt;
                leaf.box_max = pt;
                leaf.mass = 1;
                leaf.parent = parent_idx;

                const uint32_t old_parent_idx = curr.parent;
                Node& parent = pool_[parent_idx];
                parent.box_min = merged_min;
                parent.box_max = merged_max;
                parent.cut_dim = chosen_dim;
                parent.mass = static_cast<uint16_t>(curr.mass + 1);
                parent.parent = old_parent_idx;

                const bool new_leaf_goes_left = (pt[chosen_dim] <= curr.box_min[chosen_dim]);
                if (new_leaf_goes_left) {
                    parent.left = leaf_idx;
                    parent.right = curr_idx;
                    parent.cut_value = std::min(cut_val, curr.box_min[chosen_dim]);
                } else {
                    parent.left = curr_idx;
                    parent.right = leaf_idx;
                    parent.cut_value = std::max(cut_val, curr.box_max[chosen_dim]);
                }

                curr.parent = parent_idx;
                if (old_parent_idx == kNullIdx) {
                    root_idx_ = parent_idx;
                } else {
                    Node& grand = pool_[old_parent_idx];
                    if (grand.left == curr_idx) {
                        grand.left = parent_idx;
                    } else {
                        grand.right = parent_idx;
                    }
                }
                return leaf_idx;
            }

            curr.box_min = merged_min;
            curr.box_max = merged_max;
            curr.mass = static_cast<uint16_t>(curr.mass + 1);
            curr_idx = (pt[curr.cut_dim] <= curr.cut_value) ? curr.left : curr.right;
        }
    }

    void delete_point(uint32_t leaf_idx) noexcept {
        assert(leaf_idx != kNullIdx);
        const Node& leaf = pool_[leaf_idx];
        assert(leaf.is_leaf());

        const uint32_t parent_idx = leaf.parent;
        if (parent_idx == kNullIdx) {
            assert(root_idx_ == leaf_idx);
            pool_.deallocate(leaf_idx);
            root_idx_ = kNullIdx;
            return;
        }

        const Node& parent = pool_[parent_idx];
        const uint32_t sibling_idx = (parent.left == leaf_idx) ? parent.right : parent.left;
        const uint32_t grand_idx = parent.parent;

        pool_[sibling_idx].parent = grand_idx;
        if (grand_idx == kNullIdx) {
            root_idx_ = sibling_idx;
        } else {
            Node& grand = pool_[grand_idx];
            if (grand.left == parent_idx) {
                grand.left = sibling_idx;
            } else {
                grand.right = sibling_idx;
            }
        }

        pool_.deallocate(leaf_idx);
        pool_.deallocate(parent_idx);

        uint32_t curr_idx = grand_idx;
        while (curr_idx != kNullIdx) {
            Node& curr = pool_[curr_idx];
            const Node& l_child = pool_[curr.left];
            const Node& r_child = pool_[curr.right];

            curr.mass = static_cast<uint16_t>(l_child.mass + r_child.mass);
            for (size_t i = 0; i < Dim; ++i) {
                curr.box_min[i] = std::min(l_child.box_min[i], r_child.box_min[i]);
                curr.box_max[i] = std::max(l_child.box_max[i], r_child.box_max[i]);
            }
            curr_idx = curr.parent;
        }
    }

    [[nodiscard]] float codisp(uint32_t leaf_idx) const noexcept {
        if (leaf_idx == kNullIdx || leaf_idx == root_idx_) {
            return 0.0f;
        }
        float max_disp = 0.0f;
        uint32_t curr_idx = leaf_idx;

        while (curr_idx != root_idx_ && curr_idx != kNullIdx) {
            const Node& curr = pool_[curr_idx];
            const uint32_t parent_idx = curr.parent;
            if (parent_idx == kNullIdx) break;

            const Node& parent = pool_[parent_idx];
            const uint32_t sibling_idx = (parent.left == curr_idx) ? parent.right : parent.left;
            const Node& sibling = pool_[sibling_idx];

            const float ratio = static_cast<float>(sibling.mass) / static_cast<float>(curr.mass);
            if (ratio > max_disp) {
                max_disp = ratio;
            }
            curr_idx = parent_idx;
        }
        return max_disp;
    }

    [[nodiscard]] uint32_t active_nodes() const noexcept { return pool_.active_count(); }
    [[nodiscard]] uint16_t root_mass() const noexcept {
        return (root_idx_ == kNullIdx) ? 0 : pool_[root_idx_].mass;
    }

    [[nodiscard]] bool verify_invariants() const noexcept {
        if (root_idx_ == kNullIdx) return pool_.active_count() == 0;
        return verify_node_recursive(root_idx_, kNullIdx);
    }

private:
    [[nodiscard]] bool verify_node_recursive(uint32_t idx, uint32_t expected_parent) const noexcept {
        if (idx == kNullIdx) return false;
        const Node& n = pool_[idx];
        if (n.parent != expected_parent) return false;
        if (n.is_leaf()) return n.mass == 1;

        const Node& l = pool_[n.left];
        const Node& r = pool_[n.right];
        if (n.mass != l.mass + r.mass) return false;

        for (size_t i = 0; i < Dim; ++i) {
            if (std::abs(n.box_min[i] - std::min(l.box_min[i], r.box_min[i])) > 1e-6f) return false;
            if (std::abs(n.box_max[i] - std::max(l.box_max[i], r.box_max[i])) > 1e-6f) return false;
        }
        return verify_node_recursive(n.left, idx) && verify_node_recursive(n.right, idx);
    }
};

} // namespace stream_guard