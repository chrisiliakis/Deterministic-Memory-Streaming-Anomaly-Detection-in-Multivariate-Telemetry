#pragma once

#include "stream_guard/core/types.hpp"
#include <concepts>
#include <cstddef>

namespace stream_guard {

template <typename DetectorT, size_t Channels>
concept StreamingDetector = requires(DetectorT det, const TelemetryFrame<Channels>& frame) {
    { det.process_frame(frame) } noexcept -> std::same_as<AnomalyResult>;
};

} // namespace stream_guard