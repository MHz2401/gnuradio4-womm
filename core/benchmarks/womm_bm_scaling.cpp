// Does aggregate throughput scale with the number of independent parallel DSP chains?
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
//   - Per-chain work is held constant, so aggregate = nChains * per-chain rate.
//
// Three properties this harness has and its first version did not:
//
//   1. --chains and --threads are INDEPENDENT. The first version sized the pool
//      to nChains, so every point varied workload and parallel decomposition at
//      once, and the 1-chain point was a different regime entirely: one job, in
//      topological order, with no cross-thread edge. Every speedup was quoted
//      against that. Splitting them is what makes (1 chain, 2 threads) - the
//      cell that isolates partitioning cost from workload - expressible at all.
//
//   2. Throughput is measured over a STEADY-STATE WINDOW, not total elapsed
//      time. Sources run unbounded and the rate is the sink-count delta between
//      two instants well after start-up. A windowed rate cannot include graph
//      construction, so the setup-dominated error that once put the ceiling out
//      by ~13x is impossible here by construction rather than by care. It also
//      removes the ramp-down bias of finite sources, where chains retire at
//      different times and the tail systematically penalises high chain counts.
//
//   3. ONE CELL PER PROCESS. The pool is sized once, before any graph exists,
//      so replacing a live pool can never leak workers into a later point.
//      Repeats, interleaving, ABA bracketing and statistics belong to the
//      driver (scripts/womm-scaling-sweep.sh), which can reorder cells without
//      a rebuild - and interleaving is what turns thermal drift from a bias
//      into noise that a median removes.
//
// Output is one machine-readable key=value line per run.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <print>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <sys/resource.h>
#include <unistd.h>

#include <mach/mach.h> // live thread count, to detect pool leaks

#include <gnuradio-4.0/Graph.hpp>
#include <gnuradio-4.0/Scheduler.hpp>
#include <gnuradio-4.0/thread/thread_pool.hpp>

#include <gnuradio-4.0/math/Math.hpp>
#include <gnuradio-4.0/testing/NullSources.hpp>

using T = float;

inline constexpr double kB210MaxRate = 61.44e6; // samples/s, one B210 at 1x1

struct Config {
    std::size_t chains    = 1UZ;
    std::size_t threads   = 0UZ; // 0 -> same as chains
    std::size_t depth     = 8UZ; // multiply/divide pairs per chain
    gr::Size_t  buffer    = 65536U;
    double      warmupSec = 1.0;
    double      windowSec = 10.0;
    bool        roundRobin = false; // emplace stage-major across chains (the E2c control)
};

struct Result {
    double      msps          = 0.0;
    double      windowSec     = 0.0;
    double      coresUsed     = 0.0;
    std::size_t liveThreads   = 0UZ;
    bool        wedged        = false;
    bool        countOverflow = false;
};

void mustConnect(auto&& result, std::string_view what) {
    if (!result.has_value()) {
        std::println(stderr, "connect failed: {}", what);
        std::exit(1);
    }
}

std::size_t liveThreads() {
    mach_msg_type_number_t n = 0;
    thread_act_array_t     list{};
    if (task_threads(mach_task_self(), &list, &n) != KERN_SUCCESS) {
        return 0UZ;
    }
    vm_deallocate(mach_task_self(), reinterpret_cast<vm_address_t>(list), n * sizeof(thread_act_t));
    return static_cast<std::size_t>(n);
}

// Sized once, before any graph exists, so no live pool is ever replaced.
void sizeCpuPool(std::uint32_t nThreads) {
    using namespace gr::thread_pool;
    auto pool = std::make_shared<ThreadPoolWrapper>(std::make_unique<BasicThreadPool>(std::string(kDefaultCpuPoolId), TaskType::CPU_BOUND, nThreads, nThreads), "CPU");
    Manager::instance().replacePool(std::string(kDefaultCpuPoolId), std::move(pool));
}

double cpuSecondsUsed() {
    rusage usage{};
    getrusage(RUSAGE_SELF, &usage);
    const auto toSec = [](const timeval& tv) { return static_cast<double>(tv.tv_sec) + 1e-6 * static_cast<double>(tv.tv_usec); };
    return toSec(usage.ru_utime) + toSec(usage.ru_stime);
}

// Blocks are emplaced in one of two orders over an identical topology. The
// scheduler partitions by emplacement index (Scheduler.hpp:1481), so the order
// alone decides which blocks share a worker - which is exactly the variable
// under test, and the reason a partitioning win must be re-checked under the
// round-robin order before it can be believed.
struct Chains {
    std::vector<gr::testing::CountingSink<T>*> sinks;
};

Chains buildGraph(gr::Graph& graph, const Config& cfg) {
    using namespace gr::blocks::math;
    using namespace std::string_literals;

    std::vector<gr::testing::ConstantSource<T>*>       srcs(cfg.chains);
    std::vector<std::vector<MultiplyConst<T>*>>        muls(cfg.chains, std::vector<MultiplyConst<T>*>(cfg.depth));
    std::vector<std::vector<DivideConst<T>*>>          divs(cfg.chains, std::vector<DivideConst<T>*>(cfg.depth));
    std::vector<gr::testing::CountingSink<T>*>         sinks(cfg.chains);

    const auto emplaceSrc  = [&](std::size_t c) { srcs[c] = std::addressof(graph.emplaceBlock<gr::testing::ConstantSource<T>>({{"n_samples_max", gr::Size_t{0}}, {"name", std::format("src.{}", c)}})); };
    const auto emplaceMul  = [&](std::size_t c, std::size_t i) { muls[c][i] = std::addressof(graph.emplaceBlock<MultiplyConst<T>>({{"value", T(2)}, {"name", std::format("mul.{}.{}", c, i)}})); };
    const auto emplaceDiv  = [&](std::size_t c, std::size_t i) { divs[c][i] = std::addressof(graph.emplaceBlock<DivideConst<T>>({{"value", T(2)}, {"name", std::format("div.{}.{}", c, i)}})); };
    const auto emplaceSink = [&](std::size_t c) { sinks[c] = std::addressof(graph.emplaceBlock<gr::testing::CountingSink<T>>({{"n_samples_max", gr::Size_t{0}}, {"name", std::format("sink.{}", c)}})); };

    if (cfg.roundRobin) {
        for (std::size_t c = 0UZ; c < cfg.chains; ++c) {
            emplaceSrc(c);
        }
        for (std::size_t i = 0UZ; i < cfg.depth; ++i) {
            for (std::size_t c = 0UZ; c < cfg.chains; ++c) {
                emplaceMul(c, i);
            }
            for (std::size_t c = 0UZ; c < cfg.chains; ++c) {
                emplaceDiv(c, i);
            }
        }
        for (std::size_t c = 0UZ; c < cfg.chains; ++c) {
            emplaceSink(c);
        }
    } else {
        for (std::size_t c = 0UZ; c < cfg.chains; ++c) {
            emplaceSrc(c);
            for (std::size_t i = 0UZ; i < cfg.depth; ++i) {
                emplaceMul(c, i);
                emplaceDiv(c, i);
            }
            emplaceSink(c);
        }
    }

    for (std::size_t c = 0UZ; c < cfg.chains; ++c) {
        for (std::size_t i = 0UZ; i < cfg.depth; ++i) {
            if (i == 0UZ) {
                mustConnect(graph.connect(*srcs[c], "out"s, *muls[c][0], "in"s, {.minBufferSize = cfg.buffer}), "src->mul");
            } else {
                mustConnect(graph.connect(*divs[c][i - 1UZ], "out"s, *muls[c][i], "in"s, {.minBufferSize = cfg.buffer}), "div->mul");
            }
            mustConnect(graph.connect(*muls[c][i], "out"s, *divs[c][i], "in"s, {.minBufferSize = cfg.buffer}), "mul->div");
        }
        mustConnect(graph.connect(*divs[c][cfg.depth - 1UZ], "out"s, *sinks[c], "in"s, {.minBufferSize = cfg.buffer}), "div->sink");
    }

    return {std::move(sinks)};
}

// count is gr::Size_t == uint32_t and wraps at ~4.29e9. Taking the difference in
// uint32 first is modular arithmetic and therefore CORRECT across a wrap, so
// long as fewer than 2^32 samples pass through one sink inside one window.
// At ~200 Msps per chain a 10 s window is ~2e9, half the margin; the caller
// still flags anything above 3.5e9 rather than trusting that silently.
double sumCounts(const std::vector<gr::testing::CountingSink<T>*>& sinks) {
    double total = 0.0;
    for (const auto* sink : sinks) {
        total += static_cast<double>(sink->count.value);
    }
    return total;
}

Result measure(const Config& cfg) {
    gr::Graph    graph;
    const Chains chains = buildGraph(graph, cfg);

    gr::scheduler::Simple<gr::scheduler::ExecutionPolicy::multiThreaded> sched;
    if (auto ret = sched.exchange(std::move(graph)); !ret) {
        std::println(stderr, "scheduler init failed: {}", ret.error());
        std::exit(1);
    }

    Result            result;
    std::atomic<bool> ok{true};
    std::thread       runner([&] { ok.store(sched.runAndWait().has_value(), std::memory_order_relaxed); });

    std::this_thread::sleep_for(std::chrono::duration<double>(cfg.warmupSec));

    const double                                cpuAtStart = cpuSecondsUsed();
    const std::chrono::steady_clock::time_point t1         = std::chrono::steady_clock::now();
    std::vector<gr::Size_t>                     countAtStart(chains.sinks.size());
    std::ranges::transform(chains.sinks, countAtStart.begin(), [](const auto* s) { return s->count.value; });

    std::this_thread::sleep_for(std::chrono::duration<double>(cfg.windowSec));

    double total = 0.0;
    for (std::size_t i = 0UZ; i < chains.sinks.size(); ++i) {
        const gr::Size_t delta = static_cast<gr::Size_t>(chains.sinks[i]->count.value - countAtStart[i]); // modular: wrap-correct
        if (delta > 3'500'000'000U) {
            result.countOverflow = true;
        }
        total += static_cast<double>(delta);
    }
    const std::chrono::steady_clock::time_point t2      = std::chrono::steady_clock::now();
    const double                                cpuUsed = cpuSecondsUsed() - cpuAtStart;

    result.windowSec   = std::chrono::duration<double>(t2 - t1).count();
    result.msps        = total / result.windowSec / 1e6;
    result.coresUsed   = cpuUsed / result.windowSec;
    result.liveThreads = liveThreads();

    sched.requestStop();

    // Hard backstop. The previous version tested runner.joinable(), which stays
    // true until join() is actually called - so that term never ended the loop -
    // and then called join() unconditionally, which blocks forever on a wedged
    // graph. A backstop that can hang is not a backstop. Poll the scheduler
    // state instead, and on timeout DETACH and leave: the process-level alarm()
    // in main() is the layer a spinning worker cannot defeat.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (std::chrono::steady_clock::now() < deadline) {
        const gr::lifecycle::State state = sched.state();
        if (state == gr::lifecycle::State::STOPPED || state == gr::lifecycle::State::IDLE || state == gr::lifecycle::State::ERROR) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    if (sched.state() == gr::lifecycle::State::STOPPED || sched.state() == gr::lifecycle::State::IDLE) {
        runner.join();
    } else {
        result.wedged = true;
        runner.detach();
    }

    if (!ok.load(std::memory_order_relaxed)) {
        result.wedged = true;
    }
    return result;
}

[[noreturn]] void usage() {
    std::println(stderr, "usage: womm_bm_scaling [--chains N] [--threads N] [--depth N] [--buffer N]");
    std::println(stderr, "                       [--warmup SEC] [--window SEC] [--round-robin]");
    std::println(stderr, "one cell per process; repeats and statistics belong to the driver script");
    std::exit(2);
}

int main(int argc, char* argv[]) {
    Config cfg;

    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        const auto             next = [&]() -> const char* {
            if (i + 1 >= argc) {
                usage();
            }
            return argv[++i];
        };
        if (arg == "--chains") {
            cfg.chains = static_cast<std::size_t>(std::atol(next()));
        } else if (arg == "--threads") {
            cfg.threads = static_cast<std::size_t>(std::atol(next()));
        } else if (arg == "--depth") {
            cfg.depth = static_cast<std::size_t>(std::atol(next()));
        } else if (arg == "--buffer") {
            cfg.buffer = static_cast<gr::Size_t>(std::atol(next()));
        } else if (arg == "--warmup") {
            cfg.warmupSec = std::atof(next());
        } else if (arg == "--window") {
            cfg.windowSec = std::atof(next());
        } else if (arg == "--round-robin") {
            cfg.roundRobin = true;
        } else {
            usage();
        }
    }
    if (cfg.threads == 0UZ) {
        cfg.threads = cfg.chains;
    }
    if (cfg.chains == 0UZ || cfg.depth == 0UZ) {
        usage();
    }

    // Last-resort backstop: a spinning worker cannot defeat SIGALRM.
    alarm(static_cast<unsigned>(cfg.warmupSec + cfg.windowSec) + 90U);

    sizeCpuPool(static_cast<std::uint32_t>(cfg.threads));
    const Result result = measure(cfg);

    std::println("chains={} threads={} depth={} buffer={} emplace={} window_s={:.3f} msps={:.2f} xB210={:.2f} cores_used={:.2f} live_threads={} verdict={}", //
        cfg.chains, cfg.threads, cfg.depth, cfg.buffer, cfg.roundRobin ? "round-robin" : "chain-major", result.windowSec, result.msps, result.msps * 1e6 / kB210MaxRate, result.coresUsed, result.liveThreads,
        result.wedged ? "WEDGED" : (result.countOverflow ? "OVERFLOW" : (result.coresUsed > static_cast<double>(cfg.threads) * 1.1 + 1.0 ? "SPINNING" : "OK")));

    return result.wedged ? 1 : 0;
}
