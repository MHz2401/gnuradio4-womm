# NAMEMAP — UHD ↔ SoapyUHD ↔ SoapySDR, verified

**Purpose (P0b).** Identify and verify the *specific command names and settings* for every operation
`UhdSource`/`UhdSink` depend on. Not a general reading of the source.

**Method.** Work outward from the anchor: **pure UHD, where the names are known** → **SoapyUHD**, the
translation layer, which we can read → **plain SoapySDR**. Owner's provenance note makes this tractable:
*SoapySDR began as a patch of the SoapyUHD driver, generalised to other radios* — so UHD's vocabulary is
the native one and Soapy's is that vocabulary with the UHD-specific names filed off. The mapping is
close to lossless, and traceable rather than guessed.

**Source read**: `vendor/SoapyUHD/SoapyUHDDevice.cpp` @ pinned `2a5d381f` (1162 lines).
⚠ **GPL-3.0.** This document records *interface names and their correspondences* — facts about an API,
not its expression. No SoapyUHD source is reproduced here or copied into our tree.

**No row says "assumed".** Anything unverified is in §9, separately.

## 0 · Why this document exists — ask the device, do not infer

Owner's standing methodological point, and it is the reason a name map is worth building at all:

> Ask **"did underflows occur?"** and **"can we read the sample index and GPS time from tags?"** —
> not "let me use a wall clock over a bunch of runs to count samples", or "let me measure the average
> load".

The first kind is a **direct query to an authoritative source**, answered exactly. The second kind is
**inference from a noisy proxy**, and every retraction this project has recorded came from it: the
count-drift instrument whose hypotheses sat 300× closer together than its noise; the comb detector that
reported a signal on silence; the "~44 MS/s ingest cap"; the load-average measurement that is really
`sample_rate × bytes_per_sample × number_of_channels`.

**A name map is the enabling artefact for the first kind of question.** You can only ask "did underflows
occur?" if you know the exact name of the thing that answers it — here, `readStreamStatus` →
`recv_async_msg` → `EVENT_CODE_UNDERFLOW` (§7). Without that name you are reduced to inferring underflow
from sample counts and a host clock, which is how you get a plausible number that is wrong.

Every row below is therefore chosen to answer a question **exactly**, and §9 lists what we cannot yet
ask exactly.

**Working practice earned the hard way this session** (2026-08-01): before designing against a
subsystem, **get a console dump from it running**. `RUN_Start_GR310_OneUnit_TxRx.log` — sixty lines from
a working GR 3.10 flowgraph — corrected five confident wrong inferences in a row: a doubled log line
read as two blocks contending; a clock-rate default read as a renegotiation; chart-startup variance read
as radio jitter; the GUI's start order reversed; and one over-correction on top. **Every one of them
came from reasoning about a mechanism instead of looking at a trace**, which is the same fault as
inferring from a proxy, one level up. Ask for the dump early; it is cheaper than the retraction.

---

## 1 · Device args — the route to the transport

`SoapyUHDDevice.cpp:1159` constructs as `multi_usrp::make(kwargsToDict(args))`, so **every device kwarg
passes straight through to `uhd::device_addr_t`** and reaches the transport at construction.

| purpose | key | verified |
|---|---|---|
| device selection | `serial`, `type`, `addr` | `:1118-1120` via `uhd::device::find(…, uhd::device::USRP)` |
| master clock rate | `master_clock_rate` | reaches `make()` as a device arg — the F-2 route |
| host ring depth | `num_recv_frames` / `num_send_frames` | reaches `make()`; **this is where they work** (§2) |
| frame size | `recv_frame_size` / `send_frame_size` | as above |

⚠ **`_isNetworkDevice = args.count("addr") != 0`** (`:44`). A USB B210 is addressed by `serial=`, so it
is **never** a network device, which gates one stream arg away from us entirely (§2).

## 2 · Stream args — `getStreamArgsInfo` (`:136-223`), `setupStream` (`:225-248`)

`setupStream` does `stream_args.args = kwargsToDict(args)`, so declared keys land in
`uhd::stream_args_t.args`; `WIRE` is lifted out separately to `otw_format`.

| Soapy key | UHD destination | note |
|---|---|---|
| **`spp`** | `stream_args.args["spp"]` | **the real samples-per-packet knob** — what `max_chunk_size` pretended to be |
| `WIRE` | `stream_args.otw_format` | `sc8` \| `sc16` |
| `peak` | `stream_args.args` | scaling in complex-byte mode |
| `fullscale` | `stream_args.args` | full-scale amplitude for float formats |
| `recv_frame_size` / `send_frame_size` | `stream_args.args` | ⚠ **declared unconditionally, including USB** |
| `num_recv_frames` / `num_send_frames` | `stream_args.args` | ⚠ same |
| `recv_buff_size` / `send_buff_size` | `stream_args.args` | **only pushed `if (_isNetworkDevice)`** (`:180`) — unreachable on USB |
| **`underflow_policy`** (TX only) | `stream_args.args` | ★ `next_burst` \| `next_packet` — UHD's documented TX-underflow recovery policy |

**✔ Confirms `PARAMS.md` §C exactly.** `recv_frame_size` and `num_recv_frames` are *declared valid* as
stream args on USB and then ignored, because the USB transport is already built during `make()`.
**They must be device args (§1).** The declaration is the trap; the driver is not lying, it is
generalising over transports it also serves.

**Read-back**: `getStreamMTU` → `rx->get_max_num_samps()` (`:187-193`). That is the **actual negotiated
samples-per-packet**, so `spp` never has to be guessed — ask the stream what it got.

## 3 · Activate / deactivate → `uhd::stream_cmd_t` (`:264-300`)

A literal 1:1. `activateStream(flags, timeNs, numElems)`:

| Soapy | UHD |
|---|---|
| `SOAPY_SDR_HAS_TIME` set | `cmd.stream_now = false`, `cmd.time_spec = time_spec_t::from_ticks(timeNs, 1e9)` |
| `SOAPY_SDR_HAS_TIME` clear | `cmd.stream_now = true` |
| `numElems == 0` | `STREAM_MODE_START_CONTINUOUS` |
| `numElems != 0` + `SOAPY_SDR_END_BURST` | `STREAM_MODE_NUM_SAMPS_AND_DONE` |
| `numElems != 0` otherwise | `STREAM_MODE_NUM_SAMPS_AND_MORE` |
| `deactivateStream` | `STREAM_MODE_STOP_CONTINUOUS` |

⚠ **On a TX stream `activateStream` is a no-op** — `if (not stream->rx) return 0` (`:267`), documented
in-source as "NOP, does nothing, but not an error". **TX is not armed by a stream command**; TX timing
is per-burst through `tx_metadata_t`. A `UhdSink` that waits for activation to mean something will wait
forever.

## 4 · Time and command time — `setHardwareTime` is a four-way multiplexer (`:870`)

The single most useful finding in this file.

| Soapy call | UHD call |
|---|---|
| `setHardwareTime(t, "PPS")` | `set_time_next_pps(t)` |
| `setHardwareTime(t, "UNKNOWN_PPS")` | `set_time_unknown_pps(t)` — waits for a PPS *transition* first (C-9) |
| `setHardwareTime(t, "CMD")` | **`set_command_time(t)`** |
| `setHardwareTime(0, "CMD")` | **`clear_command_time()`** |
| `setHardwareTime(t, "")` | `set_time_now(t)` |
| `getHardwareTime("PPS")` | `get_time_last_pps()` |
| `getHardwareTime("")` | `get_time_now()` |
| `setCommandTime(t, …)` | **deprecated** — in-source comment says it just forwards to `setHardwareTime(t, "CMD")` |

⚠ **Wart: the capability probe lies.** `hasHardwareTime(what)` returns true only for `"PPS"` or the
empty string (`:858`), so probing `hasHardwareTime("CMD")` answers **false** while
`setHardwareTime(t, "CMD")` is fully implemented directly below it. Do not gate `CMD` on the probe.

**Layer 2 consequence**: the "five device-wide setters" are really **four distinct operations plus one
deprecated alias**. Drop `setCommandTime` from the design; use `setHardwareTime(t, "CMD")`.

## 5 · ★ Timed retune from plain SoapySDR — the recipe, confirmed

Owner's expectation was that sophisticated timed operations are reachable from regular SoapySDR. For
retune, **they are**, using only standard SoapySDR API:

```
setHardwareTime(t_future_ns, "CMD");    // → set_command_time(t)  — all subsequent calls execute at t
setFrequency(dir, channel, freq, args); // → set_rx_freq / set_tx_freq, now timed
setHardwareTime(0, "CMD");              // → clear_command_time() — back to immediate
```

Nothing SoapyUHD-specific is required. **This is the mechanism the plan says to reach rather than
rebuild.**

## 6 · Tune args — `setFrequency(dir, ch, freq, args)` (`:609-643`)

| Soapy args key | UHD |
|---|---|
| `OFFSET` | `tune_request_t(freq, offset)` — LO offset |
| `RF` | `tr.rf_freq` + `POLICY_MANUAL`; unparseable → `POLICY_NONE` |
| `BB` | `tr.dsp_freq` + `POLICY_MANUAL`; unparseable → `POLICY_NONE` |
| **everything else** | **`tr.args = kwargsToDict(args)`** — so `mode_n=integer`, `int_n_step` reach UHD |

**✔ Confirms `PARAMS.md` §A-1 and its fix.** The mechanism exists and is complete; `SoapySource` simply
calls the overload *without* args, so `tune_args` is unreachable through that block. Wiring it is a
one-argument change, not a feature.

## 7 · Async status — the TX underflow channel (`:349`)

| Soapy | UHD | maps to |
|---|---|---|
| `readStreamStatus` | `tx_streamer::recv_async_msg` | RX stream → `SOAPY_SDR_NOT_SUPPORTED` (TX-only by construction) |
| | `EVENT_CODE_UNDERFLOW`, `…_UNDERFLOW_IN_PACKET` | `SOAPY_SDR_UNDERFLOW` |
| | `EVENT_CODE_SEQ_ERROR`, `…_SEQ_ERROR_IN_BURST` | `SOAPY_SDR_CORRUPTION` |
| | `EVENT_CODE_TIME_ERROR` | **`SOAPY_SDR_TIME_ERROR`** |
| | `EVENT_CODE_BURST_ACK` | flag `SOAPY_SDR_END_BURST` |
| | `md.has_time_spec` | flag `SOAPY_SDR_HAS_TIME`, `timeNs = md.time_spec.to_ticks(1e9)` |

**This is the Ettus async message stream** the owner quoted, it is reachable, and **no in-tree block
calls it**. ⚠ `SOAPY_SDR_TIME_ERROR` is a code `SoapySource::handleStreamError` does not know — its
`default:` branch stops the graph on it.

## 8 · Clock and time source

| Soapy | UHD |
|---|---|
| `listClockSources` / `setClockSource` / `getClockSource` | `get_clock_sources(0)` / `set_clock_source(s, 0)` / `get_clock_source(0)` |
| `listTimeSources` / `setTimeSource` / `getTimeSource` | `get_time_sources(0)` / `set_time_source(s, 0)` / `get_time_source(0)` |
| `setMasterClockRate` / `getMasterClockRate` | `set_master_clock_rate` / `get_master_clock_rate` |

All hardcode **mboard 0** — correct for a B210 (one mboard per device), and a constraint to remember if
a multi-mboard device ever appears.

## 8b · Sensors, and the GPS-time question specifically

| Soapy | UHD |
|---|---|
| `listSensors()` | `get_mboard_sensor_names()` |
| `readSensor(name)` | `get_mboard_sensor(name).value` |
| `getSensorInfo(name)` | `get_mboard_sensor(name)` → `ArgInfo` (carries a type) |
| `listSensors(dir, ch)` / `readSensor(dir, ch, name)` | `get_rx_sensor_names(ch)` / `get_rx_sensor(name, ch).value` (and TX) |

**★ SoapyUHD contains no GPS-specific code whatsoever** — `grep -i gps` returns nothing. GPS reaches us
purely as **UHD mboard sensor names passed through as strings**: on a GPSDO-equipped device those are
Ettus's names (`gps_time`, `gps_locked`, `gps_gpgga`, `gps_gprmc`, `gps_servo`), and SoapyUHD neither
knows nor validates them.

So, answering the question directly:

- **Sample index in tags — yes, and exact.** It is the tag offset, an integer, with the invariance
  property of §5 of the plan (`Offset` of the k-th per-second tag is exactly `k × sample_rate`).
- **GPS time in tags — yes, via `readSensor("gps_time")`**, with three caveats that follow from the
  pass-through: the value arrives as a **`std::string` and must be parsed**, so precision can be lost by
  careless handling; the **sensor name must come from `listSensors()`**, never hardcoded (C-10 and C-11
  are both instances of hardcoding a name the device did not have); and `readSensor` is a **poll of a
  sensor, not a sample-aligned quantity** — it is not interchangeable with `rx_time`, which *is*
  sample-aligned. Carry both, labelled, rather than conflating them.
- ⚠ **`gps_locked` must be polled to a deadline, not sampled once** — the same defect as `ref_locked`
  (C-11, mistake #8).

---

## 9 · UHD itself — read at v4.6.0.0, and it changes the plan

Source: `EttusResearch/uhd` @ tag `v4.6.0.0`, sparse checkout of `host/lib` + `host/include`, 20 MB.
Version chosen to match the run log and to differ visibly from brew's 4.10.0.0.
⚠ **GPL-3.0 — read for facts and procedures. Nothing copied.**

### ★★★ 9.1 — UHD serialises device creation globally. Parallel bring-up is impossible above UHD.

**Whose mutex, and at which layer** — verified call chain, because the answer is *not* Soapy:

```
our block
  → our DeviceRegistry::findOrCreate    SoapyRaiiWrapper.hpp:219  ← our lock, held across make. OURS TO FIX
  → SoapySDR  Device::make              Factory.cpp:133-197       ← deliberately UNLOCKS around slow work ✔
  → SoapyUHD  makeUHD                   SoapyUHDDevice.cpp:1159   ← multi_usrp::make(kwargsToDict(args))
  → UHD       multi_usrp::make          multi_usrp.cpp:2762-2767  ← calls device::make(dev_addr, USRP)
  → UHD       device::make              device.cpp:110-112        ★ static std::mutex _device_mutex (:23)
```

`_device_mutex` is a **file-static `std::mutex` inside UHD's own `host/lib/device.cpp`**, private to
that translation unit. `device::make()` takes a **plain `lock_guard`** on it as its **first statement**
and holds it for the entire body — through device **discovery** (every registered find function, i.e.
USB enumeration) *and* through `maker(...)`, the actual construction. Released only on return.

⚠ **The serialising layer is UHD, not SoapySDR.** Modifying SoapySDR therefore cannot buy parallel
bring-up — the bottleneck is underneath it. The SoapySDR work still buys the **non-blocking lifecycle**
it was specified for; those are two different goods and must not be conflated.

**Does moving initialization to `init()` just relocate the wait?** Partly, and honestly: the ~14 s of
serialised UHD work still exists and still takes ~14 s. **`init()` itself never takes the mutex** — it
spawns and returns in ≪1 ms; the background thread queues on UHD's mutex. What changes is *who waits*:
from the scheduler's start path (where it causes timeouts and a dead UI) to a background thread nothing
is blocked on. **That fixes the defect. It does not make four radios come up faster.** Time-to-first-
sample may improve because `emplaceBlock` calls `init()` earlier than `start()` happens, overlapping
other graph setup — **to be measured, not assumed.**

| layer | behaviour |
|---|---|
| SoapySDR `Device::make` | **unlocks** around enumeration and around the driver call (`Factory.cpp:133-197`) ✔ |
| our `DeviceRegistry::findOrCreate` | holds a lock across the whole make ✗ — **ours to fix** |
| **UHD `device::make`** | **holds a global mutex across discovery *and* construction ✗✗ — the real ceiling** |

**Consequence: four B210s cannot initialize concurrently, whatever we do above UHD.** Fixing our
registry lock and adding an async contract to SoapySDR moves the queue; it does not remove it.

**★ This does not invalidate the plan — it sharpens what the plan buys.** Distinguish:

- **Asynchronous** — the caller is not blocked. **We get this**, from the block-level init thread, and
  it is the whole of button semantics. Four radios initialize serially *in the background* while the
  graph runs and the UI comes up.
- **Parallel** — total bring-up time shrinks. **We do not get this** without changing UHD.

The user-visible defect (a graph that will not start for ~14 s, and times out) is fixed by the first.
Total bring-up stays roughly serial. **Say so plainly; do not let "non-blocking start" be read as
"faster start".**

### ★★ 9.2 — UHD already shares one device between Source and Sink

`device.cpp:158`: a **file-static `uhd::dict<size_t, std::weak_ptr<device>> hash_to_device`**, keyed on
a hash of the discovered device address. An identical address returns the *same* `device::sptr`.

**So the owner's expectation is confirmed at the UHD layer**: a Source and a Sink on one radio share one
underlying device automatically, with no coordination by either block. What is *not* shared is the
blocks' **reflected settings** — the hardware is common, the displayed values are not, which is exactly
the gap Layer 2 has to close.

### ⚠ 9.3 — a new hazard: the cache key ignores transport args

The hash is computed at `:147` from the **discovered** `dev_addr`; hint-only keys are merged in
*afterwards* (`:150-154`). So **transport arguments do not participate in the cache key.**

**Two `make()` calls for the same radio with different `num_recv_frames` return the same cached device,
and the second caller's transport args are silently discarded.** First constructor wins. For a
Source asking for a deep ring and a Sink taking defaults, that is a silent, order-dependent difference
in overflow margin — precisely the class of bug this project keeps finding. **Layer 2 must arbitrate
device args *before* the first `make()`, not merely reconcile settings afterwards.**

### 9.4 — the 16 MHz is `DEFAULT_TICK_RATE`, and pinning the MCR disables auto-selection

`b200_impl.cpp:814-818`. The log line *"Setting master clock rate selection to 'automatic'"* is printed
**only when the user has not supplied a `master_clock_rate` device arg**; the value taken is
`device_addr.cast<double>("master_clock_rate", ad936x_manager::DEFAULT_TICK_RATE)`, and the in-source
comment is explicit that auto-selection happens *"but not if the user specifies one"*.

⚠ **So `PARAMS.md` F-2's advice cuts both ways.** Pinning `master_clock_rate` as a device arg does fix
the 16 MHz cap — **by switching off the selector that correctly chose 40 MHz in the run log.** It is a
trade, not a free win: pin it when you need a specific MCR, leave it unset when you want UHD to serve
the requested rate. Either way the *rate* is what you set (F-1).

⚠ **The selection is not a fixed multiple of the sample rate.** Measured here, on this machine, by
`qa_UhdSource` against a real B210: **1 MS/s selected 32 MHz**, where the GR 3.10 log's 10 MS/s selected
40 MHz. Ettus document the selector as *maximising* the clock so as to enable as many half-band filters
as possible, which fits both observations. An earlier draft of this document glossed the log's 40 MHz as
"4× decimation" — **wrong, and corrected by a measurement rather than by more reading.**

### ★ 9.4a — external REF gives RATE agreement, not EPOCH agreement (measured)

Owner's hypothesis, 2026-08-01: with the external clock set, SoapySDR establishes the shared
epoch automatically, and that automatic step is what was stalling `start()`.

**Measured on four B210s sharing an Octoclock**, `clock_source=external`, `time_source=external`,
using the `get_time_last_pps()` discriminator:

| what was done | last-PPS across four radios | spread |
|---|---|---|
| external REF+PPS selected, **no zeroing at all** | 11.742958 .. 20.252181 s | **8.509223 s** |
| external REF+PPS, **one transition then broadcast `PPS`** | 11.000000 on all four | **0.000000 s** |

**So selecting `external` does NOT align the epoch.** It buys **rate agreement** — every radio
ticks off one 10 MHz, so they do not drift relative to each other — but each device's time
counter keeps its own arbitrary power-on offset. The PPS signal is present and shared; what is
missing is telling each device *what time the next edge represents*. **Frequency lock is not
time alignment.**

**Corroborating signature, and a useful check on its own:** an un-zeroed radio reports a
**fractional** last-PPS time; a radio zeroed *on* a PPS edge reports an **exact integer**,
because every subsequent edge then lands on a whole second by construction. Fractional
last-PPS = that radio was never given an origin.

**On the causal half:** the ~2.8 s per radio that forced device bring-up out of `start()` is
`multi_usrp::make()` — detect, USB 3, CODEC control, radio control, register loopback
self-test, all visible in `RUN_Start_GR310_OneUnit_TxRx.log`. The `UNKNOWN_PPS` call adds its
own ~1-2 s on top, but it is the second cost, not the first.

`WOMM_NO_ZERO=1` on `womm_uhd_rx` reproduces the first row.

### ★ 9.4b — four USB B210s CANNOT be one `multi_usrp`, and nothing coordinates them in software

Owner's hypothesis, 2026-08-01: `multi_usrp` means several USRPs treated as one, as opposed
to launching independent ones — and perhaps setup is serial because the devices are assigned
some internal ID that lets them coordinate afterwards.

**First half confirmed.** `multi_usrp` holds one `device::sptr` but counts mboards from
`_tree->list("/mboards")` (`multi_usrp.cpp:791`), so one device may carry several mboards and
`ALL_MBOARDS` broadcasts across them. That IS the SIMD primitive, implemented inside UHD.

**But the B200 driver opts out.** `b200_impl.cpp:309` hardcodes `const fs_path mb_path =
"/mboards/0"` — one mboard per device, always. And `b200_find` (`:184`) walks
`separate_device_addr(hint)`, UHD's helper for the `addr0=…,addr1=…` multi-device syntax, then
**returns an empty list if any entry carries `addr` or `resource`**: the driver explicitly
refuses the addressing scheme by which networked USRPs (X300 and friends) become one device
with several mboards.

⇒ **Four USB B210s are four devices, four mboards-0, four `multi_usrp` objects.** The atomic
timed command cannot span them, which is why the multi-device epoch needs an external
broadcast (measured in §9.5: per-radio `UNKNOWN_PPS` → 6.000000 s spread; one detection then
broadcast → 0.000000 s).

**Second half not supported.** The serialisation is a plain global lock — `static std::mutex
_device_mutex` (`device.cpp:23`), taken as the first statement of `device::make` and held
through discovery *and* construction (§9.1). The neighbouring `hash_to_device` weak_ptr cache
exists to *reuse* an already-open device, not to coordinate separate ones, and no inter-device
ID or shared state is created anywhere on that path.

⇒ **Four B210s are strangers to each other. Every bit of their coordination arrives over the
Octoclock cables and none of it through software** — which is what makes the sub-mm
cable-length discipline load-bearing: there is no software layer that could compensate for it.

### 9.5 — `set_time_unknown_pps()` in full, and it is the owner's described sequence

`host/lib/usrp/multi_usrp.cpp:491-520`. One call does exactly this:

1. log **`1) catch time transition at pps edge`**
2. poll `get_time_last_pps()` until it **changes**, deadline **1100 ms** — on timeout **throws**
   *"Board 0 may not be getting a PPS signal!"*
3. log **`2) set times next pps (synchronously)`**
4. `set_time_next_pps(time_spec, ALL_MBOARDS)`
5. **`sleep_for(1 second)`** — unconditional
6. verify every mboard's clock agrees with board 0 to within **10 ms**, warn on deviation

**✔ This is the owner's description, in code**: catch the transition, arm the *next* edge, all boards
identically, then verify. It also explains the failure mode behind mistake #8 — a missing PPS is a
throw, not a silent free-run, at *this* layer.

**Cost, exactly**: ≥ 1 s of unconditional sleep per call, plus up to 1.1 s of polling.

### ⚠ 9.6 — the doubled log lines: what the code says, flagged for the owner

**The pair `1)`/`2)` is printed exactly once per `set_time_unknown_pps()` call** — both lines are inside
one function body, unconditional, no loop. Therefore **two pairs in the log means two calls.**

That partially reinstates a claim I retracted. Recording both readings rather than picking:

- **Owner, walking through the log**: *"only one of each operation, not two."* In context this sits
  under the **initialization** step, and it is plainly true of the `[B200]` block — detect, CODEC,
  radio control, loopback, MCR each appear once, **even though two blocks are present**. §9.2 explains
  why: one cached `device`, initialized once, shared.
- **The code**: the `[MULTI_USRP]` pair cannot print twice from one call. Two blocks each construct a
  `multi_usrp` over the *shared* `device`, and each calls the sync.

**These are consistent if the boundary is `device` vs `multi_usrp`**: the *device* is initialized once
and shared; the *sync* runs once per `multi_usrp`, i.e. once per block. That would mean a Source+Sink
pair pays the ≥1 s sleep twice and re-zeros the clock twice — the second landing after the first.

⚠ **Not asserted.** It rests on each GR 3.10 block constructing its own `multi_usrp`, which is *not*
read — `gr-uhd` is GPL-3.0 and out of scope for reading. **Owner to adjudicate**, since it decides
whether D-6 is a real defect in GR 3.10 or a misreading of the log for the second time.

### 9.7 — still open

| question | settled by |
|---|---|
| Was **H-1** a driver gap or our own bypass? A bare `activate()` was refused on a multi-channel streamer; the default path is not a bare activate | `b200_impl` / `rx_streamer` refusal path, plus a hardware retry |
| Which timed operations are *not* reachable from plain SoapySDR? | one row left: PPS re-sync after over/underflow |

**Retune is confirmed reachable from plain SoapySDR (§5).** PPS re-sync uses the same
`setHardwareTime(t, "UNKNOWN_PPS")` entry point (§4), so it is **reachable by the same argument** — but
whether re-syncing *mid-stream* is safe, versus only at bring-up, is a hardware question, not a
name-map question.
