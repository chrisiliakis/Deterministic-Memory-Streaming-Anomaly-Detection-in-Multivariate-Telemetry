# StreamGuard: Zero-Allocation C++20 Streaming Anomaly Detection Engine

`StreamGuard` is a header-first C++20 telemetry sketching and anomaly detection engine designed for real-time autonomous systems (avionics, robotics) and high-throughput Linux infrastructure. It combines **Takens' Phase-Space Delay Embedding**, **Robust Random Cut Forests (RRCF)**, and **Merging `t-digest` quantile sketches** over a wait-free **POSIX Shared Memory (`shm_open`/`mmap`) SPSC Ring Buffer**—enforcing **strictly zero dynamic heap allocations (`malloc`/`new`) in the critical streaming loop**.

## Key Systems & Algorithmic Features
- **Zero-Allocation Hot Path:** Dynamic tree insertions and evictions are backed by an $O(1)$ index-addressed `FixedSlabPool<Node, 2 * WindowSize>` using 32-bit array indices (`uint32_t`) instead of 64-bit heap pointers. Verified in CI by overriding global `operator new`.
- **Wait-Free IPC Ingestion:** Cache-line aligned (`alignas(64)`) Single-Producer Single-Consumer ring buffer using `std::atomic` acquire/release semantics with local index caching to eliminate false sharing across CPU cores.
- **Phase-Space Shingling:** Lifts $d$-channel sensor streams $x_t \in \mathbb{R}^d$ into an $(m \cdot d)$-dimensional Takens delay-coordinate vector to expose frequency shifts, control-surface flutter, and cross-channel phase anomalies.
- **Robust Random Cut Forest (Guha et al., ICML 2016):** Maintains dynamic bounding-box trees in $O(\text{depth})$ time per sliding-window update and scores multivariate anomalies via **Collusive Displacement (`CoDisp`)**.
- **Adaptive Tail Thresholding:** Streams `CoDisp` scores through a fixed-memory exponentially decayed **Merging `t-digest`** using the arcsine scale function $k_1(q)$ to track the live p99.5 anomaly cutoff.

## Quick Start (Build, Test & Benchmark)

```bash
mkdir -p build && cd build
cmake .. -DSTREAM_GUARD_ENABLE_ASAN=ON
cmake --build . -j$(nproc)

# Run unit tests & zero-heap-allocation proof
ctest --output-on-failure

# Run Google Benchmark throughput & latency suite
./bench_pipeline