# RESULTS

Machine: Mac Studio M2 Ultra, 16 P + 8 E cores, 192 GiB, macOS 26.5.2 (25F84), 16 KiB pages.
Repository root: `/Users/whom/dvel/ghzhub/GR4-fork/gnuradio4-womm`.

---

## Phase 3 — stock baseline

**Configuration.** `origin/main` @ `44275ed` (`4.0.0-RC2-13`), no local patches to gnuradio4.
Apple clang 21.0.0, `-DCMAKE_BUILD_TYPE=Release`, Ninja, all dependencies from the isolated
prefix. `GR_USE_FETCHCONTENT_DEPS=OFF` — no build-time downloads. Stock options otherwise:
`WARNINGS_AS_ERRORS=ON`, `GR_ENABLE_HTTP=ON`, `ENABLE_TESTING=ON`, `ENABLE_EXAMPLES=ON`,
sanitizers off, TBB off.

### Build

| Metric | Value |
| --- | --- |
| Targets | 1850 |
| Wall time | **603 s** (`-j16`) |
| Compiler errors | **0** |
| **Compiler warnings** | **0** |
| Linker warnings | 126, all one message (see below) |
| Peak compiler RSS, all 16 jobs | **11.4 GiB** (~730 MiB/TU) |

**Apple clang 21 builds gnuradio4 cleanly.** Three Phase-0 risks are closed by this:

- The `__PRETTY_FUNCTION__`-parsing reflection in `meta/…/reflection.hpp:137-183` has
  `static_assert`s that fire if the format shifts. They did not.
- `-Werror` is project-wide and Apple clang 21 is newer than the clang 20 upstream CI tests.
  **Zero compiler warnings**, so nothing to trip on.
- `blocklib_generator/tools/CMakeLists.txt:13` forces `-stdlib=libc++` unconditionally under
  Clang; the nested host-tool build succeeded.

**The 126 linker warnings are one repeated upstream wart**, not ours:
`ld: warning: ignoring duplicate libraries: 'core/libgnuradio-core.a'` — `libgnuradio-core.a`
appears twice on the link line because targets link core both directly and transitively. Benign.
Logged in `DRIFT.md` as an upstream observation; not patched.

**Job-count evidence (`DRIFT.md` D-1).** `CLAUDE.md:433` mandates `-j6`, citing swapping and OOM
kills. Measured peak across **16** concurrent compiler processes was 11.4 GiB — **6 % of this
machine's RAM**. The stated failure mode cannot occur here. The override is justified by
measurement, not assumption.

### Tests

`ctest --output-on-failure --timeout 300 -j8`

| Metric | Value |
| --- | --- |
| Tests run | 101 |
| **Passed** | **100** |
| Failed | 1 — `qa_SoapySource` |
| Wall time | 71.9 s |

Notably passing: **`qa_SoapyIntegration` (36.9 s)** and `qa_SoapyLoopback`, `qa_SoapyRaiiWrapper`.
Every SoapySDR source file in this repository is excluded from all upstream CI
(`blocks/sdr/CMakeLists.txt:9` guards on `if(TARGET SoapySDR)`, and no CI image installs it), so
these had **never been compiled or run on any platform** before this build.

#### The one failure — environmental, not a gnuradio4 defect

`qa_SoapySource.cpp:322` hardcodes `{"device", "rtlsdr"}`. No SoapyRTLSDR module is installed
(we built only SoapyUHD) and no RTL-SDR hardware is attached, so device creation fails at
`SoapyRaiiWrapper.hpp:334` and the assertion at `:339` trips.

**This test has no skip guard.** `qa_SoapyIntegration` self-skips when its device is absent
(`[SKIP] no LimeSDR device found`); `qa_SoapySource` does not, and hard-fails instead. That
inconsistency is an upstream test-design issue.

Building SoapyRTLSDR would **not** fix it — with no dongle attached, device creation would still
fail. The test presumes hardware.

Not disabled, not skipped, not worked around. Reported as a failure with diagnosis.

#### Secondary observation — possible watchdog thread leak

Run standalone rather than under ctest, `qa_SoapySource` **hangs for >300 s** instead of failing.
The test creates `createWatchdog(sched, 6s)` and then `watchdogThread.join()`s it
(`qa_SoapySource.cpp:335,343`). The hang is consistent with the watchdog thread never
terminating after the scheduler errors out.

This matters beyond one test: it is the same class of problem as the clean-shutdown acceptance
criterion (`DRIFT.md`/plan §4.C) — the scheduler's threads not winding down when a graph stops.
Flagged as a lead for Phase 3.5, not yet diagnosed.

### Hardware — B210 streaming confirmed

Through the isolated prefix: gnuradio4 `SoapySource` → SoapyUHD (`libuhdSupport.so`, ABI
`v0.8-3`) → UHD 4.10.0.0 → **USB 3** → B210.

```
[INFO] [B200] Detected Device: B210
[INFO] [B200] Operating over USB 3.
[INFO] [B200] Register loopback test passed
[INFO] [B200] Asking for clock rate 32.000000 MHz... Actually got clock rate 32.000000 MHz.
Basic API test - deviceDriver 'uhd'
  0: ret = 16384, flags(4) = [HAS_TIME], time_ns = 1286488250
  …
  6: ret = 16384, flags(4) = [HAS_TIME], time_ns = 1384792250
Basic API test - deviceDriver 'uhd' -- DONE
```

16384 samples per read, timestamps advancing 16.384 ms — consistent with 1 MS/s. All three B210s
enumerate (`SoapySDRUtil --find`).

---

## Measurement status

The Phase 3 build-time and RSS figures above are **single observations**, not medians over
repeated runs, and are labelled as such. They are process metrics, not performance claims.

The Phase 3.5 scaling curve below **is** a performance measurement: median and half-spread over
7 runs per point. No optimisation has been applied, and no ablation has yet separated the three
candidate mechanisms, so nothing below should be read as a diagnosis of *which* one dominates.

---

## Phase 3.5 — the discriminating experiment

**Question (§0):** does aggregate throughput scale with independent parallel chains? If it scales,
the DSP layer uses the machine and the ceiling is in the device/transport path. If it plateaus
early, the ceiling is in the runtime and no compiler tuning will move it.

**Harness:** `core/benchmarks/womm_bm_scaling.cpp` — new file, written rather than modifying
`bm_Scheduler.cpp`, which hard-pins the CPU pool to 2 threads (`:92`) and so cannot observe this
machine. Pure feedforward (no feedback edge, avoiding the documented ~100× cliff). Each chain is
`ConstantSource → 8×(MultiplyConst, DivideConst) → NullSink`, 1 M samples, 65536-sample buffers.
CPU pool sized to chain count per data point. Median and half-spread over **7 runs**.
`Simple<multiThreaded>`, Release, `-O3 -march=native`, Apple clang 21.

| chains | threads | median (s) | spread (s) | Msps | ×B210 | speedup |
|---|---|---|---|---|---|---|
| 1 | 1 | 0.1175 | ±0.0120 | 8.5 | 0.14× | 1.00× |
| 2 | 2 | 0.1317 | ±0.0119 | 15.2 | 0.25× | 1.79× |
| 4 | 4 | 0.1609 | ±0.0189 | 24.9 | 0.40× | 2.93× |
| 8 | 8 | 0.2969 | ±0.0475 | 26.9 | 0.44× | 3.16× |
| 16 | 16 | 0.6477 | ±0.0587 | 24.7 | 0.40× | 2.91× |

### Verdict: it plateaus early. The ceiling is in the runtime.

Peak aggregate is **~27 Msps at 8 chains — a 3.16× speedup on a machine with 16 P-cores**. Ideal
would be ~16×. Scaling is near-linear only to 2 chains (1.79×), already lossy at 4 (2.93× of an
ideal 4×), and flat thereafter.

**The 8→16 "regression" is inside the noise** — the spreads overlap (8 chains: 23.2–32.1 Msps;
16 chains: 22.6–27.2 Msps). Do not read it as a real decline. The **plateau**, however, is well
outside the noise: 4, 8 and 16 chains all land in a 24–27 Msps band while the offered work
quadruples.

**Growing spread is itself a signal:** ±0.012 s at 1–2 chains rising to ±0.059 s at 16, i.e. run-to-run
variance grows with worker count — the signature of contention rather than clean parallel work.

### Device-rate calibration

Peak throughput is **0.44× the maximum rate of a single B210**. This machine cannot sustain even
one radio at full rate through an 8-stage float graph.

*This comparison is generous to gr4, not harsh:* the benchmark streams **real** `float`, while a
B210 at 61.44 MS/s delivers **complex** samples — twice the data per sample. On like-for-like I/Q
the shortfall would be roughly double.

### What this rules in and out

- **Compiler/codegen tuning is not the answer.** No `-march`, LTO or PGO setting recovers a 3.2×
  ceiling on 16 P-cores. Hypothesis 5 is not the primary explanation. Phase 4 must not start with
  codegen knobs.
- **Consistent with per-hop cost dominating (hypothesis 1).** Single-chain 8.5 Msps through 16
  blocks is ~136 M block-hops/s on one core — order 30 cycles per hop at this clock, for work that
  is one multiply or one divide. The per-sample arithmetic is not what costs.
- **Does not yet separate the three candidate mechanisms**: the macOS mirror-`memcpy`
  (`CircularBuffer.hpp:352-378`), strided block→thread partitioning (`Scheduler.hpp:1378-1385`),
  and the absent Darwin QoS path (`thread_affinity.hpp`, 15 sites). All three predict this shape.
  Separating them is the remaining Phase 3.5 work, and they must be ablated **independently**
  before being combined.

### Reproduce

```
cd /Users/whom/dvel/ghzhub/GR4-fork/gnuradio4-womm
source scripts/env.sh
./build-baseline/core/benchmarks/womm_bm_scaling
```

### Owner-observed telemetry during the scaling run — narrows the diagnosis

Observed while `womm_bm_scaling` ran: **peak load ≈ 8 processor-equivalents** (from 16 worker
threads), and the CPU time is predominantly **`system`, not `user`**.

That single observation discriminates between the three candidate mechanisms, and it does so
against my prior ordering:

| Candidate | Predicted CPU accounting | Consistent? |
| --- | --- | --- |
| Mirror-`memcpy` (`CircularBuffer.hpp:352-378`) | **user** — a pure userspace copy | ✗ not what is seen |
| macOS 10 µs `sleep_for` poll (`thread_pool.hpp:635-657`) | **system** — a `nanosleep` syscall per iteration | ✓ |
| `std::atomic_ref::wait` → `__ulock_wait` (`Sequence.hpp:53`) | **system** — syscall | ✓ |
| Absent Darwin QoS (`thread_affinity.hpp`, 15 sites) | poor core residency, not system time per se | partial — fits ~8-of-16 utilisation |

Memory-bandwidth saturation from the mirror copy would show as user time and near-full core
occupancy. Neither is observed. **The ablation order is therefore reversed from the original
plan: wake/sleep path first, mirror-`memcpy` second.**

~8 processor-equivalents from 16 threads also means roughly half the workers are not doing useful
work, which is consistent with both the polling loop and the missing QoS hints.

*This is an owner observation from system telemetry, not an instrumented measurement by me. It
should be confirmed with a proper syscall/time-split profile before Phase 4 acts on it.*

---

## Teardown defect — `SoapySDRUtil` segfaults at exit (NOT gnuradio4)

`SoapySDRUtil --probe="driver=uhd"` completes all I/O correctly — full capability tree, sensors,
`temp 42.8 C`, `lo_locked true` — then **segfaults during `exit()`**.

Backtrace (lldb):

```
exit → __cxa_finalize_ranges
     → libuhd.4.10.0.dylib`log_resource::~log_resource()
     → __tree<…, std::function<void(uhd::log::logging_info const&)>>::destroy
     → __destroy_at → blr x8      EXC_BAD_ACCESS
```

**Root cause: static-destructor ordering across a `dlclose`d module boundary.** UHD's
`log_resource` singleton holds a map of log handlers whose values are `std::function`s. SoapySDR
unloads driver modules with `dlclose` (`vendor/SoapySDR/lib/Modules.in.cpp:300`, opened
`RTLD_LAZY | RTLD_LOCAL` at `:259`). Once `libuhdSupport.so` is unmapped, destroying those
`std::function`s calls into code that no longer exists.

**Not ours, and not gnuradio4's.** Every frame is `libuhd`, `libsystem_c` or `dyld`. It is not
caused by either of our two SoapyUHD patches, both of which are compile-time only.

**gnuradio4 is unaffected — verified.** `qa_SoapyIntegration`, `qa_SoapyLoopback` and
`qa_SoapyRaiiWrapper` all pass and exit cleanly with the same module loaded
(`qa_SoapyIntegration`, 37.5 s, repeated). gr4 never unloads the module, so it stays mapped and
the destructors find their code. The defect is reachable only via the utility's unload path.

Consequence: `SoapySDRUtil --probe` is safe to use for hardware verification — all output before
the crash is valid — but its exit status cannot be trusted. Recorded as an upstream defect;
not patched, since it does not affect the build under test.
