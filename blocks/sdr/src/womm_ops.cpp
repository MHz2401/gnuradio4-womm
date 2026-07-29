// womm_ops - S2-3: operations under load, without surprises.
//
// THE TIER-1 CRITERION (BUILD_JOURNAL.md D8) is "no loss of lock, and no
// UNANTICIPATED overflow or underflow, during operations", particularly channel
// switching, calibration and UI operations. "Unanticipated" is load-bearing: a
// retune MUST drop samples, and a drop around a deliberate reconfiguration is
// expected behaviour. The question is never "were there drops", it is "were the
// drops the ones we can account for, and on the radio we touched".
//
// So this harness runs N radios x 2 channels at capacity in ONE graph and ONE
// scheduler - the topology RESULTS.md 9.14 established is sufficient for streaming
// and the one any in-process UI would drive - then injects ONE named operation at a
// known instant and records what the driver sensed on either side of it.
//
// TWO CONTROLS, BOTH REQUIRED. An instrument that has only ever been shown a retune
// has been shown to say "yes", not to say "no" - the mistake that produced a comb
// detector reporting 40530 Hz on silence.
//
//   1. WOMM_OP=null runs the identical code path, timing and windows with NO
//      operation injected. It MUST report zero attributed events, or nothing else
//      this binary prints means anything.
//   2. Every operation targets ONE radio while the others keep streaming untouched,
//      so each run carries its own within-run control: events on the untargeted
//      radios are the noise floor for events on the targeted one.
//
// Settings reach the block through the SCHEDULER'S MESSAGE PORT, not by poking the
// object - that is the path a UI or gnuradio4-control-plane drives, and D10 makes a
// usable UI a tier-1 item. WOMM_DIRECT=1 switches to settings().setStaged() to
// separate the cost of the operation from the cost of the plumbing.
//
// RECEIVE ONLY. No SoapySink.hpp, no transmit path; assert_no_tx.cmake asserts that
// on the linked binary. No serials or RF parameters are committed here.

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <complex>
#include <cstdlib>
#include <print>
#include <ranges>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <unistd.h>

#include <gnuradio-4.0/Graph.hpp>
#include <gnuradio-4.0/Message.hpp>
#include <gnuradio-4.0/Scheduler.hpp>
#include <gnuradio-4.0/thread/thread_pool.hpp>

#include <gnuradio-4.0/sdr/SoapySource.hpp>
#include <gnuradio-4.0/testing/TagMonitors.hpp>

using TRadio   = std::complex<float>;
using TTagSink = gr::testing::TagSink<TRadio, gr::testing::ProcessFunction::USE_PROCESS_BULK>;

inline constexpr std::size_t kChannels   = 2UZ;
inline constexpr double      kMcrHz      = 30.72e6; // UHD's 2-channel maximum
inline constexpr double      kRateHz     = kMcrHz / 2.0;
inline constexpr double      kCentreFreq = 2401e6; // non-round MHz are less crowded than round ones
inline constexpr double      kRxGainDb   = 20.0;

// A far jump crosses to the 5 GHz U-NII band and forces a genuine synthesiser retune.
// A near jump stays well inside the 15.36 MHz channel, where UHD can satisfy the
// request in the DDC without touching the LO at all - the "smart about small jumps"
// behaviour the owner described. The pair is the experiment: if they cost the same,
// the cost is not the LO.
inline constexpr double kFarFreqHz  = 5785e6; // WiFi channel 157
inline constexpr double kNearFreqHz = kCentreFreq + 1e6;
inline constexpr double kAltGainDb  = 40.0;

inline constexpr double kWarmupSec = 8.0;  // startup overflows are not what is being measured
inline constexpr double kWindowSec = 4.0;  // per observation window, before and after each operation
inline constexpr double kSettleSec = 1.0;  // between injection and the start of the "after" window

inline constexpr std::array<std::string_view, 2> kRxOnlyAntennae{"RX2", "TX/RX"};
inline constexpr std::array<std::string_view, 5> kOperations{"null", "retune-far", "retune-near", "gain", "bandwidth"};

double envOr(const char* k, double d) {
    const char* v = std::getenv(k);
    return v ? std::atof(v) : d;
}

void mustConnect(auto&& r, std::string_view what) {
    if (!r.has_value()) {
        std::println(stderr, "connect failed: {}", what);
        std::exit(1);
    }
}

// Everything the driver sensed, as of one instant. Sampled either side of an
// operation; the difference is what that operation is answerable for.
struct EventCounts {
    gr::Size_t overflow    = 0U;
    gr::Size_t timeout     = 0U;
    gr::Size_t underflow   = 0U;
    gr::Size_t corruption  = 0U;
    gr::Size_t streamError = 0U;

    EventCounts operator-(const EventCounts& o) const { return {overflow - o.overflow, timeout - o.timeout, underflow - o.underflow, corruption - o.corruption, streamError - o.streamError}; }

    gr::Size_t total() const { return overflow + timeout + underflow + corruption + streamError; }

    std::string format() const { return std::format("ovf {:>4}  tmo {:>4}  unf {:>3}  cor {:>3}  err {:>3}", overflow, timeout, underflow, corruption, streamError); }
};

struct RadioTap {
    std::string                                     serial;
    std::string                                     blockName;
    gr::blocks::sdr::SoapySource<TRadio, kChannels>* src = nullptr;
    std::array<TTagSink*, kChannels>                sinks{};

    EventCounts events() const {
        return {src->_overflowCount.load(std::memory_order_relaxed), src->_timeoutCount.load(std::memory_order_relaxed), src->_underflowCount.load(std::memory_order_relaxed), src->_corruptionCount.load(std::memory_order_relaxed), src->_streamErrorCount.load(std::memory_order_relaxed)};
    }

    gr::Size_t count(std::size_t ch) const { return sinks[ch]->_nSamplesProduced; }

    // The device's own timestamp for the most recent chunk. Host time carries
    // scheduling jitter far larger than the effect being attributed, so on a
    // disciplined set-up this is the only clock on which radios are comparable.
    std::int64_t deviceTimeNs() const { return src->_deviceTimeValid.load(std::memory_order_relaxed) ? src->_lastDeviceTimeNs.load(std::memory_order_relaxed) : 0; }
};

int main(int argc, char* argv[]) {
    using namespace std::string_literals;

    const double      rateHz    = envOr("WOMM_RATE", kRateHz);
    const std::string antenna   = argc > 2 ? argv[2] : "TX/RX";
    const std::string operation = argc > 1 ? argv[1] : (std::getenv("WOMM_OP") ? std::getenv("WOMM_OP") : "null");
    const bool        direct    = std::getenv("WOMM_DIRECT") != nullptr;

    if (std::ranges::find(kRxOnlyAntennae, antenna) == kRxOnlyAntennae.end()) {
        std::println(stderr, "refusing to run: '{}' is not on the receive-only antenna allow-list", antenna);
        return 2;
    }
    if (std::ranges::find(kOperations, operation) == kOperations.end()) {
        std::println(stderr, "unknown operation '{}'; expected one of {}", operation, gr::join(kOperations, ", "));
        return 2;
    }

    std::vector<std::string> serials;
    for (const auto& kw : gr::blocks::sdr::soapy::Device::enumerate("driver=uhd")) {
        if (auto it = kw.find("serial"); it != kw.end()) {
            serials.push_back(it->second);
        }
    }
    std::ranges::sort(serials);
    if (serials.empty()) {
        std::println(stderr, "no uhd devices found");
        return 1;
    }

    // Sized ONCE for the whole machine. RESULTS.md 9.14 records this as a cliff rather
    // than a gradient - 16 threads fails at full rate where 24 works - so it is set
    // deliberately here and reported below with the result it produced.
    std::uint32_t poolThreads = 24U;
    if (const char* env = std::getenv("WOMM_THREADS"); env) {
        poolThreads = static_cast<std::uint32_t>(std::atol(env));
    }
    {
        using namespace gr::thread_pool;
        Manager::instance().replacePool(std::string(kDefaultCpuPoolId), std::make_shared<ThreadPoolWrapper>(std::make_unique<BasicThreadPool>(std::string(kDefaultCpuPoolId), TaskType::CPU_BOUND, poolThreads, poolThreads), "CPU"));
    }

    gr::Graph             graph;
    std::vector<RadioTap> taps;
    for (const auto& serial : serials) {
        // A distinct name per radio is what makes a message addressable to ONE of them:
        // Block::processMessages matches serviceName against unique_name or name, and an
        // empty serviceName broadcasts to every block in the graph.
        const std::string blockName = std::format("rx{}", taps.size());
        gr::property_map  cfg{
            {"name", blockName},
            {"device", "uhd"s},
            {"device_parameter", std::format("serial={}", serial)},
            {"num_channels", gr::Size_t{kChannels}},
            {"sample_rate", static_cast<float>(rateHz)},
            {"master_clock_rate", envOr("WOMM_MCR", kMcrHz)},
            {"frequency", std::vector<double>(kChannels, envOr("WOMM_FREQ", kCentreFreq))},
            {"rx_gains", std::vector<double>(kChannels, envOr("WOMM_GAIN", kRxGainDb))},
            {"rx_bandwidths", std::vector<double>(kChannels, rateHz)},
            {"rx_antennae", std::vector<std::string>(kChannels, antenna)},
            {"max_time_out_us", std::uint32_t{1000000}},
            {"max_overflow_count", gr::Size_t{0}}, // never stop on overflow: counting them IS the measurement
            {"max_chunk_size", std::uint32_t{512U << 4U}},
            {"emit_timing_tags", true}, // carries lo_lock_ms and device_time_ns
            {"emit_meta_info", true},
        };
        if (const char* env = std::getenv("WOMM_EXTCLK"); env && std::string_view(env) == "1") {
            cfg["clock_source"]      = std::string("external");
            cfg["time_source"]       = std::string("external");
            cfg["start_time_offset"] = 5.0f;
        }
        auto&    src = graph.emplaceBlock<gr::blocks::sdr::SoapySource<TRadio, kChannels>>(cfg);
        RadioTap tap{.serial = serial, .blockName = blockName, .src = std::addressof(src)};
        for (std::size_t ch = 0UZ; ch < kChannels; ++ch) {
            auto& sink = graph.emplaceBlock<TTagSink>({{"log_tags", true}, {"log_samples", false}, {"verbose_console", false}, {"sample_rate", static_cast<float>(rateHz)}});
            mustConnect(graph.connect(src, std::format("out#{}", ch), sink, "in"s), std::format("{} out#{}", serial, ch));
            tap.sinks[ch] = std::addressof(sink);
        }
        taps.push_back(tap);
    }

    std::println("womm ops — S2-3 operations under load");
    std::println("RECEIVE ONLY. {} radios x {} channels, {:.2f} MS/s/channel, {:.2f} MS/s aggregate", taps.size(), kChannels, rateHz / 1e6, rateHz * static_cast<double>(kChannels * taps.size()) / 1e6);
    std::println("operation '{}' via {}, pool {} threads, antenna {}", operation, direct ? "settings().setStaged()" : "scheduler message port", poolThreads, antenna);
    if (operation == "null") {
        std::println("THIS IS THE CONTROL RUN. Any attributed event here invalidates the instrument.");
    }
    std::println("");

    gr::MsgPortOut                                                       toScheduler;
    gr::scheduler::Simple<gr::scheduler::ExecutionPolicy::multiThreaded> sched;
    if (auto r = sched.exchange(std::move(graph)); !r) {
        std::println(stderr, "scheduler init failed: {}", r.error());
        return 1;
    }
    mustConnect(toScheduler.connect(sched.msgIn), "message port -> scheduler");

    const auto totalSec = kWarmupSec + static_cast<double>(taps.size()) * (2.0 * kWindowSec + kSettleSec);
    alarm(static_cast<unsigned>(totalSec) + 300U);

    std::atomic<bool> ok{true};
    std::thread       runner([&] { ok.store(sched.runAndWait().has_value(), std::memory_order_relaxed); });

    // Wait for EVERY channel to produce before timing anything. Four B210s bring up
    // serially at ~2.5 s each, and with an external time source each also blocks ~2 s
    // in set_time_unknown_pps, so a fixed sleep would measure start-up.
    const auto ready = [&] {
        return std::ranges::all_of(taps, [](const RadioTap& t) { return std::ranges::all_of(std::views::iota(0UZ, kChannels), [&t](std::size_t ch) { return t.count(ch) > 0U; }); });
    };
    const auto readyBy = std::chrono::steady_clock::now() + std::chrono::seconds(180);
    while (!ready() && std::chrono::steady_clock::now() < readyBy) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    if (!ready()) {
        std::println(stderr, "not every channel produced samples within 180 s — aborting rather than measuring a partial set");
        sched.requestStop();
        runner.join();
        return 1;
    }

    // The boot LO settle time, measured by waitForLoLock() on every radio regardless of
    // what is on the air. This is the settling number available without a transmitter.
    std::println("LO settle at boot (ms, per channel; -1 = channel exposes no lo_locked sensor):");
    for (const auto& tap : taps) {
        std::println("  {} {:>10}  {}", tap.blockName, tap.serial.substr(0, 4) + "…", gr::join(tap.src->_loLockMs, ", "));
    }
    std::println("");

    std::this_thread::sleep_for(std::chrono::duration<double>(kWarmupSec));

    const auto sleepFor = [](double sec) { std::this_thread::sleep_for(std::chrono::duration<double>(sec)); };

    // One operation per radio in turn. Every radio is measured across every operation,
    // so the untargeted radios are a within-run control for the targeted one.
    bool clean = true;
    for (std::size_t target = 0UZ; target < taps.size(); ++target) {
        std::vector<EventCounts> before(taps.size());
        std::vector<EventCounts> during(taps.size());

        std::ranges::transform(taps, before.begin(), [](const RadioTap& t) { return t.events(); });
        sleepFor(kWindowSec);
        std::ranges::transform(taps, during.begin(), [](const RadioTap& t) { return t.events(); });

        const std::int64_t devAtOp = taps[target].deviceTimeNs();
        gr::property_map   change;
        if (operation == "retune-far") {
            change["frequency"] = std::vector<double>(kChannels, envOr("WOMM_FAR", kFarFreqHz));
        } else if (operation == "retune-near") {
            change["frequency"] = std::vector<double>(kChannels, envOr("WOMM_NEAR", kNearFreqHz));
        } else if (operation == "gain") {
            change["rx_gains"] = std::vector<double>(kChannels, envOr("WOMM_ALT_GAIN", kAltGainDb));
        } else if (operation == "bandwidth") {
            change["rx_bandwidths"] = std::vector<double>(kChannels, rateHz / 2.0);
        }

        if (!change.empty()) {
            if (direct) {
                // setStaged returns what it COULD NOT set - a silently rejected key would
                // otherwise look exactly like an operation that cost nothing.
                if (const gr::property_map rejected = taps[target].src->settings().setStaged(change); !rejected.empty()) {
                    std::println(stderr, "rejected by {}: {}", taps[target].blockName, gr::join(std::views::transform(rejected, [](const auto& kv) { return std::string(kv.first); }), ", "));
                    return 1;
                }
            } else {
                gr::sendMessage<gr::message::Command::Set>(toScheduler, taps[target].blockName, gr::block::property::kSetting, change, "womm_ops");
            }
        }

        sleepFor(kSettleSec);
        std::vector<EventCounts> after(taps.size());
        std::ranges::transform(taps, after.begin(), [](const RadioTap& t) { return t.events(); });
        sleepFor(kWindowSec);
        std::vector<EventCounts> settled(taps.size());
        std::ranges::transform(taps, settled.begin(), [](const RadioTap& t) { return t.events(); });

        std::println("op {} on {} ({}…)  device_time {}.{:09d} s", operation, taps[target].blockName, taps[target].serial.substr(0, 4), devAtOp / 1'000'000'000, devAtOp % 1'000'000'000);
        for (std::size_t i = 0UZ; i < taps.size(); ++i) {
            const EventCounts baseline = during[i] - before[i];
            const EventCounts atOp     = after[i] - during[i];
            const EventCounts recovery = settled[i] - after[i];
            const bool        isTarget = (i == target);
            std::println("  {} {}   baseline[{:.0f}s] {}", isTarget ? "->" : "  ", taps[i].blockName, kWindowSec, baseline.format());
            std::println("     {}      at-op[{:.0f}s] {}", isTarget ? "  " : "  ", kSettleSec, atOp.format());
            std::println("     {}   recovery[{:.0f}s] {}", isTarget ? "  " : "  ", kWindowSec, recovery.format());
            if (!isTarget && (atOp.total() > baseline.total())) {
                std::println("     ⚠ UNTARGETED radio saw more events during the operation than at baseline");
                clean = false;
            }
            if (operation == "null" && (atOp.total() > 0U || recovery.total() > baseline.total())) {
                std::println("     ⚠ CONTROL RUN reported events — the instrument is not clean");
                clean = false;
            }
        }
        std::println("");
    }

    // Tags carry the sample offset an event landed at, which the counters cannot give.
    std::println("event tags captured on channel 0 (sample offset : keys):");
    for (const auto& tap : taps) {
        std::size_t shown = 0UZ;
        for (const auto& t : tap.sinks[0]->_tags) {
            const bool isEvent = std::ranges::any_of(t.map, [](const auto& kv) { return std::string_view(kv.first).starts_with("rx_"); });
            if (!isEvent || shown >= 4UZ) {
                continue;
            }
            std::println("  {} @ {:>12} : {}", tap.blockName, t.index, gr::join(std::views::transform(t.map, [](const auto& kv) { return std::string(kv.first); }), ", "));
            ++shown;
        }
        if (shown == 0UZ) {
            std::println("  {} — none", tap.blockName);
        }
    }
    std::println("");

    sched.requestStop();
    const auto stopBy = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (std::chrono::steady_clock::now() < stopBy) {
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

    std::println("{}", clean ? "instrument clean" : "⚠ INSTRUMENT NOT CLEAN — see warnings above");
    return clean && ok.load(std::memory_order_relaxed) ? 0 : 1;
}
