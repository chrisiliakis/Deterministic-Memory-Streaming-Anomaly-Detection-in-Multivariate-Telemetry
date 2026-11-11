#include "stream_guard/core/shm_channel.hpp"
#include "stream_guard/core/spsc_ring_buffer.hpp"
#include "stream_guard/core/types.hpp"
#include <chrono>
#include <cmath>
#include <csignal>
#include <iostream>
#include <random>
#include <thread>

namespace {
volatile std::sig_atomic_t g_running = 1;
void handle_sigint(int) { g_running = 0; }
} // namespace

int main() {
    std::signal(SIGINT, handle_sigint);
    using Frame = stream_guard::TelemetryFrame<3>;
    using Ring = stream_guard::SpscRingBuffer<Frame, 4096>;

    stream_guard::ShmChannel<Ring> channel("/stream_guard_telemetry", true);
    std::cout << "[synth_producer] Attached to /stream_guard_telemetry. Streaming 3-channel telemetry...\n";

    std::mt19937_64 rng(42ULL);
    std::normal_distribution<float> noise(0.0f, 0.04f);

    uint64_t seq = 0;
    while (g_running) {
        const double t = static_cast<double>(seq) * 0.01;
        float ch0 = static_cast<float>(std::sin(t)) + noise(rng);
        float ch1 = static_cast<float>(std::cos(t)) + noise(rng);
        float ch2 = static_cast<float>(0.5 * std::sin(2.0 * t)) + noise(rng);

        // Every 2,000 frames, inject a 15-frame parasitic flutter or spike anomaly
        if (seq > 500 && (seq % 2000 < 15)) {
            ch0 += 4.5f * static_cast<float>(std::sin(12.0 * t));
            ch2 -= 3.8f;
        }

        const auto now_ns = static_cast<uint64_t>(
            std::chrono::steady_clock::now().time_since_epoch().count());
        Frame frame{now_ns, seq, {ch0, ch1, ch2}};

        if (channel.ring().try_push(frame)) {
            ++seq;
        }
        std::this_thread::sleep_for(std::chrono::microseconds(200)); // 5 kHz live demo rate
    }
    std::cout << "\n[synth_producer] Shutting down cleanly.\n";
    return 0;
}