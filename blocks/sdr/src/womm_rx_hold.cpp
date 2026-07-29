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
// two channels active UHD caps the master clock rate at 30.72 MHz, and the
// per-channel rate is MCR/decimation - so 15.36 MS/s per channel is the ceiling,
// 30.72 MS/s aggregate per radio. Asking for 20 MS/s on two channels does not
// work and is not a gr4 defect.
//
// Antenna names are TX/RX and RX2 - in that order, as the device spells them.
// "RX/TX" is a natural thing to type and is not a name the device knows.

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <complex>
#include <cstdlib>
#include <filesystem>
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

#include <gnuradio-4.0/fileio/BasicFileIo.hpp>
#include <gnuradio-4.0/sdr/SoapySource.hpp>
#include <gnuradio-4.0/testing/NullSources.hpp>
#include <gnuradio-4.0/testing/TagMonitors.hpp>

using TRadio = std::complex<float>;

inline constexpr std::size_t kChannels     = 2UZ;
inline constexpr double      kCentreFreqHz = 2401e6; // ISM/amateur; RX-only here, nothing is emitted
inline constexpr double      kRxGainDb     = 20.0;   // well inside range for either B2xx RX antenna
inline constexpr double      kReadyTimeout = 60.0;   // B210 bring-up is ~3.4-3.7 s; three radios serialise

// UHD refuses master_clock_rate above 30.72 MHz once two RX channels are active,
// and the per-channel rate is MCR/decimation - so 15.36 MS/s per channel is the
// most a B2xx delivers on two channels, i.e. 30.72 MS/s aggregate per radio.
// Defaulted rather than left to the caller because getting it wrong fails three
// different ways: a bare activate() error, a silent halving, or an outright
// rejection. WOMM_MCR overrides.
inline constexpr double kTwoChannelMcrHz = 30.72e6;
inline constexpr double kDefaultRate     = kTwoChannelMcrHz / 2.0;

inline constexpr std::array<std::string_view, 2> kRxOnlyAntennae{"RX2", "TX/RX"};

// RF parameters stay OUT of the committed defaults: a frequency or gain checked in
// here becomes someone else's emission on someone else's licence the moment a TX
// path appears. Defaults are the documented-safe B2xx values; the actual test
// values arrive at runtime.
double envOr(const char* key, double fallback) {
    const char* v = std::getenv(key);
    return v ? std::atof(v) : fallback;
}

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
gr::property_map radioConfig(const std::string& serial, double rateHz, const std::string& antenna, std::uint32_t chunkSize) {
    const double freqHz = envOr("WOMM_FREQ", kCentreFreqHz);
    const double gainDb = envOr("WOMM_GAIN", kRxGainDb);
    using namespace std::string_literals;

    gr::property_map cfg{
        {"device", "uhd"s},
        {"device_parameter", std::format("serial={}", serial)},
        {"num_channels", gr::Size_t{kChannels}},
        {"sample_rate", static_cast<float>(rateHz)},
        {"frequency", std::vector<double>(kChannels, freqHz)},
        {"rx_gains", std::vector<double>(kChannels, gainDb)},
        {"rx_bandwidths", std::vector<double>(kChannels, rateHz)},
        {"rx_antennae", std::vector<std::string>(kChannels, antenna)},
        {"max_time_out_us", std::uint32_t{1000000}},
        {"max_overflow_count", gr::Size_t{0}}, // never stop; this run is about liveness, not overflow
        // Many buffers between tags. num_recv_frames is the USB transfer-buffer depth:
        // deep enough that host scheduling jitter cannot perturb arrival, which is the
        // thing being measured once the radios are locked. Tags stay ON — they are how
        // the device reports its own time back — but at one per second, so the read
        // loop is not building a property_map per chunk.
        {"max_chunk_size", chunkSize},
        {"stream_args", std::format("num_recv_frames={}", std::getenv("WOMM_RECV_FRAMES") ? std::getenv("WOMM_RECV_FRAMES") : "1024")},
        {"emit_timing_tags", true},
        {"emit_meta_info", true},
        {"tag_interval", 1.0f},
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
        // Absolute instant on the shared epoch that set_time_unknown_pps() establishes.
        // Must clear the ~2 s that call blocks for, and must be the SAME literal in
        // every process — it is the only thing making the three radios start together.
        cfg["start_time_offset"] = 5.0f;
    }
    return cfg;
}

int main(int argc, char* argv[]) {
    using namespace std::string_literals;

    const double      rateHz     = argc > 1 ? std::atof(argv[1]) : envOr("WOMM_RATE", kDefaultRate);
    const double      captureSec = envOr("WOMM_CAPTURE_SEC", 0.0);
    const bool        tagMode    = std::getenv("WOMM_TAGS") != nullptr;
    const std::string captureDir = std::getenv("WOMM_CAPTURE_DIR") ? std::getenv("WOMM_CAPTURE_DIR") : "data";
    const std::string antenna = argc > 2 ? argv[2] : "TX/RX"; // where the antennas are actually connected

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

    // A capture can only end on a chunk boundary, so the chunk must be no larger than
    // the request or short captures are unachievable: 8192 samples at 0.5 MS/s is
    // 16.4 ms, and nothing briefer could ever be returned. Shrink to the largest power
    // of two that fits (the setting is power-of-two constrained), floored at 512.
    std::uint32_t chunkSize = 512U << 4U;
    if (captureSec > 0.0) {
        const auto target = static_cast<std::uint64_t>(rateHz * captureSec);
        while (chunkSize > 512U && static_cast<std::uint64_t>(chunkSize) > target) {
            chunkSize >>= 1U;
        }
    }
    if (const char* env = std::getenv("WOMM_CHUNK"); env) {
        chunkSize = static_cast<std::uint32_t>(std::atol(env));
    }

    gr::Graph graph;
    auto&     src = graph.emplaceBlock<gr::blocks::sdr::SoapySource<TRadio, kChannels>>(radioConfig(serial, rateHz, antenna, chunkSize));

    // A port feeds exactly one consumer, so capture REPLACES the counting sink
    // rather than tapping alongside it. Liveness then comes from bytes written.
    using TTagSink = gr::testing::TagSink<TRadio, gr::testing::ProcessFunction::USE_PROCESS_BULK>;

    std::array<gr::testing::CountingSink<TRadio>*, kChannels>        sinks{};
    std::array<gr::blocks::fileio::BasicFileSink<TRadio>*, kChannels> fileSinks{};
    std::array<TTagSink*, kChannels>                                  tagSinks{};

    if (captureSec > 0.0) {
        std::filesystem::create_directories(captureDir);
        // Hard byte cap derived from the duration, enforced BY THE BLOCK. A comment
        // cannot stop a capture that outlives its stop signal; max_bytes_per_file can.
        // max_bytes_per_file does NOT stop at the cap - BasicFileIo.hpp:106-108 rolls the
        // file over, and in overwrite mode that restarts the SAME filename from zero.
        // So the cap must clear the deliberate overshoot below (target + 4 chunks) or it
        // silently truncates the very capture it is meant to bound. Sized at +8 chunks:
        // still a hard bound on a runaway, never reached by a normal capture.
        const auto capBytes = static_cast<gr::Size_t>((static_cast<std::uint64_t>(rateHz * captureSec) + 8UL * chunkSize) * sizeof(TRadio));
        for (std::size_t ch = 0UZ; ch < kChannels; ++ch) {
            const char*       sfx  = std::getenv("WOMM_CAPTURE_SUFFIX");
            const std::string path = std::format("{}/capture_{}_ch{}{}.bin", captureDir, serial, ch, sfx ? sfx : "");
            auto&             sink = graph.emplaceBlock<gr::blocks::fileio::BasicFileSink<TRadio>>({{"file_name", path}, {"max_bytes_per_file", capBytes}});
            mustConnect(graph.connect(src, std::format("out#{}", ch), sink, "in"s), std::format("src out#{} -> file sink", ch));
            fileSinks[ch] = std::addressof(sink);
        }
    } else if (tagMode) {
        // TagSink is upstream's, not ours - it already records tags with their sample
        // index and exposes a per-tag callback. log_samples MUST be off: it defaults
        // true and would accumulate every sample into a Tensor, which at 15 MS/s is
        // gigabytes within seconds.
        for (std::size_t ch = 0UZ; ch < kChannels; ++ch) {
            auto& sink = graph.emplaceBlock<TTagSink>({{"log_tags", true}, {"log_samples", false}, {"verbose_console", false}, {"sample_rate", static_cast<float>(rateHz)}});
            mustConnect(graph.connect(src, std::format("out#{}", ch), sink, "in"s), std::format("src out#{} -> tag sink", ch));
            tagSinks[ch] = std::addressof(sink);
        }
    } else {
        for (std::size_t ch = 0UZ; ch < kChannels; ++ch) {
            auto& sink = graph.emplaceBlock<gr::testing::CountingSink<TRadio>>({{"n_samples_max", gr::Size_t{0}}});
            mustConnect(graph.connect(src, std::format("out#{}", ch), sink, "in"s), std::format("src out#{} -> sink", ch));
            sinks[ch] = std::addressof(sink);
        }
    }

    gr::scheduler::Simple<gr::scheduler::ExecutionPolicy::multiThreaded> sched;
    if (auto r = sched.exchange(std::move(graph)); !r) {
        std::println(stderr, "scheduler init failed: {}", r.error());
        return 1;
    }

    std::println("womm hold-open RX — radio {}", serial);
    const double mcrHz = std::getenv("WOMM_MCR") ? std::atof(std::getenv("WOMM_MCR")) : kTwoChannelMcrHz;
    std::println("RECEIVE ONLY. {} channels x {:.2f} MS/s = {:.2f} MS/s aggregate, master clock {:.2f} MHz", kChannels, rateHz / 1e6, rateHz * static_cast<double>(kChannels) / 1e6, mcrHz / 1e6);
    std::println("centre {:.6f} MHz, gain {:.0f} dB, antenna {}", envOr("WOMM_FREQ", kCentreFreqHz) / 1e6, envOr("WOMM_GAIN", kRxGainDb), antenna);
    if (captureSec > 0.0) {
        std::println("CAPTURE {:.4f} s = {} samples/channel, chunk {} -> {}/capture_{}_ch*.bin", captureSec, static_cast<std::uint64_t>(rateHz * captureSec), chunkSize, captureDir, serial);
    }
    std::println("depth 0 — source straight to counting sinks, no DSP");
    std::println("");
    std::println("  >>> WATCH THE FRONT PANEL. Both RX channels should light. <<<");
    std::println("  >>> Press Enter to stop.                                  <<<");
    std::println("");

    std::atomic<bool> ok{true};
    std::thread       runner([&] { ok.store(sched.runAndWait().has_value(), std::memory_order_relaxed); });

    // Enter-to-stop belongs to the INTERACTIVE liveness mode only. In capture mode
    // the duration governs, and a waiter on stdin would end the run instantly the
    // moment stdin is /dev/null or a closed pipe - which is exactly how an
    // unattended capture silently produces zero samples.
    std::atomic<bool> stopRequested{false};
    std::thread       waiter;
    if (captureSec <= 0.0) {
        waiter = std::thread([&] {
            std::string line;
            std::getline(std::cin, line);
            stopRequested.store(true, std::memory_order_release);
        });
    }

    const auto counts = [&] {
        std::array<gr::Size_t, kChannels> c{};
        for (std::size_t ch = 0UZ; ch < kChannels; ++ch) {
            if (fileSinks[ch]) {
                c[ch] = static_cast<gr::Size_t>(fileSinks[ch]->_totalBytesWritten / sizeof(TRadio));
            } else if (tagSinks[ch]) {
                c[ch] = tagSinks[ch]->_nSamplesProduced;
            } else {
                c[ch] = sinks[ch]->count.value;
            }
        }
        return c;
    };

    // Wait for first samples before reporting rates, so bring-up does not appear
    // as a slow channel. A fixed sleep is wrong here: bring-up is ~3.4-3.7 s per B210.
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

    // In capture mode the duration is the point, so stop on it rather than waiting
    // for the operator. max_bytes_per_file remains as a second, independent bound.
    // Duration is enforced by SAMPLE COUNT, not elapsed time. A wall-clock timer plus
    // a one-second monitor loop turned a 0.2 s request into 0.152 s; the host clock
    // has no business deciding how many samples a capture contains.
    const std::uint64_t targetSamples = static_cast<std::uint64_t>(rateHz * captureSec);
    std::thread         captureTimer;
    if (captureSec > 0.0) {
        captureTimer = std::thread([&] {
            while (!stopRequested.load(std::memory_order_acquire)) {
                const auto c = counts();
                // Overshoot deliberately: stopping the graph truncates the file's tail,
                // so aiming exactly at the target lands under it. Capture past the mark
                // and trim to size afterwards - that is exact, where stopping on time
                // is not.
                if (std::ranges::all_of(c, [&](gr::Size_t v) { return static_cast<std::uint64_t>(v) >= targetSamples + 4UL * chunkSize; })) {
                    stopRequested.store(true, std::memory_order_release);
                    return;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
        });
    }

    auto prev  = counts();
    auto prevT = std::chrono::steady_clock::now();
    while (!stopRequested.load(std::memory_order_acquire)) {
        // Sleep in slices, not one second. In capture mode the stop signal must be
        // acted on promptly: a one-second latency lets the capture run past its byte
        // cap, and BasicFileSink's cap ROLLS THE FILE OVER rather than stopping, so a
        // late stop silently restarts the file and the duration is lost.
        for (int slice = 0; slice < 50 && !stopRequested.load(std::memory_order_acquire); ++slice) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }

        const auto   now     = std::chrono::steady_clock::now();
        const auto   cur     = counts();
        const double elapsed = std::chrono::duration<double>(now - prevT).count();

        for (std::size_t ch = 0UZ; ch < kChannels; ++ch) {
            const auto   delta = static_cast<gr::Size_t>(cur[ch] - prev[ch]); // modular: wrap-correct
            const double mss   = static_cast<double>(delta) / elapsed / 1e6;
            std::println("  ch{}  {:>14} samples  {:>7.2f} MS/s  {}", ch, cur[ch], mss, delta == 0U ? "*** NO DATA ***" : "");
        }
        // Reported, not merely bounded. This harness previously only set
        // max_overflow_count=0 ("never stop") and printed nothing, so a run that
        // overflowed throughout looked exactly like one that did not - and the
        // "zero overflows" attached to earlier full-rate results was read from the
        // sample ratio, never from this counter.
        std::println("       events  ovf {}  tmo {}  unf {}  cor {}  err {}", src._overflowCount.load(std::memory_order_relaxed), src._timeoutCount.load(std::memory_order_relaxed), src._underflowCount.load(std::memory_order_relaxed), src._corruptionCount.load(std::memory_order_relaxed), src._streamErrorCount.load(std::memory_order_relaxed));

        // The DEVICE's clock, not the host's. This is the only quantity that can
        // establish inter-radio epoch alignment; host timestamps carry scheduling
        // jitter orders of magnitude larger than the effect.
        if (src._deviceTimeValid.load(std::memory_order_relaxed)) {
            const std::int64_t dev = src._lastDeviceTimeNs.load(std::memory_order_relaxed);
            std::println("       device_time {}.{:09d} s", dev / 1'000'000'000, dev % 1'000'000'000);
        } else {
            std::println("       device_time UNAVAILABLE (no SOAPY_SDR_HAS_TIME on reads)");
        }
        if (tagMode && tagSinks[0]) {
            const auto& tags = tagSinks[0]->_tags;
            if (!tags.empty()) {
                const auto& last = tags.back();
                // device_time_ns is nested inside the tag's meta-info sub-map, not at
                // the top level: emitTimingTag puts device metadata under
                // tag::TRIGGER_META_INFO. pmt::Value::get_if is a MEMBER, not free.
                std::int64_t devNs = 0;
                bool         found = false;
                for (const auto& [k, v] : last.map) {
                    const auto* meta = v.get_if<gr::pmt::Value::Map>();
                    if (meta == nullptr) {
                        continue;
                    }
                    for (const auto& [mk, mv] : *meta) {
                        if (std::string_view(mk) == "device_time_ns") {
                            if (const auto* p = mv.get_if<std::int64_t>()) {
                                devNs = *p;
                                found = true;
                            }
                        }
                    }
                }
                std::println("       tags {} on ch0, last @ sample {}{}", tags.size(), last.index, found ? std::format(", device_time_ns {}", devNs) : ", (no device_time_ns in tag)");
            } else {
                std::println("       tags 0 on ch0");
            }
        }
        std::println("");
        prev  = cur;
        prevT = now;
    }

    if (captureTimer.joinable()) {
        captureTimer.join();
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
    if (waiter.joinable()) {
        waiter.detach(); // may still be blocked in getline() if we stopped for another reason
    }

    // TRIM TO EXACTLY THE REQUESTED DURATION. The capture ran past it; a file that is
    // "about" the right length is not a duration, it is an approximation, and the whole
    // point of expressing capture in samples was to stop approximating.
    if (captureSec > 0.0) {
        const auto wantBytes = static_cast<std::uintmax_t>(targetSamples * sizeof(TRadio));
        for (std::size_t ch = 0UZ; ch < kChannels; ++ch) {
            const char*       sfx  = std::getenv("WOMM_CAPTURE_SUFFIX");
            const std::string path = std::format("{}/capture_{}_ch{}{}.bin", captureDir, serial, ch, sfx ? sfx : "");
            std::error_code   ec;
            const auto        have = std::filesystem::file_size(path, ec);
            if (ec) {
                std::println(stderr, "capture {}: {}", path, ec.message());
                continue;
            }
            if (have < wantBytes) {
                std::println(stderr, "SHORT CAPTURE {}: {} of {} samples — duration NOT honoured", path, have / sizeof(TRadio), targetSamples);
                continue;
            }
            std::filesystem::resize_file(path, wantBytes, ec);
            if (ec) {
                std::println(stderr, "trim {}: {}", path, ec.message());
            }
        }
    }

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
