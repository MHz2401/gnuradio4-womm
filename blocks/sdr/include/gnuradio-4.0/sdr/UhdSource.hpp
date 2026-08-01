#ifndef GNURADIO_UHD_SOURCE_HPP
#define GNURADIO_UHD_SOURCE_HPP

#include <gnuradio-4.0/Block.hpp>
#include <gnuradio-4.0/BlockRegistry.hpp>
#include <gnuradio-4.0/Tag.hpp>
#include <gnuradio-4.0/thread/thread_pool.hpp>

#include <gnuradio-4.0/sdr/SoapyRaiiWrapper.hpp>
#include <gnuradio-4.0/sdr/UhdRing.hpp>

#include <bit>
#include <chrono>
#include <cstdint>
#include <algorithm>
#include <mutex>
#include <thread>

namespace gr::blocks::sdr {

// Device initialization is BLOCKING and lives in init(), which the graph calls from
// emplaceBlock() — before any scheduler exists and so before any timeout can expire.
// UHD serialises device creation on a global mutex (NAMEMAP.md §9.1), so N radios cost
// N x ~3.5 s whatever we do; putting that cost here means nothing is waiting on it.
GR_REGISTER_BLOCK("gr::blocks::sdr::UhdSource", gr::blocks::sdr::UhdSource, ([T], 1UZ), [ std::complex<float>, std::complex<short> ])
GR_REGISTER_BLOCK("gr::blocks::sdr::UhdDualSource", gr::blocks::sdr::UhdSource, ([T], 2UZ), [ std::complex<float>, std::complex<short> ])

template<typename T, std::size_t nPorts = 1UZ>
struct UhdSource : Block<UhdSource<T, nPorts>> {
    using Description = Doc<R"(Receive-only UHD source over SoapyUHD.

Device initialisation is blocking and happens in init() (IDLE -> INITIALISED, at graph
construction), not start(). start() only activates the stream, so the scheduler never
waits on hardware and a multi-radio graph cannot time out during bring-up.

An IO thread drains the device into a per-channel ring; processBulk publishes whatever
is present, possibly zero, and never blocks a scheduler worker.

Tags are gated on SAMPLE COUNT, never a host clock: the k-th tag lands at exactly
k * samples_per_tag, so sample index and device time stay one quantity in two units.)">;

    using TPort  = PortOut<T>;
    using TValue = T;

    // Graph::emplaceBlock calls init(progress) on the DERIVED type, so a no-argument
    // init() hook hides the framework's own two-argument init. Un-hide it.
    using gr::Block<UhdSource<T, nPorts>>::init;

    std::array<TPort, nPorts> out;

    Annotated<std::string, "device_args", Visible, Doc<"SoapySDR device kwargs: serial=..., num_recv_frames=..., recv_frame_size=..., master_clock_rate=... (transport keys only bite here, not in stream_args)">> device_args;
    Annotated<double, "sample_rate", Unit<"Hz">, Visible, Doc<"per-channel sample rate; UHD derives the master clock from it">>                                    sample_rate = 1'000'000.;
    Annotated<std::vector<double>, "frequency", Unit<"Hz">, Visible, Doc<"per-channel centre frequency">>                                                          frequency   = std::vector<double>(nPorts, 100'000'000.);
    Annotated<std::vector<double>, "rx_gains", Unit<"dB">, Visible, Doc<"per-channel RX gain; validated against getGainRange()">>                                   rx_gains    = std::vector<double>(nPorts, 30.);
    Annotated<std::vector<double>, "rx_bandwidths", Unit<"Hz">, Doc<"per-channel analogue bandwidth (0 = leave at device default)">>                               rx_bandwidths = std::vector<double>(nPorts, 0.);
    Annotated<std::vector<std::string>, "rx_antennae", Visible, Doc<"per-channel antenna; validated against listAntennas(). A B2xx offers TX/RX and RX2">>          rx_antennae = std::vector<std::string>(nPorts, "RX2");
    Annotated<std::string, "clock_source", Doc<"reference clock; validated against listClockSources(). ref_locked is polled, not sampled once">>                    clock_source;
    Annotated<std::string, "time_source", Doc<"time reference; validated against listTimeSources()">>                                                              time_source;
    Annotated<std::string, "stream_args", Doc<"SoapyUHD stream kwargs: spp (samples per packet), WIRE (sc8|sc16), peak, fullscale">>                               stream_args;
    Annotated<std::string, "tune_args", Doc<"tune kwargs reaching uhd::tune_request_t.args, e.g. mode_n=integer, int_n_step=... — wired, unlike SoapySource">>      tune_args;
    Annotated<gr::Size_t, "ring_capacity", Doc<"per-channel ring capacity in samples; rounded up to a power of two">>                                              ring_capacity   = 1U << 16U;
    Annotated<std::uint32_t, "max_time_out_us", Unit<"us">, Doc<"readStream timeout on the IO thread; a timeout is normal, not an error">>                         max_time_out_us = 100'000U;
    Annotated<float, "tag_interval", Unit<"s">, Doc<"seconds between timing tags, converted to an exact sample count (0 = no timing tags)">>                       tag_interval    = 1.f;
    Annotated<bool, "verbose_events", Doc<"log overflow and stream errors to stderr, rate limited to powers of two">>                                              verbose_events  = false;

    GR_MAKE_REFLECTABLE(UhdSource, out, device_args, sample_rate, frequency, rx_gains, rx_bandwidths, rx_antennae, clock_source, time_source, stream_args, tune_args, ring_capacity, max_time_out_us, tag_interval, verbose_events);

    // What the DEVICE reported, as opposed to what was asked of it. Written during init()
    // before the IO thread exists, so a reader after init() is safe. A requested value is
    // a request; only these are measurements.
    std::vector<double> _reportedSampleRates{};
    std::vector<double> _reportedFrequencies{};
    double              _reportedMasterClockRate = 0.;
    std::string         _reportedClockSource{};
    std::string         _reportedTimeSource{};
    std::string         _reportedRefLocked{};
    std::size_t         _reportedStreamMtu = 0UZ; // rx->get_max_num_samps(), i.e. what spp actually became

    // MONOTONIC across a run; cleared only by start(). A driver that zeroed these on the
    // next good read would leave an application unable to ask "how many this hour?".
    std::atomic<gr::Size_t> _overflowCount{0U};
    std::atomic<gr::Size_t> _timeoutCount{0U};
    std::atomic<gr::Size_t> _corruptionCount{0U};
    std::atomic<gr::Size_t> _streamErrorCount{0U};
    std::atomic<gr::Size_t> _ringFullCount{0U}; // consumer too slow: OUR loss, not the device's

    soapy::Device                          _device{};
    soapy::Device::Stream<T, SOAPY_SDR_RX> _rxStream{};
    std::array<detail::UhdRing<T>, nPorts> _rings{};

    // The device clock anchor: device time in ns at absolute sample index _anchorSample.
    // Re-set on every discontinuity, so a consumer can always map samples to device time.
    std::atomic<std::int64_t>  _anchorDeviceTimeNs{0};
    std::atomic<std::uint64_t> _anchorSample{0U};
    std::atomic<bool>          _anchorValid{false};
    std::atomic<bool>          _reanchorPending{false};

    std::uint64_t _samplesProduced = 0U; // consumer side only; absolute index of the next sample to publish
    std::uint64_t _samplesPerTag   = 0U;
    std::uint64_t _nextTagSample   = 0U;

    std::atomic<bool> _ioRunning{false};
    std::atomic<bool> _ioDone{true};
    bool              _initialised = false;

    // ---- lifecycle ----------------------------------------------------------------

    // The arming barrier holds raw pointers to live blocks, so leaving a destroyed one in
    // it is a use-after-free waiting for the next graph in the same process. Deregister
    // here, and drop the armed flag once the last member goes, so a second graph arms
    // properly instead of silently skipping it.
    ~UhdSource() {
        auto  lock    = std::lock_guard(barrier().mutex);
        auto& members = barrier().members;
        std::erase(members, this);
        if (members.empty()) {
            barrier().armed = false;
        }
    }

    // IDLE -> INITIALISED. Called from Graph::emplaceBlock, long before any scheduler
    // exists. Blocking here is deliberate and safe: there is no timeout to miss.
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

        if (const std::size_t have = _device.getNumChannels(SOAPY_SDR_RX); have < nPorts) {
            this->emitErrorMessage("init()", std::format("need {} RX channels, device has {}", nPorts, have));
            return;
        }

        if (!applyClocking() || !applyChannels()) {
            return;
        }
        if (!openStream()) {
            return;
        }
        {
            auto lock = std::lock_guard(barrier().mutex);
            barrier().members.push_back(this);
        }
        _initialised = true;
    }

    // INITIALISED -> RUNNING. Activation only, and fast: everything slow already happened.
    void start() {
        if (!_initialised) {
            this->emitErrorMessage("start()", "init() did not complete; refusing to stream");
            this->requestStop();
            return;
        }
        _overflowCount.store(0U, std::memory_order_relaxed);
        _timeoutCount.store(0U, std::memory_order_relaxed);
        _corruptionCount.store(0U, std::memory_order_relaxed);
        _streamErrorCount.store(0U, std::memory_order_relaxed);
        _ringFullCount.store(0U, std::memory_order_relaxed);
        _anchorValid.store(false, std::memory_order_relaxed);
        _reanchorPending.store(false, std::memory_order_relaxed);
        for (auto& ring : _rings) {
            ring.reset(std::bit_ceil(static_cast<std::size_t>(ring_capacity.value)));
        }
        _samplesProduced = 0U;
        _samplesPerTag   = (tag_interval > 0.f) ? static_cast<std::uint64_t>(static_cast<double>(sample_rate) * static_cast<double>(tag_interval)) : 0U;
        _nextTagSample   = 0U;

        // H-1, REPRODUCED HERE on 2026-08-01: a bare activate() on a two-channel streamer
        // is refused with SOAPY_SDR_STREAM_ERROR (-2). UHD will not "stream now" on a
        // multi-channel streamer because it cannot time-align it, so a timed start is a
        // driver requirement, not a synchronisation luxury.
        //
        // All radios must arm at the SAME absolute device time or they are merely running,
        // not running together. So the zeroing and the arming happen once, for every
        // registered radio, in two tight loops — not per block, which would spread the
        // epoch across the whole init phase.
        if (!armAllRadios()) {
            this->requestStop();
            return;
        }

        _ioRunning.store(true, std::memory_order_release);
        _ioDone.store(false, std::memory_order_release);
        thread_pool::Manager::defaultIoPool()->execute([this] { ioReadLoop(); });
    }

    void stop() {
        _ioRunning.store(false, std::memory_order_release);
        while (!_ioDone.load(std::memory_order_acquire)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        if (auto deactivated = _rxStream.deactivate(); !deactivated) {
            this->emitErrorMessage("stop()", deactivated.error());
        }
    }

    // ---- data path ----------------------------------------------------------------

    template<gr::OutputSpanLike TOutSpan>
    [[nodiscard]] work::Status processBulk(std::span<TOutSpan>& outs) {
        // Every channel comes from one streamer, so they advance in lockstep. Publishing
        // different counts per channel would desynchronise the sample index from device
        // time, which is the one invariant this block exists to preserve.
        std::size_t n = std::numeric_limits<std::size_t>::max();
        for (std::size_t ch = 0UZ; ch < outs.size(); ++ch) {
            n = std::min({n, outs[ch].size(), _rings[ch].available()});
        }
        if (n == 0UZ) {
            for (auto& span : outs) {
                span.publish(0UZ);
            }
            return work::Status::OK; // not an error: the device simply has nothing yet
        }

        // A tag may only land at an exact sample index, so never publish past one.
        if (_samplesPerTag != 0U && _samplesProduced + n > _nextTagSample && _samplesProduced < _nextTagSample) {
            n = static_cast<std::size_t>(_nextTagSample - _samplesProduced);
        }

        for (std::size_t ch = 0UZ; ch < outs.size(); ++ch) {
            _rings[ch].pop(outs[ch].data(), n);
        }

        if (_samplesPerTag != 0U && _samplesProduced == _nextTagSample) {
            publishTimingTag(outs);
            _nextTagSample += _samplesPerTag;
        }

        for (auto& span : outs) {
            span.publish(n);
        }
        _samplesProduced += n;
        return work::Status::OK;
    }

    // ---- cross-radio arming ---------------------------------------------------------

    // Every radio in this process registers during init(); the first start() zeroes them
    // all and then arms them all at one absolute device time. Graph construction always
    // finishes before the scheduler starts, so by the time the first start() runs the
    // registry is complete — no count has to be known in advance.
    struct ArmingBarrier {
        std::mutex               mutex;
        std::vector<UhdSource*>  members;
        bool                     armed = false;
    };

    static ArmingBarrier& barrier() {
        static auto* shared = new ArmingBarrier; // intentional leak: must outlive every block
        return *shared;
    }

    [[nodiscard]] bool armAllRadios() {
        auto lock = std::lock_guard(barrier().mutex);
        if (barrier().armed) {
            return true; // a peer already armed the whole set, this one included
        }
        auto& members = barrier().members;

        // Zero every clock first, in one tight loop, so the epochs are as close together
        // as the calls themselves. With an external time reference use UNKNOWN_PPS, which
        // waits for a PPS TRANSITION before arming the following edge — plain "PPS" arms
        // the next edge blind, so two radios either side of one can land a second apart.
        for (UhdSource* radio : members) {
            const bool externalTime = !radio->time_source->empty() && radio->time_source.value != "none";
            const auto zeroed       = externalTime ? radio->_device.setHardwareTime(0, "UNKNOWN_PPS") : radio->_device.setHardwareTime(0);
            if (!zeroed) {
                this->emitErrorMessage("armAllRadios()", zeroed.error());
                return false;
            }
        }

        // One literal instant on the shared, just-zeroed epoch — never getHardwareTime()
        // plus an offset, which is a race that gives each radio a different start.
        constexpr long long kArmAtNs = 500'000'000LL;
        for (UhdSource* radio : members) {
            if (auto activated = radio->_rxStream.activate(SOAPY_SDR_HAS_TIME, kArmAtNs, 0UZ); !activated) {
                this->emitErrorMessage("armAllRadios()", activated.error());
                return false;
            }
        }
        barrier().armed = true;
        return true;
    }

    // ---- helpers -------------------------------------------------------------------

    void ioReadLoop() {
        thread_pool::thread::setThreadName(std::format("uhd:{}", this->name.value));
        const std::size_t chunk = _reportedStreamMtu != 0UZ ? _reportedStreamMtu : 2040UZ;

        std::array<std::vector<T>, nPorts> scratch;
        std::vector<std::span<T>>          views;
        views.reserve(nPorts);
        for (auto& buffer : scratch) {
            buffer.resize(chunk);
            views.emplace_back(buffer);
        }

        std::uint64_t written = 0U; // absolute index of the next sample the device will give us

        while (_ioRunning.load(std::memory_order_acquire)) {
            int       flags  = 0;
            long long timeNs = 0;
            const int ret    = _rxStream.readStreamIntoBufferList(flags, timeNs, static_cast<long>(max_time_out_us.value), views);

            if (ret == SOAPY_SDR_TIMEOUT) {
                _timeoutCount.fetch_add(1U, std::memory_order_relaxed);
                continue;
            }
            if (ret < 0) {
                if (!handleStreamError(ret)) {
                    break;
                }
                continue;
            }
            if (ret == 0) {
                continue;
            }

            const auto count = static_cast<std::size_t>(ret);
            if ((flags & SOAPY_SDR_HAS_TIME) != 0 && (!_anchorValid.load(std::memory_order_relaxed) || _reanchorPending.exchange(false, std::memory_order_acq_rel))) {
                _anchorDeviceTimeNs.store(static_cast<std::int64_t>(timeNs), std::memory_order_relaxed);
                _anchorSample.store(written, std::memory_order_relaxed);
                _anchorValid.store(true, std::memory_order_release);
            }

            // Push all channels or none: a partial push would skew one channel's index.
            std::size_t room = std::numeric_limits<std::size_t>::max();
            for (auto& ring : _rings) {
                room = std::min(room, ring.space());
            }
            if (room < count) {
                // The consumer is behind. This is OUR overflow, distinct from the device's,
                // and it is counted separately so the two are never confused in a report.
                _ringFullCount.fetch_add(1U, std::memory_order_relaxed);
                _reanchorPending.store(true, std::memory_order_release);
                continue;
            }
            for (std::size_t ch = 0UZ; ch < nPorts; ++ch) {
                _rings[ch].push(scratch[ch].data(), count);
            }
            written += count;

            this->progress->incrementAndGet();
            this->progress->notify_all();
        }

        _ioDone.store(true, std::memory_order_release);
    }

    bool handleStreamError(int ret) {
        switch (ret) {
        case SOAPY_SDR_OVERFLOW: {
            const gr::Size_t total = _overflowCount.fetch_add(1U, std::memory_order_relaxed) + 1U;
            // The sample-to-time mapping just broke, so the next chunk must re-anchor it.
            _reanchorPending.store(true, std::memory_order_release);
            if (verbose_events && (total <= 4U || std::has_single_bit(total))) {
                std::println(stderr, "[UhdSource] device overflow #{}", total);
            }
            return true; // keep reading; the device recovers on its own
        }
        case SOAPY_SDR_CORRUPTION:
            _corruptionCount.fetch_add(1U, std::memory_order_relaxed);
            this->emitErrorMessage("ioReadLoop()", "stream corruption");
            this->requestStop();
            return false;
        default:
            // SOAPY_SDR_TIME_ERROR (LATE_COMMAND) and SOAPY_SDR_STREAM_ERROR
            // (BROKEN_CHAIN) both arrive here; the number alone does not say which.
            _streamErrorCount.fetch_add(1U, std::memory_order_relaxed);
            this->emitErrorMessage("ioReadLoop()", std::format("stream error {} (on UHD: LATE_COMMAND, BROKEN_CHAIN or TIME_ERROR)", ret));
            this->requestStop();
            return false;
        }
    }

    template<typename TSpans>
    void publishTimingTag(TSpans& outs) {
        // Sample index and device time are one quantity in two units. The k-th tag sits at
        // exactly k * _samplesPerTag, and its device time is computed from the anchor by
        // integer arithmetic — never sampled from a host clock, which has jitter this
        // quantity does not.
        property_map map;
        tag::put(map, "rx_rate", static_cast<double>(sample_rate));
        if (!frequency->empty()) {
            tag::put(map, "rx_freq", frequency->front());
        }
        tag::put(map, "sample_index", _samplesProduced);

        if (_anchorValid.load(std::memory_order_acquire)) {
            const auto anchorNs   = _anchorDeviceTimeNs.load(std::memory_order_relaxed);
            const auto anchorIdx  = _anchorSample.load(std::memory_order_relaxed);
            const auto deltaNs    = static_cast<std::int64_t>(static_cast<unsigned __int128>(_samplesProduced - anchorIdx) * 1'000'000'000U / static_cast<unsigned __int128>(sample_rate));
            const auto deviceTime = anchorNs + deltaNs;
            tag::put(map, "rx_time_ns", deviceTime);
            tag::put(map, "rx_time_full_secs", static_cast<std::int64_t>(deviceTime / 1'000'000'000));
            tag::put(map, "rx_time_frac_secs", static_cast<double>(deviceTime % 1'000'000'000) * 1e-9);
        }

        tag::put(map, "rx_overflow_count", static_cast<std::uint64_t>(_overflowCount.load(std::memory_order_relaxed)));
        tag::put(map, "rx_ring_full_count", static_cast<std::uint64_t>(_ringFullCount.load(std::memory_order_relaxed)));

        for (auto& span : outs) {
            span.publishTag(map, 0UZ);
        }
    }

    bool applyClocking() {
        // Validate against what the device declares, never against a remembered list: our
        // own antenna allow-list once had TX/RX reversed and would have rejected the only
        // correct name.
        if (!clock_source->empty()) {
            const auto available = _device.listClockSources();
            if (std::ranges::find(available, clock_source.value) == available.end()) {
                this->emitErrorMessage("init()", std::format("clock_source '{}' not offered; device lists: {}", clock_source.value, gr::join(available, ", ")));
                return false;
            }
            if (auto r = _device.setClockSource(clock_source.value); !r) {
                this->emitErrorMessage("init()", r.error());
                return false;
            }
            // Selecting an absent reference does NOT error — the device free-runs and
            // every downstream figure silently becomes unsynchronised. And the PLL needs
            // time, so poll to a deadline rather than sampling once.
            if (clock_source.value != "internal" && !waitForSensor("ref_locked", std::chrono::milliseconds(3000), _reportedRefLocked)) {
                this->emitErrorMessage("init()", std::format("clock_source '{}' selected but ref_locked reads '{}' — the device is free-running", clock_source.value, _reportedRefLocked));
                return false;
            }
        }
        if (!time_source->empty()) {
            const auto available = _device.listAvailableTimeSources();
            if (std::ranges::find(available, time_source.value) == available.end()) {
                this->emitErrorMessage("init()", std::format("time_source '{}' not offered; device lists: {}", time_source.value, gr::join(available, ", ")));
                return false;
            }
            if (auto r = _device.setTimeSource(time_source.value); !r) {
                this->emitErrorMessage("init()", r.error());
                return false;
            }
        }
        _reportedClockSource = _device.getClockSource();
        _reportedTimeSource  = _device.getTimeSource();
        return true;
    }

    bool applyChannels() {
        // Set the RATE and let UHD choose the master clock. Pinning the MCR would switch
        // off the auto-selector (NAMEMAP.md §9.4); pin it as a device arg if it is ever
        // genuinely needed.
        for (std::size_t ch = 0UZ; ch < nPorts; ++ch) {
            if (auto r = _device.setSampleRate(SOAPY_SDR_RX, ch, sample_rate); !r) {
                this->emitErrorMessage("init()", r.error());
                return false;
            }
        }
        const soapy::Kwargs tuneKwargs = tune_args->empty() ? soapy::Kwargs{} : soapy::parseKwargsString(tune_args.value);
        for (std::size_t ch = 0UZ; ch < nPorts; ++ch) {
            if (!applyAntenna(ch) || !applyGain(ch)) {
                return false;
            }
            if (rx_bandwidths->size() > ch && rx_bandwidths->at(ch) > 0.) {
                if (auto r = _device.setBandwidth(SOAPY_SDR_RX, ch, rx_bandwidths->at(ch)); !r) {
                    this->emitErrorMessage("init()", r.error());
                    return false;
                }
            }
            // The tune kwargs reach uhd::tune_request_t.args, which is how mode_n=integer
            // becomes reachable at all. SoapySource declares tune_args and never reads it.
            const double freq = frequency->at(std::min(ch, frequency->size() - 1UZ));
            if (auto r = _device.setCenterFrequency(SOAPY_SDR_RX, ch, freq, tuneKwargs); !r) {
                this->emitErrorMessage("init()", r.error());
                return false;
            }
        }
        for (std::size_t ch = 0UZ; ch < nPorts; ++ch) {
            _reportedSampleRates.push_back(_device.getSampleRate(SOAPY_SDR_RX, ch));
            _reportedFrequencies.push_back(_device.getCenterFrequency(SOAPY_SDR_RX, ch));
        }
        // Read the master clock AFTER the rate is applied, never before: with no
        // master_clock_rate device arg UHD picks the clock to serve the requested rate,
        // so reading it earlier reports DEFAULT_TICK_RATE — a request, not a measurement.
        _reportedMasterClockRate = _device.getMasterClockRate();
        return waitForLoLock();
    }

    bool applyAntenna(std::size_t ch) {
        if (rx_antennae->empty()) {
            return true;
        }
        const auto& name      = rx_antennae->at(std::min(ch, rx_antennae->size() - 1UZ));
        const auto  available = _device.listAvailableAntennas(SOAPY_SDR_RX, ch);
        if (std::ranges::find(available, name) == available.end()) {
            this->emitErrorMessage("init()", std::format("antenna '{}' not offered on channel {}; device lists: {}", name, ch, gr::join(available, ", ")));
            return false;
        }
        if (auto r = _device.setAntenna(SOAPY_SDR_RX, ch, name); !r) {
            this->emitErrorMessage("init()", r.error());
            return false;
        }
        return true;
    }

    bool applyGain(std::size_t ch) {
        if (rx_gains->empty()) {
            return true;
        }
        const double gain = rx_gains->at(std::min(ch, rx_gains->size() - 1UZ));
        // Out of range returns nan rather than failing, and gain has no universal
        // convention: a value that suits an RTL-SDR suits neither a B2xx nor a HackRF.
        if (auto r = _device.setGain(SOAPY_SDR_RX, ch, gain); !r) {
            this->emitErrorMessage("init()", r.error());
            return false;
        }
        if (const double readback = _device.getGain(SOAPY_SDR_RX, ch); std::isnan(readback)) {
            this->emitErrorMessage("init()", std::format("gain {} dB on channel {} reads back nan — out of range for this device", gain, ch));
            return false;
        }
        return true;
    }

    bool waitForLoLock() {
        // Tuning is not instantaneous. setCenterFrequency returns when the request is
        // accepted; lo_locked is the device's own statement that the synthesiser settled.
        // Streaming before then yields garbage that, on a synchronised multi-radio
        // capture, arrives time-aligned and is indistinguishable from signal.
        for (std::size_t ch = 0UZ; ch < nPorts; ++ch) {
            const auto sensors = _device.listChannelSensors(SOAPY_SDR_RX, ch);
            if (std::ranges::find(sensors, "lo_locked") == sensors.end()) {
                continue;
            }
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(1000);
            while (!_device.readChannelSensor(SOAPY_SDR_RX, ch, "lo_locked").starts_with("true")) {
                if (std::chrono::steady_clock::now() >= deadline) {
                    this->emitErrorMessage("init()", std::format("channel {} lo_locked did not settle within 1000 ms", ch));
                    return false;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
        }
        return true;
    }

    bool waitForSensor(const std::string& key, std::chrono::milliseconds timeout, std::string& reading) {
        const auto sensors = _device.listSensors();
        if (std::ranges::find(sensors, key) == sensors.end()) {
            return true; // the device does not expose it; not a failure
        }
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        do {
            reading = _device.readSensor(key);
            if (reading.starts_with("true")) {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        } while (std::chrono::steady_clock::now() < deadline);
        return false;
    }

    bool openStream() {
        const auto supported = _device.getStreamFormats(SOAPY_SDR_RX, 0UZ);
        const auto requested = std::string(soapy::detail::toSoapySDRFormat<T>());
        if (!supported.empty() && std::ranges::find(supported, requested) == supported.end()) {
            this->emitErrorMessage("init()", std::format("format '{}' unsupported; device offers: {}", requested, gr::join(supported, ", ")));
            return false;
        }
        std::vector<std::size_t> channels(nPorts);
        std::iota(channels.begin(), channels.end(), 0UZ);
        const soapy::Kwargs streamKwargs = stream_args->empty() ? soapy::Kwargs{} : soapy::parseKwargsString(stream_args.value);
        auto                opened       = _device.setupStream<T, SOAPY_SDR_RX>(channels, streamKwargs);
        if (!opened) {
            this->emitErrorMessage("init()", opened.error());
            return false;
        }
        _rxStream = std::move(*opened);
        // What spp actually became, asked of the stream rather than assumed from a default.
        _reportedStreamMtu = SoapySDRDevice_getStreamMTU(_device.get(), _rxStream.get());
        return true;
    }
};

template<typename T>
using UhdSimpleSource = UhdSource<T, 1UZ>;
template<typename T>
using UhdDualSource = UhdSource<T, 2UZ>;

} // namespace gr::blocks::sdr

#endif // GNURADIO_UHD_SOURCE_HPP
