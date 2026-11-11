#include "alloc_guard.hpp"
#include "stream_guard/core/normalizer.hpp"
#include "stream_guard/core/spsc_ring_buffer.hpp"
#include "stream_guard/detectors/random_cut_forest.hpp"
#include "stream_guard/sketches/reservoir.hpp"
#include "stream_guard/sketches/tdigest.hpp"
#include <atomic>
#include <cmath>
#include <gtest/gtest.h>
#include <memory>
#include <thread>

namespace stream_guard {

TEST(StreamGuardSuite, SpscRingBufferConcurrencyAndZeroAlloc) {
    auto queue = std::make_unique<SpscRingBuffer<TelemetryFrame<3>, 1024>>();
    constexpr size_t kNumFrames = 100000;
    std::atomic<bool> start_signal{false};

    std::jthread producer([&]() {
        while (!start_signal.load(std::memory_order_acquire)) {}
        testing::ScopedAllocGuard guard;
        for (size_t i = 0; i < kNumFrames; ++i) {
            TelemetryFrame<3> frame{i * 100ULL, i, {1.0f, 2.0f, 3.0f}};
            while (!queue->try_push(frame)) {}
        }
        EXPECT_EQ(guard.allocations_in_scope(), 0U);
    });

    std::jthread consumer([&]() {
        while (!start_signal.load(std::memory_order_acquire)) {}
        testing::ScopedAllocGuard guard;
        TelemetryFrame<3> out{};
        for (size_t i = 0; i < kNumFrames; ++i) {
            while (!queue->try_pop(out)) {}
            EXPECT_EQ(out.sequence_id, i);
        }
        EXPECT_EQ(guard.allocations_in_scope(), 0U);
    });

    start_signal.store(true, std::memory_order_release);
    producer.join();
    consumer.join();
}

TEST(StreamGuardSuite, DuplicatePointResilienceInRandomCutTree) {
    RandomCutTree<4, 64> tree;
    std::mt19937_64 rng(12345);
    std::array<uint32_t, 64> leaves{};
    EXPECT_TRUE(tree.verify_invariants());
    testing::ScopedAllocGuard guard;
    for (size_t i = 0; i < 64; ++i) {
        leaves[i] = tree.insert_point({5.0f, 5.0f, 5.0f, 5.0f}, rng);
    }
    EXPECT_EQ(tree.root_mass(), 64);
    EXPECT_EQ(tree.active_nodes(), 127U);
    EXPECT_TRUE(tree.verify_invariants());
    for (size_t i = 0; i < 64; ++i) {
        tree.delete_point(leaves[i]);
    }
    EXPECT_EQ(tree.root_mass(), 0);
    EXPECT_EQ(tree.active_nodes(), 0U);
    EXPECT_TRUE(tree.verify_invariants());
    EXPECT_EQ(guard.allocations_in_scope(), 0U);
}

TEST(StreamGuardSuite, TDigestTailQuantileAccuracyAndZeroAlloc) {
    TDigest<128, 256> digest(80.0);
    std::mt19937_64 rng(777ULL);
    std::uniform_real_distribution<float> dist(0.0f, 1000.0f);
    testing::ScopedAllocGuard guard;
    for (size_t i = 0; i < 50000; ++i) {
        digest.add(dist(rng));
    }
    const float p99 = digest.quantile(0.99);
    EXPECT_NEAR(p99, 990.0f, 15.0f);
    EXPECT_EQ(guard.allocations_in_scope(), 0U);
}

TEST(StreamGuardSuite, ZeroAllocHotPathAndAnomalyDetection) {
    auto forest = std::make_unique<RandomCutForest<2, 4, 32, 128>>(42ULL, 0.99);
    std::mt19937_64 noise_rng(999ULL);
    std::normal_distribution<float> noise(0.0f, 0.05f);
    testing::ScopedAllocGuard guard;
    float max_nominal_score = 0.0f;
    for (uint64_t t = 0; t < 400; ++t) {
        const float s1 = std::sin(0.1f * static_cast<float>(t)) + noise(noise_rng);
        const float s2 = std::cos(0.1f * static_cast<float>(t)) + noise(noise_rng);
        const auto res = forest->process_frame({t * 1000ULL, t, {s1, s2}});
        if (t > 200 && res.codisp_score > max_nominal_score) {
            max_nominal_score = res.codisp_score;
        }
    }
    const auto fault_res = forest->process_frame({400000ULL, 400, {12.5f, -10.0f}});
    EXPECT_EQ(guard.allocations_in_scope(), 0U);
    EXPECT_GT(fault_res.codisp_score, max_nominal_score * 2.0f);
    EXPECT_TRUE(fault_res.is_anomaly);
}

} // namespace stream_guard