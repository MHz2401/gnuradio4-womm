# WoMM README — what is ours, and where

Everything below is **unique to the `-womm` fork**. Everything not listed is upstream
`gnuradio/gnuradio4` and behaves as upstream does. `womm` = **Works On My Machine**.

⚠ `CLAUDE.md` is **upstream's** file, inherited from `gnuradio/gnuradio4` — not the owner's. See
`HANDOFF.md` for where it conflicts with this project.

---

## Start here

| path | what it is |
|---|---|
| `HANDOFF.md` | standing context; read before doing anything |
| `SPRINT.md` | current sprint, milestones, decisions awaiting the owner |
| `MANUAL.md` | one-page user manual: the CLI, known-good B210 parameters |

## Run something

| path | what it is |
|---|---|
| `scripts/womm-scan.sh` | demo CLI — capture IQ from all present radios, optionally analyse |
| `scripts/spectrum-check.py` | comb detection and per-radio LO calibration from captures |
| `scripts/epoch-check.sh` | N-radio PPS-epoch comparison from hold-open logs |
| `scripts/env.sh` | the only thing that activates the isolated prefix |

## Record of what happened

| path | what it is |
|---|---|
| `DESIGN_UhdSource.md` | **design for a conformant GR4 block over SoapyUHD** — the replacement for the abandoned Soapy block |
| `PARAMS.md` | **every `SoapySource` parameter: what it claims vs what it reaches** — the broken, the start-only, and what a replacement block must do |
| `SOAPY_UHD_MAP.md` | **Soapy→UHD parameter map extracted from the driver** — the lumped names, the silent defaults, and which of device/stream/tune args a setting must go in |
| `OPERATIONS.md` | **the tier-1 criterion as a diagnostic procedure**, plus the catalogue of anticipated causes |
| `RESULTS.md` | every measurement, including the retractions. Phase 10 is current |
| `BUILD_JOURNAL.md` | append-only decisions D1–D10, each with rationale and reversal |
| `DRIFT.md` | every deviation from upstream, by category, with removal cost |
| `MANIFEST.md` | dependency provenance and SHA pins |
| `UI_OPTIONS.md` | UI research across the three gr4 trees, feeding D10 |

## Harnesses — new source files

| path | what it is |
|---|---|
| `blocks/sdr/src/womm_rx_hold.cpp` | hold-open multi-channel RX; capture, tag and liveness modes |
| `blocks/sdr/src/womm_bmax.cpp` | ballast/deadline probe: radios plus synthetic load |
| `blocks/sdr/src/womm_mt_test.cpp` | single-process N-radio streaming benchmark (MT vs MP) |
| `blocks/sdr/src/womm_ops.cpp` | **S2-3: injects one named operation under load, with a null control** |
| `blocks/sdr/src/womm_b210_sweep.cpp` | single-radio rate sweep |
| `blocks/sdr/src/assert_no_tx.cmake` | **RX-only gate** — asserts no transmit symbol in the linked binary |
| `core/benchmarks/womm_bm_scaling.cpp` | synthetic DSP scaling benchmark, chains × threads |

## Patched upstream files — the fixes that make it work here

| path | what we changed |
|---|---|
| `blocks/sdr/include/gnuradio-4.0/sdr/SoapySource.hpp` | timed start for multi-channel; device timestamp capture; `ref_locked` polling; LO-lock wait; sample-gated tags |
| `blocks/sdr/include/gnuradio-4.0/sdr/SoapyRaiiWrapper.hpp` | `setHardwareTime` null-pointer segfault |
| `core/include/gnuradio-4.0/CircularBuffer.hpp` | five double-mapped-ring defects; **2.43× throughput** |
| `core/include/gnuradio-4.0/thread/thread_pool.hpp` | condvar replaces the macOS polling loop; **47× less CPU** |
| `core/include/gnuradio-4.0/Port.hpp` | tag-ring sizing; **3× memory cut** |
| `meta/include/gnuradio-4.0/meta/utils.hpp` | `shrinkIfSupported` — libc++ lacks a libstdc++ extension |
| `vendor/`, `patches/womm/` | SHA-pinned dependency trees and two SoapyUHD build patches |

**Full detail and reversal for every one of these is in `DRIFT.md`.**

---

## Four things that are easy to get wrong

- **Antennas are `TX/RX` and `RX2`.** `RX/TX` is not a name the device knows.
- **`master_clock_rate` is not optional for two channels** — 30e6 for 0.5 MS/s, 30.72e6 for 15.36.
- **Run `ctest` serially.** Device tests share one device instance per kwargs.
- **No serials, absolute paths or site details in the repo.** Radios come from `$B210U00…$B210U03`
  in your shell; output directories are given on the command line.

## Other topics and definitions

- `cross-process start barrier` and `cross-process arming barrier` refer to a process and mechanism
best understoood by reading `## FORWARD DEFINITIONS` section of `RESULTS.md`

## Where it stands

Four B210s, eight channels, **122.88 MS/s aggregate at ratio 1.0000**, one PPS epoch, LO agreement
to **1.72 ppb**, transmitted signal verified on every channel. Detail in `SPRINT.md`.
