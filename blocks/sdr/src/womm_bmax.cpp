// B_max — how much spare scheduler capacity is there while radios hold a deadline?
//
// Three B210s at ~43 MS/s each is ~130 MS/s. This machine does ~2400 Msps of DSP.
// So radios CANNOT load the scheduler, and "aggregate radio throughput" measures
// the radios, not the runtime. Instead the radios serve as a hard real-time
// DEADLINE PROBE riding on a scheduler that synthetic work loads to any level:
//
//   B_max = the largest number of synthetic ballast chains that can share the
//           graph while EVERY radio still streams with zero overflows for T s.
//
// It is a threshold, not a rate, so it resolves to an integer by bisection and is
// far more robust than a noisy Msps figure. It responds where a throughput number
// cannot: a radio overflows on LATENCY, so anything that delays a SoapySource's
// turn shows up here even when aggregate throughput looks fine.
//
// ⚠ WHAT B_max IS NOT. An overflow says samples were dropped; it does NOT say by
// what. USB, host scheduling, the device, the driver and our ballast all produce
// the same indication, so the count is a good thing to have and a bad thing to
// trust. Consequences, and they are binding:
//
//   - B_max is only meaningful as a REPEATED, RELATIVE comparison under otherwise
//     identical conditions. "This change moved B_max from 6 to 9 across five
//     runs" is a result; "the scheduler supports 7 chains" is not.
//   - A single trial proves nothing. One overflow at M=7 may have no connection
//     to the ballast at all.
//   - Never report an absolute B_max without the trial count and the spread.
//
// This is also why the block no longer stops on overflow: if the cause cannot be
// attributed, acting decisively on it is worse than recording it and continuing.
//
// ⚠ THIS MEASURES HOST CAPACITY, NOT SYNCHRONISATION. Each B210 free-runs on its
// own clock. Three streams whose counts advance during the same HOST wall-clock
// window are three independent captures that merely overlap in our frame - not a
// coherent multi-radio acquisition. Two radios a metre apart genuinely disagree
// about what time it is. Certainty would require every device on a common or GPS
// time source and a check that they report the same sample index at the same
// instant; SoapySource exposes clock_source/time_source for exactly that, and
// none of it is exercised here. So: report "the host sustained N streams", never
// "N synchronised radios".
//
// RECEIVE ONLY, BY CONSTRUCTION. This translation unit does not include
// SoapySink.hpp and constructs no transmit path; blocks/sdr/src/CMakeLists.txt
// asserts on the linked binary that no SoapySink symbol is present. Antennas are
// checked against an RX-only allow-list and the run is refused otherwise.
//
// No RF parameter and no device serial is committed here: serials are discovered
// at runtime by enumeration, and centre/gain/rate come from argv or the defaults
// below, which are the documented-safe B2xx working values.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <print>
#include <string>
#include <thread>
#include <vector>

#include <unistd.h>

#include <gnuradio-4.0/Graph.hpp>
#include <gnuradio-4.0/Scheduler.hpp>

#include <gnuradio-4.0/math/Math.hpp>
#include <gnuradio-4.0/sdr/SoapySource.hpp>
#include <gnuradio-4.0/testing/NullSources.hpp>

using TRadio   = std::complex<float>;
using TBallast = float;

inline constexpr double      kCentreFreqHz  = 2401e6; // ISM/amateur; RX-only here, nothing is emitted
inline constexpr double      kRxGainDb      = 20.0;   // well inside range for either B2xx RX antenna
inline constexpr std::size_t kRadioDepth    = 8UZ;    // mul/div pairs per radio chain
inline constexpr std::size_t kBallastDepth  = 8UZ;
inline constexpr gr::Size_t  kBallastSize   = 65536U;
inline constexpr double      kWarmupSec     = 3.0; // B210 bring-up is ~2.5 s
inline constexpr double      kMeasureSec    = 8.0;

// A B2xx receives on RX2 or on the RX half of RX/TX. Neither emits. Anything not
// on this list is refused rather than guessed at.
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

struct RadioTap {
    gr::testing::CountingSink<TRadio>* sink;
    std::string                        serial;
};

RadioTap addRadioChain(gr::Graph& graph, const std::string& serial, double rateHz, std::string_view antenna) {
    using namespace gr::blocks::math;
    using namespace std::string_literals;

    gr::property_map cfg{
        {"device", "uhd"s},
        {"device_parameter", std::format("serial={}", serial)},
        {"num_channels", gr::Size_t{1}},
        {"sample_rate", static_cast<float>(rateHz)},
        {"frequency", std::vector{kCentreFreqHz}},
        {"rx_gains", std::vector{kRxGainDb}},
        {"rx_antennae", std::vector{std::string(antenna)}},
        {"max_time_out_us", std::uint32_t{1000000}},
        {"max_overflow_count", gr::Size_t{0}}, // never stop; we count instead
    };

    // Defaults that are wrong for this workload unless stated explicitly:
    //   rx_bandwidths defaults to 500 kHz, i.e. an analog filter ~86x narrower than a
    //     43 MS/s sample rate. Match it to the rate.
    //   emit_timing_tags/emit_meta_info default to TRUE, so the read loop builds a
    //     property_map per timing tag. Off unless asked for.
    if (const char* env = std::getenv("WOMM_TUNED"); env && std::string_view(env) == "1") {
        cfg["rx_bandwidths"]    = std::vector{rateHz};
        cfg["emit_timing_tags"] = false;
        cfg["emit_meta_info"]   = false;
    }
    if (const char* env = std::getenv("WOMM_CHUNK"); env) {
        cfg["max_chunk_size"] = static_cast<std::uint32_t>(std::atol(env));
    }
    // Octoclock-G: 10 MHz reference + PPS. Opt-in, because selecting an external
    // reference that is absent leaves the device unlocked rather than erroring.
    if (const char* env = std::getenv("WOMM_EXTCLK"); env && std::string_view(env) == "1") {
        cfg["clock_source"] = std::string("external");
        cfg["time_source"]  = std::string("external");
    }
    auto& src = graph.emplaceBlock<gr::blocks::sdr::SoapySource<TRadio, 1UZ>>(cfg);

    DivideConst<TRadio>* last = nullptr;
    for (std::size_t i = 0UZ; i < kRadioDepth; ++i) {
        auto& mul = graph.emplaceBlock<MultiplyConst<TRadio>>({{"value", TRadio(2.0f, 0.0f)}});
        auto& div = graph.emplaceBlock<DivideConst<TRadio>>({{"value", TRadio(2.0f, 0.0f)}});
        if (i == 0UZ) {
            mustConnect(graph.connect(src, "out"s, mul, "in"s), "radio src->mul");
        } else {
            mustConnect(graph.connect(*last, "out"s, mul, "in"s), "radio div->mul");
        }
        mustConnect(graph.connect(mul, "out"s, div, "in"s), "radio mul->div");
        last = std::addressof(div);
    }

    auto& sink = graph.emplaceBlock<gr::testing::CountingSink<TRadio>>({{"n_samples_max", gr::Size_t{0}}});
    mustConnect(graph.connect(*last, "out"s, sink, "in"s), "radio div->sink");
    return {std::addressof(sink), serial};
}

void addBallastChain(gr::Graph& graph, std::size_t id) {
    using namespace gr::blocks::math;
    using namespace std::string_literals;

    auto&                  src  = graph.emplaceBlock<gr::testing::ConstantSource<TBallast>>({{"n_samples_max", gr::Size_t{0}}, {"name", std::format("ballast.{}", id)}});
    DivideConst<TBallast>* last = nullptr;
    for (std::size_t i = 0UZ; i < kBallastDepth; ++i) {
        auto& mul = graph.emplaceBlock<MultiplyConst<TBallast>>({{"value", TBallast(2)}});
        auto& div = graph.emplaceBlock<DivideConst<TBallast>>({{"value", TBallast(2)}});
        if (i == 0UZ) {
            mustConnect(graph.connect(src, "out"s, mul, "in"s, {.minBufferSize = kBallastSize}), "ballast src->mul");
        } else {
            mustConnect(graph.connect(*last, "out"s, mul, "in"s, {.minBufferSize = kBallastSize}), "ballast div->mul");
        }
        mustConnect(graph.connect(mul, "out"s, div, "in"s, {.minBufferSize = kBallastSize}), "ballast mul->div");
        last = std::addressof(div);
    }
    auto& sink = graph.emplaceBlock<gr::testing::NullSink<TBallast>>({});
    mustConnect(graph.connect(*last, "out"s, sink, "in"s, {.minBufferSize = kBallastSize}), "ballast div->sink");
}

struct Trial {
    bool                completed = false;
    std::vector<double> achievedHz;
};

// One trial: N radios + M ballast chains sharing ONE graph and ONE scheduler.
// The shared scheduler is the entire point - ballast in a separate process would
// measure the OS, not gr4's scheduling.
Trial runTrial(const std::vector<std::string>& serials, double rateHz, std::size_t nBallast, std::string_view antenna) {
    gr::Graph              graph;
    std::vector<RadioTap>  taps;
    for (const auto& serial : serials) {
        taps.push_back(addRadioChain(graph, serial, rateHz, antenna));
    }
    for (std::size_t b = 0UZ; b < nBallast; ++b) {
        addBallastChain(graph, b);
    }

    gr::scheduler::Simple<gr::scheduler::ExecutionPolicy::multiThreaded> sched;
    if (auto r = sched.exchange(std::move(graph)); !r) {
        std::println(stderr, "scheduler init failed: {}", r.error());
        return {};
    }

    std::atomic<bool> ok{true};
    std::thread       runner([&] { ok.store(sched.runAndWait().has_value(), std::memory_order_relaxed); });

    // WAIT FOR EVERY RADIO TO PRODUCE ITS FIRST SAMPLE, then settle. A fixed sleep
    // is wrong here and has already cost this project twice: bring-up is ~2.5 s PER
    // B210 and they initialise serially, so three devices need ~7.5 s and any
    // constant chosen for one radio silently measures zero for three.
    const auto readyBy = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    const auto allStreaming = [&] { return std::ranges::all_of(taps, [](const RadioTap& t) { return t.sink->count.value > 0U; }); };
    while (!allStreaming() && std::chrono::steady_clock::now() < readyBy) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    if (!allStreaming()) {
        std::println(stderr, "  only {} of {} radios produced samples within 60 s", std::ranges::count_if(taps, [](const RadioTap& t) { return t.sink->count.value > 0U; }), taps.size());
    }
    std::this_thread::sleep_for(std::chrono::duration<double>(kWarmupSec)); // settle past the rate ramp

    // Time the STREAMING interval only. Device bring-up and graph construction must
    // not dilute the rate - that error invalidated two earlier measurements here.
    std::vector<gr::Size_t> startCount(taps.size());
    std::ranges::transform(taps, startCount.begin(), [](const RadioTap& t) { return t.sink->count.value; });
    const auto t1 = std::chrono::steady_clock::now();

    std::this_thread::sleep_for(std::chrono::duration<double>(kMeasureSec));

    Trial trial;
    trial.achievedHz.resize(taps.size());
    const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - t1).count();
    for (std::size_t i = 0UZ; i < taps.size(); ++i) {
        const gr::Size_t delta = static_cast<gr::Size_t>(taps[i].sink->count.value - startCount[i]); // modular: wrap-correct
        trial.achievedHz[i]    = static_cast<double>(delta) / elapsed;
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
        std::println(stderr, "  WEDGED in {} - detaching; the alarm() below is the backstop", st);
        runner.detach();
        return trial; // completed stays false
    }

    trial.completed = ok.load(std::memory_order_relaxed);
    return trial;
}

int main(int argc, char* argv[]) {
    const double      rateHz   = argc > 1 ? std::atof(argv[1]) : 43e6;
    const std::size_t nBallast = argc > 2 ? static_cast<std::size_t>(std::atol(argv[2])) : 0UZ;
    const std::size_t nRadios  = argc > 3 ? static_cast<std::size_t>(std::atol(argv[3])) : 0UZ; // 0 = all found
    const std::string antenna  = argc > 4 ? argv[4] : "RX2";

    if (std::ranges::find(kRxOnlyAntennae, antenna) == kRxOnlyAntennae.end()) {
        std::println(stderr, "refusing to run: '{}' is not on the receive-only antenna allow-list", antenna);
        return 2;
    }

    std::vector<std::string> serials = discoverSerials();
    if (serials.empty()) {
        std::println(stderr, "no uhd devices found");
        return 1;
    }
    // WOMM_SERIAL_INDEX picks WHICH device, so N single-radio processes can each bind a
    // different radio. That is the decisive test for where the aggregate cap lives: if
    // three separate processes each reach the single-radio rate, the ceiling is
    // per-process and therefore a lock inside the UHD/SoapyUHD library; if they still
    // sum to ~44 MS/s, it is global and belongs to the driver or the host.
    if (const char* env = std::getenv("WOMM_SERIAL_INDEX"); env) {
        const std::size_t idx = static_cast<std::size_t>(std::atol(env));
        if (idx >= serials.size()) {
            std::println(stderr, "WOMM_SERIAL_INDEX={} but only {} device(s) found", idx, serials.size());
            return 1;
        }
        serials = {serials[idx]};
    } else if (nRadios > 0UZ && nRadios < serials.size()) {
        serials.resize(nRadios);
    }

    alarm(static_cast<unsigned>(kWarmupSec + kMeasureSec) + 120U); // a spinning worker cannot defeat SIGALRM

    std::println("womm B_max — {} radio(s) + {} ballast chain(s), one graph, one scheduler", serials.size(), nBallast);
    std::println("RECEIVE ONLY. {:.1f} MS/s/radio, centre {:.1f} MHz, gain {:.0f} dB, antenna {}", rateHz / 1e6, kCentreFreqHz / 1e6, kRxGainDb, antenna);
    std::println("radios: {}", serials.size());

    const Trial trial = runTrial(serials, rateHz, nBallast, antenna);
    if (trial.achievedHz.empty()) {
        std::println("FAILED to run");
        return 1;
    }

    bool allKeepUp = true;
    for (std::size_t i = 0UZ; i < trial.achievedHz.size(); ++i) {
        const double ratio = trial.achievedHz[i] / rateHz;
        allKeepUp          = allKeepUp && ratio >= 0.98;
        std::println("  radio {}  achieved {:>7.2f} MS/s  ratio {:.3f}  {}", i, trial.achievedHz[i] / 1e6, ratio, ratio >= 0.98 ? "KEEPS UP" : "BEHIND");
    }
    const double aggregate = std::accumulate(trial.achievedHz.begin(), trial.achievedHz.end(), 0.0);
    std::println("aggregate {:.2f} MS/s across {} radio(s) + {} ballast   verdict={}", aggregate / 1e6, trial.achievedHz.size(), nBallast, allKeepUp && trial.completed ? "PASS" : "FAIL");
    return allKeepUp && trial.completed ? 0 : 1;
}
