#include "stream_guard/detectors/random_cut_forest.hpp"
#include <benchmark/benchmark.h>
#include <cmath>
#include <memory>

static void BM_RandomCutForest_ProcessFrame(benchmark::State& state) {
    using Forest = stream_guard::RandomCutForest<3, 4, 32, 256>;
    auto forest = std::make_unique<Forest>(42ULL, 0.995);
    uint64_t seq = 0;

    for (auto _ : state) {
        const float t = static_cast<float>(seq) * 0.05f;
        stream_guard::TelemetryFrame<3> frame{
            seq * 1000ULL, seq, {std::sin(t), std::cos(t), 0.5f * std::sin(2.0f * t)}};
        auto res = forest->process_frame(frame);
        benchmark::DoNotOptimize(res);
        ++seq;
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_RandomCutForest_ProcessFrame);
BENCHMARK_MAIN();