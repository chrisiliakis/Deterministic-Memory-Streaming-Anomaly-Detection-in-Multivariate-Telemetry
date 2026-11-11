#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace stream_guard {

inline constexpr uint32_t kNullIdx = 0xFFFFFFFFU;

template <size_t Channels>
struct alignas(32) TelemetryFrame {
    uint64_t timestamp_ns{0};
    uint64_t sequence_id{0};
    std::array<float, Channels> channels{};
};

struct AnomalyResult {
    uint64_t sequence_id{0};
    float codisp_score{0.0f};
    float adaptive_threshold{0.0f};
    bool is_anomaly{false};
    bool is_warmed_up{false};
};

} // namespace stream_guard