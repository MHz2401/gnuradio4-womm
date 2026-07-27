// End-to-end rate sweep: real B210 -> DSP chain -> sink.
//
// The practical question this project actually has to answer: at what sample
// rate can this machine sustain a real radio through a real DSP graph without
// falling behind?
//
// Method: stream for a fixed wall time, then stop the graph explicitly and
// divide the samples the sink actually received by the elapsed time. That is
// the achieved rate. A radio source never finishes on its own, so a sink's
// n_samples_max cannot end the graph - the tests use a watchdog for the same
// reason (qa_SoapySource.cpp:335).
//
// Not a synthetic benchmark. Nothing here is meaningful without hardware.

#include <algorithm>
#include <atomic>
#include <thread>
#include <chrono>
#include <cstdlib>
#include <print>
#include <string>
#include <vector>

#include <unistd.h>

#include <gnuradio-4.0/Graph.hpp>
#include <gnuradio-4.0/Scheduler.hpp>

#include <gnuradio-4.0/math/Math.hpp>
#include <gnuradio-4.0/sdr/SoapySource.hpp>
#include <gnuradio-4.0/testing/NullSources.hpp>

using T = std::complex<float>; // what the radio actually delivers

inline std::size_t          kDepth        = 8UZ;   // argv[2] overrides; 0 = source straight to sink    // multiply/divide pairs, matches womm_bm_scaling
inline constexpr double      kDurationSec  = 8.0;    // per rate point
inline constexpr double      kCentreFreqHz = 2401e6; // legal for HAM and WiFi; safe default even if TX ever appears
inline constexpr double      kRxGainDb     = 20.0;  // B2xx RX2 tops out at 76 dB; 20 is a sane working value
inline constexpr double      kB210MaxRate  = 61.44e6;
inline bool                  kMinimalCfg   = false; // argv[4]=min -> device only
inline std::string           kDevice       = "uhd";  // argv[3] overrides (e.g. "loopback")

void mustConnect(auto&& r, std::string_view what) {
    if (!r.has_value()) {
        std::println(stderr, "  CONNECT FAILED: {}", what);
        std::exit(1);
    }
}

struct Point {
    double rateHz;
    double elapsedSec;
    double achievedHz;
    bool   completed;
};

Point runAtRate(double rateHz) {
    using namespace gr::blocks::math;
    using namespace gr::blocks::sdr;
    using namespace std::string_literals;

    gr::Graph graph;
    // control: "constant" swaps in a known-good source through the identical
    // harness, so a zero here indicts the measurement rather than the block
    if (kDevice == "constant") {
        auto& csrc = graph.emplaceBlock<gr::testing::ConstantSource<T>>({{"n_samples_max", gr::Size_t{0}}});
        auto& csink = graph.emplaceBlock<gr::testing::CountingSink<T>>();
        mustConnect(graph.connect(csrc, "out"s, csink, "in"s), "constant->sink");
        auto* cp = std::addressof(csink);
        gr::scheduler::Simple<gr::scheduler::ExecutionPolicy::multiThreaded> csched;
        if (auto r = csched.exchange(std::move(graph)); !r) { return {rateHz, 0.0, 0.0, false}; }
        const auto c0 = std::chrono::steady_clock::now();
        std::thread cr([&] { std::ignore = csched.runAndWait(); });
        std::this_thread::sleep_for(std::chrono::duration<double>(kDurationSec));
        csched.requestStop();
        cr.join();
        const double ce = std::chrono::duration<double>(std::chrono::steady_clock::now() - c0).count();
        return {rateHz, ce, static_cast<double>(cp->count) / ce, true};
    }

    // MINIMAL settings on purpose: each settings-driven reinitDevice() bumps
    // DeviceRegistry::pendingUsers, but registerActivation decrements it only
    // once, so extra acquisitions can leave activation permanently unfired.
    gr::property_map srcCfg{{"device", kDevice}};
    if (!kMinimalCfg) {
        srcCfg["num_channels"]       = gr::Size_t{1};
        srcCfg["sample_rate"]        = static_cast<float>(rateHz);
        srcCfg["frequency"]          = std::vector{kCentreFreqHz};
        srcCfg["rx_gains"]           = std::vector{kRxGainDb};
        srcCfg["max_time_out_us"]    = std::uint32_t{1000000}; // 1 s, vs 1 ms default
    }
    auto& src = graph.emplaceBlock<SoapySource<T, 1UZ>>(srcCfg);

    DivideConst<T>* last = nullptr;
    for (std::size_t i = 0UZ; i < kDepth; ++i) {
        auto& mul = graph.emplaceBlock<MultiplyConst<T>>({{"value", T(2.0f, 0.0f)}});
        auto& div = graph.emplaceBlock<DivideConst<T>>({{"value", T(2.0f, 0.0f)}});
        if (i == 0UZ) {
            mustConnect(graph.connect(src, "out"s, mul, "in"s), "src->mul");
        } else {
            mustConnect(graph.connect(*last, "out"s, mul, "in"s), "div->mul");
        }
        mustConnect(graph.connect(mul, "out"s, div, "in"s), "mul->div");
        last = std::addressof(div);
    }

    auto& sink = graph.emplaceBlock<gr::testing::CountingSink<T>>(); // 0 = run until stopped
    if (last == nullptr) {
        mustConnect(graph.connect(src, "out"s, sink, "in"s), "src->sink");
    } else {
        mustConnect(graph.connect(*last, "out"s, sink, "in"s), "div->sink");
    }
    auto* sinkPtr = std::addressof(sink);

    gr::scheduler::Simple<gr::scheduler::ExecutionPolicy::multiThreaded> sched;
    if (auto ret = sched.exchange(std::move(graph)); !ret) {
        std::println(stderr, "  scheduler init failed: {}", ret.error());
        return {rateHz, 0.0, 0.0, false};
    }

    // A radio source never finishes on its own, so the sink cannot end the graph:
    // run for a fixed wall time and stop explicitly. This also yields the achieved
    // sample rate directly, which is the number we actually want.
    std::atomic<bool> ok{true};
    const auto        t0 = std::chrono::steady_clock::now();
    std::thread       runner([&] { ok.store(sched.runAndWait().has_value(), std::memory_order_relaxed); });

    // Device init (B210: ~2.5 s of FPGA/codec/clock bring-up) must NOT count toward
    // the streaming interval, or the achieved rate is diluted by setup - the same
    // error that invalidated the first scaling curve. Time from the first sample.
    std::chrono::steady_clock::time_point tFirst{};
    while (sinkPtr->count == 0U && std::chrono::steady_clock::now() - t0 < std::chrono::seconds(20)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    tFirst = std::chrono::steady_clock::now();
    const gr::Size_t countAtFirst = sinkPtr->count;

    std::this_thread::sleep_for(std::chrono::duration<double>(kDurationSec));
    const gr::Size_t countAtStop = sinkPtr->count;
    const double     streamSec   = std::chrono::duration<double>(std::chrono::steady_clock::now() - tFirst).count();
    sched.requestStop();

    // Hard backstop. The previous version tested runner.joinable(), which stays true
    // until join() is actually called - so that term never ended the loop - and then
    // called join() unconditionally, which blocks forever on a wedged graph. That is
    // the 1717%-CPU hang with a 15 s delay in front of it, not a backstop.
    // Poll the scheduler state, and on timeout DETACH rather than join: a thread we
    // refuse to wait for cannot hold the process. main()'s alarm() is the layer below.
    bool       wedged   = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (std::chrono::steady_clock::now() < deadline) {
        const gr::lifecycle::State state = sched.state();
        if (state == gr::lifecycle::State::STOPPED || state == gr::lifecycle::State::IDLE || state == gr::lifecycle::State::ERROR) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    if (const gr::lifecycle::State state = sched.state(); state == gr::lifecycle::State::STOPPED || state == gr::lifecycle::State::IDLE) {
        runner.join();
    } else {
        std::println(stderr, "  WEDGED: scheduler still in {} after stop request - detaching", state);
        wedged = true;
        runner.detach();
    }
    const auto t1 = std::chrono::steady_clock::now();

    const double elapsed  = std::chrono::duration<double>(t1 - t0).count();
    const double achieved = streamSec > 0.0 ? static_cast<double>(countAtStop - countAtFirst) / streamSec : 0.0;
    return {rateHz, elapsed, achieved, ok.load(std::memory_order_relaxed) && !wedged};
}

int main(int argc, char* argv[]) {
    std::vector<double> rates = {1e6, 4e6, 8e6, 16e6, 32e6, 56e6};
    if (argc > 1) {
        rates = {std::atof(argv[1])};
    }
    if (argc > 2) {
        kDepth = static_cast<std::size_t>(std::atol(argv[2]));
    }
    if (argc > 3) {
        kDevice = argv[3];
    }
    if (argc > 4) {
        kMinimalCfg = (std::string(argv[4]) == "min");
    }

    // Last-resort backstop, below the per-point one: a spinning worker cannot defeat
    // SIGALRM. Device bring-up on a B210 is ~2.5 s, so budget generously per point.
    alarm(static_cast<unsigned>(static_cast<double>(rates.size()) * (kDurationSec + 60.0)) + 60U);

    std::println("womm B210 end-to-end rate sweep");
    std::println("chain: SoapySource({}, complex<float>) -> {}x(mul,div) -> CountingSink", kDevice, kDepth);
    std::println("{:.1f} s per point, centre {:.1f} MHz, gain {:.0f} dB\n", kDurationSec, kCentreFreqHz / 1e6, kRxGainDb);
    std::println("{:>10}  {:>9}  {:>10}  {:>12}  {:>8}  {:>9}", "rate_Msps", "xB210", "elapsed_s", "achieved_Msps", "ratio", "verdict");

    for (double r : rates) {
        const Point p = runAtRate(r);
        if (!p.completed && p.achievedHz == 0.0) {
            std::println("{:>10.2f}  {:>8.2f}x  {:>10}  {:>12}  {:>8}  {:>9}", r / 1e6, r / kB210MaxRate, "-", "-", "-", "FAILED");
            continue;
        }
        const double ratio = p.achievedHz / r;
        std::println("{:>10.2f}  {:>8.2f}x  {:>10.2f}  {:>12.2f}  {:>8.3f}  {:>9}", //
            r / 1e6, r / kB210MaxRate, p.elapsedSec, p.achievedHz / 1e6, ratio, ratio >= 0.98 ? "KEEPS UP" : "BEHIND");
    }
    return 0;
}
