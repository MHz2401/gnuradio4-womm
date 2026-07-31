# MANUAL — `womm-scan`, and known-good B210 parameters

One page. Everything here was measured on this machine, not read off a datasheet, except where
marked *(spec)*.

---

## Quick start

```bash
export B210U00=<serial>          # in your shell, NOT in the repo
mkdir -p ~/captures
scripts/womm-scan.sh -o ~/captures -a
```

Captures 0.2 s from every named-and-present radio at 2401.1042 MHz, then analyses it.

## Options

| flag | meaning | default |
|---|---|---|
| `-o DIR` | output directory. **Required, and must already exist** | — |
| `-f HZ` | centre frequency | `2401.1042e6` |
| `-r HZ` | sample rate, which is also the bandwidth | `0.5e6` |
| `-t SEC` | capture duration, **clipped** to `[0.001, 1.0]` | `0.2` |
| `-g dB` | RX gain | `20` |
| `-a` | also run the spectrum analysis; without it, capture only | off |

**Radios come from `$B210U00 … $B210U03`**, intersected with what actually enumerates. A named
radio that is absent is skipped with a note. If none are set, every visible radio is used.

**Output:** `capture_<serial>_ch<N>_fc<MHz>MHz_<n>ch_<ms>ms_<timestamp>.bin` — raw interleaved
`complex<float>` (IQIQ, host byte order), one file per channel. Read with
`numpy.fromfile(path, dtype=numpy.complex64)`.

Two steps by design: capture is a C++ binary, analysis is `scripts/spectrum-check.py`. `-a` runs
both. Analyse an earlier capture with:

```bash
scripts/spectrum-check.py ~/captures --centre 2401.1042e6 --rate 0.5e6
```

---

## Known-good B210 parameters, SoapySDR driver

### The two that bite

**`master_clock_rate` is not optional for two channels.** UHD refuses MCR above **30.72 MHz** with
two RX channels active, and the per-channel rate is MCR/decimation. Getting it wrong fails three
different ways, none of which names the constraint: a bare `activate()` `STREAM_ERROR`, a silent
halving, or an outright rejection.

| you want | set MCR to | decimation |
|---|---|---|
| 0.5 MS/s × 2 ch | **30e6** | 60 |
| 15.36 MS/s × 2 ch (the max) | **30.72e6** | 2 |
| any single-channel rate | leave at 0 (auto) | — |

**A multi-channel stream needs a timed start.** UHD rejects "stream now" on a streamer covering
several channels: *"stream now on multiple channels in a single streamer will fail to time align"*.
`SoapySource` now issues a timed start automatically when `num_channels > 1`. Nothing to set.

### Values

| setting | known-good | notes |
|---|---|---|
| `device` | `uhd` | |
| `device_parameter` | `serial=<serial>` | **serial, never an index** — UHD only enumerates unclaimed devices, so indices shift as peers start |
| `num_channels` | `1` or `2` | must match the `nPorts` template argument, or `setupStream` and the port count silently disagree |
| `rx_antennae` | `TX/RX` or `RX2` | **`RX/TX` is not a name the device knows.** Device reports `Antennas: TX/RX, RX2` |
| `rx_gains` | `20` dB | range differs by antenna: RX2 tops at ~76 dB, `TX/RX` at ~88 dB. Out of range returns `nan` |
| `sample_rate` | see MCR table | readback is checked; >1 % mismatch is an error |
| `rx_bandwidths` | = sample rate | **UHD's filters do not work much below 0.5 MHz** — do not ask for less |
| `clock_source` | `external` | with an Octoclock. **Poll `ref_locked`**; selecting an absent reference does not error, it free-runs |
| `time_source` | `external` | |
| `max_chunk_size` | `8192` | power of two. Sets capture granularity: 8192 at 0.5 MS/s = 16.4 ms, so short captures need a smaller chunk (the CLI shrinks it automatically) |
| `stream_args` | `num_recv_frames=1024` | USB transfer-buffer depth |
| `tag_interval` | `1.0` s | converted through `sample_rate`, so tags land at deterministic sample indices |

### Measured ceilings

| configuration | per channel | per radio | notes |
|---|---|---|---|
| 1 channel | ~43 MS/s | ~43 MS/s | above ~40 is a marginal band |
| **2 channels** | **15.36 MS/s** | **30.72 MS/s** | MCR limit; Nyquist relation to the sample clock |
| 4 radios × 2 channels | 15.36 MS/s | — | **122.88 MS/s aggregate, ratio 1.0000, ~29 % of the machine idle** |

Four radios locked to a common 10 MHz agree to **1.72 ppb**, and share a PPS epoch to within 39.8 ms.

---

## Gotchas

- **Gain has no universal convention.** A value that suits an RTL-SDR suits neither a B2xx nor a
  HackRF. `qa_SoapySource` hardcodes an RTL-SDR and a gain around 1000; that is out of range for a
  B210 and is why the test fails here. Not a defect — a test written for one platform.
- **Some operations must drop samples.** Retuning among them. A drop around a deliberate
  reconfiguration is expected, not a fault.
- **Device tests are not parallel-safe.** `DeviceRegistry::findOrCreate` shares one device per
  kwargs. Run `ctest` serially.
- **Bring-up is ~2.5 s per B210.** Never time a capture from process start; the CLI waits for
  samples first.
- **`max_bytes_per_file` rolls the file over, it does not stop at the cap.** In overwrite mode that
  restarts the same filename from zero. Size any cap above the intended capture, not at it.
- **2401.0 MHz is WiFi channel 1.** Non-round MHz are less crowded; the default centre is offset for
  that reason.

## RF safety

**Transmit is never automated.** Every harness here is receive-only by construction — no
`SoapySink`, and `assert_no_tx.cmake` asserts on the linked binary that no transmit symbol is
present. Keying a transmitter is the operator's action, on the operator's licence.

## Secrets

**Serials, absolute paths and site details stay out of the repository.** Radios are named through
`$B210U00 … $B210U03` in your shell. Captures go to a directory you name on the command line; the
default output location is nowhere.
