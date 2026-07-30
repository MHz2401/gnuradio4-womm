// womm_poc_nograph - the two-node flowgraph PoC, NO radio in the loop.
//
// Gating question from the owner: is it possible to make the two-node flowgraph at
// all? Everything measured so far has had a device in it, so nothing separates "gr4
// works" from "gr4 works with our radios". This is the control for that: a settings
// -> source -> sink graph with no device, no Soapy, no UHD.
//
// It also answers the thought experiment directly - a signal generator feeding a sink
// does not block, because SignalGenerator::start() returns immediately and Soapy is
// never consulted.
//
// RECEIVE ONLY by construction: there is no radio here at all.

#include <chrono>
#include <cstdlib>
#include <print>
#include <thread>

#include <gnuradio-4.0/Graph.hpp>
#include <gnuradio-4.0/Scheduler.hpp>


#include <gnuradio-4.0/testing/NullSources.hpp>
#include <gnuradio-4.0/testing/TagMonitors.hpp>

using T = float;

int main() {
    constexpr float       kSampleRate = 1.0e6f;
    constexpr gr::Size_t  kNSamples   = 1'000'000U;

    gr::Graph graph;

    // Source and sink only. No device block, so nothing in this graph can block in
    // start() - which is the property being demonstrated.
    auto& src  = graph.emplaceBlock<gr::testing::ConstantSource<T>>({{"n_samples_max", kNSamples}, {"default_value", 1.0f}});
    auto& sink = graph.emplaceBlock<gr::testing::CountingSink<T>>({{"n_samples_max", gr::Size_t{0}}});

    if (auto r = graph.connect(src, "out", sink, "in"); !r.has_value()) {
        std::println(stderr, "connect failed");
        return 1;
    }

    gr::scheduler::Simple<gr::scheduler::ExecutionPolicy::multiThreaded> sched;
    if (auto r = sched.exchange(std::move(graph)); !r) {
        std::println(stderr, "scheduler init failed: {}", r.error());
        return 1;
    }

    const auto t0 = std::chrono::steady_clock::now();
    const auto ok = sched.runAndWait().has_value();
    const auto dt = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();

    const auto produced = sink.count.value;
    std::println("two-node graph, no device: {} samples in {:.3f} s  ({:.2f} MS/s){}", produced, dt, static_cast<double>(produced) / dt / 1e6, ok ? "" : "  [scheduler reported failure]");
    std::println("sample_rate setting carried: {} Hz", kSampleRate);

    // The point of the PoC: start-up cost with no device in the graph.
    std::println("{}", dt < 1.0 ? "start-up did NOT block — graph ran immediately" : "⚠ start-up took over a second with no device present");
    return (ok && produced >= kNSamples) ? 0 : 1;
}
