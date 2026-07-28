// Hold-open two-channel RX - eyes-on proof that BOTH channels of a B2xx stream.
//
// WHY THIS EXISTS. Every throughput figure recorded by this project so far was
// taken with num_channels=1 (womm_bmax.cpp, womm_b210_sweep.cpp). The owner's
// prior working system ran TWO channels per radio, so every comparison drawn
// against it was off by a factor of two before any performance question was
// asked.
//
// Worse, the instrument cannot detect its own error: a harness that counts
// AGGREGATE samples reports the same number for "two channels at half rate" and
// "one channel at full rate, one silently dead". Both look like half of what was
// asked for, which is indistinguishable from a performance problem - and this
// project has already been burned twice by measurements that could not see their
// own confound.
//
// So this program deliberately reports two things the aggregate cannot fake:
//   - a SEPARATE running counter per channel, so a dead channel reads as a flat
//     zero rather than as a low total;
//   - a stream that HOLDS OPEN until the operator presses Enter, so the front
//     panel LEDs can be read directly. The LED is out-of-band: no defect in this
//     code can light it.
//
// Neither instrument is trusted alone. The counters can be fooled by a mis-wired
// graph; the LEDs cannot tell us the rate. Agreement between them is the result.
//
// DEPTH ZERO BY CONSTRUCTION. Source -> sink, no DSP. Anything this cannot
// sustain is not a DSP cost.
//
// RECEIVE ONLY. This translation unit does not include SoapySink.hpp and builds
// no transmit path; CMakeLists.txt asserts on the linked binary that no
// SoapySink symbol is present. Antennas are checked against an RX-only
// allow-list and the run is refused otherwise. No RF parameter or device serial
// is committed here.
//
// B2xx NOTE, because it is the usual reason a second channel stays dark: with
// two channels active the device's master clock rate is capped near 30.72 MHz,
// so each channel must stay under that. The 20 MS/s default below is inside it.
// Asking for 43 MS/s on two channels will not work and is not a gr4 defect.

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <complex>
#include <cstdlib>
#include <iostream>
#include <print>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <unistd.h>

#include <gnuradio-4.0/Graph.hpp>
#include <gnuradio-4.0/Scheduler.hpp>
#include <gnuradio-4.0/thread/thread_pool.hpp>

#include <gnuradio-4.0/sdr/SoapySource.hpp>
#include <gnuradio-4.0/testing/NullSources.hpp>

using TRadio = std::complex<float>;

inline constexpr std::size_t kChannels     = 2UZ;
inline constexpr double      kCentreFreqHz = 2401e6; // ISM/amateur; RX-only here, nothing is emitted
inline constexpr double      kRxGainDb     = 20.0;   // well inside range for either B2xx RX antenna
inline constexpr double      kReadyTimeout = 60.0;   // B210 bring-up is ~2.5 s; three radios serialise

// UHD refuses master_clock_rate above 30.72 MHz once two RX channels are active,
// and the per-channel rate is MCR/decimation - so 15.36 MS/s per channel is the
// most a B2xx delivers on two channels, i.e. 30.72 MS/s aggregate per radio.
// Defaulted rather than left to the caller because getting it wrong fails three
// different ways: a bare activate() error, a silent halving, or an outright
// rejection. WOMM_MCR overrides.
inline constexpr double kTwoChannelMcrHz = 30.72e6;
inline constexpr double kDefaultRate     = kTwoChannelMcrHz / 2.0;

inline constexpr std::array<std::string_view, 2> kRxOnlyAntennae{"RX2", "RX/TX"};

void mustConnect(auto&& r, std::string_view what) {
    if (!r.has_value()) {
        std::println(stderr, "connect failed: {}", what);
        std::exit(1);
    }
}

std::vector<std::string> discoverSerials() {
    std::vector<std::string> serials;
    for (const auto& kw : gr::blocks::sdr::soapy::Device::enumerate("driver=uhd")) {
        if (auto it = kw.find("serial"); it != kw.end()) {
            serials.push_back(it->second);
        }
    }
    std::ranges::sort(serials); // stable ordering across runs
    return serials;
}

// Per-channel vectors are supplied in full rather than relying on the block's
// broadcast-from-one-element behaviour: applyFrequency() verifies the readback
// against the vector it was given, so a one-element vector against two channels
// reports a spurious mismatch.
gr::property_map radioConfig(const std::string& serial, double rateHz, const std::string& antenna) {
    using namespace std::string_literals;

    gr::property_map cfg{
        {"device", "uhd"s},
        {"device_parameter", std::format("serial={}", serial)},
        {"num_channels", gr::Size_t{kChannels}},
        {"sample_rate", static_cast<float>(rateHz)},
        {"frequency", std::vector<double>(kChannels, kCentreFreqHz)},
        {"rx_gains", std::vector<double>(kChannels, kRxGainDb)},
        {"rx_bandwidths", std::vector<double>(kChannels, rateHz)},
        {"rx_antennae", std::vector<std::string>(kChannels, antenna)},
        {"max_time_out_us", std::uint32_t{1000000}},
        {"max_overflow_count", gr::Size_t{0}}, // never stop; this run is about liveness, not overflow
        {"emit_timing_tags", false},
        {"emit_meta_info", false},
    };

    cfg["master_clock_rate"] = kTwoChannelMcrHz;
    if (const char* env = std::getenv("WOMM_MCR"); env) {
        cfg["master_clock_rate"] = std::atof(env);
    }
    // Escalation ladder for a dark second channel, opt-in so the default path stays
    // the one that is known to work.
    if (const char* env = std::getenv("WOMM_FEMAP"); env) {
        cfg["frontend_mapping"] = std::string(env);
    }
    // Octoclock-G: 10 MHz reference + PPS. Opt-in, because selecting an external
    // reference that is absent leaves the device unlocked rather than erroring.
    if (const char* env = std::getenv("WOMM_EXTCLK"); env && std::string_view(env) == "1") {
        cfg["clock_source"] = std::string("external");
        cfg["time_source"]  = std::string("external");
    }
    return cfg;
}

int main(int argc, char* argv[]) {
    using namespace std::string_literals;

    const double      rateHz  = argc > 1 ? std::atof(argv[1]) : kDefaultRate;
    const std::string antenna = argc > 2 ? argv[2] : "RX2";

    if (std::ranges::find(kRxOnlyAntennae, antenna) == kRxOnlyAntennae.end()) {
        std::println(stderr, "refusing to run: '{}' is not on the receive-only antenna allow-list", antenna);
        return 2;
    }

    std::vector<std::string> serials = discoverSerials();
    if (serials.empty()) {
        std::println(stderr, "no uhd devices found");
        return 1;
    }
    // WOMM_SERIAL names the device explicitly so that N single-radio processes can
    // each bind a different radio. It must be a serial and not an index: UHD only
    // enumerates devices not already claimed, so indices shift underfoot as peers
    // start up.
    std::string serial = serials.front();
    if (const char* env = std::getenv("WOMM_SERIAL"); env) {
        serial = env;
    }

    // Deliberately no alarm() by default: this test is supposed to run until the
    // operator stops it. WOMM_MAX_SEC arms a backstop for unattended use.
    if (const char* env = std::getenv("WOMM_MAX_SEC"); env) {
        alarm(static_cast<unsigned>(std::atol(env)));
    }

    // WOMM_THREADS sizes this process's CPU pool, and it is not optional for a
    // multi-process run: the default is hardware_concurrency() PER PROCESS (24
    // here), so three radios in three processes claim 72 workers on 24 cores.
    // gr4's multi-threaded workers never back off, so that is 3x oversubscription
    // of pure spin - previously measured as 3.54 vs 40.89 MS/s aggregate.
    if (const char* env = std::getenv("WOMM_THREADS"); env) {
        const auto n = static_cast<std::uint32_t>(std::atol(env));
        using namespace gr::thread_pool;
        Manager::instance().replacePool(std::string(kDefaultCpuPoolId), std::make_shared<ThreadPoolWrapper>(std::make_unique<BasicThreadPool>(std::string(kDefaultCpuPoolId), TaskType::CPU_BOUND, n, n), "CPU"));
    }

    gr::Graph graph;
    auto&     src = graph.emplaceBlock<gr::blocks::sdr::SoapySource<TRadio, kChannels>>(radioConfig(serial, rateHz, antenna));

    std::array<gr::testing::CountingSink<TRadio>*, kChannels> sinks{};
    for (std::size_t ch = 0UZ; ch < kChannels; ++ch) {
        auto& sink = graph.emplaceBlock<gr::testing::CountingSink<TRadio>>({{"n_samples_max", gr::Size_t{0}}});
        mustConnect(graph.connect(src, std::format("out#{}", ch), sink, "in"s), std::format("src out#{} -> sink", ch));
        sinks[ch] = std::addressof(sink);
    }

    gr::scheduler::Simple<gr::scheduler::ExecutionPolicy::multiThreaded> sched;
    if (auto r = sched.exchange(std::move(graph)); !r) {
        std::println(stderr, "scheduler init failed: {}", r.error());
        return 1;
    }

    std::println("womm hold-open RX — radio {}", serial);
    const double mcrHz = std::getenv("WOMM_MCR") ? std::atof(std::getenv("WOMM_MCR")) : kTwoChannelMcrHz;
    std::println("RECEIVE ONLY. {} channels x {:.2f} MS/s = {:.2f} MS/s aggregate, master clock {:.2f} MHz", kChannels, rateHz / 1e6, rateHz * static_cast<double>(kChannels) / 1e6, mcrHz / 1e6);
    std::println("centre {:.1f} MHz, gain {:.0f} dB, antenna {}", kCentreFreqHz / 1e6, kRxGainDb, antenna);
    std::println("depth 0 — source straight to counting sinks, no DSP");
    std::println("");
    std::println("  >>> WATCH THE FRONT PANEL. Both RX channels should light. <<<");
    std::println("  >>> Press Enter to stop.                                  <<<");
    std::println("");

    std::atomic<bool> ok{true};
    std::thread       runner([&] { ok.store(sched.runAndWait().has_value(), std::memory_order_relaxed); });

    std::atomic<bool> stopRequested{false};
    std::thread       waiter([&] {
        std::string line;
        std::getline(std::cin, line);
        stopRequested.store(true, std::memory_order_release);
    });

    const auto counts = [&] {
        std::array<gr::Size_t, kChannels> c{};
        for (std::size_t ch = 0UZ; ch < kChannels; ++ch) {
            c[ch] = sinks[ch]->count.value;
        }
        return c;
    };

    // Wait for first samples before reporting rates, so bring-up does not appear
    // as a slow channel. A fixed sleep is wrong here: bring-up is ~2.5 s per B210.
    const auto readyBy = std::chrono::steady_clock::now() + std::chrono::duration<double>(kReadyTimeout);
    while (!stopRequested.load(std::memory_order_acquire) && std::chrono::steady_clock::now() < readyBy) {
        const auto c = counts();
        if (std::ranges::all_of(c, [](gr::Size_t v) { return v > 0U; })) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    {
        const auto c = counts();
        for (std::size_t ch = 0UZ; ch < kChannels; ++ch) {
            if (c[ch] == 0U) {
                std::println("  !!! channel {} produced NO samples during bring-up !!!", ch);
            }
        }
    }

    auto prev  = counts();
    auto prevT = std::chrono::steady_clock::now();
    while (!stopRequested.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));

        const auto   now     = std::chrono::steady_clock::now();
        const auto   cur     = counts();
        const double elapsed = std::chrono::duration<double>(now - prevT).count();

        for (std::size_t ch = 0UZ; ch < kChannels; ++ch) {
            const auto   delta = static_cast<gr::Size_t>(cur[ch] - prev[ch]); // modular: wrap-correct
            const double mss   = static_cast<double>(delta) / elapsed / 1e6;
            std::println("  ch{}  {:>14} samples  {:>7.2f} MS/s  {}", ch, cur[ch], mss, delta == 0U ? "*** NO DATA ***" : "");
        }
        std::println("");
        prev  = cur;
        prevT = now;
    }

    sched.requestStop();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (std::chrono::steady_clock::now() < deadline) {
        const gr::lifecycle::State st = sched.state();
        if (st == gr::lifecycle::State::STOPPED || st == gr::lifecycle::State::IDLE || st == gr::lifecycle::State::ERROR) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    if (const gr::lifecycle::State st = sched.state(); st == gr::lifecycle::State::STOPPED || st == gr::lifecycle::State::IDLE) {
        runner.join();
    } else {
        std::println(stderr, "WEDGED in {} — detaching", st);
        runner.detach();
    }
    waiter.detach(); // may still be blocked in getline() if we stopped for another reason

    const auto final = counts();
    std::println("");
    std::println("final counts — radio {}", serial);
    bool bothLive = true;
    for (std::size_t ch = 0UZ; ch < kChannels; ++ch) {
        bothLive = bothLive && final[ch] > 0U;
        std::println("  ch{}  {:>14} samples  {}", ch, final[ch], final[ch] > 0U ? "LIVE" : "DEAD");
    }
    std::println("verdict={}", bothLive && ok.load(std::memory_order_relaxed) ? "BOTH CHANNELS LIVE" : "INCOMPLETE");
    return bothLive && ok.load(std::memory_order_relaxed) ? 0 : 1;
}
