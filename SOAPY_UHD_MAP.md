# SOAPY → UHD parameter map — extracted from the driver, not remembered

**Why this exists.** The owner (7-8 years with UHD, Soapy and GR): *the Soapy parameters map to UHD,
but it is often done in a confusing and ambiguous way. Some are nice (auto-sync) but
not well-documented, most parameters have different names than the UHD equivalent — lumping two
things into one, or the reverse — and **in a few cases name X is used in both UHD and Soapy but
means a different thing in each.***

That is exactly the class of error this project keeps paying for. Two examples from 2026-07-29/30
alone: `num_recv_frames` set in **stream args** where it is inert, when it only works in **device
args** (`RESULTS.md` §10.15); and `master_clock_rate` set through a *setter* after device creation,
where UHD has already chosen 16 MHz, rather than as a device arg (§10.11).

⚠ **Attribution, corrected by the owner 2026-07-30:** *SoapyUHD does a pretty good job of matching
UHD — it is the **universal Soapy driver** that is confusing.* That is the right target. The lumping
documented below is largely **forced on SoapyUHD by the SoapySDR universal API**, which must cover
RTL-SDR, HackRF, LimeSDR and UHD through one interface. `setHardwareTime(timeNs, what)` has a single
universal signature; mapping five distinct UHD time calls onto it is SoapyUHD making the best of a
constrained API, not SoapyUHD inventing confusion. Read the tables as *where the universal
abstraction loses information*, not as a criticism of the UHD shim.

Everything below is **extracted from `vendor/SoapyUHD/SoapyUHDDevice.cpp`** — the copy we build,
byte-identical to pothosware master (§10.26). Line numbers are that file. It is not from memory and
not from documentation.

---

## 1 · The dangerous one — `setHardwareTime` is FIVE UHD calls behind one name

`:870-878`. The second argument is a **magic string** that selects the UHD call:

| Soapy call | UHD call | what it actually does |
|---|---|---|
| `setHardwareTime(t, "PPS")` | `set_time_next_pps(t)` | arms the **next** PPS edge, blind |
| `setHardwareTime(t, "UNKNOWN_PPS")` | `set_time_unknown_pps(t)` | **waits for a PPS transition first**, then arms the following edge — the only multi-device-safe one. Blocks 1 to ~2 s by construction |
| `setHardwareTime(0, "CMD")` | `clear_command_time()` | ⚠ **not a time set at all** |
| `setHardwareTime(t, "CMD")` | `set_command_time(t)` | ⚠ sets the *command* time, a different concept |
| `setHardwareTime(t, <anything else>)` | `set_time_now(t)` | **silent default** — an unrecognised string does not error |

**Three traps here.** The `"CMD"` cases do not set the device time at all. The fall-through means a
typo (`"UNKNOWN PPS"`, `"unknown_pps"`) silently becomes `set_time_now` — no error, and every radio
lands on a different instant. And `timeNs == 0` changes the *meaning* of `"CMD"`.

`getHardwareTime` (`:864-868`) is asymmetric: `"PPS"` → `get_time_last_pps()`, anything else →
`get_time_now()`. So `"UNKNOWN_PPS"` is **not** a valid getter argument even though it is a valid
setter argument.

**This is `HANDOFF.md` C-9 generalised.** C-9 records that `UNKNOWN_PPS` and not `PPS` is the
multi-device primitive; the wider point is that one Soapy name hides five UHD behaviours with a
silent default.

## 2 · The other lumped one — `activateStream` is a whole `stream_cmd_t`

`:264-283`. Four Soapy arguments become one UHD stream command:

| Soapy | UHD field | note |
|---|---|---|
| `numElems == 0` | `mode = STREAM_MODE_START_CONTINUOUS` | |
| `numElems > 0`, `flags & SOAPY_SDR_END_BURST` | `mode = STREAM_MODE_NUM_SAMPS_AND_DONE` | |
| `numElems > 0`, otherwise | `mode = STREAM_MODE_NUM_SAMPS_AND_MORE` | |
| `flags & SOAPY_SDR_HAS_TIME` | `cmd.stream_now = (flags & HAS_TIME) == 0` | **inverted sense** — absence of the flag means "now" |
| `timeNs` | `cmd.time_spec = from_ticks(timeNs, 1e9)` | on the **device** clock, whatever `time_source` selects |
| `numElems` | `cmd.num_samps` | |

**This is H-1.** `activate()` with no arguments is `flags = 0` ⇒ `stream_now = true`, which UHD
refuses on a multi-channel streamer (§10.27).

## 3 · Straight pass-throughs — no translation, no surprise

| Soapy | UHD | line |
|---|---|---|
| `setSampleRate(RX, ch, rate)` | `set_rx_rate(rate, ch)` | `:757` |
| `setBandwidth(RX, ch, bw)` | `set_rx_bandwidth(bw, ch)` | |
| `setGain(RX, ch, g)` | `set_rx_gain(g, ch)` | |
| `setAntenna(RX, ch, name)` | `set_rx_antenna(name, ch)` | |
| `setMasterClockRate(rate)` | `set_master_clock_rate(rate)` | `:815` |
| `setClockSource(src)` | `set_clock_source(src, 0)` | `:832` |
| `setTimeSource(src)` | `set_time_source(src, 0)` | `:847` |

⚠ `setGain` also reaches `set_rx_agc` — automatic gain control is behind the *gain* family, not a
separate concept.

## 4 · `setFrequency` — a `tune_request_t` built from optional string keys

`:610-645`. Not a scalar set. It constructs `uhd::tune_request_t` and the kwargs steer the RF/DSP
split:

| kwarg | effect |
|---|---|
| *(none)* | `tune_request_t(frequency)` — **both policies AUTO**, UHD splits between LO and DDC as it sees fit |
| `OFFSET` | `tune_request_t(frequency, offset)` |
| `RF` | `tr.rf_freq`, `rf_freq_policy = POLICY_MANUAL` (unparseable ⇒ `POLICY_NONE`) |
| `BB` | `tr.dsp_freq`, `dsp_freq_policy = POLICY_MANUAL` (unparseable ⇒ `POLICY_NONE`) |

**This is the mechanism behind "the tuner is smart about small jumps"** — with AUTO policies a small
retune can be absorbed entirely by the DDC without moving the LO. `RF`/`BB` are how you force it.
Note the silent degradation: a value that fails `lexical_cast` becomes `POLICY_NONE` rather than an
error.

## 5 · Where a parameter is *entered* decides whether it works at all

Three different channels, easily confused, and the driver does not warn:

| channel | reaches | our setting |
|---|---|---|
| **device args** | `multi_usrp::make(args)` → device + transport construction | `SoapySource.device_parameter` |
| **stream args** | `uhd::stream_args_t.args` → `get_rx_stream(...)` (`:238-246`) | `SoapySource.stream_args` |
| **tune args** | `tune_request_t.args` per `setFrequency` call | `SoapySource.tune_args` |

**Measured:** `num_recv_frames` in stream args is **inert**; in device args it cuts overflow ~3×
(§10.15, §10.19). Ettus documents `recv_frame_size` as *"part of the device args"*.

## 6 · Open — what this map does NOT yet cover

The owner reports cases where **the same name means different things in UHD and in Soapy**. The
lumping above is documented; genuine name collisions are not yet enumerated here, because they need
identifying case by case against the UHD API rather than from the shim alone.

**Candidates to check, not yet verified:**

- `bandwidth` — Soapy `setBandwidth` → UHD `set_rx_bandwidth` (analogue front-end filter), which is
  *not* sample rate, though our harnesses set them to the same value.
- `gain` — Soapy has both an overall gain and named gain elements; `listGains`/`getGainRange` may not
  correspond one-to-one with UHD's gain profile.
- `time` — `setHardwareTime("CMD")` versus device time versus `time_source`, three distinct notions
  reachable through overlapping names (§1).

**Rule for this project:** before relying on a Soapy parameter, find its line in
`vendor/SoapyUHD/SoapyUHDDevice.cpp` and read what it becomes. It is a 1200-line file and the answer
is always there.
