// Phase 3.5 discriminating experiment: does aggregate throughput scale with the
// number of independent parallel DSP chains?
//
// This is a NEW benchmark rather than a modification of bm_Scheduler.cpp, which
// hard-pins the CPU pool to 2 threads (bm_Scheduler.cpp:92) and therefore cannot
// observe a machine with more than 2 cores. Keeping that file untouched holds
// our drift inside gnuradio4 to a single line in this directory's CMakeLists.
//
// Design constraints, from the Phase 0 findings:
//   - PURE FEEDFORWARD. docs/USER_API_Connecting_Blocks.md documents a ~100x
//     cliff for any feedback edge (one sample per scheduler cycle); a feedback
//     path here would mask everything else.
//   - Chains are INDEPENDENT, so ideal scaling is linear in chain count.
//   - The CPU pool is sized per data point; n_batches = min(maxThreads, nBlocks)
//     (Scheduler.hpp:1368), so the pool is what actually caps parallelism.
//   - Per-chain work is held constant, so aggregate = nChains * N_SAMPLES.
//
// Reports median and half-spread over N_REPEAT runs, plus throughput as a
// multiple of real device rate. Raw synthetic ops/s is not the yardstick: a
// B210 tops out near 61.44 MS/s.

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <print>
#include <string>
#include <vector>

#include <gnuradio-4.0/Graph.hpp>
#include <gnuradio-4.0/Scheduler.hpp>
#include <gnuradio-4.0/thread/thread_pool.hpp>

#include <gnuradio-4.0/math/Math.hpp>
#include <gnuradio-4.0/testing/NullSources.hpp>

using T = float;

inline constexpr gr::Size_t  kSamplesPerChain = 1'000'000U;
inline constexpr std::size_t kDepth           = 8UZ;   // Multiply/Divide pairs per chain
inline constexpr gr::Size_t  kBufferSize      = 65536U;
inline constexpr std::size_t kRepeat          = 7UZ;   // odd, so the median is a real sample
inline constexpr double      kB210MaxRate     = 61.44e6; // samples/s, B210 ceiling

void mustConnect(auto&& result, std::string_view what) {
    if (!result.has_value()) {
        std::println(stderr, "connect failed: {}", what);
        std::exit(1);
    }
}

// One independent feedforward chain: source -> (mult,div)*depth -> sink
void addChain(gr::Graph& graph, std::size_t chainId) {
    using namespace gr::blocks::math;
    using namespace std::string_literals;

    auto&           src  = graph.emplaceBlock<gr::testing::ConstantSource<T>>({{"n_samples_max", kSamplesPerChain}, {"name", std::format("src.{}", chainId)}});
    DivideConst<T>* last = nullptr;

    for (std::size_t i = 0UZ; i < kDepth; ++i) {
        auto& mul = graph.emplaceBlock<MultiplyConst<T>>({{"value", T(2)}, {"name", std::format("mul.{}.{}", chainId, i)}});
        auto& div = graph.emplaceBlock<DivideConst<T>>({{"value", T(2)}, {"name", std::format("div.{}.{}", chainId, i)}});

        if (i == 0UZ) {
            mustConnect(graph.connect(src, "out"s, mul, "in"s, {.minBufferSize = kBufferSize}), "src->mul");
        } else {
            mustConnect(graph.connect(*last, "out"s, mul, "in"s, {.minBufferSize = kBufferSize}), "div->mul");
        }
        mustConnect(graph.connect(mul, "out"s, div, "in"s, {.minBufferSize = kBufferSize}), "mul->div");
        last = std::addressof(div);
    }

    auto& sink = graph.emplaceBlock<gr::testing::NullSink<T>>({{"name", std::format("sink.{}", chainId)}});
    mustConnect(graph.connect(*last, "out"s, sink, "in"s, {.minBufferSize = kBufferSize}), "div->sink");
}

void sizeCpuPool(std::uint32_t nThreads) {
    using namespace gr::thread_pool;
    auto pool = std::make_shared<ThreadPoolWrapper>(std::make_unique<BasicThreadPool>(std::string(kDefaultCpuPoolId), TaskType::CPU_BOUND, nThreads, nThreads), "CPU");
    Manager::instance().replacePool(std::string(kDefaultCpuPoolId), std::move(pool));
}

double runOnce(std::size_t nChains) {
    gr::Graph graph;
    for (std::size_t c = 0UZ; c < nChains; ++c) {
        addChain(graph, c);
    }

    gr::scheduler::Simple<gr::scheduler::ExecutionPolicy::multiThreaded> sched;
    if (auto ret = sched.exchange(std::move(graph)); !ret) {
        std::println(stderr, "scheduler init failed: {}", ret.error());
        std::exit(1);
    }

    const auto t0 = std::chrono::steady_clock::now();
    if (auto ret = sched.runAndWait(); !ret) {
        std::println(stderr, "scheduler run failed: {}", ret.error());
        std::exit(1);
    }
    const auto t1 = std::chrono::steady_clock::now();
    return std::chrono::duration<double>(t1 - t0).count();
}

int main() {
    std::println("womm parallel-chain scaling");
    std::println("hardware_concurrency = {}, depth = {} mul/div pairs, {} samples/chain, {} runs/point",
                 std::thread::hardware_concurrency(), kDepth, kSamplesPerChain, kRepeat);
    std::println("");
    std::println("{:>7}  {:>8}  {:>11}  {:>9}  {:>8}  {:>10}", "chains", "threads", "median_s", "spread_s", "Msps", "xB210");

    double baseline = 0.0;
    for (std::size_t nChains : {1UZ, 2UZ, 4UZ, 8UZ, 16UZ}) {
        sizeCpuPool(static_cast<std::uint32_t>(nChains));

        std::vector<double> times;
        times.reserve(kRepeat);
        for (std::size_t r = 0UZ; r < kRepeat; ++r) {
            times.push_back(runOnce(nChains));
        }
        std::ranges::sort(times);
        const double median = times[times.size() / 2UZ];
        const double spread = 0.5 * (times.back() - times.front());

        const double totalSamples = static_cast<double>(nChains) * static_cast<double>(kSamplesPerChain);
        const double msps         = totalSamples / median / 1e6;
        if (nChains == 1UZ) {
            baseline = msps;
        }

        std::println("{:>7}  {:>8}  {:>11.4f}  {:>9.4f}  {:>8.1f}  {:>9.2f}x", nChains, nChains, median, spread, msps, msps * 1e6 / kB210MaxRate);
    }

    std::println("");
    std::println("ideal scaling would be linear in chain count; a plateau means the");
    std::println("runtime is the ceiling, not the DSP work. baseline 1-chain = {:.1f} Msps", baseline);
    return 0;
}
