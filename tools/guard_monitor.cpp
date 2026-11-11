#include "stream_guard/core/shm_channel.hpp"
#include "stream_guard/core/spsc_ring_buffer.hpp"
#include "stream_guard/detectors/detector_concept.hpp"
#include "stream_guard/detectors/random_cut_forest.hpp"
#include <chrono>
#include <csignal>
#include <iomanip>
#include <iostream>
#include <memory>

namespace {
volatile std::sig_atomic_t g_running = 1;
void handle_sigint(int) { g_running = 0; }
} // namespace

int main() {
    std::signal(SIGINT, handle_sigint);
    using Frame = stream_guard::TelemetryFrame<3>;
    using Ring = stream_guard::SpscRingBuffer<Frame, 4096>;
    using Forest = stream_guard::RandomCutForest<3, 4, 32, 256>;
    static_assert(stream_guard::StreamingDetector<Forest, 3>);

    auto detector = std::make_unique<Forest>(42ULL, 0.995);
    stream_guard::ShmChannel<Ring> channel("/stream_guard_telemetry", false);
    std::cout << "[guard_monitor] Connected to /stream_guard_telemetry. Monitoring stream...\n";

    Frame frame{};
    while (g_running) {
        if (channel.ring().try_pop(frame)) {
            const auto t0 = std::chrono::steady_clock::now();
            const auto res = detector->process_frame(frame);
            const auto t1 = std::chrono::steady_clock::now();
            const double proc_us =
                std::chrono::duration<double, std::micro>(t1 - t0).count();

            if (res.is_anomaly) {
                std::cout << "[ANOMALY] seq=" << std::setw(7) << res.sequence_id
                          << " | CoDisp=" << std::fixed << std::setprecision(2) << res.codisp_score
                          << " (thresh=" << res.adaptive_threshold << ")"
                          << " | proc_latency=" << proc_us << " us\n";
            } else if (res.sequence_id % 1000 == 0 && res.is_warmed_up) {
                std::cout << "[NOMINAL] seq=" << std::setw(7) << res.sequence_id
                          << " | CoDisp=" << std::fixed << std::setprecision(2) << res.codisp_score
                          << " (thresh=" << res.adaptive_threshold << ")"
                          << " | proc_latency=" << proc_us << " us\n";
            }
        }
    }
    return 0;
}