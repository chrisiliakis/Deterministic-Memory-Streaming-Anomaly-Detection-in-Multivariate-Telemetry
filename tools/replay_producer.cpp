#include "stream_guard/core/normalizer.hpp"
#include "stream_guard/core/shm_channel.hpp"
#include "stream_guard/core/spsc_ring_buffer.hpp"
#include "stream_guard/core/types.hpp"
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <fcntl.h>
#include <iostream>
#include <sys/mman.h>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>

namespace {
volatile std::sig_atomic_t g_running = 1;
void handle_sigint(int) { g_running = 0; }
} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <path_to_3channel_telemetry.csv>\n";
        return 1;
    }

    std::signal(SIGINT, handle_sigint);
    const int fd = ::open(argv[1], O_RDONLY);
    if (fd < 0) {
        std::cerr << "[replay_producer] Failed to open CSV file: " << argv[1] << "\n";
        return 1;
    }

    struct stat st{};
    if (::fstat(fd, &st) != 0 || st.st_size <= 0) {
        ::close(fd);
        std::cerr << "[replay_producer] Invalid or empty CSV file.\n";
        return 1;
    }

    const size_t file_size = static_cast<size_t>(st.st_size);
    const char* data = static_cast<const char*>(::mmap(nullptr, file_size, PROT_READ, MAP_PRIVATE, fd, 0));
    if (data == MAP_FAILED) {
        ::close(fd);
        std::cerr << "[replay_producer] mmap failed.\n";
        return 1;
    }

    using Frame = stream_guard::TelemetryFrame<3>;
    using Ring = stream_guard::SpscRingBuffer<Frame, 4096>;
    stream_guard::ShmChannel<Ring> channel("/stream_guard_telemetry", true);
    stream_guard::OnlineNormalizer<3> normalizer(256, true);

    std::cout << "[replay_producer] Streaming memory-mapped CSV (" << file_size << " bytes)...\n";

    const char* cur = data;
    const char* end = data + file_size;
    uint64_t seq = 0;

    while (cur < end && g_running) {
        // Skip header or comment lines starting with alphabetic characters or '#'
        if ((*cur >= 'a' && *cur <= 'z') || (*cur >= 'A' && *cur <= 'Z') || *cur == '#') {
            while (cur < end && *cur != '\n') ++cur;
            if (cur < end) ++cur;
            continue;
        }

        char* next = nullptr;
        const float c0 = std::strtof(cur, &next);
        if (next == cur) break;
        cur = (*next == ',') ? next + 1 : next;

        const float c1 = std::strtof(cur, &next);
        cur = (*next == ',') ? next + 1 : next;

        const float c2 = std::strtof(cur, &next);
        cur = next;
        while (cur < end && *cur != '\n') ++cur;
        if (cur < end) ++cur;

        const auto norm_channels = normalizer.normalize({c0, c1, c2});
        const auto now_ns = static_cast<uint64_t>(
            std::chrono::steady_clock::now().time_since_epoch().count());
        Frame frame{now_ns, seq, norm_channels};

        while (g_running && !channel.ring().try_push(frame)) {
            std::this_thread::sleep_for(std::chrono::microseconds(50));
        }
        ++seq;
        std::this_thread::sleep_for(std::chrono::microseconds(200));
    }

    ::munmap(const_cast<char*>(data), file_size);
    ::close(fd);
    std::cout << "[replay_producer] Finished streaming " << seq << " frames.\n";
    return 0;
}