#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fcntl.h>
#include <stdexcept>
#include <string>
#include <sys/mman.h>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>

namespace stream_guard {

template <typename RingBufferT>
class ShmChannel {
private:
    static constexpr uint64_t kReadyMagic = 0x5354524D47524431ULL; // "STRMGRD1"

    struct alignas(64) ShmEnvelope {
        alignas(64) std::atomic<uint64_t> magic{0};
        alignas(64) RingBufferT ring{};
    };

    std::string shm_name_;
    int fd_{-1};
    bool is_owner_{false};
    ShmEnvelope* env_{nullptr};

public:
    ShmChannel(std::string name, bool create_and_own)
        : shm_name_(std::move(name)), is_owner_(create_and_own) {
        const int flags = create_and_own ? (O_CREAT | O_RDWR) : O_RDWR;
        fd_ = ::shm_open(shm_name_.c_str(), flags, 0666);
        if (fd_ < 0) {
            throw std::runtime_error("shm_open failed for " + shm_name_);
        }
        if (create_and_own) {
            if (::ftruncate(fd_, static_cast<off_t>(sizeof(ShmEnvelope))) != 0) {
                ::close(fd_);
                ::shm_unlink(shm_name_.c_str());
                throw std::runtime_error("ftruncate failed for " + shm_name_);
            }
        } else {
            struct stat st{};
            for (int retry = 0; retry < 200; ++retry) {
                if (::fstat(fd_, &st) == 0 && static_cast<size_t>(st.st_size) == sizeof(ShmEnvelope)) {
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }

        void* mapped = ::mmap(nullptr, sizeof(ShmEnvelope), PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0);
        if (mapped == MAP_FAILED) {
            ::close(fd_);
            if (create_and_own) ::shm_unlink(shm_name_.c_str());
            throw std::runtime_error("mmap failed for " + shm_name_);
        }

        if (create_and_own) {
            env_ = new (mapped) ShmEnvelope();
            env_->magic.store(kReadyMagic, std::memory_order_release);
        } else {
            env_ = static_cast<ShmEnvelope*>(mapped);
            while (env_->magic.load(std::memory_order_acquire) != kReadyMagic) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
    }

    ~ShmChannel() noexcept {
        if (env_ != nullptr) {
            if (is_owner_) {
                env_->magic.store(0ULL, std::memory_order_release);
            }
            ::munmap(static_cast<void*>(env_), sizeof(ShmEnvelope));
        }
        if (fd_ >= 0) {
            ::close(fd_);
        }
        if (is_owner_) {
            ::shm_unlink(shm_name_.c_str());
        }
    }

    ShmChannel(const ShmChannel&) = delete;
    ShmChannel& operator=(const ShmChannel&) = delete;

    [[nodiscard]] RingBufferT& ring() noexcept { return env_->ring; }
    [[nodiscard]] const RingBufferT& ring() const noexcept { return env_->ring; }
};

} // namespace stream_guard