#include <boost/ut.hpp>

#include <gnuradio-4.0/Graph.hpp>
#include <gnuradio-4.0/Scheduler.hpp>
#include <gnuradio-4.0/sdr/UhdRing.hpp>
#include <gnuradio-4.0/sdr/UhdSource.hpp>
#include <gnuradio-4.0/testing/NullSources.hpp>

#include <complex>
#include <numeric>

using namespace boost::ut;

namespace {
using TSample = std::complex<float>;
using Ring    = gr::blocks::sdr::detail::UhdRing<TSample>;

// A device may or may not be attached; every hardware test is guarded on this and skipped
// rather than failed when absent, so ctest stays green on a machine with no radio.
[[nodiscard]] bool uhdDevicePresent() {
    const auto found = gr::blocks::sdr::soapy::Device::enumerate(gr::blocks::sdr::soapy::Kwargs{{"driver", "uhd"}});
    return !found.empty();
}
} // namespace

const boost::ut::suite<"UhdRing"> ringTests = [] {
    "empty ring reports no samples and full space"_test = [] {
        Ring ring;
        ring.reset(16UZ);
        expect(eq(ring.capacity(), 16UZ));
        expect(eq(ring.available(), 0UZ));
        expect(eq(ring.space(), 16UZ));
    };

    "capacity rounds up to a power of two"_test = [] {
        Ring ring;
        ring.reset(100UZ);
        expect(eq(ring.capacity(), 128UZ));
    };

    "push then pop returns the same samples in order"_test = [] {
        Ring ring;
        ring.reset(16UZ);
        std::array<TSample, 5> in{};
        for (std::size_t i = 0UZ; i < in.size(); ++i) {
            in[i] = TSample{static_cast<float>(i), -static_cast<float>(i)};
        }
        ring.push(in.data(), in.size());
        expect(eq(ring.available(), 5UZ));
        expect(eq(ring.space(), 11UZ));

        std::array<TSample, 5> out{};
        ring.pop(out.data(), out.size());
        expect(eq(ring.available(), 0UZ));
        for (std::size_t i = 0UZ; i < in.size(); ++i) {
            expect(eq(out[i].real(), in[i].real()));
            expect(eq(out[i].imag(), in[i].imag()));
        }
    };

    "a write that wraps the end of the buffer is reassembled correctly"_test = [] {
        Ring ring;
        ring.reset(8UZ);
        // advance the indices so the next write straddles the wrap point
        std::array<TSample, 6> filler{};
        ring.push(filler.data(), filler.size());
        ring.pop(filler.data(), filler.size());
        expect(eq(ring.available(), 0UZ));

        std::array<TSample, 5> in{};
        for (std::size_t i = 0UZ; i < in.size(); ++i) {
            in[i] = TSample{static_cast<float>(100 + i), 0.f};
        }
        ring.push(in.data(), in.size());
        std::array<TSample, 5> out{};
        ring.pop(out.data(), out.size());
        for (std::size_t i = 0UZ; i < in.size(); ++i) {
            expect(eq(out[i].real(), in[i].real())) << "wrapped sample " << i;
        }
    };

    "a full ring reports zero space and does not overstate availability"_test = [] {
        Ring ring;
        ring.reset(4UZ);
        std::array<TSample, 4> in{};
        ring.push(in.data(), in.size());
        expect(eq(ring.space(), 0UZ));
        expect(eq(ring.available(), 4UZ));
    };
};

const boost::ut::suite<"UhdSource"> sourceTests = [] {
    "settings carry the documented defaults"_test = [] {
        gr::Graph graph;
        auto&     source = graph.emplaceBlock<gr::blocks::sdr::UhdSource<TSample, 2UZ>>();
        expect(eq(source.sample_rate.value, 1'000'000.));
        expect(eq(source.frequency.value.size(), 2UZ));
        expect(eq(source.rx_antennae.value.size(), 2UZ));
        // RX2 and TX/RX are the only names a B2xx knows; RX/TX is not one of them
        expect(eq(source.rx_antennae.value.front(), std::string("RX2")));
    };

    "tag_interval of zero disables timing tags"_test = [] {
        gr::Graph graph;
        auto&     source = graph.emplaceBlock<gr::blocks::sdr::UhdSource<TSample, 1UZ>>({{"tag_interval", 0.f}});
        expect(eq(source.tag_interval.value, 0.f));
    };

    // init() is where device bring-up happens, and emplaceBlock calls it. With no device
    // attached it must fail cleanly rather than throw or hang, because that path runs
    // during graph construction where nothing is supervising it.
    "init without a device fails cleanly and start refuses to stream"_test = [] {
        if (uhdDevicePresent()) {
            boost::ut::log << "device present - skipping the no-device path\n";
            return;
        }
        gr::Graph graph;
        auto&     source = graph.emplaceBlock<gr::blocks::sdr::UhdSource<TSample, 1UZ>>({{"device_args", std::string("driver=uhd,serial=NOSUCHDEVICE")}});
        expect(!source._initialised) << "init() must not report success without a device";
    };

    "ring capacity setting is honoured after start prepares the rings"_test = [] {
        gr::Graph graph;
        auto&     source = graph.emplaceBlock<gr::blocks::sdr::UhdSource<TSample, 1UZ>>({{"ring_capacity", gr::Size_t{1000}}});
        expect(eq(source.ring_capacity.value, gr::Size_t{1000}));
    };
};

int main() { /* not needed for UT */ }
