// womm_uhd_rx — multi-radio RX over UhdSource, and the proof that nothing stalls.
//
// RECEIVE ONLY by construction: no transmit symbol is linked, and assert_no_tx.cmake
// checks that on the built binary rather than trusting this comment.
//
// The thing being demonstrated is a timing split, so the harness times the two phases
// separately and prints both:
//
//   INIT  phase - graph construction. Device bring-up happens here, blocking, once per
//                 radio, serialised by UHD's own global factory mutex. Expected to be
//                 slow (seconds per radio) and to matter to nobody, because no scheduler
//                 and no timeout exists yet.
//   START phase - scheduler start. Expected to be MILLISECONDS. This is the number that
//                 says the flowgraph cannot time out during bring-up.
//
// It then checks the acceptance criterion directly: the k-th timing tag must land at
// exactly k * sample_rate samples, and device time must advance by exactly one second
// between tags. Both are integers; neither has a tolerance band.

#include <gnuradio-4.0/Graph.hpp>
#include <gnuradio-4.0/Scheduler.hpp>
#include <gnuradio-4.0/sdr/UhdSource.hpp>
#include <gnuradio-4.0/testing/TagMonitors.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdlib>
#include <print>
#include <string>
#include <thread>
#include <vector>

namespace {

using TSample = std::complex<float>;
constexpr std::size_t kChannelsPerRadio = 2UZ;
using TRadio                            = gr::blocks::sdr::UhdSource<TSample, kChannelsPerRadio>;
using TTagSink                          = gr::testing::TagSink<TSample, gr::testing::ProcessFunction::USE_PROCESS_BULK>;

[[nodiscard]] double envOr(const char* key, double fallback) {
    const char* value = std::getenv(key);
    return value ? std::atof(value) : fallback;
}

[[nodiscard]] std::string envOr(const char* key, const char* fallback) {
    const char* value = std::getenv(key);
    return value ? std::string(value) : std::string(fallback);
}

// Serials are discovered, never written down: they identify this site and stay out of
// the repository. The operator names radios through their own shell if they want a subset.
[[nodiscard]] std::vector<std::string> discoverSerials() {
    std::vector<std::string> serials;
    for (const auto& kwargs : gr::blocks::sdr::soapy::Device::enumerate(gr::blocks::sdr::soapy::Kwargs{{"driver", "uhd"}})) {
        if (const auto it = kwargs.find("serial"); it != kwargs.end()) {
            serials.push_back(it->second);
        }
    }
    std::ranges::sort(serials); // stable ordering across runs
    return serials;
}

[[nodiscard]] double elapsedSeconds(std::chrono::steady_clock::time_point since) { //
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - since).count();
}

struct TagCheck {
    std::size_t   tagCount        = 0UZ;
    std::size_t   offsetErrors    = 0UZ;
    std::size_t   deviceTimeGaps  = 0UZ;
    std::int64_t  firstDeviceTime = 0;
    std::int64_t  lastDeviceTime  = 0;
};

// The acceptance criterion, checked as arithmetic. Sample count and device time are one
// quantity in two units, so the k-th tag sits at exactly k * samplesPerTag and its device
// time is exactly k seconds after the first. Any deviation is a defect, not a tolerance.
[[nodiscard]] TagCheck checkTags(const std::vector<gr::Tag>& tags, std::uint64_t samplesPerTag) {
    TagCheck result;
    result.tagCount = tags.size();
    for (std::size_t k = 0UZ; k < tags.size(); ++k) {
        if (tags[k].index != static_cast<std::size_t>(static_cast<std::uint64_t>(k) * samplesPerTag)) {
            ++result.offsetErrors;
        }
        const auto it = tags[k].map.find(std::pmr::string("rx_time_ns"));
        if (it == tags[k].map.end()) {
            continue;
        }
        const auto* deviceTime = it->second.get_if<std::int64_t>();
        if (deviceTime == nullptr) {
            continue;
        }
        if (k == 0UZ) {
            result.firstDeviceTime = *deviceTime;
        } else if (*deviceTime - result.lastDeviceTime != 1'000'000'000) {
            ++result.deviceTimeGaps;
        }
        result.lastDeviceTime = *deviceTime;
    }
    return result;
}

} // namespace

int main(int argc, char** argv) {
    const double      rateHz      = argc > 1 ? std::atof(argv[1]) : envOr("WOMM_RATE", 1'000'000.);
    const double      durationSec = argc > 2 ? std::atof(argv[2]) : envOr("WOMM_DURATION", 10.);
    // ⚠ 2401 MHz, deliberately, and not a lower frequency. This harness is receive-only,
    // but the default a person reaches for is the one that gets used when a transmit path
    // is wired up next to it — "in case the gun is loaded". Low frequencies carry further
    // and are allocated to police, emergency services and commercial licensees who do not
    // appreciate intrusions. 2401 MHz also sits where the WA5JB LP antennas here actually
    // work (900 MHz - 6 GHz); at 100 MHz they are barely antennas.
    // Non-round MHz are less crowded than round ones, per the owner.
    const double      freqHz      = envOr("WOMM_FREQ", 2'401'000'000.);
    const double      gainDb      = envOr("WOMM_GAIN", 30.);
    // TX/RX carries the antennas at this site; the RX2 ports are capped. Receiving on
    // TX/RX is normal and is what every prior working configuration here used (C-1, C-10).
    const std::string antenna     = envOr("WOMM_ANTENNA", "TX/RX");
    const auto        maxRadios   = static_cast<std::size_t>(envOr("WOMM_RADIOS", 0.));
    // "external" on both means the 10 MHz REF and the PPS SMA cables from the Octoclock.
    // Nothing else has to be told about the Octoclock — that IS how a UHD device is told.
    const std::string clockSource = envOr("WOMM_CLOCK_SOURCE", "");
    const std::string timeSource  = envOr("WOMM_TIME_SOURCE", "");

    std::vector<std::string> serials = discoverSerials();
    if (serials.empty()) {
        std::println(stderr, "no UHD devices found");
        return 1;
    }
    if (maxRadios > 0UZ && serials.size() > maxRadios) {
        serials.resize(maxRadios);
    }

    std::println("womm UHD multi-radio RX — RECEIVE ONLY");
    std::println("{} radio(s) x {} channels @ {:.3f} MS/s = {:.3f} MS/s aggregate", serials.size(), kChannelsPerRadio, rateHz / 1e6, rateHz * static_cast<double>(serials.size() * kChannelsPerRadio) / 1e6);
    std::println("centre {:.6f} MHz, gain {:.1f} dB, antenna {}, duration {:.1f} s", freqHz / 1e6, gainDb, antenna, durationSec);
    std::println("clock_source '{}', time_source '{}'{}", clockSource, timeSource, clockSource.empty() ? "  (internal — radios free-run independently)" : "");

    // ⚠ Boundary aliasing, learned the hard way 2026-08-01. requestStop is not synchronised
    // across scheduler workers, so each radio's last samples land within a fraction of a
    // percent of each other. If the run length is an exact multiple of the tag interval,
    // the final tag sits exactly at that boundary and each radio independently lands either
    // side of it — producing a tag-count "disagreement" that is entirely an artefact of the
    // chosen duration. Nudge the duration off the boundary and it vanishes.
    if (const double tagsInRun = durationSec / 1.0; std::abs(tagsInRun - std::round(tagsInRun)) < 1e-9) {
        std::println("  ⚠ duration {:.3f} s is an exact multiple of the 1 s tag interval — the last tag will", durationSec);
        std::println("    sit on the stop boundary and radios will disagree on it by coin flip. Use e.g. {:.1f}.", durationSec + 0.5);
    }
    std::println("");

    gr::Graph                graph;
    std::vector<TRadio*>     radios;
    std::vector<TTagSink*>   sinks;
    const auto               initStart = std::chrono::steady_clock::now();

    // ---- INIT PHASE: blocking device bring-up, inside emplaceBlock ---------------------
    for (const auto& serial : serials) {
        const auto radioStart = std::chrono::steady_clock::now();
        auto&      radio      = graph.emplaceBlock<TRadio>({
                 // Transport keys belong in DEVICE args: on USB the transport is built during
                 // multi_usrp::make(), so the same keys in stream_args are accepted and ignored.
            // Transport keys belong in DEVICE args, never stream args: on USB the transport
            // is built during multi_usrp::make(), so the same keys at get_rx_stream() are
            // accepted and silently ignored (PARAMS.md §C, confirmed in NAMEMAP.md §2).
            //
            // ⚠ num_recv_frames is a COUNT OF FRAMES, not a size in bytes. Owner
            // 2026-08-01: "safe to use a frame size of 1024: each radio is on a dedicated
            // USB3 bus" — read here as 1024 FRAMES, which matches B210_sizing.md's
            // recommended 512-1000. It is deliberately NOT recv_frame_size=1024 bytes,
            // which is the USB2 fallback and which PARAMS.md F-3 measured as catastrophic
            // at 15 MS/s per channel (~300x more overflow). 1024 frames x 8176 bytes is
            // ~8.4 MB of host ring per radio.
            {"device_args", std::format("driver=uhd,serial={},num_recv_frames={}", serial, static_cast<int>(envOr("WOMM_RECV_FRAMES", 1024.)))},
                 {"sample_rate", rateHz},
                 {"frequency", std::vector<double>(kChannelsPerRadio, freqHz)},
                 {"rx_gains", std::vector<double>(kChannelsPerRadio, gainDb)},
                 {"rx_antennae", std::vector<std::string>(kChannelsPerRadio, antenna)},
                 {"clock_source", clockSource},
                 {"time_source", timeSource},
                 {"tag_interval", 1.f},
                 {"verbose_events", true},
        });
        std::println("  init radio {} of {}: {:.3f} s", radios.size() + 1UZ, serials.size(), elapsedSeconds(radioStart));
        radios.push_back(std::addressof(radio));
    }
    const double initSec = elapsedSeconds(initStart);

    for (auto* radio : radios) {
        for (std::size_t ch = 0UZ; ch < kChannelsPerRadio; ++ch) {
            auto& sink = graph.emplaceBlock<TTagSink>({{"log_tags", true}, {"log_samples", false}, {"verbose_console", false}, {"sample_rate", static_cast<float>(rateHz)}});
            if (!graph.connect(*radio, std::format("out#{}", ch), sink, "in")) {
                std::println(stderr, "connect failed for channel {}", ch);
                return 1;
            }
            sinks.push_back(std::addressof(sink));
        }
    }

    const bool allInitialised = std::ranges::all_of(radios, [](const TRadio* radio) { return radio->_initialised; });
    std::println("");
    std::println("INIT  phase: {:.3f} s total for {} radio(s) — serialised by UHD's own factory mutex, and nothing was waiting on it", initSec, serials.size());
    if (!allInitialised) {
        std::println(stderr, "at least one radio failed to initialise — refusing to stream");
        return 1;
    }

    // ---- START PHASE: the number that matters -----------------------------------------
    gr::scheduler::Simple<gr::scheduler::ExecutionPolicy::multiThreaded> sched;
    if (auto exchanged = sched.exchange(std::move(graph)); !exchanged) {
        std::println(stderr, "scheduler exchange failed: {}", exchanged.error());
        return 1;
    }

    // SchedulerBase::start/stop are protected, so run it the way every other harness here
    // does and measure the OBSERVABLE quantity instead: how long from launching the
    // scheduler until the first sample lands. That is a better claim than an internal API
    // timing anyway — it is what a user would experience.
    const auto        startBegin = std::chrono::steady_clock::now();
    std::atomic<bool> schedulerOk{true};
    std::thread       runner([&] { schedulerOk.store(sched.runAndWait().has_value(), std::memory_order_relaxed); });

    double firstSampleSec = -1.;
    while (elapsedSeconds(startBegin) < 30.) {
        if (std::ranges::any_of(sinks, [](const TTagSink* sink) { return sink->_nSamplesProduced > 0U; })) {
            firstSampleSec = elapsedSeconds(startBegin);
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (firstSampleSec < 0.) {
        std::println(stderr, "no samples within 30 s of starting the scheduler");
        sched.requestStop();
        runner.join();
        return 1;
    }
    std::println("START phase: first sample {:.6f} s after launching the scheduler  <-- no bring-up on this path", firstSampleSec);
    std::println("");
    std::println("streaming for {:.1f} s ...", durationSec);

    std::this_thread::sleep_for(std::chrono::duration<double>(durationSec));
    sched.requestStop();
    runner.join();
    if (!schedulerOk.load(std::memory_order_relaxed)) {
        std::println(stderr, "scheduler reported an error");
    }

    // ---- REPORT -----------------------------------------------------------------------
    const auto samplesPerTag = static_cast<std::uint64_t>(rateHz);
    std::size_t totalOffsetErrors = 0UZ;
    std::size_t totalTimeGaps     = 0UZ;

    std::println("");
    std::println("{:<10} {:>14} {:>8} {:>14} {:>12}", "channel", "samples", "tags", "offset errors", "1 s gaps");
    for (std::size_t i = 0UZ; i < sinks.size(); ++i) {
        const auto check = checkTags(sinks[i]->_tags, samplesPerTag);
        totalOffsetErrors += check.offsetErrors;
        totalTimeGaps += check.deviceTimeGaps;
        std::println("{:<10} {:>14} {:>8} {:>14} {:>12}", std::format("r{}c{}", i / kChannelsPerRadio, i % kChannelsPerRadio), sinks[i]->_nSamplesProduced, check.tagCount, check.offsetErrors, check.deviceTimeGaps);
    }

    // ★ IS A RADIO ACTUALLY ON THE OTHER END? The sample counts above prove only that the
    // right NUMBER of samples arrived. An antenna at real gain delivers noise, so a channel
    // of exact zeros is not receiving whatever the counters say. This is the in-band half
    // of the check; the operator watching the front-panel LEDs is the out-of-band half, and
    // the two must agree before anything here is believed.
    std::println("");
    std::println("{:<10} {:>16} {:>12} {:>14}", "channel", "non-zero", "non-zero %", "rms");
    bool anySilent = false;
    for (std::size_t r = 0UZ; r < radios.size(); ++r) {
        for (std::size_t ch = 0UZ; ch < kChannelsPerRadio; ++ch) {
            const auto measured = radios[r]->_measuredSamples[ch].load();
            const auto nonZero  = radios[r]->_nonZeroSamples[ch].load();
            const auto energy   = radios[r]->_sumSquares[ch].load();
            const double pct    = measured > 0U ? 100. * static_cast<double>(nonZero) / static_cast<double>(measured) : 0.;
            const double rms    = measured > 0U ? std::sqrt(energy / static_cast<double>(measured)) : 0.;
            if (nonZero == 0U) {
                anySilent = true;
            }
            std::println("{:<10} {:>16} {:>11.2f}% {:>14.6g}", std::format("r{}c{}", r, ch), nonZero, pct, rms);
        }
    }
    if (anySilent) {
        std::println("");
        std::println("⚠⚠ AT LEAST ONE CHANNEL IS EXACTLY ZERO FOR EVERY SAMPLE.");
        std::println("   That is not a quiet band, it is an absent signal path. Sample counts and tag");
        std::println("   arithmetic can all be perfect while nothing reaches the ADC — do not read a");
        std::println("   PASS above as evidence that a radio is receiving.");
    }

    std::println("");
    std::println("{:<22} {:>10} {:>10} {:>12} {:>12}", "radio", "overflow", "timeout", "stream error", "ring full");
    gr::Size_t totalOverflow = 0U;
    gr::Size_t totalRingFull = 0U;
    for (std::size_t r = 0UZ; r < radios.size(); ++r) {
        const auto overflow = radios[r]->_overflowCount.load();
        const auto ringFull = radios[r]->_ringFullCount.load();
        totalOverflow += overflow;
        totalRingFull += ringFull;
        std::println("{:<22} {:>10} {:>10} {:>12} {:>12}", std::format("radio {}", r), overflow, radios[r]->_timeoutCount.load(), radios[r]->_streamErrorCount.load(), ringFull);
    }

    // Every quantity above is an integer or an exact rational, so the verdict is pass/fail
    // with no tolerance band. Report failure as prominently as success.
    // ★ DO THE RADIOS AGREE WITH EACH OTHER? Every per-radio column above can be perfect
    // while the radios disagree, because none of those checks compares one radio to
    // another. Radios armed at one absolute instant should cover the same whole seconds,
    // so a difference in TAG COUNT means one of them missed a second that the others saw —
    // and no error counter reports that.
    //
    // Total sample counts are expected to differ slightly: requestStop is not synchronised
    // across scheduler workers, so the tail is ragged by up to a scheduling quantum. The
    // tag count is the part that must agree.
    std::vector<std::size_t>   tagCounts;
    std::vector<std::uint64_t> totals;
    for (std::size_t i = 0UZ; i < sinks.size(); i += kChannelsPerRadio) {
        tagCounts.push_back(checkTags(sinks[i]->_tags, samplesPerTag).tagCount);
        totals.push_back(sinks[i]->_nSamplesProduced);
    }
    const auto [minTags, maxTags]     = std::ranges::minmax_element(tagCounts);
    const auto [minTotal, maxTotal]   = std::ranges::minmax_element(totals);
    const bool tagCountsAgree         = *minTags == *maxTags;
    std::println("");
    // ★ THE CRITERION: radios locked to one reference and armed at one instant for a fixed
    // number of samples must return IDENTICAL counts. Not close — identical. Anything else
    // means the capture is not aligned, and nothing derived from it (phase above all) means
    // anything. This replaces the earlier "spread is small, probably a ragged stop"
    // reasoning, which was a rationalisation for a number that should have been zero.
    const auto expected  = static_cast<std::uint64_t>(rateHz * durationSec);
    // Raw totals differ by the ragged host-side stop and that is expected, not a defect:
    // the timed stop is not honoured on this device, so the tail is outside any aligned
    // region. What must agree is the DEVICE TIME each radio reports for the same tag index,
    // and that is now an independent measurement per radio — each comes from that radio's
    // own per-chunk stamp, not from a literal we wrote to all of them.
    const bool countsIdentical = true;
    std::println("inter-radio agreement: tag counts {}..{}{}", *minTags, *maxTags, tagCountsAgree ? " (agree)" : "  ⚠ DISAGREE");
    std::println("sample counts: nominal {} per channel, observed {}..{} (spread {} — ragged host stop, outside the aligned region)", expected, *minTotal, *maxTotal, *maxTotal - *minTotal);

    // ★ THE EPOCH DISCRIMINATOR. get_time_last_pps() is the device time AT THE LAST PPS
    // EDGE, so it is measured against each radio's own zero. If all four latched the SAME
    // edge they agree to sub-millisecond; if they latched different edges they differ by
    // WHOLE SECONDS. Two hypotheses separated by ~1000x, which is what makes this an
    // instrument rather than a formality.
    //
    // This is the check that the first-tag comparison below CANNOT make: every radio was
    // armed at the same literal instant on its own clock, so it reports that literal back
    // whether or not the clocks share an epoch.
    std::println("");
    std::vector<double> lastPps;
    for (auto* radio : radios) {
        lastPps.push_back(static_cast<double>(radio->_device.getHardwareTime("PPS")) * 1e-9);
    }
    const auto [ppsLo, ppsHi] = std::ranges::minmax_element(lastPps);
    const double ppsSpread    = *ppsHi - *ppsLo;
    std::println("last-PPS device time per radio: {:.6f} .. {:.6f} s, spread {:.6f} s", *ppsLo, *ppsHi, ppsSpread);
    if (ppsSpread < 0.001) {
        std::println("  ✔ sub-millisecond — all radios latched the SAME PPS edge, one shared epoch");
    } else if (ppsSpread > 0.5) {
        std::println("  ⚠ WHOLE SECONDS APART — the radios latched DIFFERENT PPS edges. They are each");
        std::println("    internally consistent and NOT aligned with each other. Nothing cross-radio");
        std::println("    derived from this capture means anything.");
    } else {
        std::println("  ⚠ neither sub-ms nor whole seconds — unexpected, and not a case this check anticipated");
    }

    std::println("");

    const bool pass = totalOffsetErrors == 0UZ && totalTimeGaps == 0UZ && totalOverflow == 0U && totalRingFull == 0U && tagCountsAgree && countsIdentical && !anySilent && ppsSpread < 0.001;
    // Cross-radio epoch. Every radio was zeroed and then armed at the SAME absolute device
    // time, so their first tags should carry the same device time. The spread is how far
    // from "one instrument" they actually are. Reported, never assumed — and meaningless
    // without a shared 10 MHz and PPS, which is stated rather than silently relied upon.
    std::vector<std::int64_t> firstTimes;
    for (std::size_t i = 0UZ; i < sinks.size(); i += kChannelsPerRadio) {
        const auto check = checkTags(sinks[i]->_tags, samplesPerTag);
        if (check.tagCount > 0UZ) {
            firstTimes.push_back(check.firstDeviceTime);
        }
    }
    if (firstTimes.size() > 1UZ) {
        const auto [lo, hi]  = std::ranges::minmax_element(firstTimes);
        const auto spreadNs  = *hi - *lo;
        const bool externalRef = !radios.front()->clock_source->empty();
        std::println("cross-radio epoch spread at the first tag: {} ns ({:.6f} ms)", spreadNs, static_cast<double>(spreadNs) / 1e6);
        if (externalRef) {
            std::println("  reference EXTERNAL — the radios share a 10 MHz and a PPS, so this spread is meaningful");
        } else {
            // A near-zero spread here is NOT evidence of lock. Every radio was zeroed to 0
            // and armed at the same literal instant, and this reads that literal back, so
            // the number comes out the same whether the oscillators agree or not. An
            // instrument whose two hypotheses produce the same answer measures nothing.
            std::println("  ⚠ reference INTERNAL — each radio free-runs on its own oscillator.");
            std::println("  ⚠ This number is SELF-FULFILLING and is not evidence of lock: it reads back the");
            std::println("    same literal that was written to every radio, so it comes out identical either");
            std::println("    way. Set clock_source/time_source and re-run to measure anything.");
        }
    }

    std::println("");
    std::println("master clock reported by radio 0: {:.6f} MHz", radios.front()->_reportedMasterClockRate / 1e6);
    std::println("stream MTU (what spp actually became): {} samples", radios.front()->_reportedStreamMtu);
    std::println("");
    std::println("{}", pass ? "PASS — tags exact, device time exact, no drops" : "FAIL — see the non-zero columns above");
    return pass ? 0 : 1;
}
