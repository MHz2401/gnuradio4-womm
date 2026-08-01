#ifndef GNURADIO_UHD_SINK_HPP
#define GNURADIO_UHD_SINK_HPP

// ⚠ THIS BLOCK CAN TRANSMIT.
//
// It is built, and it is not run without an operator present. The interlock is
// blocks/sdr/src/womm_tx_arm.hpp, enforced on the harness rather than here: a block
// cannot know whether the graph around it is a test or an emission, but the binary that
// links a transmit path can be required to link the interlock too, and
// assert_tx_gated.cmake checks exactly that on the built artefact.
//
// Transmitting without a licence, or outside its terms, is a federal offence. Nothing in
// this file may be wired into an automated test.

#include <gnuradio-4.0/Block.hpp>
#include <gnuradio-4.0/BlockRegistry.hpp>
#include <gnuradio-4.0/Tag.hpp>
#include <gnuradio-4.0/thread/thread_pool.hpp>

#include <gnuradio-4.0/sdr/SoapyRaiiWrapper.hpp>
#include <gnuradio-4.0/sdr/UhdRing.hpp>

#include <bit>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <thread>

namespace gr::blocks::sdr {

GR_REGISTER_BLOCK("gr::blocks::sdr::UhdSink", gr::blocks::sdr::UhdSink, ([T], 1UZ), [ std::complex<float>, std::complex<short> ])
GR_REGISTER_BLOCK("gr::blocks::sdr::UhdDualSink", gr::blocks::sdr::UhdSink, ([T], 2UZ), [ std::complex<float>, std::complex<short> ])

template<typename T, std::size_t nPorts = 1UZ>
struct UhdSink : Block<UhdSink<T, nPorts>> {
    using Description = Doc<R"(Transmit UHD sink over SoapyUHD. THE PEER OF UhdSource, NOT A SUBSET.

Device initialisation is blocking and happens in init(), exactly as for UhdSource, so a
TX graph cannot stall the scheduler during bring-up either.

Two differences from the receive side, both from the driver rather than from choice:
  - activateStream is a NO-OP on a TX stream (SoapyUHDDevice.cpp:267). Transmit is not
    armed by a stream command; timing is per burst, carried on the write.
  - UNDERFLOW is real and it is a TX condition. The device consumes at a constant rate and
    underflows when the host cannot keep up. It arrives on the ASYNC MESSAGE STREAM via
    readStreamStatus, which no in-tree block reads, so this block runs a second IO thread
    for it.)">;

    using TPort = PortIn<T>;

    // Graph::emplaceBlock calls init(progress) on the DERIVED type, so the no-argument
    // init() hook would hide the framework's own two-argument init. Un-hide it.
    using gr::Block<UhdSink<T, nPorts>>::init;

    std::array<TPort, nPorts> in;

    Annotated<std::string, "device_args", Visible, Doc<"SoapySDR device kwargs: serial=..., num_send_frames=..., send_frame_size=..., master_clock_rate=...">>  device_args;
    Annotated<double, "sample_rate", Unit<"Hz">, Visible, Doc<"per-channel sample rate; UHD derives the master clock from it">>                                 sample_rate = 1'000'000.;
    Annotated<std::vector<double>, "frequency", Unit<"Hz">, Visible, Doc<"per-channel centre frequency">>                                                       frequency   = std::vector<double>(nPorts, 100'000'000.);
    Annotated<std::vector<double>, "tx_gains", Unit<"dB">, Visible, Doc<"per-channel TX gain. ⚠ THIS SETS RADIATED POWER — validated against the device range">> tx_gains    = std::vector<double>(nPorts, 0.);
    Annotated<std::vector<double>, "tx_bandwidths", Unit<"Hz">, Doc<"per-channel analogue bandwidth (0 = device default)">>                                     tx_bandwidths = std::vector<double>(nPorts, 0.);
    Annotated<std::vector<std::string>, "tx_antennae", Visible, Doc<"per-channel antenna. A B2xx transmits on TX/RX only — RX2 is receive-only">>               tx_antennae = std::vector<std::string>(nPorts, "TX/RX");
    Annotated<std::string, "clock_source", Doc<"reference clock; validated against listClockSources()">>                                                        clock_source;
    Annotated<std::string, "time_source", Doc<"time reference; validated against listTimeSources()">>                                                           time_source;
    Annotated<std::string, "stream_args", Doc<"SoapyUHD TX stream kwargs: spp, WIRE, peak, fullscale, underflow_policy (next_burst|next_packet)">>              stream_args;
    Annotated<std::string, "tune_args", Doc<"tune kwargs reaching uhd::tune_request_t.args, e.g. mode_n=integer">>                                              tune_args;
    Annotated<gr::Size_t, "ring_capacity", Doc<"per-channel ring capacity in samples; rounded up to a power of two">>                                           ring_capacity   = 1U << 16U;
    Annotated<std::uint32_t, "max_time_out_us", Unit<"us">, Doc<"writeStream timeout on the IO thread">>                                                        max_time_out_us = 100'000U;
    Annotated<bool, "verbose_events", Doc<"log underflow and stream errors to stderr, rate limited to powers of two">>                                          verbose_events  = false;

    GR_MAKE_REFLECTABLE(UhdSink, in, device_args, sample_rate, frequency, tx_gains, tx_bandwidths, tx_antennae, clock_source, time_source, stream_args, tune_args, ring_capacity, max_time_out_us, verbose_events);

    std::vector<double> _reportedSampleRates{};
    std::vector<double> _reportedFrequencies{};
    double              _reportedMasterClockRate = 0.;
    std::size_t         _reportedStreamMtu       = 0UZ;

    // ★ The async-message counters. These come from readStreamStatus, i.e. UHD's
    // recv_async_msg, and they are the ONLY place a transmit underflow is visible.
    std::atomic<gr::Size_t> _underflowCount{0U};
    std::atomic<gr::Size_t> _seqErrorCount{0U};
    std::atomic<gr::Size_t> _timeErrorCount{0U};
    std::atomic<gr::Size_t> _burstAckCount{0U};
    std::atomic<gr::Size_t> _writeErrorCount{0U};
    std::atomic<gr::Size_t> _ringStarvedCount{0U}; // we had nothing to send: the host side of underflow

    soapy::Device                          _device{};
    soapy::Device::Stream<T, SOAPY_SDR_TX> _txStream{};
    std::array<detail::UhdRing<T>, nPorts> _rings{};

    std::atomic<bool> _ioRunning{false};
    std::atomic<bool> _writeDone{true};
    std::atomic<bool> _statusDone{true};
    bool              _initialised = false;

    void init() {
        if (_initialised) {
            return;
        }
        auto kwargs = device_args->empty() ? soapy::Kwargs{} : soapy::parseKwargsString(device_args.value);
        auto made   = soapy::Device::make(kwargs);
        if (!made) {
            this->emitErrorMessage("init()", made.error());
            return;
        }
        _device = std::move(*made);

        if (const std::size_t have = _device.getNumChannels(SOAPY_SDR_TX); have < nPorts) {
            this->emitErrorMessage("init()", std::format("need {} TX channels, device has {}", nPorts, have));
            return;
        }
        if (!applyChannels() || !openStream()) {
            return;
        }
        _initialised = true;
    }

    void start() {
        if (!_initialised) {
            this->emitErrorMessage("start()", "init() did not complete; refusing to transmit");
            this->requestStop();
            return;
        }
        _underflowCount.store(0U, std::memory_order_relaxed);
        _seqErrorCount.store(0U, std::memory_order_relaxed);
        _timeErrorCount.store(0U, std::memory_order_relaxed);
        _burstAckCount.store(0U, std::memory_order_relaxed);
        _writeErrorCount.store(0U, std::memory_order_relaxed);
        _ringStarvedCount.store(0U, std::memory_order_relaxed);
        for (auto& ring : _rings) {
            ring.reset(std::bit_ceil(static_cast<std::size_t>(ring_capacity.value)));
        }

        // No activate call: activateStream is a documented no-op on a TX stream, so asking
        // for one would only look like arming without being it.
        _ioRunning.store(true, std::memory_order_release);
        _writeDone.store(false, std::memory_order_release);
        _statusDone.store(false, std::memory_order_release);
        thread_pool::Manager::defaultIoPool()->execute([this] { ioWriteLoop(); });
        thread_pool::Manager::defaultIoPool()->execute([this] { ioStatusLoop(); });
    }

    void stop() {
        _ioRunning.store(false, std::memory_order_release);
        while (!_writeDone.load(std::memory_order_acquire) || !_statusDone.load(std::memory_order_acquire)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    template<gr::InputSpanLike TInSpan>
    [[nodiscard]] work::Status processBulk(std::span<TInSpan>& ins) {
        // Consume the same count on every channel: one streamer sends them together, so a
        // per-channel difference would misalign the burst.
        std::size_t n = std::numeric_limits<std::size_t>::max();
        for (std::size_t ch = 0UZ; ch < ins.size(); ++ch) {
            n = std::min({n, ins[ch].size(), _rings[ch].space()});
        }
        if (n == 0UZ) {
            for (auto& span : ins) {
                std::ignore = span.consume(0UZ);
            }
            return work::Status::OK; // the ring is full; back-pressure, not an error
        }
        for (std::size_t ch = 0UZ; ch < ins.size(); ++ch) {
            _rings[ch].push(ins[ch].data(), n);
            std::ignore = ins[ch].consume(n);
        }
        return work::Status::OK;
    }

    void ioWriteLoop() {
        thread_pool::thread::setThreadName(std::format("uhdtx:{}", this->name.value));
        const std::size_t chunk = _reportedStreamMtu != 0UZ ? _reportedStreamMtu : 2040UZ;

        std::array<std::vector<T>, nPorts> scratch;
        std::vector<std::span<T>>          views;
        views.reserve(nPorts);
        for (auto& buffer : scratch) {
            buffer.resize(chunk);
            views.emplace_back(buffer);
        }

        while (_ioRunning.load(std::memory_order_acquire)) {
            std::size_t ready = std::numeric_limits<std::size_t>::max();
            for (auto& ring : _rings) {
                ready = std::min(ready, ring.available());
            }
            if (ready == 0UZ) {
                // Nothing to send. On a constant-rate transmitter this is the host side of
                // an underflow, so it is counted rather than silently spun on.
                _ringStarvedCount.fetch_add(1U, std::memory_order_relaxed);
                std::this_thread::sleep_for(std::chrono::microseconds(200));
                continue;
            }
            const std::size_t count = std::min(ready, chunk);
            for (std::size_t ch = 0UZ; ch < nPorts; ++ch) {
                _rings[ch].pop(scratch[ch].data(), count);
                views[ch] = std::span<T>(scratch[ch].data(), count);
            }

            int       flags = 0;
            const int sent  = _txStream.writeStreamFromBufferList(flags, 0LL, static_cast<long>(max_time_out_us.value), views);
            if (sent < 0) {
                _writeErrorCount.fetch_add(1U, std::memory_order_relaxed);
                if (verbose_events) {
                    std::println(stderr, "[UhdSink] writeStream error {}", sent);
                }
            }
        }
        _writeDone.store(true, std::memory_order_release);
    }

    // ★ The async message stream. SoapyUHD maps UHD's recv_async_msg onto Soapy codes and
    // returns NOT_SUPPORTED for a receive stream, so this exists only on the transmit side
    // — and it is the only channel on which an underflow is ever reported.
    void ioStatusLoop() {
        thread_pool::thread::setThreadName(std::format("uhdtxst:{}", this->name.value));
        while (_ioRunning.load(std::memory_order_acquire)) {
            std::size_t chanMask = 0UZ;
            int         flags    = 0;
            long long   timeNs   = 0;
            const int   status   = SoapySDRDevice_readStreamStatus(_device.get(), _txStream.get(), &chanMask, &flags, &timeNs, 100'000L);
            switch (status) {
            case SOAPY_SDR_TIMEOUT: continue;         // nothing to report; the normal case
            case SOAPY_SDR_NOT_SUPPORTED: return;     // driver has no async channel: stop asking
            case SOAPY_SDR_UNDERFLOW: {
                const gr::Size_t total = _underflowCount.fetch_add(1U, std::memory_order_relaxed) + 1U;
                if (verbose_events && (total <= 4U || std::has_single_bit(total))) {
                    std::println(stderr, "[UhdSink] UNDERFLOW #{} — the host did not keep up", total);
                }
                break;
            }
            case SOAPY_SDR_CORRUPTION: _seqErrorCount.fetch_add(1U, std::memory_order_relaxed); break;
            case SOAPY_SDR_TIME_ERROR: _timeErrorCount.fetch_add(1U, std::memory_order_relaxed); break;
            default:
                if ((flags & SOAPY_SDR_END_BURST) != 0) {
                    _burstAckCount.fetch_add(1U, std::memory_order_relaxed);
                }
                break;
            }
        }
        _statusDone.store(true, std::memory_order_release);
    }

    bool applyChannels() {
        for (std::size_t ch = 0UZ; ch < nPorts; ++ch) {
            if (auto r = _device.setSampleRate(SOAPY_SDR_TX, ch, sample_rate); !r) {
                this->emitErrorMessage("init()", r.error());
                return false;
            }
        }
        const soapy::Kwargs tuneKwargs = tune_args->empty() ? soapy::Kwargs{} : soapy::parseKwargsString(tune_args.value);
        for (std::size_t ch = 0UZ; ch < nPorts; ++ch) {
            // ⚠ Antenna first, and validated: a B2xx transmits on TX/RX. Sending a burst
            // to a port that is not the transmit port is a hardware question, not a
            // software one, so an unlisted name is refused rather than passed through.
            const auto& antenna   = tx_antennae->at(std::min(ch, tx_antennae->size() - 1UZ));
            const auto  available = _device.listAvailableAntennas(SOAPY_SDR_TX, ch);
            if (std::ranges::find(available, antenna) == available.end()) {
                this->emitErrorMessage("init()", std::format("TX antenna '{}' not offered on channel {}; device lists: {}", antenna, ch, gr::join(available, ", ")));
                return false;
            }
            if (auto r = _device.setAntenna(SOAPY_SDR_TX, ch, antenna); !r) {
                this->emitErrorMessage("init()", r.error());
                return false;
            }
            const double gain = tx_gains->at(std::min(ch, tx_gains->size() - 1UZ));
            if (auto r = _device.setGain(SOAPY_SDR_TX, ch, gain); !r) {
                this->emitErrorMessage("init()", r.error());
                return false;
            }
            if (std::isnan(_device.getGain(SOAPY_SDR_TX, ch))) {
                this->emitErrorMessage("init()", std::format("TX gain {} dB on channel {} reads back nan — out of range", gain, ch));
                return false;
            }
            if (tx_bandwidths->size() > ch && tx_bandwidths->at(ch) > 0.) {
                if (auto r = _device.setBandwidth(SOAPY_SDR_TX, ch, tx_bandwidths->at(ch)); !r) {
                    this->emitErrorMessage("init()", r.error());
                    return false;
                }
            }
            const double freq = frequency->at(std::min(ch, frequency->size() - 1UZ));
            if (auto r = _device.setCenterFrequency(SOAPY_SDR_TX, ch, freq, tuneKwargs); !r) {
                this->emitErrorMessage("init()", r.error());
                return false;
            }
        }
        for (std::size_t ch = 0UZ; ch < nPorts; ++ch) {
            _reportedSampleRates.push_back(_device.getSampleRate(SOAPY_SDR_TX, ch));
            _reportedFrequencies.push_back(_device.getCenterFrequency(SOAPY_SDR_TX, ch));
        }
        _reportedMasterClockRate = _device.getMasterClockRate();
        return true;
    }

    bool openStream() {
        std::vector<std::size_t> channels(nPorts);
        std::iota(channels.begin(), channels.end(), 0UZ);
        const soapy::Kwargs streamKwargs = stream_args->empty() ? soapy::Kwargs{} : soapy::parseKwargsString(stream_args.value);
        auto                opened       = _device.setupStream<T, SOAPY_SDR_TX>(channels, streamKwargs);
        if (!opened) {
            this->emitErrorMessage("init()", opened.error());
            return false;
        }
        _txStream          = std::move(*opened);
        _reportedStreamMtu = SoapySDRDevice_getStreamMTU(_device.get(), _txStream.get());
        return true;
    }
};

template<typename T>
using UhdSimpleSink = UhdSink<T, 1UZ>;
template<typename T>
using UhdDualSink = UhdSink<T, 2UZ>;

} // namespace gr::blocks::sdr

#endif // GNURADIO_UHD_SINK_HPP
