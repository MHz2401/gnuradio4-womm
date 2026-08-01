// womm_uhd_tx — the transmit counterpart of womm_uhd_rx.
//
// ⚠ THIS BINARY CAN RADIATE. It is built so that UhdSink is compiled and reviewable; it is
// not run without an operator at the console. Every emission goes through womm::txArm,
// which prints the parameters actually requested at runtime and REFUSES to proceed when
// stdin is not a terminal — which is what keeps it out of ctest, CI and background jobs.
//
// assert_tx_gated.cmake checks on the linked binary that the interlock is present.
//
// The default is a single short burst at minimum gain. There is no "run forever" mode and
// no way to raise the duration without saying so on the command line.

#include <gnuradio-4.0/Graph.hpp>
#include <gnuradio-4.0/Scheduler.hpp>
#include <gnuradio-4.0/sdr/UhdSink.hpp>
#include <gnuradio-4.0/testing/NullSources.hpp>

#include "womm_tx_arm.hpp"

#include <chrono>
#include <complex>
#include <cstdlib>
#include <print>
#include <string>
#include <thread>

namespace {

using TSample = std::complex<float>;
constexpr std::size_t kChannels = 1UZ;
using TSink                     = gr::blocks::sdr::UhdSink<TSample, kChannels>;

[[nodiscard]] double envOr(const char* key, double fallback) {
    const char* value = std::getenv(key);
    return value ? std::atof(value) : fallback;
}

} // namespace

int main(int argc, char** argv) {
    const double rateHz      = argc > 1 ? std::atof(argv[1]) : envOr("WOMM_TX_RATE", 1'000'000.);
    const double durationSec = argc > 2 ? std::atof(argv[2]) : envOr("WOMM_TX_DURATION", 1.);
    const double freqHz      = envOr("WOMM_TX_FREQ", 0.);
    const double gainDb      = envOr("WOMM_TX_GAIN", 0.);
    const auto   serial      = std::getenv("WOMM_TX_SERIAL");

    // No default frequency. A transmit centre frequency is a licensing decision and must be
    // stated deliberately, not inherited from a constant someone left in a header.
    if (freqHz <= 0.) {
        std::println(stderr, "WOMM_TX_FREQ is required and must be stated explicitly — there is no default transmit frequency.");
        return 2;
    }
    if (serial == nullptr) {
        std::println(stderr, "WOMM_TX_SERIAL is required — name the radio explicitly rather than transmitting from whichever enumerates first.");
        return 2;
    }

    gr::Graph  graph;
    const auto initStart = std::chrono::steady_clock::now();
    auto&      sink      = graph.emplaceBlock<TSink>({
             {"device_args", std::format("driver=uhd,serial={}", serial)},
             {"sample_rate", rateHz},
             {"frequency", std::vector<double>(kChannels, freqHz)},
             {"tx_gains", std::vector<double>(kChannels, gainDb)},
             {"verbose_events", true},
    });
    const double initSec = std::chrono::duration<double>(std::chrono::steady_clock::now() - initStart).count();
    std::println("INIT phase: {:.3f} s (blocking, in init(), before any scheduler exists)", initSec);

    if (!sink._initialised) {
        std::println(stderr, "device did not initialise — refusing to continue");
        return 1;
    }

    // Report what the DEVICE settled on, not what was asked for, and arm on those values.
    const womm::TxEmission emission{
        .device      = std::format("uhd serial={}", serial),
        .frequencyHz = sink._reportedFrequencies.empty() ? freqHz : sink._reportedFrequencies.front(),
        .bandwidthHz = sink._reportedSampleRates.empty() ? rateHz : sink._reportedSampleRates.front(),
        .gainDb      = gainDb,
        .antenna     = sink.tx_antennae.value.front(),
        .durationSec = durationSec,
        .dutyCycle   = 1.,
    };
    if (!womm::txArm(emission)) {
        return 3; // not armed: nothing was transmitted
    }

    auto& source = graph.emplaceBlock<gr::testing::NullSource<TSample>>();
    if (!graph.connect(source, "out", sink, "in#0")) {
        std::println(stderr, "connect failed");
        return 1;
    }

    gr::scheduler::Simple<gr::scheduler::ExecutionPolicy::multiThreaded> sched;
    if (auto exchanged = sched.exchange(std::move(graph)); !exchanged) {
        std::println(stderr, "scheduler exchange failed: {}", exchanged.error());
        return 1;
    }

    std::thread runner([&] { std::ignore = sched.runAndWait(); });
    std::this_thread::sleep_for(std::chrono::duration<double>(durationSec));
    sched.requestStop();
    runner.join();

    std::println("");
    std::println("underflow {}  seq_error {}  time_error {}  burst_ack {}  write_error {}  host_starved {}", //
        sink._underflowCount.load(), sink._seqErrorCount.load(), sink._timeErrorCount.load(), sink._burstAckCount.load(), sink._writeErrorCount.load(), sink._ringStarvedCount.load());
    std::println("underflow is the TX condition: the device consumes at a constant rate and the host did not keep up.");
    return 0;
}
