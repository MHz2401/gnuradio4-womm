# PARAMS — every `SoapySource` parameter, what it claims, what it reaches

**Purpose.** Owner's direction, 2026-07-30: *focus on reading documentation and avoid running with
mystery params.* This is the inventory that replaces guessing. **No hardware was used to produce it.**

**Completeness is checked, not assumed:** `GR_MAKE_REFLECTABLE` lists **37** members; the file
declares **35** `Annotated` fields plus **2** ports (`clk_in`, `out`). 35 + 2 = 37. ✔

**Verification status is marked per row.** Today produced three errors from reading only part of a
function (`RESULTS.md` §10.29, §10.36 and the `apply*` scan below), so anything not read end to end
is labelled **unverified** rather than asserted.

---

## A · Broken — declared and never read

Verified line by line: each appears **exactly twice**, at its declaration and in
`GR_MAKE_REFLECTABLE`, and nowhere else.

| parameter | claims | reality |
|---|---|---|
| **`tune_args`** | *"per-channel tuning kwargs"* | **never read.** `applyFrequency()` calls `setCenterFrequency(dir, ch, freq)` — the overload **without** args. So SoapyUHD's declared tune args `mode_n` (integer vs fractional N divider) and `int_n_step` are **unreachable through this block** |
| **`ppm_tag_threshold`** | *"emit corrected frequency/rate when ppm drift exceeds this"* | **never read.** The feature does not exist |

## B · Broken — read, but does not do what it says

| parameter | claims | reality |
|---|---|---|
| **`max_chunk_size`** | *"max samples per read (ideally N x 512)"*, marked `Visible` | reaches the read path **not at all**. Both loops size from a hard-coded `constexpr kReadSize = 512UZ * 16UZ` (`:317`, `:407`). Its single use (`:986`) sets the ppm-estimator update rate, and only when `ppm_estimator_cutoff > 0` — **which defaults to 0, so by default it does nothing** (§10.35). The knob it pretends to be is SoapyUHD's declared `spp`, which nothing in this tree sets |

## C · Works, but silently ineffective for USB

| parameter | reality |
|---|---|
| **`stream_args`** | read once (`:696`) → `parseKwargsString` → `setupStream` → `uhd::stream_args_t.args` → `get_rx_stream()`. SoapyUHD **declares** `num_recv_frames` and `recv_frame_size` as valid stream args, but on USB the transport is already built during `multi_usrp::make()`, so transport keys arrive too late and are **accepted without error and ignored** (§10.15, §10.19, `SOAPY_UHD_MAP.md` §7). `recv_buff_size` is not even offered for USB — it is pushed only `if (_isNetworkDevice)` |

## D · Start-only — changing these at runtime does nothing

`settingsChanged()` (`:128-177`) dispatches on exactly **11** keys. Everything else reflected is
applied only during `start()` → `reinitDevice()`.

**Runtime-changeable (11):** `frequency`, `sample_rate`, `rx_antennae`, `rx_gains`, `rx_bandwidths`,
`gain_mode`, `frequency_correction`, `dc_offset_mode`, `dc_offset`, `iq_balance`, `device_settings`
— plus `dc_blocker_cutoff` / `dc_blocker_enabled` / `ppm_estimator_cutoff`, which set dirty flags
rather than touching the device.

**Start-only, silently:** `device`, `device_parameter`, `master_clock_rate`, `clock_source`,
`time_source`, `reference_clock_rate`, `num_channels`, `stream_args`, `tune_args`, `frontend_mapping`,
`start_time_offset`, `max_chunk_size`.

⚠ **Nothing warns you.** A settings message changing `clock_source` at runtime is accepted, staged,
and never applied. For a UI (D10) this is a trap: the control moves, the radio does not.

## E · Correct, and doing what they claim

`device`, `device_parameter`, `sample_rate`, `num_channels`, `frequency`, `rx_gains`,
`rx_bandwidths`, `rx_antennae`, `clock_source`, `time_source`, `start_time_offset`,
`max_time_out_us`, `max_overflow_count`, `max_fragment_count`, `verbose_overflow`, `trigger_name`,
`emit_timing_tags`, `emit_meta_info`, `tag_interval`, `dc_blocker_enabled`, `dc_blocker_cutoff`,
`ppm_estimator_cutoff`, `gain_mode`, `frequency_correction`, `dc_offset_mode`, `dc_offset`,
`iq_balance`, `device_settings`, `frontend_mapping`, `reference_clock_rate`, `master_clock_rate`.

**Unverified within this group** — read at their declaration and dispatch but their `apply*` bodies
not read end to end: `frequency_correction`, `dc_offset_mode`, `dc_offset`, `iq_balance`,
`frontend_mapping`, `device_settings`, `reference_clock_rate`. A scripted scan of `apply*` bodies
overran its window and produced contaminated results, so those are **not** asserted here.

## F · Configuration defects, not parameter defects

| # | issue | evidence |
|---|---|---|
| F-1 | **We set both `sample_rate` and `master_clock_rate`.** Ettus recommends setting one — the sample rate. `applyClockConfig()` calls `setMasterClockRate()` (`:764`) and `applySampleRate()` calls `setSampleRate()` (`:783`) on every start | owner, from the B210 manual |
| F-2 | **`master_clock_rate = 0` does not mean "auto" usefully.** UHD chooses at `multi_usrp::make()`, before any rate is requested, and picks **16 MHz** — capping dual-channel at 8 MS/s. To let UHD choose well it must be a **device arg** | §10.11, §10.34 |
| F-3 | **`recv_frame_size=1024` is catastrophic at 15 MS/s/channel** — ~300× more overflow. Ettus recommends it *"if there are issues with performance or stability"*, which is guidance for a constrained host or low rate | §10.34 |
| F-4 | **Nothing validates `clock_source` / `time_source`.** The Doc strings say "e.g.", not an enumeration; the device answers `none, internal, external, gpsdo` (`time_source` also accepts `none`; `clock_source` does not). Selecting an absent reference **does not error** — the device free-runs (`:673`) | device probe |

## G · What a conformant replacement block must therefore do

1. **`start()` returns immediately** — defer device init, as `HttpBlock` defers via `readAsync`.
2. **Implement `processBulk`, not a `work()` override** — `SoapySource::work()` returns
   `{requestedWork, 0UZ, OK}` forever and bypasses the scheduler entirely (§10.24). `AudioBlocks`
   shows a device block doing this correctly: IO thread **and** `processBulk`.
3. **Put transport parameters in device args**, not stream args.
4. **Set the sample rate, not the MCR** (F-1), and if the MCR must be pinned, pin it as a device arg (F-2).
5. **Expose `spp`** — the real samples-per-packet knob — instead of a `max_chunk_size` that reaches nothing.
6. **Wire `tune_args`** so `mode_n=integer` is reachable; integer-N tuning is the standard B2xx measure
   for deterministic tuning and reduced fractional-N spurs.
7. **Validate `clock_source`/`time_source` against `listClockSources()`/`listTimeSources()`** and poll
   `ref_locked` rather than trusting the setter.
8. **Say which settings are runtime-changeable** and reject or warn on the rest, rather than silently
   staging them (D).

## H · Still to read

- The `apply*` bodies listed unverified in E, each read end to end.
- `getSettingInfo()` on an attached device — enumerates driver-declared **device settings**, the
  vocabulary behind `device_settings`. Read-only introspection, but needs hardware present.
- UHD's `stream_args_t` documentation, for which keys are honoured at `get_rx_stream()` versus at
  `make()` — the question behind C.
