// womm_scope_demo - a sine wave you can actually watch, with no radio and no GUI.
//
// WHAT THIS IS, PRECISELY. An earlier PoC (womm_poc_nograph) was described as a
// "two-node flowgraph" and that read as more than it was: it is ConstantSource ->
// CountingSink and it plots NOTHING. This one plots.
//
// It is NOT a graphical scope. There is no window, no GUI toolkit, no Studio, and no
// control plane. gnuradio4's only in-tree drawable is ImChartMonitor, declared
// Drawable<UICategory::Content, "console">, which renders an ASCII chart to the
// terminal. Its ConsoleDebugSink variant - ImChartMonitor<T, false> - calls draw()
// directly from processBulk, so it needs no UI host at all.
//
// So: a sine wave, redrawn in the terminal, from a real gr4 flowgraph. That is the
// honest extent of what this tree can display today.
//
// NO RADIO IS INVOLVED. Nothing here opens a device.

#include <chrono>
#include <cstdlib>
#include <print>
#include <thread>

#include <gnuradio-4.0/Graph.hpp>
#include <gnuradio-4.0/Scheduler.hpp>

#include <gnuradio-4.0/basic/SignalGenerator.hpp>
#include <gnuradio-4.0/testing/ImChartMonitor.hpp>

using T = float;

double envOr(const char* k, double d) {
    const char* v = std::getenv(k);
    return v ? std::atof(v) : d;
}

int main() {
    const float  sampleRate = static_cast<float>(envOr("WOMM_RATE", 1000.0));
    const float  frequency  = static_cast<float>(envOr("WOMM_FREQ", 5.0));
    const double runSec     = envOr("WOMM_SEC", 20.0);

    gr::Graph graph;

    auto& src = graph.emplaceBlock<gr::basic::SignalGenerator<T>>({
        {"sample_rate", sampleRate},
        {"frequency", static_cast<double>(frequency)},
        {"amplitude", 1.0},
        {"signal_type", "Sin"},
    });

    // ImChartMonitor<T, false> is the ConsoleDebugSink variant: draw() is called from
    // processBulk rather than by a UI host, so the chart appears without any front end.
    auto& sink = graph.emplaceBlock<gr::testing::ImChartMonitor<T, false>>({
        {"sample_rate", sampleRate},
        {"signal_name", std::string("sine")},
        {"timeout_ms", static_cast<gr::Size_t>(200)}, // redraw ~5x/s
        {"chart_width", static_cast<gr::Size_t>(100)},
        {"chart_height", static_cast<gr::Size_t>(28)},
    });

    if (auto r = graph.connect(src, "out", sink, "in"); !r.has_value()) {
        std::println(stderr, "connect failed");
        return 1;
    }

    std::println("sine {:.1f} Hz at {:.0f} S/s — ASCII chart, no GUI. {:.0f} s, Ctrl-C to stop early.", frequency, sampleRate, runSec);

    gr::scheduler::Simple<gr::scheduler::ExecutionPolicy::multiThreaded> sched;
    if (auto r = sched.exchange(std::move(graph)); !r) {
        std::println(stderr, "scheduler init failed: {}", r.error());
        return 1;
    }

    // A signal generator never finishes on its own, so stop on a timer plus a hard
    // backstop - the wedged-graph lesson from RESULTS.md, applied to a graph with no radio.
    std::thread runner([&] { std::ignore = sched.runAndWait(); });
    std::this_thread::sleep_for(std::chrono::duration<double>(runSec));
    sched.requestStop();

    const auto stopBy = std::chrono::steady_clock::now() + std::chrono::seconds(10);
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
    return 0;
}
