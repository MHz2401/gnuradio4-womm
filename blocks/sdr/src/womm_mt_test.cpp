// Is multi-process actually necessary? N radios x 2 channels in ONE graph.
//
// THE QUESTION. Every figure that made multi-process look necessary came from
// womm_bmax - the single-process harness - and every one of those runs was
// SINGLE-CHANNEL at DSP depth 8, i.e. crippled by the same bug that made all
// pre-2026-07-27 measurements half-blind. The 122.88 MS/s that works was taken
// with N separate processes, 2 channels, depth 0. Topology, channel count and
// DSP depth were all changed at once, so none of them is attributable.
//
// This harness holds everything at the working configuration - 2 channels,
// depth 0, pool sized once - and changes ONLY the topology: one process, one
// graph, one scheduler, N radios. Compare its aggregate against 122.88 MS/s.
//
//   matches  -> multi-process is not required, and the cross-process barrier
//               problem is work we may never need
//   short    -> multi-process is genuinely necessary, and for the first time we
//               know why, because every other variable is held fixed
//
// RECEIVE ONLY. No SoapySink.hpp, no transmit path; assert_no_tx.cmake asserts
// that on the linked binary. No serials or RF parameters are committed here.

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <complex>
#include <cstdlib>
#include <numeric>
#include <print>
#include <string>
#include <thread>
#include <vector>

#include <unistd.h>

#include <gnuradio-4.0/Graph.hpp>
#include <gnuradio-4.0/Scheduler.hpp>
#include <gnuradio-4.0/thread/thread_pool.hpp>

#include <filesystem>

#include <gnuradio-4.0/fileio/BasicFileIo.hpp>
#include <gnuradio-4.0/sdr/SoapySource.hpp>
#include <gnuradio-4.0/testing/NullSources.hpp>

using TRadio = std::complex<float>;

inline constexpr std::size_t kChannels    = 2UZ;
inline constexpr double      kMcrHz       = 30.72e6; // UHD's 2-channel maximum
inline constexpr double      kRateHz      = kMcrHz / 2.0;
inline constexpr double      kCentreFreq  = 2401e6;
inline constexpr double      kRxGainDb    = 20.0;
inline constexpr double      kWarmupSec   = 5.0;
inline constexpr double      kMeasureSec  = 20.0;

inline constexpr std::array<std::string_view, 2> kRxOnlyAntennae{"RX2", "TX/RX"};

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

struct RadioTap {
    std::string                                                       serial;
    std::array<gr::testing::CountingSink<TRadio>*, kChannels>         sinks{};
    std::array<gr::blocks::fileio::BasicFileSink<TRadio>*, kChannels> files{};

    gr::Size_t count(std::size_t ch) const { return files[ch] ? static_cast<gr::Size_t>(files[ch]->_totalBytesWritten / sizeof(TRadio)) : sinks[ch]->count.value; }
};

int main(int argc, char* argv[]) {
    using namespace std::string_literals;

    const double      rateHz  = argc > 1 ? std::atof(argv[1]) : envOr("WOMM_RATE", kRateHz);
    const std::string antenna = argc > 2 ? argv[2] : "TX/RX";
    if (std::ranges::find(kRxOnlyAntennae, antenna) == kRxOnlyAntennae.end()) {
        std::println(stderr, "refusing to run: '{}' is not on the receive-only antenna allow-list", antenna);
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

    // Sized ONCE, for the whole machine - which is the point. Multi-process forces
    // every process to size its own pool, and getting that wrong cost 12x
    // (3.54 vs 40.89 MS/s) before it was found.
    if (const char* env = std::getenv("WOMM_THREADS"); env) {
        const auto n = static_cast<std::uint32_t>(std::atol(env));
        using namespace gr::thread_pool;
        Manager::instance().replacePool(std::string(kDefaultCpuPoolId), std::make_shared<ThreadPoolWrapper>(std::make_unique<BasicThreadPool>(std::string(kDefaultCpuPoolId), TaskType::CPU_BOUND, n, n), "CPU"));
    }

    const double      captureSec = envOr("WOMM_CAPTURE_SEC", 0.0);
    const std::string captureDir = std::getenv("WOMM_CAPTURE_DIR") ? std::getenv("WOMM_CAPTURE_DIR") : "data";
    const char*       sfx        = std::getenv("WOMM_CAPTURE_SUFFIX");

    // A capture ends on a chunk boundary, so the chunk must fit inside the request.
    std::uint32_t chunkSize = 512U << 4U;
    if (captureSec > 0.0) {
        const auto want = static_cast<std::uint64_t>(rateHz * captureSec);
        while (chunkSize > 512U && static_cast<std::uint64_t>(chunkSize) > want) {
            chunkSize >>= 1U;
        }
        std::filesystem::create_directories(captureDir);
    }
    const auto targetSamples = static_cast<std::uint64_t>(rateHz * captureSec);

    gr::Graph             graph;
    std::vector<RadioTap> taps;
    for (const auto& serial : serials) {
        gr::property_map cfg{
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
            {"max_overflow_count", gr::Size_t{0}},
            {"max_chunk_size", chunkSize},
            {"emit_timing_tags", false}, // depth 0 and throughput only: no tag cost
            {"emit_meta_info", false},
        };
        if (const char* env = std::getenv("WOMM_EXTCLK"); env && std::string_view(env) == "1") {
            cfg["clock_source"]      = std::string("external");
            cfg["time_source"]       = std::string("external");
            cfg["start_time_offset"] = 5.0f;
        }
        auto&    src = graph.emplaceBlock<gr::blocks::sdr::SoapySource<TRadio, kChannels>>(cfg);
        RadioTap tap{.serial = serial};
        for (std::size_t ch = 0UZ; ch < kChannels; ++ch) {
            if (captureSec > 0.0) {
                // Cap must clear the deliberate overshoot below: max_bytes_per_file ROLLS
                // THE FILE OVER rather than stopping, and in overwrite mode that restarts
                // the same filename from zero.
                const std::string path = std::format("{}/capture_{}_ch{}{}.bin", captureDir, serial, ch, sfx ? sfx : "");
                auto&             sink = graph.emplaceBlock<gr::blocks::fileio::BasicFileSink<TRadio>>({{"file_name", path}, {"max_bytes_per_file", static_cast<gr::Size_t>((targetSamples + 8UL * chunkSize) * sizeof(TRadio))}});
                mustConnect(graph.connect(src, std::format("out#{}", ch), sink, "in"s), std::format("{} out#{} -> file", serial, ch));
                tap.files[ch] = std::addressof(sink);
            } else {
                auto& sink = graph.emplaceBlock<gr::testing::CountingSink<TRadio>>({{"n_samples_max", gr::Size_t{0}}});
                mustConnect(graph.connect(src, std::format("out#{}", ch), sink, "in"s), std::format("{} out#{}", serial, ch));
                tap.sinks[ch] = std::addressof(sink);
            }
        }
        taps.push_back(tap);
    }

    std::println("womm MT test — {} radios x {} channels in ONE graph, ONE scheduler", taps.size(), kChannels);
    std::println("RECEIVE ONLY. {:.2f} MS/s/channel, MCR {:.2f} MHz, antenna {}", rateHz / 1e6, envOr("WOMM_MCR", kMcrHz) / 1e6, antenna);
    std::println("target if topology is irrelevant: {:.2f} MS/s aggregate", rateHz * static_cast<double>(kChannels * taps.size()) / 1e6);

    gr::scheduler::Simple<gr::scheduler::ExecutionPolicy::multiThreaded> sched;
    if (auto r = sched.exchange(std::move(graph)); !r) {
        std::println(stderr, "scheduler init failed: {}", r.error());
        return 1;
    }

    alarm(static_cast<unsigned>(kWarmupSec + kMeasureSec) + 240U);

    std::atomic<bool> ok{true};
    std::thread       runner([&] { ok.store(sched.runAndWait().has_value(), std::memory_order_relaxed); });

    // Wait for EVERY channel to produce, then settle. Four B210s bring up serially
    // at ~2.5 s each, and with an external time source each also blocks ~2 s in
    // set_time_unknown_pps - so a fixed sleep would measure start-up, not rate.
    const auto ready = [&] {
        return std::ranges::all_of(taps, [](const RadioTap& t) {
            for (std::size_t ch = 0UZ; ch < kChannels; ++ch) {
                if (t.count(ch) == 0U) {
                    return false;
                }
            }
            return true;
        });
    };
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(180);
    while (!ready() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    if (!ready()) {
        std::println(stderr, "not every channel produced samples within 180 s");
    }
    std::this_thread::sleep_for(std::chrono::duration<double>(kWarmupSec));

    std::vector<std::array<gr::Size_t, kChannels>> start(taps.size());
    for (std::size_t i = 0UZ; i < taps.size(); ++i) {
        for (std::size_t ch = 0UZ; ch < kChannels; ++ch) {
            start[i][ch] = taps[i].count(ch);
        }
    }
    const auto t0 = std::chrono::steady_clock::now();
    if (captureSec > 0.0) {
        // Duration by SAMPLE COUNT, overshooting deliberately because stopping the graph
        // truncates the tail; trimmed to exact length after the stop.
        const auto by = std::chrono::steady_clock::now() + std::chrono::seconds(120);
        while (std::chrono::steady_clock::now() < by) {
            bool done = true;
            for (const auto& t : taps) {
                for (std::size_t ch = 0UZ; ch < kChannels; ++ch) {
                    done = done && static_cast<std::uint64_t>(t.count(ch)) >= targetSamples + 4UL * chunkSize;
                }
            }
            if (done) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    } else {
        std::this_thread::sleep_for(std::chrono::duration<double>(kMeasureSec));
    }
    const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();

    double aggregate = 0.0;
    std::println("");
    for (std::size_t i = 0UZ; i < taps.size(); ++i) {
        for (std::size_t ch = 0UZ; ch < kChannels; ++ch) {
            const auto   delta = static_cast<gr::Size_t>(taps[i].count(ch) - start[i][ch]);
            const double mss   = static_cast<double>(delta) / elapsed / 1e6;
            aggregate += mss;
            std::println("  {} ch{}  {:>8.4f} MS/s  ratio {:.4f}", taps[i].serial, ch, mss, mss * 1e6 / rateHz);
        }
    }
    const double target = rateHz * static_cast<double>(kChannels * taps.size()) / 1e6;
    std::println("");
    std::println("AGGREGATE {:.2f} MS/s of {:.2f} target   ratio {:.4f}", aggregate, target, aggregate / target);

    if (captureSec > 0.0) {
        const auto wantBytes = static_cast<std::uintmax_t>(targetSamples * sizeof(TRadio));
        for (const auto& t : taps) {
            for (std::size_t ch = 0UZ; ch < kChannels; ++ch) {
                const std::string path = std::format("{}/capture_{}_ch{}{}.bin", captureDir, t.serial, ch, sfx ? sfx : "");
                std::error_code   ec;
                const auto        have = std::filesystem::file_size(path, ec);
                if (ec || have < wantBytes) {
                    std::println(stderr, "SHORT CAPTURE {}: {} of {} samples", path, ec ? 0UL : have / sizeof(TRadio), targetSamples);
                    continue;
                }
                std::filesystem::resize_file(path, wantBytes, ec);
            }
        }
        std::println("captured {} samples/channel to {}", targetSamples, captureDir);
    }

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
    return aggregate / target >= 0.98 ? 0 : 1;
}
