# RESULTS

Machine: Mac Studio M2 Ultra, 16 P + 8 E cores, 192 GiB, macOS 26.5.2 (25F84), 16 KiB pages.
Repository root: `/Users/whom/dvel/ghzhub/GR4-fork/gnuradio4-womm`.

---

## FORWARD DEFINITIONS 

Some terms are initially clear from context, and then re-used without repeating the definition, 
which is acceptable practice.  In such cases, definitions are added here.

### (1) cross process arming barrier/start barrier

The `cross-process start barrier` / `cross-process arming barrier` is a recurring topic and a 
challenging problem.  It eventually needs a cleaner solution beyond the scope of current 
session (pre-Sprint-2 asof 20260728 1900H Pacific). 

This is fundamentally a process-and-multiple-device sync mechanism that's best understood from 
a combination of code and usage. See the implementation of `womm_bmax.cpp:233-246` and do a 
case-insensitive search for `cross-process` in `RESULTS.md` (this document). 

What it currently does: each process touches `${WOMM_BARRIER_DIR}/ready.<pid>`, then polls until
`WOMM_BARRIER_N` such files exist, with a 120 s deadline. What it does **not** do is bound the
window between the last arrival and any subsequent action, which is the only property that would
make PPS-edge agreement structural rather than observed. Specifying it is sprint-2 work, not a
one-line reuse.

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

---

## Phase 3.5b — harness confound ruled out; curve re-run on the fixed tree

I flagged a possible confound in my own harness: `womm_bm_scaling` calls `Manager::replacePool()`
once per data point, so leaked pools could have left stale workers spinning and flattened the
curve. Settled by instrumenting live thread count (`task_threads`) and re-running the sweep two
ways — in one process, and one process per point.

| chains | A: in-process | B: per-process | live threads | expected |
|---|---|---|---|---|
| 1 | 8.4 Msps | 8.4 | 3 | 3 |
| 2 | 15.5 | 15.2 | 4 | 4 |
| 4 | 24.2 | 24.4 | 6 | 6 |
| 8 | 27.4 | 23.3 | 10 | 10 |
| 16 | 25.9 | 25.4 | 18 | 18 |

**No leak.** Live thread count is exactly `nThreads + 2` at every point, identical in both
methods; a leak would have made A grow relative to B. Throughputs agree within noise throughout.
**The plateau is real, not an artefact of my harness.**

### The correctness fixes do not move throughput

Cherry-picked tree vs stock baseline: 8.4/15.5/24.2/27.4/25.9 against 8.5/15.2/24.9/26.9/24.7
Msps. Unchanged within noise, as expected — they fix a watchdog leak, a deadlock, restart
correctness and some allocation churn, none of which is on this hot path. **The ceiling is
untouched by them**, which further isolates it to the wake/sleep and buffer paths.

### Where the diagnosis now stands

18 live threads producing ~8 processor-equivalents of load, predominantly **system** time,
with throughput flat from 4 chains on. Threads exist and are scheduled; they are not computing.
Remaining suspects, in the order the evidence now supports:

1. macOS 10 µs `sleep_for` polling loop (`thread_pool.hpp:635-657`) — syscall per iteration
2. Absent Darwin QoS (`thread_affinity.hpp`, 15 no-op sites) — no P-core preference
3. Mirror-`memcpy` (`CircularBuffer.hpp:352-378`) — demoted; would burn user, not system, time
4. Strided block→thread partitioning (`Scheduler.hpp:1378-1385`)

---

## ⚠ Phase 3.5c — CORRECTION. The earlier curve measured setup, not throughput

**The Phase 3.5 headline above is wrong and is retracted.** It reported peak throughput of
0.44× a single B210's rate and concluded the machine could not sustain one radio. That was an
artefact of a benchmark dominated by graph construction, not steady-state DSP.

### How it was caught

`/usr/bin/time -l` on the 16-chain point:

| | 1 M samples/chain | 20 M samples/chain |
|---|---|---|
| throughput | 25.7 Msps | **335.4 Msps** |
| user / sys | 10.0 s / 64.0 s | 46.9 s / **61.2 s** |
| page reclaims | 1,710,937 | **1,764,674** |
| peak RSS | 6.6 GiB | 8.0 GiB |

Twenty times the work, **the same system time and the same page-fault count**. The 63 s of system
time is a *fixed per-setup cost*, not per-sample. At 1 M samples/chain the benchmark spent most of
its wall time allocating and first-touching buffers, and I reported that as a runtime ceiling.

### The true curve — 20 M samples/chain, one process per point

| chains | threads | median (s) | spread (s) | Msps | ×B210 | speedup |
|---|---|---|---|---|---|---|
| 1 | 1 | 0.1185 | ±0.0099 | 168.8 | **2.75×** | 1.00× |
| 2 | 2 | 0.2379 | ±0.0695 | 168.1 | 2.74× | 1.00× |
| 4 | 4 | 0.2703 | ±0.0293 | 295.9 | 4.82× | 1.75× |
| 8 | 8 | 0.5007 | ±0.0898 | 319.5 | 5.20× | 1.89× |
| 16 | 16 | 0.9175 | ±0.0729 | 348.8 | **5.68×** | 2.07× |

**A single chain sustains 2.75× a B210's maximum rate** through eight multiply/divide stages.
SIMD is clearly working: 168.8 Msps × 16 blocks ≈ 2.7 G block-operations/s on one worker, which
at this clock is only plausible with vectorisation.

### What survives, and what does not

- **Retracted:** "cannot sustain even one radio at full rate." Wrong by ~13×. The practical
  capability is comfortable — several B210s' worth of headroom per chain.
- **Survives, but milder:** parallel scaling is still poor. 16 chains yield **2.07×**, not 16×,
  and 1→2 chains yields *nothing at all* (168.8 → 168.1). The runtime does not use the machine's
  width. That question is unchanged; its urgency is much reduced.
- **New finding — graph lifecycle leaks memory.** Seven graph builds produced **8 GiB peak RSS**
  and **1.7 M page reclaims** for a workload whose live sample data is ~64 MB. Buffers are
  evidently not released between graph lifecycles. On 16 KiB pages that is ~28 GB of first-touch
  activity. This is a real defect and it is the dominant cost of any build-run-teardown cycle —
  which is exactly the shape of a rate sweep, or of any application that reconfigures a flowgraph.

### Methodological lesson

A benchmark must be shown to be measuring its steady state before its numbers mean anything. The
tell was available in the first run and I did not look for it: `/usr/bin/time -l` costs nothing.
Every throughput figure in this document is now taken at 20 M samples/chain, where the marginal
cost per sample dominates the fixed cost.

---

## Phase 5 attempt — end-to-end B210 sweep: BLOCKED, not completed

`blocks/sdr/src/womm_b210_sweep.cpp` streams a real B210 through the same 8-stage chain and
reports the achieved sample rate. **It does not yet work: the sink receives zero samples.**

### What is ruled out

| Hypothesis | Test | Result |
|---|---|---|
| DSP chain at fault | ran with depth 0, source wired straight to sink | still 0 samples |
| Silent connection failure | replaced `std::ignore =` with a checked `mustConnect` | all connections succeed |
| `complex<float>` unsupported by math blocks | `Math.hpp:27` registration | `std::complex<float>` **is** registered |
| Device not initialising | UHD log | B210 detected, USB 3, clock rate 32 MHz set |
| Scheduler erroring | `runAndWait()` return value | returns **success** |
| Wrong channel/antenna | set `num_channels=1`, `rx_antennae={"RX2"}` from the probe | no change |

So the graph is wired, the radio initialises, the scheduler runs and exits cleanly — and no
samples arrive.

### What is not ruled out

Either my configuration of `SoapySource` is wrong in some way not yet found, **or** the block
itself has never worked against real hardware. The second is not far-fetched: in this tree the
raw `SoapyRaiiWrapper` path demonstrably streams (`ret = 16384, flags = [HAS_TIME]`, observed
earlier), but the **block-level** `SoapySource` has no passing hardware test anywhere —
`qa_SoapySource`'s only device case hardcodes an absent RTL-SDR, and `qa_SoapyIntegration`
self-skips without a LimeSDR. Combined with every Soapy file being excluded from all upstream CI,
the block-level path may never have been exercised end-to-end by anyone.

**Stated as unresolved.** Distinguishing the two needs a run of the block against the synthetic
`LoopbackDevice`, which is the cheapest next experiment and does not need hardware.

### Two harness defects of mine, fixed along the way

1. **Relying on `CountingSink::n_samples_max` to end the graph.** A radio source never finishes,
   so the sink's DONE cannot stop it. The first version wedged at **1717 % CPU** — 17 cores
   spinning in `waitDone` → `sleep_for` and `BasicThreadPool::worker` → `sleep_for` — and had to
   be killed. `qa_SoapySource.cpp:335` uses an explicit watchdog for exactly this reason.
   Replaced with `requestStop()` after a fixed duration, plus a 15 s hard backstop so a wedged
   graph can never hold the machine again.
2. **`std::ignore = graph.connect(...)`** discarded connection failures. Now checked.

That first wedge is worth noting beyond this harness: it is precisely the "threads outlive the
graph, machine needs a reboot" failure mode, reachable by ordinary misuse of the API.

### Lead for next session (owner's hypothesis, not yet tested)

The Soapy abstraction may omit device-specific stream-setup semantics that a USRP requires and a
repurposed TV tuner does not. Concrete form: `activateStream(flags, timeNs, numElems)` — **UHD
treats a non-zero `numElems` as a finite burst; RTL-SDR ignores the argument.** If `SoapySource`
passes a burst count, or does not express continuous-stream intent, an RTL-SDR would appear to
work while a B210 delivers nothing. That matches the observed symptom exactly.

Order for next session:
1. Read how `SoapySource` calls `activateStream` (`SoapyRaiiWrapper.hpp`, activate path).
2. Run `SoapySource` against the synthetic `LoopbackDevice` — no hardware, settles
   "my config" vs "the block".
3. Only then return to the B210.

Also open, and may need a decision: HackRF Pro reportedly adds UHD-like clocking, so it would
likely hit the same path — relevant if the hardware list grows.

---

## Phase 5 follow-up — `SoapySource` yields no samples. Now well-controlled.

Ran the overnight experiments. The finding is firmer and two hypotheses are dead.

### Control establishes the harness is sound

| Source | Device | Achieved |
|---|---|---|
| `SoapySource` | B210 (real) | **0 Msps** |
| `SoapySource` | `LoopbackDevice` (synthetic, no hardware) | **0 Msps** |
| `SoapySource` | loopback, minimal settings (device only) | **0 Msps** |
| **`ConstantSource`** | — (control, identical harness) | **573 Msps** |

Same graph shape, same `requestStop` path, same `CountingSink::count` read. The measurement is
sound; `SoapySource` is the variable. It is **not** the B210, not UHD, not USB, not the DSP chain,
and not my instrumentation.

### Hypothesis 1 — `activateStream` burst semantics: KILLED

Owner's hypothesis was that Soapy omits stream-setup a USRP needs, concretely a non-zero
`numElems` making UHD treat the stream as a finite burst while an RTL-SDR ignores it.
`SoapySource.hpp:182` calls `_rxStream.activate()` with **defaults — `flags=0, timeNs=0,
numElems=0`** (`SoapyRaiiWrapper.hpp:750`). `numElems=0` is continuous-until-deactivated. The
call is correct. Also independently ruled out by the loopback failing the same way.

### Hypothesis 2 — deferred activation never fires: PLAUSIBLE, NOT CONFIRMED

`SoapySource::start()` does not activate the stream directly. It registers a callback:

```cpp
reinitDevice();
if (!_device.get() || !_rxStream.get()) { return; }        // silent
DeviceRegistry::registerActivation(_devKwargs, [this]{ activate(); start ioReadLoop(); });
```

`registerActivation` (`SoapyRaiiWrapper.hpp:249-266`) fires only when `pendingUsers` reaches
zero. `findOrCreate` sets it to 1 on first creation (`:245`) and **increments** on every
subsequent acquisition (`:224`); `registerActivation` decrements **once** (`:259`). So any extra
device acquisition leaves the count above zero and the stream is never activated — with no error
on either path.

I predicted settings-driven `reinitDevice()` calls would cause the extra increment, and tested it
by constructing with **device only**. **Still zero samples, so that prediction is wrong.** The
mechanism remains plausible from the code but is unproven; the extra acquisition, if any, comes
from somewhere else.

### Decisive next step (5 minutes)

Instrument whether the activation callback runs at all — a single print inside the
`registerActivation` lambda, plus one on the `!_device.get()` early return. That splits three
ways: callback never fires (hypothesis 2, find the extra increment), callback fires but
`ioReadLoop` produces nothing (look at `readStream`), or `start()` returns early (look at
`reinitDevice`). No hardware needed; loopback reproduces it.

Worth noting how this went unseen: every Soapy file is excluded from all upstream CI,
`qa_SoapySource`'s only device case hardcodes an absent RTL-SDR, `qa_SoapyIntegration` self-skips
without a LimeSDR, and `qa_SoapyLoopback` exercises the raw device rather than the block. The
block-level path appears never to have been run to completion by anyone.

---

## ✅ Phase 5 — END-TO-END WORKS. B210 → DSP → sink, rate swept.

**RETRACTION FIRST.** The previous section reported that `SoapySource` yields zero samples and
raised the possibility that the block had never worked against hardware. **That was wrong, and the
fault was mine.** `kDurationSec` was 2.0 s while a B210 needs ~2.5 s of bring-up (device
detection, FPGA load, codec init, clock negotiation). `requestStop()` therefore fired *before the
reader thread was ever scheduled*. Instrumenting `ioReadLoop` showed it plainly:

```
2 s run:   ioReadLoop top: state=STOPPED  isActive=false   <- loop body never executes
12 s run:  ioReadLoop top: state=RUNNING  isActive=true
           read#0    ret=8192  totalRead=8192
           reserve#0 want=8192 got=8192 empty=false
```

**There is no gnuradio4 defect here.** `SoapySource` works correctly against a B210. All
instrumentation has been reverted; `SoapySource.hpp` is byte-identical to upstream.

Two corollaries worth keeping:
- The `LoopbackDevice` was never a valid control. It is a *loopback* — with no `SoapySink`
  writing into it there is nothing to read, so its zeros were correct behaviour, not a symptom.
  Reasoning "it fails on loopback too, therefore device-agnostic" was wrong.
- `SoapyRaiiWrapper.hpp:769-771` returns `SOAPY_SDR_TIMEOUT` when the device or stream is null,
  making a configuration failure indistinguishable from an ordinary timeout. Not our bug, and not
  what bit us, but it is a genuine diagnosability wart worth remembering.

### The result — B210, complex<float>, 8 multiply/divide stages, 8 s per point

Measured over the **streaming interval only**; device init is excluded, because including it was
exactly the setup-contamination error that invalidated the first scaling curve.

| requested | ×B210 max | achieved Msps | ratio | verdict |
|---|---|---|---|---|
| 1.00 | 0.02× | 1.00 | 0.999 | **KEEPS UP** |
| 4.00 | 0.07× | 4.00 | 1.000 | **KEEPS UP** |
| 8.00 | 0.13× | 8.00 | 1.000 | **KEEPS UP** |
| 16.00 | 0.26× | 15.93 | 0.996 | **KEEPS UP** |
| 32.00 | 0.52× | 24.76 | 0.774 | behind |
| 56.00 | 0.91× | 26.68 | 0.476 | behind, UHD printed `O` (overflow) |

**gnuradio4 on this machine sustains a B210 at 16 MS/s complex, lossless, through an 8-stage DSP
chain.** The path saturates at roughly **25–27 MS/s**, where UHD reports genuine overflows.

### Where the ceiling is not

16 MS/s complex is ~32 M floats/s. The synthetic steady-state measurement puts single-chain DSP
throughput at **168 Msps** — five times the load at which the radio path saturates. So the
25–27 MS/s ceiling is **not** the DSP layer, and not the scheduler plateau discussed earlier. It
is in the device/transport path: USB 3 bulk transfer, the UHD receive path, or `ioReadLoop`'s
fixed 8192-sample reads. That is where Phase 4 effort should go if higher rates are wanted.

Harness: `blocks/sdr/src/womm_b210_sweep.cpp`. Reproduce:
```
source scripts/env.sh
./build-fixed/blocks/sdr/src/womm_b210_sweep
```

---

## Test-suite stability — NOT deterministic. Blocks the stability criterion.

Full-suite results for **identical code** (`build-fixed`, post-cherry-pick, `SoapySource.hpp`
byte-identical to upstream):

| Run | Mode | Result | Failures |
|---|---|---|---|
| A | `-j8` | 101/102 | `qa_SoapySource` (SIGTRAP) |
| B | `-j8` | 100/102 | `qa_SoapySource` (SEGFAULT), `qa_SoapyIntegration` |
| C | **serial** | 100/102 | `qa_SoapySource` (SEGFAULT), `qa_BasicFileIo` (**Timeout**) |

### Two distinct instabilities

**1. Device contention under parallel ctest.** Run B's `qa_SoapyIntegration` failure was
*"2-channel TX→RX round-trip … channel 0: got 0, expected at least 4500"*
(`qa_SoapyIntegration.cpp:399`). It shares the synthetic loopback device with `qa_SoapyLoopback`,
and `DeviceRegistry::findOrCreate` returns the **same instance** for matching kwargs — so two
concurrently-running tests interfere. Re-running the Soapy tests **serially** gives 3/4 with only
the known environmental failure. **Device-touching tests are not parallel-safe.**

**2. `qa_BasicFileIo` has ~34× run-to-run variance.** Same binary, same machine:

| Run | Duration |
|---|---|
| baseline `-j8` | 48.03 s |
| fixed `-j8` | **8.84 s** |
| fixed serial | **Timeout (>300 s)** |

The serial run — which should be the *least* contended — was the worst. That is not contention;
it is non-determinism in the test or in the runtime beneath it. Not yet diagnosed.

`qa_SoapySource`'s crash mode also varies (SIGTRAP vs SEGFAULT) for what is the same underlying
environmental cause (absent RTL-SDR).

### Consequence for the acceptance criteria

The original brief requires **N ≥ 5 consecutive clean full-suite runs** for "stable". **We cannot
currently meet that**, and no amount of re-running fixes it — the variance is real. Prerequisites:

1. Serialise device-touching tests (a ctest RESOURCE_LOCK or a test fixture would do it
   upstream-shaped, no source patch).
2. Diagnose `qa_BasicFileIo`'s variance. Given the scheduler's spin-without-backoff behaviour and
   absent Darwin QoS, starvation of a file-I/O thread is a plausible mechanism and would connect
   this to the parallel-scaling question.
3. Only then attempt the 5-run stability gate.

Reported rather than worked around. No test has been disabled, retried, or excluded.

---

## ★ ROOT CAUSE — the macOS thread-pool polling loop burns ~13 cores doing nothing

This is the most consequential finding of the project. It was reached by chasing
`qa_BasicFileIo`'s run-to-run variance.

### The measurement

`qa_BasicFileIo` — a **file I/O test that never uses the thread pool**, run standalone:

| Metric | Value |
|---|---|
| wall | 5.46 s |
| user | 2.71 s |
| **system** | **71.54 s** |
| involuntary context switches | **2,974,438** (545 k/s) |
| voluntary context switches | **0** |

71 s of kernel time inside a 5.5 s run means roughly **13 cores permanently in the kernel**.

### The mechanism

`thread_pool.hpp:635-657`, macOS-only, added to work around a libc++ defect. Upstream's own
comment:

> *macOS + Homebrew libc++ workaround: `condition_variable::wait_for()` with per-thread mutexes
> triggers EINVAL (POSIX requires the same mutex for all concurrent waiters on the same condvar,
> and macOS enforces this unlike Linux). Use a short-sleep polling loop with 10 μs granularity
> instead.*

Every other platform blocks on a condition variable. macOS spins on `sleep_for(10 µs)`. The pool
eagerly creates `hardware_concurrency()` = **24** workers at singleton construction
(`thread_pool.hpp:805-823`), so **every gnuradio4 process on this machine** runs 24 threads each
issuing ~100,000 `nanosleep` syscalls per second — ~2.4 M/s — whether or not any work exists.

`sample` on a hung run: **1345 of 1349 samples** in `BasicThreadPool::worker → sleep_for →
nanosleep`, at **1411 % CPU**.

### Causal confirmation

Changing the one constant 10 µs → 2 ms (experiment only, since reverted):

| | 10 µs | 2 ms | change |
|---|---|---|---|
| user | 2.71 s | 1.30 s | −52 % |
| **system** | **71.54 s** | **1.85 s** | **−97.4 %** |
| involuntary ctx sw | 2,974,438 | 95,852 | −96.8 % |
| wall | 5.46 s | 9.30 s | **+70 % worse** |

**The polling loop causes 97.4 % of all system time.** But a longer sleep is *not* the fix — it
trades syscall storm for task-pickup latency and makes wall time substantially worse. The correct
fix is to restore blocking: give all waiters on the condvar a **shared** mutex, which is what
POSIX requires anyway and what the EINVAL is complaining about. That is an upstream-shaped change
to `BasicThreadPool`, not a constant tweak.

### What this explains

- **The owner's "system rather than user CPU" observation** — confirmed instrumentally, and it is
  far more extreme than it looked.
- **The ~3 M involuntary context switches** seen in the scaling benchmark.
- **Why `qa_BasicFileIo` hangs ~33 % of the time when run alone but passes under `-j8`**: idle
  pool workers get more CPU when nothing else competes, so the syscall storm is *worse* when the
  machine is otherwise idle. A test that fails when idle and passes under load.
- **Plausibly the original §0 anomaly.** A MacBook Air runs 8 polling threads; an M2 Ultra runs 24.
  Both waste proportionally, and useful work is throttled by the syscall storm either way — which
  is exactly the "something constant across both machines sets the ceiling" shape the brief
  described. **Not proven** — the anomaly was measured on GNU Radio 3.x, a different codebase —
  but it is the first mechanism found that has the right shape.

### Status

Experiment reverted; `thread_pool.hpp` is byte-identical to upstream. The real fix (shared-mutex
condvar) is **not** attempted here — it is a genuine design change and deserves its own session
with the thread-pool tests in front of it.

### Secondary, still open

The main thread in a hung run is stuck in
`poolWorker → BlockWrapper<BasicFileSink<double>>::work → workInternal` under a **singleThreaded**
scheduler. Whether that is an independent defect or a downstream consequence of CPU starvation
from the polling storm is not yet determined.

---

## ★★ THE FIX — shared-mutex condvar replaces the macOS polling loop

Implemented the fix identified above. **Net −4 lines of code.** Results across every measurement
this project has taken.

### The change

`core/include/gnuradio-4.0/thread/thread_pool.hpp`:

1. Added `std::mutex _conditionMutex` beside `_condition`.
2. `worker()` no longer creates a **function-local** `std::mutex` per thread. That was the actual
   POSIX violation: every concurrent waiter on one condition variable must use the **same** mutex.
   Linux tolerates the violation; macOS returns EINVAL — which is why the polling loop existed.
3. The wait now takes `_conditionMutex` in a narrow scope, so workers block instead of polling.
4. **The entire `#if defined(__APPLE__)` branch is deleted.** One code path for all platforms.
5. The task-submission path notifies while holding the shared mutex, closing a lost-wakeup window
   that could otherwise stall a worker for `keepAliveDuration` (10 s).

### Results

**CPU burn — `qa_BasicFileIo`, a file test that never uses the pool:**

| | upstream | fixed | factor |
|---|---|---|---|
| user | 2.71 s | 1.28 s | 2.1× less |
| **system** | **71.54 s** | **0.30 s** | **238× less** |
| involuntary ctx switches | 2,974,438 | 3,817 | **779× less** |
| **total CPU** | **74.25 s** | **1.58 s** | **47× less** |
| wall | 5.46 s | 6.48 s | 19 % worse |

**Test suite (serial):**

| | before | after |
|---|---|---|
| result | 100/102, `qa_BasicFileIo` **Timeout** | **101/102** |
| wall | 461.68 s | **180.56 s** (2.6× faster) |

The only remaining failure is the known environmental `qa_SoapySource` RTL-SDR case.
**The ~33 % intermittent `qa_BasicFileIo` hang is gone** — the stability blocker is cleared.

**Parallel-chain scaling (20 M samples/chain):**

| chains | before | after | change |
|---|---|---|---|
| 1 | 168.8 | 167.6 | — |
| 2 | 168.1 | 170.0 | — |
| 4 | 295.9 | 303.4 | +2.5 % |
| 8 | 319.5 | **439.7** | **+37.6 %** |
| 16 | 348.8 | 404.5 | +16.0 % |

Peak **+26 %**; best speedup **2.07× → 2.62×**; run-to-run spread **3.4× tighter**
(±0.0729 → ±0.0215 s at 16 chains).

**B210 end-to-end:**

| requested | before | after |
|---|---|---|
| 16 MS/s | 15.93 (0.996) | **15.98 (0.999)** |
| 32 | 24.76 | 25.90 |
| 56 | 26.68 | **32.50 (+21.8 %)** |

The radio ceiling rises from ~26.7 to ~32.5 MS/s.

### What is NOT fixed — stated plainly

**The scaling plateau survives.** 2.62× on a 16 P-core machine is still far from linear, and
16 chains is now genuinely *worse* than 8 (404.5 vs 439.7, and the spreads no longer overlap, so
this is real rather than noise). The polling storm was a **contributor, not the cause**.

Remaining suspects, untested: strided block→thread partitioning
(`Scheduler.hpp:1378-1385`), absent Darwin QoS (`thread_affinity.hpp`, 15 no-op sites), the macOS
mirror-`memcpy` (`CircularBuffer.hpp:352-378`), and oversubscription past 8 workers.

**Wall time on a single short test is 19 % worse** than the polling loop. That is inherent: a
condvar wakeup costs more than a 10 µs poll that is already spinning. Notifying under the mutex
did not recover it (6.50 s vs 6.48 s), so it is latency, not a lost wakeup. The trade — 47× less
CPU for 19 % more wall on one short test — is overwhelmingly favourable, and the full suite got
**2.6× faster**, so the effect does not generalise to real workloads.

---

## qa_SoapySource — skip guards added; one real defect remains

**Two device tests had no availability guard** and hard-failed on any machine without the dongle
(`rtlsdr` at `:322`, `lime` at `:355`). Both now skip cleanly, using the same idiom
`qa_SoapyIntegration.cpp:433` already uses. **The RTL-SDR choice is deliberately preserved** — it
is receive-only, so an unattended CI run cannot transmit; see `HANDOFF.md` RF safety.

**One genuine defect found, and deliberately NOT skipped.** `qa_SoapySource.cpp:172` runs against
a **present** B210:

```
SoapySDRUtil --probe="driver=uhd"  ->  Supports AGC: YES   (both RX channels)
qa_SoapySource "gain" test         ->  FAILED, false == true
    RX active gain: nan
```

The test does `setAutomaticGainControl(RX, 0, !autoGain)` then reads it back with
`isAutomaticGainControl()` and expects the new value. It does not round-trip. **SoapyUHD reports a
capability it does not implement** — the test's logic is correct and the driver's claim is not.
Note also the reported active gain is `nan`.

Skipping this would be green-washing: the device is present and the assertion is sound. It is
reported as a failure with diagnosis.

Net: `qa_SoapySource` goes from 2 failures plus 2 unconditional hardware dependencies, to
**1 failure that is a real, diagnosed driver defect**. The full-suite count is unchanged at
101/102, but the remaining failure is now an honest one rather than a missing dongle.

**Consequence for the stability gate:** still unmet, but for a defensible reason. Options are to
fix or work around the AGC round-trip in SoapyUHD (ours is a vendored, patchable snapshot), or to
accept it in writing as an upstream driver defect.

---

## CORRECTION — the "gain" failure is an out-of-range value, not an AGC defect

I previously wrote that `SoapyUHD` "reports a capability it does not implement", based on
`Supports AGC: YES` plus a failing assertion near `qa_SoapySource.cpp:172` and `RX active gain:
nan`. **That conclusion was wrong and is withdrawn.**

The owner identified the actual cause: on a B2xx the **RX2** antenna tops out around **76 dB**
while **RX/TX (RX1)** reaches ~88 dB. A gain roughly 2× out of range yields `nan` from the device.
Re-probing confirms it — RX reports `Full gain range: [0, 76, 1] dB`. The 89.75 dB figure quoted
earlier was from the probe's **TX** section, which I misattributed to RX.

So: a value out of range for the selected antenna, not a driver lying about AGC. The `Supports
AGC: YES` line is almost certainly accurate; it was never the failing thing.

**Consequences applied:**
- `womm_b210_sweep.cpp` defaults corrected: RX gain **30 → 20 dB** (well inside range for either
  antenna), centre frequency **100 MHz → 2401 MHz** (legal for amateur and WiFi use, and a safe
  default should anything ever transmit).
- The `qa_SoapySource` "gain" failure remains **open and undiagnosed at the source level** — the
  test is upstream's and its gain value has not been examined. It is *not* evidence of a SoapyUHD
  defect.

**Method note:** the mistake was reading one number from a long probe dump and not checking which
section it came from. Antenna-specific ranges differ on this hardware; quote the section, not just
the number.

---

## Phase 6 — THE DOUBLE-MAPPED RING. 2.43× throughput, and a data race only hardware caught

### 6.1 First: the instrument was invalid, and two recorded numbers with it

`womm_bm_scaling` sized the CPU pool to the chain count, so **every published point varied workload
and parallel decomposition simultaneously**, and the 1-chain point was a different regime entirely:
one job, topological order, zero cross-thread edges. Every speedup was normalised against that.

**"1→2 chains gains nothing" was an artefact of the axes, not a property of the runtime.** Rebuilt
with independent `--chains`/`--threads`, a steady-state window, and one cell per process. Retract
the 2.07×-from-16-chains curve entirely.

**Retraction:** the recorded **168.8 Msps** single-chain figure was *still* setup-diluted at 20 M
samples/chain. Windowed, that same cell is **291 Msps**. `time -l` was not enough; only a windowed
rate makes setup contamination impossible rather than merely unlikely.

### 6.2 The profile — where the time actually went

One worker of a 16-thread run, 5441 samples (`sample`, zero code edits):

| cost | samples | share |
|---|---|---|
| `invokeProcessOneSimd` — the actual DSP | 2303 | 42 % |
| `~OutputSpan()` → `_platform_memmove` | 1804 | **33 %** |
| `computeSampleLimits` (tag scan, port cache) | 558 | 10 % |
| `poolWorker` self time (`_nWorkersInWork` lives here) | 138 | 2.5 % |

The prior ranking had this third and scheduled it last. It was the whole thing. The two atomic
suspects — `_nWorkersInWork` and the graph-global `progress` counter — are **≤2.5 % and not
visible respectively**; both are amortised over large per-`work()` chunks, exactly as predicted by
the skeptical reading. Neither is worth pursuing until something else dominates.

### 6.3 Three defects, found in order

1. **The mmap gate tested `__NR_memfd_create`** — a Linux syscall number — so Darwin fell back to
   copying. `shm_open` + immediate `shm_unlink` is the equivalent; `MAP_FIXED` needed for the
   second mapping.
2. **The double-mapped path was dead code on every platform.** `Graph::connect` passes
   `std::pmr::get_default_resource()` — non-null — and `Port::resizeBuffer` treated any non-null
   pointer as an override, never consulting `DefaultAllocator()`. **Linux is paying this copy too.**
3. **That path published with no release fence** (it sat inside the `!_isMmapAllocated` branch).
   On ARM64 payload stores may become visible after the cursor update. Hidden by defect 2.

### 6.4 Results — windowed steady state, correct data

Final, all five defects fixed, quiet machine, 5 s windows:

| cell | before | after | gain |
|---|---|---|---|
| 1 chain, 1 thread | 291 | **476** | 1.64× |
| 16 chains, 1 thread | 231 | **350** | 1.52× |
| 16 chains, 8 threads | 789 | **1754** | 2.22× |
| 16 chains, 16 threads | 985 | **2416** | **2.45×** |

Peak **16× → 39.3× a B210**. 16-thread scaling **4.16× → 6.90×**: the copy is memory-bandwidth
bound, so cores contended for it and it flattened the curve rather than merely taxing each core.
Within 3 % of a mirror-elision probe, confirming the copy was the entire cost.

**Hardware, after all five fixes** — full sweep, one B210, RX-only, 2401 MHz, 20 dB:

| rate | verdict |
|---|---|
| 1, 4, 8, 16 MS/s | KEEPS UP, ratio 1.000 |
| 32 MS/s | KEEPS UP, 31.93 achieved |
| 56 MS/s | OVERFLOW |

Ceiling ~32 MS/s, matching the previously recorded 32.50 — **no regression and no improvement**,
which is the expected result: that ceiling is USB/UHD/`ioReadLoop`, not the DSP layer, and the DSP
layer now has ~75× the headroom the radio needs.

Note against the "is 32.5 the 2×2 figure?" question: at 32 MS/s × 4 B (sc16) this is only
~128 MB/s, far below USB 3's practical ~400 MB/s. **Bandwidth is not the limiter**, so the ceiling
is more consistent with per-read overhead on the fixed 8192-sample reads than with channel count.
Still open.

**Also still open:** the 56 MS/s point dies on an *uncaught* exception propagating out of the
scheduler. The watchdog rework covers a wedged graph, not this; a rate sweep should degrade to a
recorded FAILED row rather than terminating the process.

Serial `ctest` **101/102 on three consecutive runs** (152.6 / 152.4 / 142.3 s); the single failure
is the known pre-existing `qa_SoapySource` out-of-range gain.

### 6.5 ⚠ The regression, and why hardware found what 102 tests could not

Enabling the double-mapped path **broke the radio**. A B210 at 16 MS/s that previously kept up
cleanly began overflowing immediately.

| DSP depth | 16 MS/s |
|---|---|
| 0, 1, 2 | keeps up |
| 4, 8 | **OVERFLOW, uncaught, process dies** |

Throughput could not explain it — the synthetic control ran 238 Msps against a 16 MS/s need. The
depth dependence pointed at ordering rather than speed, which located defect 3 above. Moving the
fence out of the branch restored 16 MS/s at every depth.

**The full serial suite was green across three runs while this data race was present.** Only
streaming hardware caught it. This is the strongest evidence this project has produced for keeping
a radio in the loop, and it is the direct vindication of that instruction.

### 6.5b Then two more, each exposed by fixing the one before it

Fixing the fence exposed an **intermittent SIGBUS, ~20 % of 16-thread runs**. `do_allocate_internal`
mapped 2×, `munmap`ed the upper half, and re-mapped into the resulting **hole**. In that window the
hole is unowned, so any other thread's `mmap` — libmalloc's included — can take it, and `MAP_FIXED`
then silently clobbers the victim. Replaced with reserve-`PROT_NONE`-then-overlay: both halves land
in a range the process already owns, so no hole ever exists.

And `do_deallocate` unmapped `size` where `do_allocate` mapped `2 * size` — **the mirror half of
every buffer leaked**, which also fragments the address space the reservation above has to fit into,
making the race likelier the longer a process runs.

**Five defects total**, and the reason they coexisted is defect 2: with the allocator unreachable
from any graph, nothing downstream of it was ever executed. Dead code does not merely fail to help
— it silently accumulates bugs that the test suite reports as passing.

**Method note — mine to own.** The very first 16-thread run after enabling the path produced no
output line at all. I noted it, saw the next three runs pass, and moved on. That was the SIGBUS,
~4 runs from reproducing. An unexplained missing result is a finding, not noise; three green runs
do not retire it.

### 6.6 Retraction — "graph lifecycle leaks memory"

**Withdrawn. There is no leak.** Measured directly: a **single** 16-chain graph cycle peaks at
**6.92 GiB**. The recorded 8 GiB across *seven* cycles is therefore consistent with memory being
released each cycle — a leak would have shown ~42 GiB. The prior reasoning also had the evidence
available and unread: page reclaims were 1 710 937 at 1 M samples/chain and 1 764 674 at 20 M —
**flat**, i.e. a fixed allocation cost, not an accumulating one.

The real cause is static over-allocation, dominated by one line. `Port::resizeBuffer` passes the
**same element count** to the stream and tag buffers, and `Tag` is `alignas(kCacheLine)` = **128 B
on Apple ARM64**. A connected `float` port at 65536 therefore gets 256 KiB of stream and **16 MiB
of tags** — 64×, and 4× worse than the same graph on Linux/x86-64. `Tag` is not trivially copyable,
so tag buffers stay on the copying path and are untouched by the Category G fix.

**Open, tier 2:** decouple tag-buffer sizing from stream-buffer sizing. Nothing in this workload
needs 65536 tags in flight per port.

### 6.7 Upstream scan — all five defects are novel; one fix ran the other way

Done at the owner's suggestion: a high-impact finding is worth checking against the better-staffed
fork before assuming it is new. Compared ours, `fair-acc/main` (`92278b6`) and
`gnuradio/gnuradio4-core` (`c35f5c8`, the split-repo direction gnuradio.org is moving to).

| Defect | ours | fair-acc | gnuradio4-core |
|---|---|---|---|
| G-1 Linux-only mmap gate | fixed | unfixed | unfixed |
| G-2 dead code / resource override | fixed | **unfixed** | **unfixed** |
| G-3 missing release fence | fixed | **unfixed** | **unfixed** |
| G-4 mmap hole race | fixed | **unfixed** | **unfixed** |
| G-5 deallocate unmaps half | fixed | **unfixed** | **unfixed** |
| G-6 tag ring sized per-sample | **was unfixed** | **FIXED** | unfixed |

**All five present in both upstreams.** G-1 is expected (fair-acc dropped macOS ARM64). But G-3, G-4
and G-5 are **latent on Linux in both trees** — unreachable only because of G-2, and they would bite
the moment anyone fixed it. G-4 is silent corruption, not merely a crash.

**And the traffic ran the other way once.** fair-acc had already capped the tag ring, which we had
not: a tag ring holds only tags still *in flight*, never a slot per sample. Adopting
`std::min(min_size, kDefaultBufferSize)`:

| | before | after |
|---|---|---|
| peak RSS, 16-chain graph | 6.92 GiB | **2.27 GiB** (3.05×) |
| page reclaims | 423 210 | **139 367** |
| throughput | 2416 Msps | 2409 (unchanged, within noise) |

That closes the item §4.3's withdrawn "leak" turned out to actually be — **a 3× memory cut for one
line, found by reading upstream rather than by inventing a fix.** New invariant I-14.

---

## Phase 7 — two corrections from the owner's review

### 7.1 The B210 "32.5 MS/s ceiling" was an artefact of the sweep's rate list

The owner corrected the mechanism first: the B210's rate limit is a **local-oscillator / front-end
sync constraint, per radio** — the LO must hold ~1e9:1 frequency resolution in sync with digital
sampling, and syncing *two* front-ends simultaneously is what caps a B2xx near 32 MS/s against a
56 MS/s max clock. **It has nothing to do with USB bandwidth**, so the earlier "per-read overhead on
8192-sample reads" inference was wrong too.

That prompted the obvious check nobody had run. The old sweep used rates `{1, 4, 8, 16, 32, 56}`,
so **32 was simply the highest passing entry in a list that skipped everything between 32 and 56.**
The ceiling was never bracketed. Measured, `num_channels=1`, depth 8:

| requested | achieved | ratio | verdict |
|---|---|---|---|
| 32 MS/s | 31.93 | 0.998 | KEEPS UP |
| 40 MS/s | — | — | OVERFLOW (see below) |
| 44 MS/s | **43.28** | 0.984 | KEEPS UP |
| 50 MS/s | 48.57 | 0.971 | BEHIND |
| 56 MS/s | — | — | OVERFLOW |

**Single-front-end sustained rate is ~43 MS/s, not 32.5** — a third higher than recorded, and
consistent with the owner's LO explanation that 32 is the two-FE figure.

The 40 MS/s failure is **non-monotonic** and therefore not a capability limit. UHD negotiated the
exact requested master clock in both cases (40.0 and 44.0 MHz, decimation 1), so it is not an
MCR/decimation artefact either. With 44 at 0.984 and 50 at 0.971, everything from ~40 up sits in a
marginal band where accumulating 10 overflows inside an 8 s window is stochastic. Treat ~43 MS/s as
the sustained figure and anything above ~40 as marginal.

### 7.2 The tag-ring cap is a symptom fix — fair-acc redesigned Tag itself

The owner flagged the 64:1 tag-to-stream ratio as a "4-5 orders-of-magnitude WTF" worth checking
against the *intended* design rather than patching. That was right, and the check found more than
the cap.

The ratio is **three compounding factors**, of which the cap addresses one:

| factor | ours | fair-acc |
|---|---|---|
| ring length | one slot per sample (65536) | capped at 4096 — **adopted** |
| slot size | `alignas(kCacheLine)` → **128 B/tag** | no alignas, `index + ValueMapView` ≈ 32 B |
| copyability | `property_map` member → **not** trivially copyable → tag rings take the copying path (2× allocation + a mirror copy per publish) | `static_assert(is_trivially_copyable_v<Tag>)` → double-mapped, no copy |

fair-acc `a944ddc` — *"non-owning Tag + generic ChunkBuffer<T> tag buffer"* — fixes the **root**:
one trivially-copyable `Tag` whose variable-size payload lives in a separate chunk pool, explicitly
so "tags move by value through device/USM paths". No wire-format change. Combined that is ~128×
smaller than our original, which matches the owner's order-of-magnitude read far better than the
16× cap alone.

**Consequence for us:** our tag rings still pay a mirror copy on every tag publish, because our
`Tag` is still non-trivially-copyable. The cap is consistent with fair-acc's direction and not
contradicted by it, but it is not the design correction.

**Open decision (tier 2/3):** adopting the non-owning Tag is a large change — `Tag`, `ValueMap`,
`ChunkBuffer`, every block that reads tags, and the test suite — and it is LGPL-licensed, so it
lands in `DRIFT.md` Category E rather than being ours. Not taken unilaterally.

---

## Phase 8 — ⚠ TOTAL SDR INGEST CAPS AT ~40-44 MS/s, HOWEVER IT IS DIVIDED

The tier-1 headline, and it is not the DSP layer.

Harness: `blocks/sdr/src/womm_bmax.cpp` — N radios + M synthetic ballast chains in
**one graph and one scheduler**. Receive-only by construction; the build asserts on the linked
binary that no `SoapySink`/`writeStream`/`SOAPY_SDR_TX` symbol is present
(`blocks/sdr/src/assert_no_tx.cmake`), because a comment cannot make that guarantee.

| radios | requested each | aggregate achieved | verdict |
|---|---|---|---|
| 1 | 43 MS/s | **42.18** | PASS |
| 2 | 43 MS/s | 38.71 | FAIL |
| 3 | 43 MS/s | 43.83 | FAIL |
| 3 | 16 MS/s | 41.54 | FAIL |
| 3 | 12 MS/s | **35.85** | **PASS** |

**The cap is on the aggregate, not per radio.** Three radios at 12 MS/s (36 total) sustain
cleanly; three at 16 (48 total) do not. One radio alone reaches 42. Total ingest saturates around
**40-44 MS/s no matter how it is divided** — against **2400 Msps** of DSP capacity measured in
Phase 6, i.e. the radios can feed roughly **1/55th** of what this machine can process.

**Ruled out:** USB bandwidth. The three B210s are on **separate XHCI controllers**
(`AppleT8112USBXHCI@01/02/03`), and 44 MS/s x 4 B is ~176 MB/s against USB 3's practical ~400 MB/s
*per controller*.

**Not yet localised.** Candidates, in the order worth testing:
1. A serialising lock in UHD or SoapyUHD — most likely, and cheap to test with `sample` during a
   3-radio run.
2. `DeviceRegistry` (`SoapyRaiiWrapper.hpp`) — distinct serials give distinct devices, but a shared
   mutex on the registry path would still serialise.
3. IO-pool behaviour. `ioReadLoop` is dispatched to `defaultIoPool()`
   (`SoapySource.hpp:189`); the pool grows on demand to a very large `ioMax`, so this is unlikely,
   but the thread count during a 3-radio run has not actually been counted.
4. Per-read overhead: fixed 8192-sample reads (`max_chunk_size`) at ~5.4 k reads/s aggregate.

### 8.1 What this measurement does NOT establish

**Host capacity, not synchronisation.** Each B210 free-runs on its own clock, so three streams
whose counts advance during the same *host* wall-clock window are three independent captures that
merely overlap in our frame — not a coherent acquisition. Certainty needs every device on a common
or GPS time source and a check that they report the same sample index at the same instant.

The hardware here **can** do that: the B210s are on an **Octoclock-G** (10 MHz reference + PPS,
GPS-disciplined), and the device reports `Clock sources: internal, external, gpsdo` /
`Time sources: none, internal, external, gpsdo`. gr4 already exposes both as settings
(`clock_source`, `time_source`, `reference_clock_rate` — `SoapySource.hpp:48,60,61`). The harness
does **not** currently set them, so every figure above is free-running. Setting both to `external`
is a two-setting change and turns the synchronisation question from a disclaimer into a test.

### 8.2 And the overflow count is an indicator, not a measurement

An overflow says samples were dropped; it does not say **by what**. USB, host scheduling, the
device, the driver and our own ballast all produce the same indication. Therefore `B_max` is only
meaningful as a **repeated, relative** comparison under otherwise-identical conditions — "this
change moved B_max from 6 to 9 across five runs" is a result, "the scheduler supports 7 chains" is
not. A single trial proves nothing. This is also why `SoapySource` no longer stops on overflow: if
the cause cannot be attributed, acting decisively on it is worse than recording it and continuing.

### 8.3 NEGATIVE RESULT — the scheduler's per-iteration global mutex is not the ingest cap

Profiling a 3-radio run (`sample`, 6 s) found the contended-mutex path
(`_pthread_mutex_firstfit_lock_slow` 96 samples, `psynch_mutexwait` 87) sitting **entirely in
gr4's own scheduler** — `poolWorker` 45, `adoptBlocks` 23, `cleanupZombieBlocks` 22,
`processScheduledMessages` 27 — and **nothing in the radio, SoapyUHD or USB path at all.**

The mechanism is real: `poolWorker` calls `cleanupZombieBlocks()` and `adoptBlocks()` on **every
iteration** (`Scheduler.hpp:748-750`), and each takes a **global** mutex to inspect a container
that is empty in steady state. N workers therefore serialise on one lock. It does not show in the
synthetic benchmark because each pass there moves a large chunk, amortising the lock; a
rate-limited radio makes workers pass constantly while doing little, so acquisitions per unit of
work explode.

Fixed with a lock-free fast path: two atomics (`_zombiesPending`, `_adoptionPending`), each written
**only while holding the mutex that guards its container**, so an adder cannot lose a concurrent
clear and a stale read costs at most one extra iteration of latency.

**It did not lift the cap.** 3 radios at 43 MS/s: 43.83 → 38.48 MS/s aggregate, i.e. unchanged
within noise. Synthetic: 2416 → 2428 Msps at 16 threads, 1754 → 1806 at 8 — marginally positive
but **not above the noise floor**, so it is not claimed as a win.

**Conclusion: the mutex contention was a symptom, not the cause** — workers spinning with nothing
to do were *hitting* the lock, rather than the lock gating ingest. The ~40-44 MS/s aggregate cap
remains unlocalised, and the profile has now **excluded gr4's scheduler as its source**, which is
the useful part of a negative result.

Still to test, in order: a serialising lock inside UHD/SoapyUHD (the profile above covers gr4's own
frames; a lock held *inside* the UHD library would appear differently); `DeviceRegistry`; the
IO-pool thread count during a 3-radio run; per-read overhead at the fixed 8192-sample
`max_chunk_size`.

### 8.4 Timed start — the machinery exists, one setting is missing

Standard multi-radio practice is to arm every radio with a start time of about -1.0 s and let them
all latch the next 1-second boundary, after which they stay in lock. That needs three things, and
we have two:

- `clock_source`/`time_source` — **exposed** (`SoapySource.hpp:48,60`), currently unset.
- `activate(int flags, long long timeNs, std::size_t numElems)` — **already in the wrapper**
  (`SoapyRaiiWrapper.hpp:750`), and `setHardwareTime(long long)` at `:486`.
- A `start_time` setting on the block — **missing**. `SoapySource::start()` calls `activate()`
  **bare** (`SoapySource.hpp:182`), so flags/timeNs/numElems are all 0 and no timed start is
  expressible today.
- `setHardwareTime(long long timeNs, const std::string& event)` — **in the wrapper**
  (`SoapyRaiiWrapper.hpp:486`) and **never called**. The `event` argument is the important part:
  `setHardwareTime(0, "PPS")` maps onto UHD's `set_time_next_pps()`, which is what actually
  disciplines several radios to a common epoch. `getHardwareTime` (`:484`) is likewise unused.

**`stream_args='sync=pps'` does not work here — checked.** The device advertises its stream args
explicitly and the list is `spp`, `WIRE`, `peak`, `recv_frame_size`, `num_recv_frames`, `fullscale`
— **no `sync` key**. The only PPS string in `libuhdSupport.so` is `UNKNOWN_PPS`, which is a
*time_source* value, not a stream argument. Since SoapySDR silently ignores unrecognised args
rather than rejecting them, passing `sync=pps` would be a **no-op that looks configured** — the
worst failure mode available. The equivalent capability is real but lives on `setHardwareTime`'s
`event` parameter, not in stream args.

**So the whole sync path is plumbing, not capability.** All three primitives already exist; the
block wires none of them together.

`time_ns` is **signed 64-bit integer nanoseconds** end to end, with no narrowing in our path, so the
negative-offset convention maps directly (-1.0 s → -1'000'000'000).

**But integer nanoseconds is a systems-programmer's timestamp, not a radio one**, and it caps what
this stack can ever express. UHD's own `time_spec_t` is integer full seconds **plus a `double`
fractional second**, precisely because 1 ns is not enough resolution for radio work; SoapySDR
quantises that to 1 ns and discards the rest. Resolution matters in metres, not clock ticks:

| use | 1 ns granularity |
|---|---|
| timed start on a PPS boundary | **fine** — you are naming a second boundary; the 10 MHz reference and PPS do the disciplining in hardware |
| sample alignment | **fine** — one sample at 61.44 MS/s is 16.3 ns, so 1 ns is sub-sample |
| phase coherence (MIMO, direction finding, beamforming) | **useless** — 1 ns ≈ 0.3 m ≈ **2.4 wavelengths at 2.4 GHz** |

So the Soapy path is adequate for the synchronisation work queued here and is a hard ceiling on any
future phase-coherent work, which would have to bypass Soapy for UHD directly or work in
sample-clock ticks. Worth knowing before anyone designs against it.

**And the host clock is not a nanosecond clock either.** Measured on this machine: `hw.tbfrequency`
is **24 MHz → 41.7 ns per tick**, and `steady_clock` shows a smallest non-zero step of **41 ns**
with a 42 ns median back-to-back read over 95 221 samples. The type says nanoseconds; the hardware
delivers 42× coarser, and other arm64/amd64 platforms with a 3 MHz architected timer are coarser
still (~333 ns). In propagation terms **41.7 ns ≈ 12.5 m**, about 100 wavelengths at 2.4 GHz.

Two consequences:

- **Never align radios using host time.** It is two orders of magnitude worse than the API
  quantisation, which is itself useless for phase work. Alignment belongs to the 10 MHz reference
  and PPS; host time should only ever *name* a second boundary.
- `SoapySource`'s free-running mode emits "best-effort wall-clock timestamps on every chunk"
  from `detail::wallClockNs()` (`system_clock::now()`). Those carry ~42 ns granularity at best and,
  being `system_clock` rather than `steady_clock`, also step with NTP. Best-effort is the correct
  description; they must not be read as sample-accurate.

**Sequencing constraint:** the radios must be powered and booted, and their reference lock
established, *before* parameters are applied — and that should be verified by reading the
`ref_locked`/`gps_locked` sensors rather than by sleeping.

### 8.5 Settings ruled out; the separate-process test needs a barrier

**Settings are not the cap.** The owner supplied a parameter list; diffing it against the harness
found three genuine gaps — `rx_bandwidths` left at its **500 kHz default** against a 43 MS/s sample
rate (an analog filter ~86x too narrow), and `emit_timing_tags`/`emit_meta_info` both defaulting to
**true**, so the read loop builds a `property_map` per timing tag. All three are worth setting
correctly regardless. None of them moves the ceiling: 3 radios at 43 MS/s went 38.36 → 40.32 MS/s
aggregate, inside the 38-44 run-to-run band already observed. Now selectable via `WOMM_TUNED=1`.

**`_clockEosReceived` is not a timed start.** Checked, because the guess was reasonable: it is set
when `clk_in` receives an **END_OF_STREAM** tag and its only effect is `requestStop()`
(`SoapySource.hpp:420-422`) — shutdown propagation from the timing input, the opposite end of the
lifecycle. The timed-start equivalent remains `activate(flags | SOAPY_SDR_HAS_TIME, timeNs, ...)`,
which the block never uses.

**⚠ The separate-process test is confounded and its numbers must not be quoted.** Running one radio
per process, staggered:

| configuration | result |
|---|---|
| 3 processes x 43 MS/s | 9.25 + 23.79, third failed device creation |
| 2 processes x 20 MS/s | 4.18 + 10.23 |
| 2 processes x 12 MS/s | 4.35 + 7.30 |

Compare 3 radios at 12 MS/s **in one process**, which passes cleanly at 35.85 MS/s aggregate. The
separate-process figures are far worse, and the reason is the harness, not the driver: with
staggered starts one process's B210 bring-up — FPGA load and USB enumeration, ~2.5 s of heavy work
— lands **inside** another process's measurement window. A process reading 4.18 MS/s that reads 42
when alone is measuring interference from a neighbour's start-up.

Two design requirements before this test means anything:
1. **A start barrier.** Every process must reach "streaming" and only then begin measuring, so no
   measurement window contains any other process's bring-up.
2. **Simultaneous enumeration fails.** Launching three processes at once produced
   `device creation failed for [(driver, uhd), (serial, ...)]` — UHD discovery does not tolerate
   concurrent enumeration, so device acquisition must be serialised even once measurement is not.

Until both hold, **whether the ~40-44 MS/s cap is per-process or global remains open**, and with it
whether the cause is a lock inside UHD/SoapyUHD or something in the driver or host.

### 8.6 ★ THE INGEST CAP IS GLOBAL, NOT PER-PROCESS

The decisive test, done properly this time. Three requirements had to be met before the numbers
meant anything, and each one was learned by getting it wrong first:

1. **A cross-process start barrier.** Staggered launches are unavoidable (see 2), but they put one
   process's ~2.5 s B210 bring-up inside another's measurement window. Every process now announces
   readiness and waits for all peers before measuring (`WOMM_BARRIER_DIR`, `WOMM_BARRIER_N`).
2. **Serialised device acquisition, addressed by SERIAL and not by index.** UHD enumeration only
   reports devices **not already claimed by another process**, so indices shift underfoot — a third
   process asking for index 2 was told "only 1 device found". Enumerate once before anything is
   claimed, then pass serials in (`WOMM_SERIAL`).
3. **Equal thread budgets.** `womm_bmax` did not size its CPU pool, so each process took the
   default `hardware_concurrency()` = 24. Three processes = **72 spinning workers on 24 cores**,
   and multi-threaded workers never back off. That measured thread thrash, not the driver, and
   produced a spectacular 3.54 MS/s aggregate. With `WOMM_THREADS=8` (24 total) it is a fair test.

| configuration | aggregate |
|---|---|
| 1 radio, 1 process | 42.18 MS/s |
| 3 radios, **one** process | 43.83 MS/s |
| 3 radios, **three** processes, barrier-synchronised, 8 threads each | **40.89 MS/s** |

**Separate processes do not raise the ceiling.** Each has its own UHD/SoapyUHD library instance and
therefore its own library-level locks, so this **excludes a per-process lock** — in gr4, in
SoapySDR, and in UHD — as the cause. Combined with the earlier exclusions (USB bandwidth: three
separate XHCI controllers; gr4's scheduler: fixing its global per-iteration mutex changed nothing;
block settings: worth 2 MS/s inside a 6 MS/s noise band), the bottleneck sits **below the process
boundary**.

Remaining candidates, now a much shorter list: the macOS kernel USB stack / `IOUSBHostDevice`
layer, libusb's transfer handling, or a system-wide limit on aggregate isochronous/bulk throughput.
None of these is a gr4 defect, which is itself the useful conclusion — **no amount of gr4 tuning
will move this number.**

**Side finding, and it is a real one for deployment:** gr4's default CPU pool is
`hardware_concurrency()` per *process*. Any multi-process gr4 deployment therefore oversubscribes
the machine by the process count, with workers that spin rather than back off. Three processes cost
12x throughput here. A multi-process design must size pools explicitly.

---

## Phase 9 — ★ TWO-CHANNEL RECEIVE HAS NEVER WORKED. Every prior figure is single-channel.

Run 2026-07-27, at the owner's suggestion: a harness that counts *aggregate* samples cannot tell
"two channels running" from "one running and one silently dead" — both read as half rate, which is
indistinguishable from a performance problem. He proposed a hold-open console run so the front
panel LEDs could be read directly, the LED being out-of-band evidence no defect in our code can
fake. That was the best-value suggestion in the project so far.

Harness: `blocks/sdr/src/womm_rx_hold.cpp` — one radio, `num_channels = 2`, depth 0, per-channel
counters printed live, streams until Enter. RX-only by construction (`assert_no_tx.cmake` gate on
the linked binary).

### 9.1 The gap was total, not partial

`womm_bmax.cpp:116` and `womm_b210_sweep.cpp:86` both set `num_channels = 1`. So:

| | channels | per channel | aggregate |
|---|---|---|---|
| every figure recorded before today | **1 per radio** | ~14.6 MS/s (3-radio case) | ~44 MS/s |
| the owner's prior working system (MCM) | **2 per radio** | 20 MS/s | 120 MS/s |

**The headline MCM comparison was never like-for-like.** `SoapySource` *supports* two channels —
per-channel loops at `:569-704`, `channelIndices` at `:533`, a separate `readStreamIntoBufferList`
path at `:305` — but that path had never been executed on hardware.

### 9.2 Two defects, the second unreachable until the first was fixed

See `DRIFT.md` Category H for the full write-up.

- **H-1** `SoapySource::start()` called `activate()` bare, i.e. "stream now". UHD **refuses** that
  on a multi-channel streamer: *"Invalid recv stream command - stream now on multiple channels in a
  single streamer will fail to time align."* Reproduced **outside gr4** with
  `SoapySDRUtil --rate=15.36e6 --channels="0,1" --direction=RX`, so it is a UHD contract — but gr4
  had no way to satisfy it. §8.4 had filed the missing timed start under *synchronisation*; it is
  actually a prerequisite for two-channel receive existing at all.
- **H-2** `SoapyRaiiWrapper.hpp:487` passed `nullptr` to `setHardwareTime` for an empty event. The
  C shim constructs that into a `std::string` → `strlen(nullptr)` → **SIGSEGV**. Found on hardware
  via the crash report, not by any test. Both `setHardwareTime` and `getHardwareTime` were entirely
  uncalled before today (§8.4), which is how a crash on a default argument survived.

### 9.3 Result — both channels live

One B210 (31FE7A2), 2 channels, depth 0, MCR 30.72 MHz, centre 2401 MHz, gain 20 dB, RX2:

| | achieved | verdict |
|---|---|---|
| ch0 | 15.36 MS/s | LIVE |
| ch1 | 15.36 MS/s | LIVE |

Sample counts **identical** between channels for the whole run — the timed start aligned them.
Single-channel regression check re-run afterwards: `womm_bmax` 1 radio at 20 MS/s → 19.81 MS/s,
PASS. The bare-activate path is unchanged when `num_channels == 1`.

### 9.4 ⚠ The B2xx two-channel ceiling, and an open contradiction with the MCM

UHD refuses `master_clock_rate > 30.72 MHz` with two RX channels active, and per-channel rate is
MCR/decimation. Measured consequences:

| request | outcome |
|---|---|
| 2 ch @ 20 MS/s, MCR auto (20 MHz) | `activate()` STREAM_ERROR |
| 2 ch @ 20 MS/s, MCR pinned 40 MHz | rejected: *"exceeds maximum possible master clock rate (30.72 MHz) when using 2 RX channels"* |
| 2 ch @ 20 MS/s, MCR pinned 30.72 MHz | silently delivered **15.36** MS/s |
| 2 ch @ 15.36 MS/s, MCR 30.72 MHz | **PASS, both live** |

So a B210 sustains **30.72 MS/s aggregate across both channels**. That *is* the "~32 MS/s with two
front ends" figure in `HANDOFF.md` — it is an aggregate per radio, and it is correct.

**This contradicts the MCM.** 3 radios x 2 channels x 20 MS/s requires **40 MS/s aggregate per
radio**, above what UHD 4.10 permits here. Three B210s cap at **92.16 MS/s**, not 120. Unresolved,
and put to the owner rather than guessed at: the candidates are that the MCM's "20 MHz" was
bandwidth rather than sample rate, that it ran 10 MS/s per channel, or that an older UHD imposed a
different limit.

**This narrows the gr4 gap rather than widening it** — the target to beat is 92.16 MS/s across
three radios, not 120.

### 9.5 What this does NOT yet establish

Not run: three radios, three processes (the MCM topology), core utilisation during a two-channel
run, or the depth sweep. E0.1/E0.2 are unchanged and still outstanding.

### 9.6 ★★ THE ~40-44 MS/s "INGEST CAP" IS REFUTED. Three radios reach the hardware ceiling.

Owner confirmed the LEDs by eye on all three units — **A and B frontends, both on RX2** — the first
time two-channel receive has been seen in this project. With that established, three radios were run
as **three separate processes** (the MCM topology), six channels, depth 0, `WOMM_THREADS=8` each.

| | per channel | per radio | aggregate |
|---|---|---|---|
| 31FE7A2 ch0 / ch1 | 15.361 / 15.361 MS/s | 30.72 | |
| 32FCCF7 ch0 / ch1 | 15.360 / 15.360 MS/s | 30.72 | |
| 32FCD05 ch0 / ch1 | 15.359 / 15.359 MS/s | 30.72 | |
| **total** | | | **92.16 MS/s** |

**Ratio to the hardware maximum: 1.0000. Zero overflows, zero errors, 43 s sustained.**

Against `RESULTS.md` §8's headline of "total ingest saturates at ~40-44 MS/s however it is divided",
this is **2.1x higher and at the physical limit of the hardware.** §8's cap was not a property of
USB, the driver, the host or gr4. **§8.6 is withdrawn in full, and §8's "the tier-1 bottleneck is
SDR ingest" is withdrawn with it.**

CPU during the run, sampled six times at 3 s intervals: **~42 % user, ~15 % sys, ~43 % idle** —
about 13.7 of 24 cores. So the machine sustains its radios' full output with ~43 % idle, and the
owner's "¼-⅓ of 16 cores" observation is consistent with a *lightly loaded* system rather than a
starved one.

**What this does NOT attribute.** The new configuration differs from §8's in three ways at once —
two channels instead of one, depth 0 instead of 8, three processes instead of one graph. It refutes
the cap; it does not say which of the three explained the old number. That is E0.1's job (depth
sweep at fixed channel count and topology) and it is still outstanding.

**Standing correction:** `HANDOFF.md` §1b ("★ THE TIER-1 BOTTLENECK IS NOW SDR INGEST, NOT DSP")
is refuted by this measurement and needs to be retracted there.

### 9.7 External reference — one radio does not lock, and one instrument does not work

Owner directed setting the radios to the external Octoclock reference and PPS, and to `TX/RX`
antennas where the antennas are physically connected.

**Corrections to my own work, both caught within minutes of each other:**

- I added a `ref_locked` check that **sampled the sensor once**, immediately after
  `setClockSource()`. The reference PLL needs time to acquire, so it read `false` on a perfectly
  good reference and I told the owner to go check his cabling. Wrong. Ettus document a
  loop-until-locked pattern precisely because **no device argument demands lock on start**; there
  is nothing to set. Now polls to a 3 s deadline, and two of three radios lock immediately.
- The antenna allow-list read `"RX/TX"`. The device spells it **`TX/RX`** (`Antennas: TX/RX, RX2`),
  so our own guard would have refused the correct name. `HANDOFF.md` carries the same reversal.

**Real finding: `32FCCF7` does not lock.** `31FE7A2` and `32FCD05` both report `ref_locked=true`
and stream at full rate on the external reference; `32FCCF7` still reads `false` after 3000 ms and
refuses to run. Isolated to one unit, with the other two as controls on the same code path. That is
a hardware/cabling item, not a software one.

**⚠ Retracted instrument: cross-process count-difference drift.** I compared cumulative sample
counts between two radios over time, reasoning that locked radios hold a constant difference and
free-running ones drift. Run against both a locked and a free-running pair, it reports "wanders, no
offset" for **both**, and assigns the locked pair a *worse* apparent offset (244 ppm) than the
free-running one (26 ppm). It cannot distinguish them.

The reason is sampling, not clocks: the counts come from log lines written by two independent
processes at uncoordinated instants, and that skew is ~300 000 samples (~20 ms). A real 1 ppm
offset over 60 s is ~900 samples — **the noise floor is 300x the effect.** No conclusion about lock
may be drawn from it, in either direction.

**What actually establishes lock is the device's own `ref_locked` sensor**, which is direct and
which the two working radios report true. The correct instrument for *epoch* alignment is the
device timestamp carried in the timing tags (`emit_timing_tags`, one per second), not host-side
counters — host-side counters never can be, whatever the sampling discipline.

**Still not established:** that the radios start on the same PPS edge. Three processes launched 3 s
apart each call `set_time_unknown_pps()` against a different edge, so they share a rate but not
necessarily an epoch. Needs either a cross-process arming barrier or a common absolute epoch.

### 9.8 ★ THE DEVICE TIMESTAMP WAS BEING DISCARDED — now captured, and it works

Chasing the owner's instruction to use the tags rather than host-derived rates, and following §9.7's
retraction of the host-side drift instrument.

**The defect.** `SoapySDR` fills a hardware timestamp into `time_ns` on **every** read. In
`SoapySource::ioReadLoop` that variable is declared, passed by reference into `readStream`
(`:278`) and `readStreamIntoBufferList` (`:348`), and **never read again** — four occurrences, all
at the call sites. `handleStreamFlags(flags)` is called but the only `SOAPY_SDR_HAS_TIME` reference
in the entire SDR tree is inside `getSoapyFlagNames()`, **a debug string formatter**. Meanwhile
`emitTimingTag` stamps `TRIGGER_TIME` from `detail::wallClockNs()` — the **host** clock.

So the radio hands gr4 its own clock on every read and gr4 throws it away, then labels the samples
with host time. Same shape as Category G and H: the capability is fully plumbed and simply unwired.

**Why it matters.** §9.7 retracted a drift instrument because host-side sampling skew (~300 000
samples) swamped the effect (~900 samples). *Any* host-derived timestamp has that problem — the tags
as previously emitted could not have verified inter-radio alignment either, because they carried the
same host clock. The device timestamp is the only quantity that can.

**The fix.** Capture `time_ns` when `flags & SOAPY_SDR_HAS_TIME`, hold it in an atomic, and publish
it in the timing tag's meta info as `device_time_ns` (plus `device_time_source`). Carried
**alongside** `TRIGGER_TIME`, not replacing it: `TRIGGER_TIME` is UTC wall time by contract, whereas
a PPS-zeroed device clock counts from an arbitrary epoch. Consumers comparing radios want the new
key; consumers wanting UTC keep the old one.

**Verified on hardware** — B210 `31FE7A2`, free-running, one radio, `HAS_TIME` present on reads:

```
device_time 1.145869172 s
device_time 2.151202505 s   (+1.005333333)
device_time 3.156002504 s   (+1.004799999)
device_time 4.157602503 s   (+1.001599999)
device_time 5.162402502 s   (+1.004799999)
```

Increments are chunk-quantised (8192 samples / 15.36 MS/s = 533.33 us) and exceed the nominal 1 s
poll because the host interval genuinely is slightly over a second. **The device clock is the
accurate one** — which is the whole point.

**What this unlocks.** With `time_source=external` and `set_time_unknown_pps()`, every radio's clock
is zeroed on a common PPS edge; comparing `device_time_ns` across radios at the same sample index
then proves epoch alignment directly, immune to host scheduling. That is the instrument §9.7 said
was needed. Still to build: a tag-capturing sink so the comparison runs off the published tags
rather than off the block member.

### 9.9 ★ EPOCH ALIGNMENT CONFIRMED — two locked radios share a PPS edge

First use of the device-timestamp instrument from §9.8, and the first inter-radio timing result this
project has been able to make at all.

`31FE7A2` and `32FCD05`, both locked to the Octoclock (`ref_locked=true`), `time_source=external`,
`set_time_unknown_pps()`, launched **3 s apart** in separate processes:

| | |
|---|---|
| samples compared | 31 |
| mean offset | **-3.4 ms** |
| min / max | -10.1 ms / +6.4 ms |
| spread | 16.5 ms |

**Verdict: same PPS edge — a shared epoch.** Different edges would place the offset at ~1.000 s or
an integer multiple of it. The measured offset is **three orders of magnitude smaller**.

**Why this instrument is sound where §9.7's was not.** The retracted drift test tried to resolve an
effect ~900 samples against ~300 000 samples of host sampling skew — noise 300x the signal. Here the
two hypotheses are separated by 1000x (10 ms versus 1000 ms), so the same read skew cannot reach
across the gap. The residual +/-10 ms **is** that read skew: two processes sampled from different log
lines, with device time chunk-quantised at 533 us. It is the instrument's resolution, not a clock
offset. **The lesson is to choose measurements whose hypotheses differ by more than the noise, not
to sample more carefully.**

The owner predicted this ("I think that's the default behaviour, to launch in sync") and was right:
`set_time_unknown_pps()` waits for a PPS *transition* before arming the following edge, so radios
configured at different moments still latch the same one.

**Caveat, stated because one run is one run:** this holds for a 3 s stagger with B210 bring-up
(~2.5 s) in the path. It is not proof that arbitrary launch timing always lands on one edge — a
cross-process arming barrier would guarantee what was here observed. Worth having before anything
depends on alignment.

**Still open:** `32FCCF7` remains unlocked; the owner found it was not connected to the clock. Its
reconnection is not yet reflected in a run. Antennas are currently disconnected, which defers the
content/sine-wave check but affects nothing measured here — sample counting and device timestamps do
not depend on what is on the connector.

### 9.10 ★★★ FOUR RADIOS, EIGHT CHANNELS, 122.88 MS/s, ONE EPOCH

The strongest result this project has produced. A fourth B210 (`32C7510`, previously not
enumerating) was brought online and `32FCCF7`'s missing clock connection restored — the owner found
it genuinely disconnected, so §9.7's `ref_locked=false` was a true reading of a real fault.

Four radios, four **separate processes**, `WOMM_THREADS=6` each, `clock_source`/`time_source` =
`external`, `set_time_unknown_pps()`, launched 3 s apart:

| radio | ch0 | ch1 |
|---|---|---|
| 31FE7A2 | 15.3605 | 15.3605 MS/s |
| 32C7510 | 15.3615 | 15.3615 MS/s |
| 32FCCF7 | 15.3595 | 15.3595 MS/s |
| 32FCD05 | 15.3605 | 15.3605 MS/s |

**Aggregate 122.88 MS/s across 8 channels — ratio 1.0000 to the hardware maximum.**
All four `ref_locked=true`. CPU ~53 % user, ~18 % sys, **~29 % idle**.

**Epoch, by `scripts/epoch-check.sh`:**

| radio | n | mean offset | verdict |
|---|---|---|---|
| 32C7510 | 37 | +28.9 ms | SAME EDGE |
| 32FCCF7 | 46 | +3.1 ms | SAME EDGE |
| 32FCD05 | 46 | +39.8 ms | SAME EDGE |

**Worst offset 39.8 ms — 25x below the discriminator, and ~1/25th of the one full second that a
different PPS edge would produce. All four share an epoch.** The offsets are larger than §9.9's
two-radio run (3.4 ms) because more processes means more log-sampling skew; the quantity being
bounded is unchanged.

### Against the MCM, which is the comparison that matters

| | MCM (GR 3.x, ~2 yr ago) | this |
|---|---|---|
| aggregate | 120 MS/s complex | **122.88 MS/s** |
| radios x channels | 3 x 2 | **4 x 2** |
| per-channel liveness | not verified | **verified, LEDs + counters** |
| common epoch | not demonstrated | **demonstrated, 39.8 ms worst** |
| machine headroom | unrecorded | **~29 % idle** |

**gr4 on this machine now exceeds the prior working system, on the same hardware, with two
properties the MCM never established.** Note the owner has since corrected the MCM's own figure: its
"20 MS/s per channel" came from a misconfigured value in a repo, and 15.36 is the dual-channel
Nyquist limit — so the MCM was very likely running the same 15.36 and its true aggregate was lower.

### What this does not claim

- **Content is unverified.** Antennas were disconnected for this run. Counting and device
  timestamps are indifferent to what is on the connector, so nothing above is affected — but no
  claim is made that the samples contain signal. That is the sine-wave capture, still to do, and it
  needs the owner to key the B200 (never automated: RX-only by construction).
- **Not a soak.** ~45 s per run.
- **Shared epoch is observed, not guaranteed.** `set_time_unknown_pps()` landed all four on one edge
  at this launch timing; a cross-process arming barrier would make it structural.

### 9.11 ★★★ CONTENT VERIFIED — the comb is on all eight channels, and four radios agree to 1.72 ppb

Closes the disclaimer §9.10 carried: that the samples were proven to *flow*, at rate and on a shared
epoch, but never proven to *contain* anything.

Owner transmitted a complex sum spaced 40 kHz about f_C = 2401 MHz, rolling off toward zero by
f_C +/- 250 kHz — ~13 lines across 500 kHz, which 0.512 MS/s (+/-256 kHz) just covers. Four radios,
1 s capture each, 33 MB total. **TX was the owner's action throughout; nothing here automates it.**

| radio | ch0 | ch1 | within-radio |
|---|---|---|---|
| 31FE7A2 | +12914.2 | +12914.2 Hz | 0.0 Hz |
| 32C7510 | +12916.2 | +12916.2 Hz | 0.0 Hz |
| 32FCCF7 | +12917.7 | +12918.2 Hz | 0.5 Hz |
| 32FCD05 | +12918.3 | +12918.3 Hz | 0.0 Hz |

**All 8 channels COMB.** 13 pickets used on every one; spacing **40001.3-40001.4 Hz** against 40000
transmitted; fit rms **3.2-3.5 Hz**, about 0.008 % of spacing.

### The prediction that was wrong, and why that is the result

I predicted offsets would **DIFFER** across radios — independent LOs, independent calibration. They
agree to **4.1 Hz out of 2.401 GHz = 1.72 ppb.**

The prediction carried a free-running assumption into a locked configuration. Every LO is
synthesised from the same 10 MHz, so **LO error is common-mode**: +12916 Hz is the *transmitter's*
offset from 2401 MHz, seen identically by four receivers because they share a clock. Free-running,
each B210's own TCXO (+/-2 ppm = +/-4.8 kHz at 2.4 GHz) would have scattered these by kilohertz.

**So the agreement IS the calibration result, and a better one than the one predicted: four
independent radios, four USB paths, four processes, agreeing to 1.72 ppb.** That is the figure that
matters for treating them as one instrument.

`scripts/spectrum-check.py` was corrected accordingly — it had flagged this as "SUSPICIOUSLY EQUAL,
check for duplicated data", a rule written for free-running radios that would have misled the next
reader. Independence is now checked by **level**, not frequency: SNR spans 61.6-76.3 dB and peak
|IQ| 0.0017-0.0071, so these are genuinely distinct captures. Raw peak counts differ too (13 to 17)
— ISM traffic varying by antenna.

The owner's antenna prediction held exactly: **`32C7510`, the odd antenna, has the LOWEST SNR and
the CLEANEST comb** — 13 detected, 13 used, no interferers to trim. The better antennas pulled in
more 2401 MHz traffic and needed 2-4 outliers rejected.

### Two detector defects found before trusting it, both of the same family

1. **False positive on silence.** The first comb detector reported spacing 40530 and 39089 Hz on a
   capture with nothing transmitting. Its peak-thinning keeps detections at least half a spacing
   apart, so fitting them against 40 kHz indices yields a ~40 kHz slope from pure noise — **the
   detector manufactured the answer it was hunting for**, exactly like §9.7's retracted drift test.
   The discriminator was already in the output, unused: fit rms 11 kHz, 27 % of spacing. Fit quality
   became a hard criterion rather than a printed column.
2. **Assuming every peak belongs to the comb.** First pass on real data rejected 3 of 4 radios —
   14-17 peaks against a 13-line comb, offsets still clustered at +12.6 to +15.3 kHz. Not absence of
   signal: interferers taking wrong indices and dragging the line. Fixed with iterative outlier
   trimming. **The acceptance criterion was not relaxed** — still rms < 5 % of spacing over >= 5
   retained pickets — and every channel now fits to 3.2-3.5 Hz.

Validated in **both** directions before use, which §9.7's instrument never was: synthetic comb at
known +1234.0 / -3210.0 Hz recovered as +1233.9 / -3210.0 (0.1 Hz error); pure noise rejected.

### 9.12 Tags: sample-deterministic placement, read from the published stream

Two owner-directed changes, and one correction to my own plan.

**`TagSink` already existed.** `blocks/testing/.../TagMonitors.hpp:376`, alongside `TagMonitor` — it
records tags with their sample index and exposes a per-tag callback. I had it queued as "build a
tag-capturing sink". Checked before building, per `CLAUDE.md` §8.5. One trap: `log_samples` defaults
**true**, which at 15 MS/s accumulates every sample into a `Tensor` — gigabytes within seconds. Off.

**Tag placement now gates on SAMPLES, not the host clock.** It previously compared
`tWallNs - _lastTagTimeNs` against `tag_interval`, so tag positions depended on host scheduling —
the same dependency that invalidated §9.7's instrument. `tag_interval` is still expressed in
seconds but is converted through `sample_rate`, so a tag lands at a deterministic sample index and
two identical captures get tags in identical places.

Measured, 0.512 MS/s, `tag_interval` 1 s, `max_chunk_size` 8192:

| tag | sample index | device_time_ns |
|---|---|---|
| 3 | 516096 | 6008070991 |
| 4 | 1032192 | 7016070991 |
| 5 | 1548288 | 8024070990 |
| 6 | 2064384 | 9032070989 |

**Interval exactly 516096 samples every time** = `ceil(512000/8192) = 63` chunks = 1.008 s. A tag can
only land on a chunk boundary, so that is the true achievable period, and it is now *constant*
rather than wobbling with host load. The device-clock deltas are **1.008000000 s exactly**, agreeing
with the sample count to the nanosecond — two independent clocks confirming each other.

**This closes the instrument chain.** §9.7 retracted a host-derived measurement; §9.8 captured the
device timestamp; §9.9 used it for epoch alignment via a block member; §9.12 moves it onto published
tags, which is what an application actually consumes.

### 9.13 The cross-process arming barrier is NOT defined — stated because it was assumed

**Superseded in part:** the owner has since added a front-matter definition — see "FORWARD
DEFINITIONS" (1) at the top of this document, which binds `cross-process start barrier` and
`cross-process arming barrier` as aliases. What remains open below is the *contract*, not the term.

The owner asked whether it is clearly defined anywhere. **It is not.** It exists only as an
implementation in `womm_bmax.cpp:233-246` plus a passing mention at `RESULTS.md:1241`. No contract,
no stated guarantee, no failure modes. §9.9 and §9.10 both proposed "a cross-process arming barrier
would make the shared epoch structural" as though referring to a defined thing.

What it currently does: each process touches `${WOMM_BARRIER_DIR}/ready.<pid>`, then polls until
`WOMM_BARRIER_N` such files exist, with a 120 s deadline. What it does **not** do is bound the
window between the last arrival and any subsequent action, which is the only property that would
make PPS-edge agreement structural rather than observed. Specifying it is sprint-2 work, not a
one-line reuse.

### 9.14 ★ MULTI-PROCESS IS NOT REQUIRED — and why this was the right time to find out

The tier-1 goal is full performance before anything else, so re-checking the topology **as soon as
full performance existed** is protocol, not a detour. It could not have been asked earlier: until
2026-07-27 there was no full-performance configuration to hold constant. The one thing hindsight
would change is that it belonged in the same breath as the D10 "Usable UI" decision, since both are
tier-1 and the topology determines how hard the UI integration is.

Harness: `blocks/sdr/src/womm_mt_test.cpp` — N radios × 2 channels, depth 0, **one graph, one
scheduler, one process**. Everything held at the working configuration; only topology varies.

| aggregate target | pool threads | result |
|---|---|---|
| 30.72 MS/s | 16 | ratio **1.0000** |
| 61.44 MS/s | 16 | ratio **1.0000** |
| 122.88 MS/s | 16 | **FAILED** — overflows, then `stream error: -2` |
| **122.88 MS/s** | **24** | **122.24 MS/s, ratio 0.9948** |

**One process reaches within 0.5 % of four processes.** The intermediate failure was **my own pool
under-sizing**: the multi-process run used 6 threads × 4 processes = 24 total, so 16 in one process
was never the same experiment.

### What was mis-attributed, and how

Every figure that made multi-process look necessary came from `womm_bmax` — itself a **single-process**
harness — and every one of those runs was **single-channel at DSP depth 8**, crippled by the bug that
made all pre-2026-07-27 measurements half-blind. §8.3 then profiled scheduler mutex contention among
workers sharing one graph, which *shaped* the conclusion toward "radios in one scheduler contend",
even though §8.3's own finding was that the contention was a symptom.

**The topology was never the problem. It was convicted on evidence about something else.**

### ⚠ Split verdict: MT for streaming, MP still earns its keep for capture

MT throughput is proven. **MT capture is not.** In one process the radios start staggered, so file
lengths diverge badly — 104 608 samples on one radio against 31 904 on another in the same 0.2 s
request — while the fastest hits its byte cap and rolls over. Independent per-radio files are
something multi-process gets for free.

So `scripts/womm-scan.sh` **stays on the multi-process path for capture**, and `womm_mt_test` is
retained as the single-process streaming/throughput benchmark. Neither is deprecated.

### Consequences

- **S2-1 is largely dissolved, not solved.** The cross-process start barrier is the cost of a
  topology that streaming does not need.
- **Pool sizing is a cliff, not a gradient.** 16 threads silently fails at full rate where 24 works,
  with no warning. This is now the most dangerous undocumented parameter in the project.
- **Fault isolation is genuinely lost in MT.** One stream error takes the whole graph down — exactly
  what happened at 16 threads. In four processes, three would have survived.

### Not settled

Depth 0 only, so §8.3's scheduler-contention concern is untested in this topology at full rate.
One run per point. Startup overflows occur at every rate — survivable at 24 threads, fatal at 16.

---

## Phase 10 — S2-3: the criterion could not be evaluated, because nothing counted

Session 2026-07-29. Sprint item S2-3, with the owner's steer that the §9.14 follow-ups are its
first part rather than a separate item.

### 10.1 ★ WITHDRAWN — C-3's "zero overflows" was never measured

`HANDOFF.md` C-3 and §9.6/§9.10 all record "zero overflows" at full rate. **No counter existed.**
`womm_rx_hold` — the harness that produced those runs — set `max_overflow_count = 0` ("never stop")
and reported nothing. The claim was read off the sample ratio.

Measured now, with a counter, in the same four-process topology (`WOMM_THREADS=6` each, external
clock, ~70 s):

| radio | overflows | timeouts | rate/channel |
|---|---|---|---|
| A | 14 | 4 | 15.35–15.36 MS/s |
| B | 12 | 4 | 15.35–15.36 MS/s |
| C | 12 | 4 | 15.35–15.36 MS/s |
| D | 9 | 4 | 15.35–15.36 MS/s |

**Multi-process at full rate overflows.** The rate is at nominal and the earlier ratio was right;
"lossless" was an inference from it, and the inference was wrong.

**A ratio near 1.0 and "no overflow" are not the same claim.** Both harnesses now report both.

### 10.2 §9.14's 0.9948 is a favourable single draw, not a value

Seven runs of `womm_mt_test`, 4 radios × 2 channels, depth 0, 24 threads, external clock:

| | ratio |
|---|---|
| runs | 0.9891, 0.9901, 0.9929, 0.9931, 0.9938, 0.9942, 0.9966 |
| **mean** | **0.9928** |
| range | 0.9891–0.9966 |

§9.14's single 0.9948 lies inside this range. Two consequences, and they point opposite ways:

- **No regression from this session's instrumentation** — the added counters and device-time tags
  cost nothing measurable. That was the first thing checked and it is why the repeats were run.
- **§9.14's "within 0.5 %" was more precise than its evidence.** It compared one MT run against one
  MP run. MT never reached 1.0000 in seven attempts.

### 10.3 ⚠ OPEN CONTRADICTION — MT has FEWER overflows than MP, yet a lower ratio

Same instrument, same session:

| topology | overflows/radio | ratio |
|---|---|---|
| MP, 4 processes, 6 threads each, ~70 s | 9–14 | ~1.000 (15.35–15.36 MS/s per channel) |
| MT, 1 process, 24 threads, ~50 s | 6–8 | 0.9896–0.9911 |

**Device overflow cannot explain both.** If MT's ~1 % deficit were dropped samples, MT would show
*more* overflow events than MP, not fewer. So either MT loses samples somewhere that is not the
device, or the two ratios are not measuring the same thing.

**Stated as unexplained rather than guessed at.** Candidate directions, none tested: the two
harnesses compute the ratio over different windows (one 20 s window vs repeated 1 s windows, the
latter displayed to 2 decimals and unable to resolve 0.999 from 1.000); or MT loses samples
downstream of the source. This is the next thing to settle, and it bears directly on §9.14's
headline.

### 10.4 ★ THE S2-3 CONTROL FAILS — the baseline is not quiet at any rate tried

`womm_ops null` runs the operations harness with **no operation injected**. It must report zero
attributed events, or no operation result taken at that operating point means anything.

| aggregate rate | per 4 s window, per radio |
|---|---|
| 122.88 MS/s | 0–18 overflows, bursty |
| 61.44 MS/s | 1–2 overflows |
| 61.44 MS/s, timing tags off | 1–2 overflows |

**So the tier-1 criterion cannot yet be evaluated at full rate.** "No unanticipated overflow during
operations" is unmeasurable while the background rate is larger than any operation's effect. Finding
the rate at which the control is clean is now the precondition for the rest of S2-3.

### The residual is perfectly correlated across four independent radios

At 61.44 MS/s every radio reported **identical** counts in every window — 2/0/1, 2/0/1, 2/0/1,
2/0/1. Four B210s on separate XHCI controllers do not agree by chance; this is one cause in the
host stalling all four read loops together, not four independent device events.

**Refuted, not assumed: periodic tag emission is not the cause.** `emit_timing_tags` and
`emit_meta_info` run on the read loop and were the obvious suspect — `womm_mt_test` already disables
them, commented "no tag cost". With both off the counts are unchanged (1/0/2 against 2/0/1).

### 10.5 The boot LO measurement is a BOUND, not a settle time

`waitForLoLock()` now retains its duration instead of discarding it on success. Measured per
channel, all four radios: **0.06–0.17 ms**.

**This is not "the LO settles in 0.1 ms".** The loop records ~0 when the sensor already reads
`true` on the first poll, which is what happened on every channel — so the synthesiser had already
locked before `waitForLoLock()` was reached, during the preceding device setup. The number bounds
the settle time from above by everything that ran before the check; it does not resolve it.

Runtime retune settling is a separate and currently unobservable quantity: `applyFrequency()`
(`SoapySource.hpp:750`) never calls `waitForLoLock()`, and the harness cannot poll the sensor itself
because the read loop is the only thread permitted to touch the device.

### 10.6 Operational — consecutive runs need a settle gap

Five back-to-back `womm_mt_test` invocations with no gap: the first produced no result at all. With
a 15 s gap between runs, five of five succeeded. The devices need time to be released between
processes.

Recorded because the first attempt at the repeats **hid** this: it piped each run through
`grep AGGREGATE`, so a failed run wrote nothing and looked the same as a run that had not finished.
The same family as the retracted instruments — a measurement that cannot report its own failure.

### 10.7 ★ THE MT-vs-MP COMPARISON WAS CONFOUNDED — stream args were never held constant

Owner's review, 2026-07-29, pointed at the B200 "Known issues" section. Checking it found a
configuration difference that §9.14 had claimed did not exist.

> **Ettus, B200 manual, "Known issues":** "The default streaming settings do not work optimally for
> all use cases. If there are issues with performance or stability, it can help to modify the
> `recv_frame_size` values, e.g., by setting `recv_frame_size=1024` as part of the device args."

| harness | topology | `stream_args` |
|---|---|---|
| `womm_rx_hold` | **MP** | `num_recv_frames=1024` |
| `womm_mt_test` | **MT** | *(none — UHD defaults)* |
| `womm_bmax`, `womm_ops` | MT | *(none — UHD defaults)* |

**§9.14 said "everything held at the working configuration; only the topology changed". It was
not.** The MT harness ran with UHD's default USB transfer-buffer depth while the MP harness ran with
1024. `num_recv_frames` (frame **count**) and `recv_frame_size` (**bytes** per frame) are different
knobs, and the one Ettus documents as the remedy — `recv_frame_size` — had never been set here at all.

**Effect on `womm_ops` at full rate, per 4 s window, per radio:**

| `stream_args` | overflows |
|---|---|
| *(none)* | 0–18 |
| `num_recv_frames=1024` | **0–2** |
| `num_recv_frames=1024,recv_frame_size=1024` | 0–1 |
| `recv_frame_size=1024` | 0–2 |

**An order of magnitude, from one device arg.** Both harnesses now default to
`num_recv_frames=1024`, overridable by `WOMM_STREAM_ARGS`.

### But it does NOT explain the MT ratio deficit, and §10.3 still stands

`womm_mt_test` with the arg matched to the MP harness, three runs: **0.9946, 0.9975, 0.9892**
(mean 0.9938) against the previous seven runs' mean of 0.9928, range 0.9891–0.9966. **Overlapping
ranges — no clear effect on the ratio.**

So the stream-args confound is real and worth fixing, but the thing it fixes is the *overflow rate
in `womm_ops`*, not MT's ~0.7 % sample deficit. Those are two different problems, and conflating
them would repeat exactly the error §9.14 made.

### And the control is still not clean

At full rate with `num_recv_frames=1024` the null control reports **1 overflow per window** rather
than up to 18 — and **all four radios still report identical counts** (1/1/0 on each). The magnitude
fell by an order of magnitude; the correlation did not change at all. Whatever stalls all four read
loops together is still there, just smaller.

### 10.8 Master clock rate — we run at the documented dual-channel ceiling

> **Ettus, B200 manual, "Changing the Master Clock Rate":** "The clock rate can be set to any value
> between 5 MHz and 61.44 MHz (or 30.72 MHz for dual-channel mode). Note that rates above 56 MHz are
> possible, but not recommended."

We use `master_clock_rate = 30.72e6` with two channels — **exactly the documented dual-channel
maximum** — and a per-channel rate of 15.36 MS/s, i.e. **decimation of 2, the minimum**.

The "not recommended above 56 MHz" caveat applies to single-channel operation and does not bite us.
But the shape of the guidance does: Ettus treats the top of the range as marginal, and

> **"Automatic Clock Rate Setting":** "The auto clock rate selection attempts to use the largest
> possible clock rate as to enable as many half-band filters as possible."

— so the driver's own auto-selection exists to *maximise* available half-band filtering, and pinning
MCR to 30.72 with decimation 2 gives it the fewest stages to work with. Every "at capacity" run this
project has recorded has used the most marginal configuration the part offers.

**Not yet tested: lowering the MCR itself.** §10.4's half-rate run lowered `WOMM_RATE` to 7.68 MS/s
but left `WOMM_MCR` at 30.72e6, so it changed decimation, not the clock rate. Lowering MCR is a
distinct experiment and is the outstanding one.

### 10.9 Driver provenance — checked, and correct

Owner asked whether an earlier session left us on a Soapy that gr4 does not use, possibly less
feature-rich. Checked:

| | version | source |
|---|---|---|
| SoapySDR | `v0.8.1-g17e590ba`, ABI `v0.8-3` | vendored master (2026-01-02), built into the prefix |
| SoapyUHD | vendored master `2a5d381f` (2025-10-05) | **reports "0.4.1"** — upstream never tagged a newer release |
| UHD | 4.10.0 | brew, by decision I-3 |

**The "0.4.1" is a stale version string in master, not an old driver.** Evidence it is master: the
vendored source carries `post_input_action`/`post_output_action` (`UHDSoapyDevice.cpp:711,863`),
which exist only for UHD 4.8+; and our two local patches are `cxx17-for-uhd-4.10` and
`boost-190-lexical-cast`, neither of which a genuine 0.4.1 would need or build with.

Nothing in `/opt/homebrew` or `/usr/local` can shadow the prefix's SoapySDR — there is no other copy
on the machine — and `scripts/verify-vendor.sh` reports **all seven vendored trees reproduce from
HEAD**. No action needed.

### 10.10 ★ THE UNIFIED-INSTRUMENT PATH DOES NOT EXIST FOR B210-OVER-USB

Owner, 2026-07-29: *"Radio people do not consider a bunch of un-synchronized radios to be unified in
any way."* — and the observation that `multi_usrp` is UHD's **multi-device** abstraction, which we
call four times, once per serial.

The point is correct and worth stating plainly. "One instrument" in radio terms needs three things.
We have two:

| property | source | have it? |
|---|---|---|
| common frequency reference | Octoclock 10 MHz | ✔ |
| common time reference | Octoclock PPS via `set_time_unknown_pps` | ✔ |
| **sample-aligned streaming** | one streamer spanning all channels | **✘** |

Ettus, "Multiple USRPs": with one streamer over several channels *"MIMO operations is automatically
chosen. This means samples will be aligned between streams automatically."* That is the property we
do not have — four `multi_usrp::make()` calls, four streamers, four independent read loops.

**So: can the four B210s be one `multi_usrp`? No.** Tested at both layers:

| layer | command | result |
|---|---|---|
| Soapy | `SoapySDRUtil --probe="driver=uhd,serial0=…,serial1=…,serial2=…,serial3=…"` | **one** mboard, `Channels: 2 Rx` |
| UHD directly | `uhd_usrp_probe --args="serial0=…,serial1=…"` | **one** mboard, one "B-Series Device" |

**UHD itself takes one mboard, so this is not a Soapy limitation.** The multi-device compound in the
Ettus manual is for network devices (`addr0=…,addr1=…`, X3xx examples); the B200/B210 USB driver
creates one device per USB connection, and the B210 has no MIMO cable.

**Owner, confirming and sharpening this:** multi-device aggregation applies only to the **N- and
X-series RFNoC** devices. Note the distinction, which is easy to garble — `multi_usrp::make()` is
the ordinary UHD entry point and *is* used for the B210 (SoapyUHD calls it for every device, see
§10.13); what is N/X-series-only is putting **several motherboards inside one** `multi_usrp`. The
measurement above and this statement agree, arrived at independently.

**Consequences, and they are clarifying rather than discouraging:**

- **Sample-aligned multi-radio streaming is not available on this hardware.** It cannot be obtained
  by configuring UHD differently.
- **S2-1's barrier problem is therefore intrinsic**, not an artefact of our topology choice. It is
  not dissolved by MT (§9.14) and it is not dissolved by a unified `multi_usrp` either.
- **The shared-PPS-epoch plus per-device-timestamp approach is the only mechanism the hardware
  offers**, which retroactively justifies §9.8's recovery of the discarded device timestamp: that
  timestamp is not a nicety, it is the substitute for MIMO alignment.
- **The correlated overflow is not explained by missing MIMO aggregation** — that path does not exist
  to be missing.

### 10.11 Master clock rate: auto-selection picks 16 MHz, which cannot carry full rate

With `master_clock_rate = 0` the B210 selects **16.000000 MHz** — a round number, as the owner
predicted. UHD says so itself: `[B200] Setting master clock rate selection to 'automatic'. Asking
for clock rate 16.000000 MHz`.

**But 16 MHz caps dual-channel at 8 MS/s per channel**, well below the 15.36 requested. The
auto-selection happens at device creation, before a sample rate is requested, so it cannot know what
is wanted. **The explicit `master_clock_rate = 30.72e6` is therefore load-bearing for reaching full
rate, not an unexamined assumption.**

Owner's recommended 15.00 MS/s at MCR 30.00 MHz was tested against the 15.36/30.72 baseline. Both
achieve their requested rate exactly (device-reported 15.000000 and 15.360000). **Neither made the
control clean**, and a single run each is not enough to rank them — recorded as tested, not settled.

⚠ **A flaw in the first attempt at this test, recorded because it is the recurring family:** the
first run compared auto-MCR against explicit-MCR *without checking the achieved sample rate*, so
"auto" was silently running at a different rate than "explicit". Which is why `_reportedSampleRates`
was fixed first — see below.

### 10.12 `_reportedSampleRates` was declared and never assigned

A field sitting with the other device-reported values, documented as "only these are measurements",
which **no code ever wrote**. It read as an empty measurement rather than a missing one. Now
populated in `applySampleRate()` from the readback that was already being performed and discarded.

Also added: `_reportedMasterClockRate`, read after the clock config is applied, because with
`master_clock_rate = 0` what the device chose is a measurement and not an echo of a request.

### 10.13 SoapyUHD does no automatic clock sync — in any version

Owner's hypothesis: that S1/S2's reports of slowness launching multiple SoapyUHD devices were the
driver's automatic slow-but-reliable Ettus sync being mistaken for a fault.

**The mechanism is not there.** In the vendored SoapyUHD (`2a5d381f`, the copy we build and load):

- `make_uhd` (`:1145`) does an ABI check, registers a log handler, calls `multi_usrp::make(args)`.
- The constructor (`:41`) sets only optional `rx_subdev`/`tx_subdev`.
- Every clock and time call is a bare passthrough: `set_clock_source` (`:832`), `set_time_source`
  (`:847`), `set_time_next_pps` / `set_time_unknown_pps` (`:873-874`).
- `set_time_unknown_pps` — the multi-second call — is reachable **only** via
  `setHardwareTime(t, "UNKNOWN_PPS")`, and in our tree the caller is our own `SoapySource::start()`,
  gated on an external time source.
- The changelog back to 2017 records no sync automation in any release; master adds only an X300
  tree fix, a `getBandwidthRange` fix and a C++14 bump over 0.4.1.

**But the conclusion may still hold by a different mechanism.** `multi_usrp::make()` performs full
device bring-up — firmware load, CODEC init, register loopback tests, clock-rate selection — at
~2.5 s per B210, and that is genuinely slow-but-reliable. It is plausible that this was read as a
driver or latency fault. The slowness is real and expected; it is UHD device initialisation, not a
Soapy default.

### 10.14 `waitForLoLock()` is ours, and it is a no-op

Added by us in `43d9724` (2026-07-27), rationale: *"applyFrequency() confirms the requested VALUE
reads back, not that the synthesiser has settled. Streaming before LO lock yields garbage."* Sound
reasoning, never tested.

**Measured: every channel on every radio reads `lo_locked` true on the first poll.** Across all runs
this session the recorded duration is 0.06–0.21 ms, which is sensor-read latency. By the time
`waitForLoLock()` runs — after `applyFrequency`, `applyGain`, `applyBandwidth`, `applyDeviceSettings`
and before `setupStream` — the synthesiser has always already locked.

So it protects against nothing at that call site, while creating the impression of protection. The
owner's flag was right: this is a case of building on an earlier, less-informed assumption.

**Not removed**, because it is cheap and it is the correct guard *if* it were placed where tuning
actually happens — which is the runtime retune path (`applyFrequency`), where it is absent and where
nothing waits for lock at all. That asymmetry is the real finding.

⚠ It is also **not recorded in `DRIFT.md`** as its own entry — only this session's modification of it
is. A functional addition to upstream code that the drift register does not list.

### 10.15 ⚠ CORRECTION TO §10.7 — `num_recv_frames` in stream args does NOTHING

**My §10.7 claim of "an order of magnitude from one device arg" was wrong, and the error was n=1.**
Owner's caution — *"there are variations between names used in the soapy driver and ettus… 'the
place to enter them' are a source of confusion"* — is exactly what this was.

Total overflow across all windows and radios, full rate, one run each:

| where `num_recv_frames=1024` was put | total overflow |
|---|---|
| nowhere (baseline) | 20 |
| **stream args** — where every harness has always put it | **20** |
| **device args** — where Ettus says to put it | **7** |
| both | **3** |

**Stream args are inert: identical to not setting it at all.** The improvement reported in §10.7 was
run-to-run variance in a single measurement, and it was reported before repeats — the same mistake
§10.2 had just finished documenting in someone else's work.

**Two consequences:**

- **The `womm_rx_hold` / `womm_mt_test` stream-args difference is real but inert.** The harnesses did
  differ, so §9.14's "only the topology changed" is still not true — but the difference had no
  effect, so it does not explain anything either. The confound stands; the mechanism does not.
- **`recv_frame_size` and `num_recv_frames` in *device* args have never been set by any harness**,
  which is the one place they work. That is now the most promising untried lever.

Repeats of the device-args configuration are in progress; the table above is one run per cell and is
**not** a settled result. Recorded this way deliberately.

**And it is not a USB bandwidth story.** Owner: *"We have firmly established that no issues with
radios and bandwidth are related to USB."* Consistent — `num_recv_frames` is host-side buffer
*depth*, so it buys tolerance for host scheduling stalls rather than transport throughput. That fits
the correlated-stall observation in §10.4 rather than contradicting it.

### 10.16 ⚠ "~2.5 s per B210 bring-up" is a prior-session figure, and it is wrong

Caught by the owner quoting it back. `HANDOFF.md` carries "Device init (~2.5 s per B210)" and the
harness comments repeat it ("B210 bring-up is ~2.5 s"), and I restated it as established fact. Under
the authoritative-sources rule a prior-session timing statement **is not evidence**, and this one had
never been re-measured.

**Measured, 2026-07-29, `uhd_usrp_probe --args=serial=…`, wall clock:**

| radio | real |
|---|---|
| A | **0.93 s** |
| B | **0.52 s** |

`uhd_usrp_probe` includes `multi_usrp::make()` *plus* the full property-tree walk and printing, so
device bring-up alone is **under a second** — three to five times faster than the inherited figure.

**Probable reconciliation:** with an external time source our own `start()` calls
`set_time_unknown_pps()`, which blocks up to ~2 s by construction waiting for a PPS transition — and
that is documented in our own code comment. The "~2.5 s device init" most likely conflated UHD
device initialisation with **our** PPS wait. Stated as the likely explanation, not a measured one:
the 0.5–0.9 s figures above were taken without an external clock.

**This also weakens §10.13's "the slowness is real".** Half a second is not obviously slow enough to
be mistaken for a driver fault. What *is* slow is the PPS wait, which is ours and deliberate.

**Consequence:** readiness timeouts and warm-up windows across the harnesses were sized against a
number that was too large by 3–5×. Harmless — they are generous, not tight — but they are not
evidence of anything about the hardware.

### 10.17 STANDING RULE — do not run MIMO

Owner, 2026-07-29: **MIMO is implicitly a transmit mode.** It is therefore covered by the standing
RF-transmission rule in `HANDOFF.md`, and no test in this project may enable it.

This matters because §10.10 discusses MIMO *streamer alignment* — the property whereby one streamer
spanning several channels aligns their samples. **That discussion is about what the hardware cannot
give us, not a proposal to enable anything**, and the point is moot regardless: multi-mboard
aggregation is unavailable for B210-over-USB, so the configuration cannot be reached even by
accident.

All harnesses remain RX-only by construction, and `assert_no_tx.cmake` asserts on the linked binary
that no transmit symbol is present.

### 10.18 ⚠ CORRECTION TO §10.16 — my own bring-up figure used the wrong instrument

§10.16 reported 0.52–0.93 s from `uhd_usrp_probe` and called the inherited "~2.5 s" wrong by 3–5×.
**The instrument was wrong.** Owner: *"that's basically a 'dump your parameters' command, it doesn't
run anything, but it does load missing firmware except when it doesn't"* — so it is a warm-start
property dump on radios whose FPGA was already loaded, and it performs no clock sync at all. It does
not measure the path the harness takes.

**Measured properly — one radio, `womm_rx_hold`, 0.1 s capture, warm FPGA, wall clock:**

| configuration | run 1 | run 2 |
|---|---|---|
| `WOMM_EXTCLK=0` (device init only) | **3.70 s** | 3.35 s |
| `WOMM_EXTCLK=1` (init + reference + PPS + start offset) | **10.20 s** | 9.89 s |

**The owner predicted 3.7 s before this was run, and `WOMM_EXTCLK=0` measured 3.70 s.**

So the record is now: the inherited "~2.5 s" was **too low**, not too high; §10.16's 0.5–0.9 s is
**withdrawn** as a warm-start property dump rather than a bring-up; and the real device-init figure
is **~3.4–3.7 s**.

**The external-clock path costs ~6.3 s more, and it is all ours by choice:**

| component | cost | where |
|---|---|---|
| `ref_locked` polling | up to 3 s | `kRefLockTimeoutMs = 3000` |
| `set_time_unknown_pps()` | 1 – ~2 s | waits for a PPS transition, then arms the following edge |
| `start_time_offset` | **5 s** | set by both harnesses when `WOMM_EXTCLK=1` |

5 s of that is a deliberate arming offset we chose, not anything the driver imposes. This decomposes
the ~10 s completely and shows most of it is policy rather than hardware.

**This is the third time in one session that a timing figure turned out to be measuring something
other than what it named** — after §10.6's grep that hid failures and §10.11's rate comparison that
did not check the achieved rate. The pattern is not carelessness about arithmetic; it is naming a
quantity before checking that the instrument produces it.

### 10.19 Device args, repeated — the effect is real

§10.15 reported one run per cell. Repeated at full rate, total overflow across all windows and
radios per run:

| configuration | runs | mean | range |
|---|---|---|---|
| no `num_recv_frames` anywhere | 20, 25, 32, 24 | **25.3** | 20–32 |
| `num_recv_frames=1024` in **device args** | 3, 4, 17, 6 | **7.5** | 3–17 |

**Roughly 3× fewer overflows, and the ranges barely overlap** — the baseline minimum (20) exceeds
the device-args maximum (17). Unlike §10.7's withdrawn claim, this one survives repeats.

Still true, and still the point: **`num_recv_frames` in *stream* args does nothing** (§10.15), which
is where every harness has always put it. The working placement is device args, which none has used.

**Not a USB bandwidth effect**, per the owner: no radio or bandwidth issue here is USB-related.
`num_recv_frames` is host-side buffer depth, so it buys tolerance for host scheduling stalls. That is
consistent with the correlated stall of §10.4 — deeper buffers absorb it, they do not remove it, and
the counts remain correlated across radios.

### 10.20 ★ THE SERIAL BLOCK START IS UPSTREAM GNURADIO4'S, NOT OURS — and GR 3.10 does it right

Owner asked directly: did we write the serial loop in `Scheduler.hpp`, or someone else?

**Someone else. It is upstream `gnuradio/gnuradio4`, unmodified by us.**

- `git blame` on the loop (`Scheduler.hpp:660-675`): Sergio Martins (13 lines), Ivan Čukić, Ralph J.
  — all upstream authors. No womm commit touches it.
- Upstream `gnuradio/gnuradio4` main carries the identical structure at its `:586-597`:
  `graph::forEachBlock<TransparentBlockGroup>(...)` → `block->changeStateTo(lifecycle::RUNNING)`.

**What that means for four radios.** `forEachBlock` is a serial walk under `_executionOrderMutex`.
Each `changeStateTo(RUNNING)` runs `SoapySource::start()` → `reinitDevice()`, measured at
**~3.4-3.7 s per B210** (§10.18). Four radios therefore bring up **one after another**, ~14 s end to
end, with the first streaming while the last is still initialising.

**Owner: in a GNU Radio 3.10 flowgraph, four radios start in different threads simultaneously.**
Each spins up its LO and tuner, attaches to the external reference and unknown-PPS, reports status
as it goes, then all rendezvous on a PPS edge and start together — issuing a beginning-of-stream tag
carrying GPS time and sample count 0, followed by time-aligned samples.

**So this is a gr4 architectural regression against 3.10 for multi-device work**, in upstream code,
and it is not something our fork introduced. Recorded here because it changes who owns the problem.

### What the startup SHOULD look like (owner, 2026-07-29)

Stated as the target, since it is not what we do:

1. Spin each radio up in **its own thread**, as fast as possible; the thread only starts the radio.
2. Then do **almost nothing** for ~4 s — watch for tags and messages. LO-lock and similar status
   reports are expected here.
3. **Overflow and underflow do not apply during startup.** The concept does not exist yet.
4. Receive four **beginning-of-stream tags** — sample 0 and its time. That is the starting gun.
5. **If you take no other action and see overflow or underflow, something is wrong.**

Our harnesses do none of this: they wait for a sample *count* to become non-zero rather than for a
start-of-stream tag, and they count overflow from `start()` — including the whole serial bring-up.

### 10.21 ⚠ REFUTED — widening the arming window does not fix it

My hypothesis from the serial-start finding: `start_time_offset = 5 s` cannot cover ~14 s of serial
bring-up, so widening it should let all four arm before any starts.

Total overflow, full rate, `num_recv_frames` in device args:

| `start_time_offset` | runs |
|---|---|
| 5 s | 10, 5 |
| 20 s | 15, 6 |
| 30 s | 5, 8 |

**No effect.** The prediction was wrong. Either the arming window is not what serialises the start,
or the overflow being counted is not the startup overflow at all. Recorded as a refuted prediction
rather than quietly dropped.

### 10.22 There is no "the gr4 Soapy driver" to start over from

Owner asked whether we are using the GR4 Soapy driver or something else, and whether starting over
from the GR4 one had been tried.

**Answer: there is nothing to start over from, and no, it has not been tried.**

Upstream `gnuradio4`'s entire SoapySDR provision is `CMakeLists.txt:700-702`:

```cmake
# Fetch SoapySDR -- needed since the distribution version is incompatible w.r.t. stdlibc++ vs. libc++
if(CMAKE_CXX_COMPILER_ID MATCHES "(GNU|Clang)") # WIP
  find_package(SoapySDR CONFIG)
endif()
```

**The comment says "Fetch" and the code does not fetch.** It calls `find_package` on whatever is
installed, guarded by a `# WIP` marker. There is no pinned version, no submodule, no FetchContent
declaration, and no CI that exercises it — `.github/workflows/` mentions neither Soapy nor UHD.

So gr4 has **no opinion** about which SoapySDR is correct. Our vendoring was a local choice filling a
gap upstream left open, not a deviation from an upstream pin.

**What is NOT yet investigated, and the owner's hypothesis stands:** whether GNU Radio **3.10.x**
packages a known-good Soapy + UHD combination that would be the better basis. The reasoning is sound
— 3.10 ships a much richer set of Ettus utilities via `apt install gnuradio`, and the absence of any
UHD/HackRF mention in gr4's docs would be explained if maintainers assume everyone gets drivers from
the 3.10 packaging. **Not checked. Open.**

### 10.23 ★★★ GR 3.10 HAS THE START BARRIER BUILT IN. S2-1 IS A SOLVED PROBLEM UPSTREAM OF US.

Owner's experiment, 2026-07-29: examine GR 3.10.9's flowgraph initialisation and how it would handle
four Soapy radio blocks. Read-only shallow clone of `gnuradio/gnuradio` at **`v3.10.9.2`**
(`c7c828a`), kept outside the repository.

**The answer is unambiguous, and it is a standard framework feature.**

`gnuradio-runtime/lib/scheduler_tpb.cc`, the thread-per-block scheduler constructor:

```cpp
thread::barrier_sptr start_sync =
    std::make_shared<thread::barrier>(blocks.size() + 1);

// Fire off a thead for each block
for (size_t i = 0; i < blocks.size(); i++) {
    d_threads.create_thread(thread::thread_body_wrapper<tpb_container>(
        tpb_container(blocks[i], block_max_noutput_items, start_sync), ...));
}
start_sync->wait();
```

The full chain, verified end to end:

| step | file:line | what happens |
|---|---|---|
| 1 | `scheduler_tpb.cc` | barrier sized `blocks.size() + 1`; **one thread per block**, all fired off together |
| 2 | `tpb_thread_body.cc:26` | each thread's ctor init-list constructs `d_exec(block, …)` |
| 3 | `block_executor.cc:251` | `d_block->start(); // enable any drivers, etc.` — **concurrently, one per thread** |
| 4 | `tpb_thread_body.cc:66` | `start_sync->wait()` — every thread rendezvous |
| 5 | — | barrier releases; **all work loops begin together** |

And for a Soapy radio, `gr-soapy/lib/block_impl.cc:362-380`:

```cpp
bool block_impl::start() {
    d_stream = d_device->setupStream(...);
    d_mtu    = d_device->getStreamMTU(d_stream);
    set_max_noutput_items(d_mtu);      // work invocations limited to one MTU
    d_device->activateStream(d_stream);
}
```

**So four Soapy radios in a GR 3.10 flowgraph: four threads, four concurrent `activateStream()`
calls, then a barrier before any of them reads a sample.** Exactly what the owner described from
operating experience, now confirmed in the source.

### What this means for S2-1

**`SPRINT.md` S2-1 — the "cross-process arming barrier", which this project has carried as one of
the hardest open problems and "one of the core console tasks of the MCM" — is a built-in feature of
the GR 3.10 scheduler that gnuradio4 dropped.** gr4's `Scheduler.hpp` contains **no barrier of any
kind** (`grep -i barrier` returns nothing), and starts blocks by a serial `forEachBlock` walk
(§10.20).

Three honest qualifications, so this is not oversold:

- **GR 3.10's barrier is in-process** — a `thread::barrier` across block threads, not across
  processes. It solves S2-1 for the MT topology, not the MP one.
- **It synchronises the start of work loops, not the hardware arming.** Hardware alignment still
  comes from the PPS. What the barrier guarantees is that no block begins consuming until every
  block has finished `start()` — which is precisely what stops radio 0 streaming while radio 3 is
  still initialising.
- **`set_max_noutput_items(d_mtu)` is a second thing 3.10 does that we do not**: work invocations are
  capped at one device MTU, chosen from the device rather than configured by hand.

### Consequences

1. **S2-1 should be re-scoped.** It is not a novel problem to be invented; it is a feature to be
   ported. The design is `thread::barrier(N+1)` + start-in-thread, and it is ~15 lines in 3.10.
2. **`OPERATIONS.md` A-1 ("startup overflow is anticipated") is downgraded.** It is anticipated
   *given gr4's serial start*, and that is a gr4 defect against its own predecessor rather than a
   property of the hardware. Under the owner's model — overflow does not apply during startup at all
   — A-1 should not exist.
3. **The correlated residual (§10.4) is still unexplained**, and is NOT this: it persists long after
   warm-up, and widening the arming window changes nothing (§10.21).

**Clone is read-only, at a tag, outside the repository, and nothing from it has been copied in.**
Licence caution for later: GR 3.10 is **GPL-3.0**, so its code cannot be lifted into this MIT tree —
the design can be reimplemented, the source cannot be copied.

### 10.24 How gr4 collects four streams — and the provenance of the polling

Owner: *"if GR4.0 starts the radios in serial, what does it do to collect multiple streams of data?
If your answer is 'polling', check for a provenance of that code to FAIR."*

**Reading is NOT serial. Only `start()` is.** Each `SoapySource` launches its own IO thread:

```cpp
thread_pool::Manager::defaultIoPool()->execute([this]() { ioReadLoop(); });   // SoapySource.hpp:266
```

Four radios therefore get four concurrent reader threads doing blocking `readStream` calls. The
serial `forEachBlock` walk (§10.20) delays *when each thread is created*, not how they run afterwards.

**But `SoapySource::work()` never produces a sample.** It checks three liveness flags and returns
`{requestedWork, 0UZ, work::Status::OK}` (`:278-291`). The scheduler calls it forever and it does
nothing every time. The data path is entirely: IO thread → port buffer → scheduler workers move it
downstream.

**And the scheduler polls.** `poolWorker` (`Scheduler.hpp:704`) spins:

| line | code |
|---|---|
| `:44` | `std::this_thread::sleep_for(…delay_ms…); // fallback spin sleep` |
| `:783` | `std::this_thread::yield();` |
| `:800`, `:803` | `std::this_thread::sleep_for(std::chrono::milliseconds(timeout_ms));` |
| `:846` | progress watchdog comparing `currentProgress == lastProgress` |

### Provenance: FAIR, and in both trees

The owner's suspicion is correct.

| where | `poolWorker` present? | polling present? |
|---|---|---|
| `gnuradio/gnuradio4` main | **yes** (3) | yes — `// fallback spin sleep`, `yield()` |
| `fair-acc/gnuradio4` main | **yes** (3) | yes — same lines |

`git blame` on `Scheduler.hpp:704-830`, by author:

| lines | author | affiliation |
|---|---|---|
| 58 | **Ralph J. Steinhagen** (3 commits, incl. `e608f23` "added optional scheduler blocking (#376)") | GSI/FAIR — the fair-acc/gnuradio4 lead |
| 12 | `rstein` | same person |
| 6 | **Alexander Krimm** | GSI/FAIR |
| 7 | Sergio Martins | |
| 18 | `mhertz2401` | ours |

The **other** polling loop — the macOS thread-pool spin this project already replaced with a condvar
(`DRIFT.md` Category F, 47× less CPU) — blames to the same two: **Ralph J. Steinhagen** and
**Alexander Krimm**.

**So the polling design is FAIR-authored throughout, and it is in `gnuradio/gnuradio4` because that
tree derives from the FAIR work** — which is the same reason the two trees are in a licence dispute
rather than a technical one (`HANDOFF.md` upstream landscape).

**Contrast with GR 3.10 (§10.23), which is the point:** 3.10 gives each block a thread that *blocks*
on its own work, with a barrier to align the start. gr4 gives blocks to a worker pool that *polls*
for work, with no barrier at all. Two of this project's largest wins — the Category F condvar and the
Category G ring-buffer fixes — were both repairs to that polling machinery.

### 10.25 ★ THE SEPARATED REPOS HAVE NOT TOUCHED IT — there is no "real" radio-start loop

Owner's hypothesis: the serial init loop came from FAIR, whose domain is digitisation — many
low-bandwidth sensors, where nothing cares whether detectors power up together and microseconds of
slop are irrelevant. Reasonable, and it fits. But then gnuradio.org's own people probably do it like
GR 3.x, and the active development is in the **separated** `gnuradio4-*` repos rather than the
monolith we forked. So: check those before asking the mailing list.

**Checked. There is nothing there.**

| repo | HEAD | date |
|---|---|---|
| `gnuradio4-core` | `c35f5c8` | 2026-07-13 |
| `gnuradio4-blocks` | `bfafe4a` | 2026-07-23 |

Both are more recent than our fork's base, so they *are* being worked on. But:

- **`-core`'s `Scheduler.hpp` is byte-identical to `gnuradio/gnuradio4` main.** Not similar —
  `diff` reports no differences at all. This extends `HANDOFF.md`'s "(C) is (A) repackaged" from a
  16-commit sample to the specific file in question.
- **Zero barrier mentions** in it (`grep -ci barrier` → 0).
- **Same serial `forEachBlock` start**, same `// fallback spin sleep` at line 44, same `yield()` at
  196 and 689 — identical line numbers.
- **`Scheduler.hpp` has not been touched since 2026-04-20**, and the last three commits to it are
  sonar lint fixes (`cpp:S5421`, `cpp:S3743`, `cpp:S3608`) plus "more eager message processing".
  **Nothing on the start path since April.**

**And `-blocks`' `SoapySource.hpp` is the same design as ours**, with zero barrier mentions,
`registerActivation` + `defaultIoPool()->execute([this]{ ioReadLoop(); })`.

### ⚠ It also still carries the defect that made multi-channel receive impossible

`gnuradio4-blocks/blocks/sdr/include/gnuradio-4.0/sdr/SoapySource.hpp:184`:

```cpp
soapy::detail::DeviceRegistry::registerActivation(_devKwargs, [this] {
    if (auto r = _rxStream.activate(); !r) {          // ← bare activate()
```

**That is H-1**, the defect this project found and fixed on 2026-07-27 (`DRIFT.md` Category H): a bare
`activate()` means "stream now", and UHD refuses it on a multi-channel streamer. **The actively
maintained separated repo still cannot do two-channel receive on a B2xx.**

### Conclusion: the code has no answer, so the question is worth asking

There is no alternative radio-start path hiding in the separated repos. The serial start, the absent
barrier and the polling worker are the current state of gnuradio.org's actively maintained tree, and
the SDR block there still has H-1.

**A mailing-list question is now well-founded and can be asked with citations** rather than
impressions. What it would need to establish:

1. Is the serial `forEachBlock` → `changeStateTo(RUNNING)` start intended, or has nobody exercised a
   multi-device flowgraph in gr4?
2. Is there a planned equivalent of 3.10's `scheduler_tpb` start barrier
   (`thread::barrier(blocks.size()+1)` + per-block thread), or is that considered out of scope?
3. Is `SoapySource`'s bare `activate()` known to be incompatible with multi-channel UHD streamers?

**Licence note for any port:** GR 3.10 is GPL-3.0. Its *design* may be reimplemented in this MIT
tree; its source may not be copied. The gr4 trees are MIT (`-studio` GPL-3.0), so nothing about
contributing a barrier upstream is licence-blocked.

### 10.26 ★ THE VENDORED SOAPY IS NOT "SPECIAL" — it is unmodified pothosware master

Owner asked directly whether the diffs between the "special" version we pulled and mainline SoapySDR
had ever been confirmed. **They had not.** `scripts/verify-vendor.sh` proves the committed trees
reproduce from our own HEAD — internal consistency, not agreement with pothosware.

Confirmed now, by cloning pothosware and checking out the pinned SHAs:

| tree | pinned SHA | date | vs pothosware at that SHA |
|---|---|---|---|
| SoapySDR | `1551ea0d` | 2026-01-02 | **byte-identical**, 168 files each |
| SoapyUHD | `2a5d381f` | 2025-10-05 | **byte-identical** |

**Instrument controlled**, per this project's own rule: injecting a one-character change into a
vendored file makes the diff report it, so the empty result means "identical", not "compared nothing".

The two womm patches (`cxx17-for-uhd-4.10`, `boost-190-lexical-cast`) are applied at **build time**
from `patches/womm/` (`scripts/build-prefix.sh:37`), so the vendored *source* is pristine upstream
and only the *built module* differs. There is no forked or hand-modified Soapy in this project.

### And GR 3.10 does not pin SoapySDR either

`gr-soapy/CMakeLists.txt:15` — `find_package(SoapySDR 0.7.2)`. A **minimum, not a pin.** Ours reports
`v0.8.1-g17e590ba`, ABI `v0.8-3`, which satisfies it comfortably and is the same 0.8.1 family the
distributions ship. So "the version they use" is not a specific thing in 3.10 any more than it is in
gr4 (§10.22).

**Consequence for the mailing-list question:** we are on plain upstream master of both components, so
the question cannot be deflected as "you are on a fork we do not recognise".

### ⚠ But the owner's predicted deflection (A) is still likely — and would be correct

*"this is a question for Soapy maintainers"*. The bare `activate()` is **not** in SoapySDR. It is in
**gnuradio4's own** `SoapySource.hpp:184`, calling Soapy's API without the time flag. So it is a
gnuradio4 question and can be framed that way from the start, with the file and line.

### Why nobody upstream has hit H-1 — the HackRF observation explains it

The owner: *"I think the soapySDR in GR3 can launch multiple HackRFs, so I don't see why it wouldn't
launch multiple UHDs."* It does, and that is the distinction worth being precise about:

| | what it needs | does it hit H-1? |
|---|---|---|
| **multiple devices** — 4 HackRFs, or 4 B210s | independent Soapy device objects, one per radio | **no** |
| **multiple channels on ONE device** — a B210's 2 RX | one streamer spanning both, which UHD requires be **timed** | **yes** |

**H-1 is a multi-channel problem, not a multi-device one.** HackRF and RTL-SDR have a single RX
channel each, so a maintainer testing with them can never reach it. The B2xx is the cheapest
multi-channel device in common use, and gr4's `GR4_ENABLE_SDR` is OFF by default with no CI covering
it (§10.22).

So the honest framing for a mailing-list question is not "your code is broken" but "has anyone run a
multi-channel device here?" — and the evidence says no. That also predicts deflection (B), *"what do
you mean?"*, is a genuine question rather than a brush-off.

### 10.27 ⚠ CORRECTIONS — H-1 defined, "timed" made precise, and the serial start IS the production path

Owner's review of §10.26 raised four things. Three are corrections to me.

#### (a) I was NOT saying two devices in two blocks is impossible. It works.

If the §10.26 table implied that, it was badly framed. **Four `SoapySource` blocks, four devices, one
`gr::Graph`, has run all session at 122 MS/s** (§10.2). Two devices in two blocks is not the problem
and never was.

The structure, stated the owner's way and confirmed in our code:

| case | shape | status in gr4 |
|---|---|---|
| two devices, both RX | **two blocks** | works — this is what every harness here does |
| n channels of one device | **n ports on one block** | works *now* — but see H-1 below |
| TX and RX on one device | **two blocks** (a source and a sink) | mechanism exists — `DeviceRegistry` counts `pendingUsers` per device kwargs and defers activation until all have registered (`SoapyRaiiWrapper.hpp:204-265`), with an explicit check "expected at most 2 (one Source + one Sink)" |

That last row answers the owner's open question about two blocks on one device: gr4 has a
**per-device rendezvous**. What it has no equivalent of is a **cross-device** one — which is exactly
the gap GR 3.10's `thread::barrier` fills (§10.23).

#### (b) H-1, defined — it was referenced without definition

`DRIFT.md` Category H, found 2026-07-27. **`SoapySource::start()` called `_rxStream.activate()` with
no arguments.** In SoapySDR that is `flags = 0`, and `SoapyUHDDevice.cpp:277` turns that into:

```cpp
cmd.stream_now = (flags & SOAPY_SDR_HAS_TIME) == 0;      // flags==0  ->  stream_now = TRUE
cmd.time_spec  = uhd::time_spec_t::from_ticks(timeNs, 1e9);
```

UHD then refuses the command when the streamer covers more than one channel:

> `RuntimeError: Invalid recv stream command - stream now on multiple channels in a single streamer will fail to time align.`

Reproduced outside gr4 with `SoapySDRUtil --channels="0,1"`, so it is a UHD contract, not a gr4 bug —
but gr4 had no way to satisfy it, which is the defect.

#### (c) "timed" was imprecise. Here is what it actually means.

Not a vague property. It means **`SOAPY_SDR_HAS_TIME` must be set in the activate flags**, which is
the only thing that makes SoapyUHD set `cmd.stream_now = false` and honour `cmd.time_spec`.

And the owner is right that "which clock" is a separate question. `time_spec` is
`from_ticks(timeNs, 1e9)` on the **device's** clock — whatever `time_source` was set to (internal,
external, or GPSDO). So the flag selects *timed vs immediate*; `time_source` selects *whose time*.
Two independent choices, and §10.26 blurred them.

#### (d) ★ The serial start is the PRODUCTION path, not test-harness code

The owner's hypothesis — that it might be harness code not running in a real flowgraph — is worth
settling, and the answer is no. The full chain, every link cited:

| # | code | file:line |
|---|---|---|
| 1 | `sched.runAndWait()` → `this->changeStateTo(RUNNING)` | `Scheduler.hpp:528` |
| 2 | `if constexpr (requires(TDerived& d) { d.start(); }) { … invokeLifecycleMethod(&TDerived::start, …); }` | `LifeCycle.hpp:241-243` |
| 3 | `SchedulerBase::start()` → `std::lock_guard lock(_executionOrderMutex);` then `graph::forEachBlock<TransparentBlockGroup>(*_graph, [](auto& block){ … block->changeStateTo(lifecycle::RUNNING); })` | `Scheduler.hpp:647`, `:660-675` |
| 4 | same lifecycle hook, now per block → `SoapySource::start()` | `LifeCycle.hpp:241` |
| 5 | → `reinitDevice()` (**~3.4-3.7 s**, §10.18) → `registerActivation` → `_rxStream.activate(...)` | `SoapySource.hpp:195-266` |

`LifeCycle.hpp:128` states the contract in words: *"`start()` when transitioning from INITIALISED to
RUNNING"*.

**So every `runAndWait()` on every real graph goes through this.** In the DAG framing: connect →
validate → initialise → establish connections all happen earlier; this is the final "start the
runnable objects" stage, and it is the one that is serial. There is no separate production path.

**This is the snippet set to put in front of the mailing list**, and it makes the question concrete:
not "is something wrong" but "step 3 walks blocks serially and each `SoapySource::start()` blocks for
~3.5 s — is that intended for multi-device graphs, and is a `scheduler_tpb`-style barrier planned?"

### 10.28 ★★ MEASURED, NOT INFERRED — `start()` is serial, on one thread, and it blocks

Owner's challenge: the claim that the serial start is "part of GR4" came from the same source that
also claimed UHD cannot run multithreaded and that ingest caps at ~46 MS/s — **both of which were
wrong, and both of which were configuration errors.** So the claim deserves a measurement, not a
code read. Correct, and §10.27 argued from structure without measuring.

**Instrumented `SoapySource::start()` with wall-clock entry/exit and thread id (`WOMM_TIME_START=1`),
four B210s, one `gr::Graph`, `womm_mt_test`:**

| # | ENTER | EXIT | elapsed | thread |
|---|---|---|---|---|
| 1 | `…446521000` | `…839430000` | **2.393 s** | 68432 |
| 2 | `…839463000` | `…010343000` | **2.171 s** | 68432 |
| 3 | `…010358000` | `…226462000` | **2.216 s** | 68432 |
| 4 | `…226475000` | `…509799000` | **2.283 s** | 68432 |

**All four on the same thread. Intervals disjoint and back-to-back** — each EXIT is followed by the
next ENTER within 13-33 µs. Total ~9.06 s of serial bring-up.

**So it is serial, it is on the caller's thread, and each call blocks for ~2.2 s.** Not an artefact
of reading `Scheduler.hpp`.

### It is NOT device code, and NOT a demo — which makes it worse, not better

The owner's alternative hypothesis was that this might be somebody's example flowgraph or a
test/demo hard-coded for RTL-SDR rather than core machinery. Checked:

| file | mentions of `rtl` / `soapy` / `sdr` / `usrp` / `uhd` / `hackrf` |
|---|---|
| `Scheduler.hpp` | **0** |
| `LifeCycle.hpp` | **0** |

**The start path is entirely device-agnostic.** It is the generic block-start traversal in the core
scheduler: `SchedulerBase::start()` walks every block and invokes the optional `start()` lifecycle
method that `LifeCycle.hpp` documents. It knows nothing about radios.

**The defect is therefore not "device start code is serial". It is that the core traversal is serial
and `start()` is permitted to block.** Any block with a slow `start()` serialises the whole graph.
At least ten in-tree blocks implement `start()` — `ClockSource`, `AudioBlocks`, `SignalGenerator`,
`HttpBlock`, `PythonBlock`, `BasicFileIo` and more — so this is not an SDR-only exposure.

### Answering the thought experiment: signal generator → GUI scope, no device

**Nothing blocks.** Two `samp_rate`/`sine_freq` settings, a signal source and a scope: the traversal
still visits every block and still calls `start()` where implemented, but `SignalGenerator::start()`
does not talk to hardware and returns immediately. Soapy is never involved — it is not "asked" and
does not need to know there are no devices, because nothing in the scheduler references it. The
graph runs normally.

That is the point: the serial traversal is invisible until some block's `start()` takes seconds. A
radio is simply the first block anyone has plugged in that does.

### And the owner's distinction between "RUNNING" and "data flowing" holds

`changeStateTo(RUNNING)` does two things: it sets the block's state, **and** it invokes the
lifecycle `start()` hook (`LifeCycle.hpp:241-243`). The state change is the cheap abstraction the
owner describes; the hook is where the blocking happens. In `SoapySource` the actual data flow does
**not** begin at `start()` either — `start()` ends by launching an IO thread
(`defaultIoPool()->execute([this]{ ioReadLoop(); })`), and *that* thread is where samples move. So
"all blocks RUNNING" and "data flowing" are indeed distinct, and the ~2.2 s is spent in neither: it
is `reinitDevice()`, before either happens.

**Measured answer to "is each device block RUNNING as a separate thread when it executes
`SoapySource::start()`?" — No.** All four ran on thread 68432, the scheduler's calling thread. The
per-radio IO threads are created *from inside* `start()`, after the blocking work is done.

### 10.29 ⚠ MY LIST WAS TRUNCATED — and the conclusion changes, but not the way it looks

**My error.** §10.28's "at least ten in-tree blocks implement `start()`" came from a list piped
through `head`, which cut 20 results to 10 and dropped the SDR ones. The owner reasonably inferred
from the absence that `SoapySource` was not a real in-tree block. **The premise was mine and it was
wrong.** Full list: 20 files, including `SoapySource.hpp` (#16), `SoapySink.hpp` (#15) and
`RTL2832Source.hpp` (#12).

**`SoapySource` is a `Block`, and it is in-tree upstream:**

```cpp
GR_REGISTER_BLOCK("gr::blocks::sdr::SoapySource",     …, ([T], 1UZ), […])
GR_REGISTER_BLOCK("gr::blocks::sdr::SoapyDualSource", …, ([T], 2UZ), […])
struct SoapySource : Block<SoapySource<T, nPorts>> {        // SoapySource.hpp:27
```

Registered twice, reflectable via `GR_MAKE_REFLECTABLE`, and **identical in the actively maintained
`gnuradio4-blocks`** (`struct SoapySource : Block<…>` at its `:29`). It is not an out-of-tree
throwaway.

### But the owner's DESIGN criticism is correct, and `HttpBlock` proves it

The comparison the owner drew is exactly right. `blocks/http/include/gnuradio-4.0/http/HttpBlock.hpp`:

```cpp
void openReader() {
    auto readerExp = fileio::readAsync(url.value, readerConfig());   // ← async
    …
    _reader = std::move(readerExp.value());
}

void start() { openReader(); }        // :103 — returns immediately
```

**`HttpBlock::start()` does not block on I/O.** It hands the slow work to an async reader and
returns. That is the in-tree convention.

**`SoapySource::start()` does the opposite**: it calls `reinitDevice()` synchronously and occupies
the caller's thread for **2.17-2.39 s** (measured, §10.28) before returning.

### The synthesis — this narrows the fix considerably

Both things are true at once, and neither cancels the other:

| | status |
|---|---|
| the serial traversal in `SchedulerBase::start()` | **real, measured, core, device-agnostic** (§10.28) |
| `SoapySource` being a legitimate registered in-tree Block | **true** |
| `SoapySource::start()` blocking ~2.2 s | **a violation of the convention every other in-tree block follows** |

The serial traversal only *bites* because one block breaks the convention. So there are two possible
fix sites, and they are very different in cost:

1. **Make `SoapySource::start()` async, as `HttpBlock` does** — move `reinitDevice()` onto a thread
   and let the four radios initialise concurrently. **Block-level, no core change, matches in-tree
   convention, and is the fix upstream would most plausibly accept.**
2. **Add a start barrier to the core traversal**, the GR 3.10 `scheduler_tpb` approach (§10.23).
   Core change, larger, and still wanted eventually for the *rendezvous* property — but not needed
   merely to stop serial bring-up.

**(1) is the cheap one and should be tried first.** It also reframes the mailing-list question from
"your scheduler is wrong" to "should `SoapySource::start()` defer device init the way `HttpBlock`
defers its reader?" — a question with an obvious answer that needs no architectural agreement.

**Note this does NOT explain the correlated overflow residual** (§10.4), which survives warm-up long
after any start() has returned.

### 10.30 ★ THE THREE GATING ISSUES — answered, and none is a show-stopper

Owner's proposal: do not modify the in-tree `SoapySource` (fixing an in-tree block only invites
complaint); instead wrap Soapy/UHD in a **conformant, properly threaded Block** of our own. Three
gating issues were named first.

#### Gate 1 — how many other blocks are conformant vs phoned-in?

Audited **all 20 in-tree blocks that implement `start()`**, on the two axes the owner named: does
`start()` block, and is the block **actually schedulable** (real `processOne`/`processBulk`) rather
than bypassing the scheduler with a side-channel thread.

| verdict | count | blocks |
|---|---|---|
| **conformant** — fast `start()`, real processing | **17 / 20 (85 %)** | ClockSource, SignalGenerator, FunctionGenerator, Trigger, BasicFileIo, WavBlocks, HttpBlock, PythonBlock, ExpressionBlocks, Delay, SavitzkyGolayFilter, SvdDenoiser, FrequencyEstimator, ImChartMonitor, PerformanceMonitor, TagMonitors, **AudioBlocks** |
| **not schedulable** — `work()` override + side-channel IO thread | 3 | `RTL2832Source`, `SoapySink`, `SoapySource` |
| **also blocks in `start()`** | 2 | `SoapySink`, `SoapySource` |

**Not a show-stopper — the opposite.** 85 % are fully conformant, and every non-conformant block is
an SDR block. The rot is confined to exactly the corner we care about.

**★ And `AudioBlocks.hpp` is the proof that a *device* block can be conformant:** it runs an IO
thread **and** implements `processBulk`, so it is genuinely schedulable. That is the pattern for the
replacement block — not a novel design to invent, an in-tree precedent to copy.

`SoapySource` by contrast overrides `work()` wholesale and returns `{requestedWork, 0UZ, OK}`
forever (§10.24): its IO thread writes to the output port directly, bypassing the scheduler's flow
control entirely. It is not schedulable in any meaningful sense.

#### Gate 2 — GUI status: weak, and the weakest of the three

From `UI_OPTIONS.md`, unchanged this session:

| option | status | Mac | licence |
|---|---|---|---|
| in-tree `Drawable` | **implemented but barely populated** — one consumer, `ImChartMonitor`, which draws to a **console** | yes, builds today | MIT |
| `-studio` + `-control-plane` | self-described "active prototype" | **unknown** | GPL-3.0 / MIT |
| `opendigitizer` | working but FAIR's, not a gr4 UI | **no** | LGPL-3.0 |

No ImGui/OpenGL/GLFW dependency is wired into this tree's `CMakeLists.txt` at all. **There is no
usable GUI today**, and the prior session's recommendation stands: evaluate `-control-plane` first
because it is MIT, small (24 commits), and the only unknown that matters.

#### Gate 3 — ✅ the two-node flowgraph works without a radio

`blocks/sdr/src/womm_poc_nograph.cpp` — `ConstantSource` → `CountingSink`, no device, no Soapy, no UHD:

```
two-node graph, no device: 1000000 samples in 0.108 s  (9.29 MS/s)
start-up did NOT block — graph ran immediately
real 0.80
```

**Separates "gr4 works" from "gr4 works with our radios" — which nothing else in this project did.**
It also confirms the owner's thought experiment empirically: with no device block, nothing blocks,
because `ConstantSource::start()` returns immediately and Soapy is never consulted.

### Where the replacement block would start

Owner asked the status of the Soapy this block was built against versus current SoapyUHD. Established
in §10.26: our vendored **SoapySDR `1551ea0d`** and **SoapyUHD `2a5d381f`** are **byte-identical to
pothosware master** — nothing stale, nothing forked. So a new block would target current upstream
Soapy, and the work is entirely on the gr4 side.

**Shape of a conformant SDR block**, from `AudioBlocks` (schedulable device block) and `HttpBlock`
(non-blocking `start()`):

1. `start()` returns immediately — device init deferred, as `HttpBlock` defers via `readAsync`.
2. Implement **`processBulk`**, not a `work()` override, so the scheduler actually drives it.
3. Keep the IO thread for the device, as `AudioBlocks` does — the thread is fine; bypassing
   `processBulk` is not.

**This does not close S2-3.** The correlated overflow residual (§10.4) survives warm-up and is
unexplained by any of the above.

### 10.31 ★★★ THE CONTROL PASSES — a quiet baseline exists, and S2-3 is unblocked

The owner's question — *"we're seeing overflow markers with no operations at all, just a radio tuned
to an arbitrary frequency?"* — prompted checking a combination that had never been run. Device args
had only ever been tested at **full** rate, and reduced rate only **without** device args. The
diagonal was empty.

**Filled in. It is zero.**

| aggregate rate | `num_recv_frames=1024` in device args | total overflow, whole run |
|---|---|---|
| 122.88 MS/s | no | 20, 25, 32, 24 |
| 122.88 MS/s | **yes** | 3, 4, 17, 6 |
| 61.44 MS/s | no | ~1-2 per window |
| **61.44 MS/s** | **yes** | **0, 0** |
| **30.72 MS/s** | **yes** | **0, 0** |

**Four radios, eight channels, 61.44 MS/s aggregate, tuned and streaming, no operations: zero
overflow, zero timeouts, zero of everything. Twice.**

### This reframes the residual — it is not an unexplained host stall

§10.4 and `OPERATIONS.md` A-3 described a "correlated residual" that survived every intervention and
implied one mysterious host-side event stalling all four read loops together. **That framing was
wrong, or at least badly overstated.** The behaviour is coherent and ordinary:

- it is **rate-dependent** — halving the rate removes it;
- it is **buffer-depth-dependent** — `num_recv_frames` in the right place removes it at 61.44 and
  cuts it ~3× at 122.88;
- the **correlation across radios** is what you would expect from a *shared host resource* (one
  scheduler, one thread pool) rather than four independent device faults — and it disappears
  entirely once there is enough margin.

So: the host cannot always keep up at the hardware ceiling, deeper buffering absorbs the shortfall,
and at half rate with correct buffering the margin is large enough that it never occurs. No mystery
required.

**What remains true:** at 122.88 MS/s the baseline is still not clean (3-17 per run), so the
tier-1 criterion cannot be evaluated *at the ceiling*. That is now a statement about margin at
maximum rate, not about an unexplained defect.

### Consequences

1. **S2-3 is unblocked.** There is now an operating point where the null control passes, which is the
   precondition the procedure requires (`OPERATIONS.md`). Operations under load can be measured at
   **61.44 MS/s aggregate with `num_recv_frames=1024` in device args**, and any event attributed
   there is genuinely attributable.
2. **`OPERATIONS.md` A-3 is downgraded** from "unresolved, blocking" to "insufficient margin at
   maximum rate, mitigated by buffer depth".
3. **The right place for `num_recv_frames` is now load-bearing**, not a curiosity — it is the
   difference between a measurable baseline and an unmeasurable one.

**Caveat, stated plainly:** n = 2 at each of the two clean points. Zero is a strong signal and it is
the first time the control has ever passed, but it is not yet a repeated result at the standard
§10.2 set for throughput.

### 10.32 ⚠⚠ EVERY NUMBER IN PHASE 10 WAS MEASURED ON A HEAVILY LOADED MACHINE

Owner, 2026-07-30: *"What time were you testing it? I have big CPU-intensive batch jobs running late
at night, such as now."*

**Checked, and the answer is bad.** At 05:49 on 2026-07-30:

```
load averages: 35.02 31.84 30.50          (24 cores: 16 P + 8 E)
 65.3 %  airportd
 51.7 %  Python 3.14
 43.2 %  Python 3.14
 42.0 %  Python 3.14
 37.6 %  Python 3.14
 35.2 %  Python 3.14
 33.4 %  PyCharm
```

**Load average ~35 against 24 cores — the machine was oversubscribed by roughly 50 %.**

Timestamps of this session's measurement artefacts: `mt_1.txt` 01:56, `mt_5.txt` 02:00,
`mp_31FE7A2.txt` 02:03, `mtc_1.txt` 02:05, and everything since, through 05:49. **All of Phase 10
was measured inside the owner's overnight batch window.** Machine load was never checked — not once,
in any run.

### What this invalidates

**Every measured number in Phase 10 is provisional and must be re-taken on a quiet machine:**

| § | figure | status |
|---|---|---|
| 10.2 | MT ratio mean 0.9928, range 0.9891-0.9966 (n=7) | **suspect** |
| 10.1, 10.3 | MP 9-14 overflows/radio; MT fewer-but-lower-ratio contradiction | **suspect** |
| 10.4 | the "correlated residual" | **suspect — and see below** |
| 10.18 | bring-up 3.35-3.70 s / 9.89-10.20 s | **suspect** |
| 10.19 | device args 25.3 → 7.5 mean overflow | **suspect** |
| 10.28 | `start()` 2.17-2.39 s per radio | **duration suspect; the serialisation itself is structural** |
| 10.31 | zero overflow at 61.44 MS/s | **suspect — though a clean result under load is the least fragile direction** |

### ★ And it is the most likely explanation of the thing I called a mystery

§10.4 reported four radios overflowing in **perfect lockstep** and I attributed it to "one host-side
cause stalling all four read loops together". **A competing batch load at ~1.5× core count is
exactly that cause.** It delays the scheduler's worker threads and the IO threads simultaneously, so
every radio overflows in the same window. §10.31 already reframed this as a margin problem; the
missing term was that the margin was being eaten by something outside gr4 entirely.

This also fits §10.3's contradiction — MT showing fewer overflows but a lower sample ratio — since a
host-clock-windowed ratio is directly sensitive to scheduling delay in a way an event counter is not.

### What survives

**The structural findings are unaffected**, because they come from reading code and diffing trees,
not from timing:

- the serial `forEachBlock` start and the absence of any barrier (§10.20, §10.28)
- GR 3.10's `thread::barrier` + thread-per-block (§10.23)
- vendored Soapy byte-identical to pothosware (§10.26)
- the block-conformance audit, 17/20 (§10.30)
- `SoapySource::work()` returning 0 samples forever (§10.24)
- H-1's mechanism, `stream_now` vs `SOAPY_SDR_HAS_TIME` (§10.27)
- the separated repos being unchanged (§10.25)

**Rule earned, and it belongs with the instrument lessons:** *check machine load before believing a
timing measurement, and record it alongside the result.* Every harness here reports rates and counts
and none reports the load average it ran under. That is now the obvious gap.

### 10.33 ⚠ CORRECTION — "the host can't keep up" is wrong, and I over-withdrew C-3

Owner, 2026-07-30, correcting my §10.31 phrasing *"the host can't always keep up at the hardware
ceiling"*.

#### The hardware can keep up. This is measured and it is arithmetic.

**Owner's prior-session result (2026-07-27/28, therefore authoritative under the timing rule):** four
radios, **separate processes**, synced to REF and PPS, **at full capacity, for several minutes,
repeated** — no overflow or underflow detected; per-second tags from each radio showed no drops and
no loss of lock. Mac system monitor throughout: **20-25 % user-space, kernel-space no higher than
idle.**

**And the transport has margin by arithmetic:**

| quantity | value |
|---|---|
| USB ports on the Mac Studio | 8, **each with its own bus** (6 are Thunderbolt-4 enumerating as USB-3 for these devices) |
| pessimistic per-port rate, 2023 Mac Studio, after coding and protocol overhead | **400 MB/s** |
| bytes per `complex<float>` sample | 8 |
| ⇒ pessimistic per-port sample rate | **50 MS/s** |
| both channels of one B210 at full rate | 30.72 MS/s = **~60 % of that** |

**So a fully loaded B210 uses about 60 % of its port's pessimistic capacity, and the CPU sat at
20-25 %.** The practical ceiling is not throughput — it is that there are only 8 ports and 2 are
occupied. The owner's estimate of the real limit was **10-12 more B210s**.

**My sentence was wrong.** What could not keep up in my measurements was a machine with a load
average of 35 on 24 cores (§10.32) — and possibly gr4's software path — **not the host hardware.**
"Host" conflated the machine with the software running on it, and the machine is not the constraint.
That distinction is the point of this project.

#### And C-3's "zero overflows" should not have been withdrawn as unmeasured

§10.1 withdrew it on the grounds that no counter existed and the claim came from the sample ratio.
**Checked, and that is wrong on both halves:**

- `_overflowCount` **already existed** at session start — what I added was `womm_rx_hold` *printing*
  it, not the counter itself.
- **`emitOverflowTag()` and the `rx_overflow` tag also already existed** — 4 occurrences at
  `75f9bb3`, from upstream `b4189e4` — and `womm_rx_hold`'s tag mode captures tags through `TagSink`
  with `log_tags = true`.

So the prior session had a working instrument: **per-second tags per radio, checked for drops and
loss of lock.** That is direct observation, not inference from a ratio.

**§10.1 is corrected to a narrower statement:** `womm_rx_hold` did not *print* the overflow counter,
so a run that overflowed would not have shown it in the summary line — but the tag stream would have,
and the owner reports it was watched. **C-3 stands.** My own contradicting measurement (9-14
overflows per radio, §10.1) was taken under the load documented in §10.32 and is the suspect one.

#### Consequence for (A), (B), (C)

The owner notes these come from the Ettus B210 manual as specific best-practice configurations —
authoritative source (2) and (3) under the timing rule, not suggestions:

- **(A)** request **15.000000 MS/s** rather than 15.36 — headroom rather than the absolute limit
- **(B)** set the **sample rate, not the MCR**, and read back the reported rate once settled
- **(C)** **1024-byte** buffers, better than 512 or 2048

**Status: none is properly tested.** (A) was run once at MCR 30.00/rate 15.00 *without* device args
(§10.11); (B) only as `master_clock_rate = 0`, which makes UHD choose 16 MHz at device-creation time
before any rate is requested (§10.11) — **`master_clock_rate` as a *device arg* is untested, and that
is the same wrong-place trap that made `num_recv_frames` inert** (§10.15); (C) once, at full rate,
mixed with other changes.

All three must be re-run **on a quiet machine, with load average recorded per run** (§10.32).

### 10.34 ★ QUIETER MACHINE — (A) helps, (B) is neutral, and (C) is CATASTROPHIC

Owner cleared the machine (load average **3.52** before the matrix, against **35.02** during Phase
10's original runs). Full rate, null control, `WOMM_EXTCLK=1`, two runs each, total overflow per run:

| # | configuration | runs | load pre-run |
|---|---|---|---|
| 1 | baseline — MCR 30.72 / 15.36, `num_recv_frames` in **stream** args | 20, 48 | 10.5, 10.1 |
| 2 | + `num_recv_frames=1024` in **device** args | 17, 10 | 12.4, 13.1 |
| 3 | **(A)** 15.000000 MS/s, MCR 30.00, + device args | **10, 3** | 15.0, 14.6 |
| 4 | **(B)** `master_clock_rate` as a **device arg**, rate requested separately | 7, 13 | 4.8, 10.5 |
| 5 | **(C)** + `recv_frame_size=1024` | **3616, 3683** | 8.9, 4.9 |

#### ⚠ (C) `recv_frame_size=1024` is catastrophic at this rate — three orders of magnitude worse

**3616 and 3683 overflows against 3-20 for every other configuration.** Not noise, not load: the
effect is ~300× and both runs agree.

`recv_frame_size` is **bytes per USB frame**. At 1024 bytes that is 128 `complex<float>` samples per
frame, so at 15 MS/s per channel the host must service roughly **117 000 frames per second per
channel**. The per-frame overhead dominates.

**This does not contradict Ettus** — their known-issues text recommends it *"if there are issues with
performance or stability"*, which is guidance for a constrained host or a low rate. **At 15 MS/s per
channel on this machine it is strongly counter-indicated**, and that is worth recording precisely
because it came from the manual and would otherwise be adopted on authority.

#### (A) is the best configuration measured

**15.000000 MS/s with MCR 30.000000 — 10 and 3 overflows** — the lowest pair at full-ish rate, and
consistent with the owner's reasoning that running at the absolute limit (15.36 = MCR/2 exactly,
decimation 2) invites trouble while a little headroom costs almost nothing. **0.6 % less throughput
for a large reduction in overflow.**

#### (B) is neutral here, but the mechanism was worth testing

Passing `master_clock_rate=30e6` in **device args** and requesting the sample rate separately gave
7 and 13 — indistinguishable from (A) at this sample size. It does **not** reproduce the §10.11
failure where `master_clock_rate = 0` let UHD pick 16 MHz at device-creation time, because the device
arg is present at `make()`. So the mechanism works as documented; it simply does not beat setting it
explicitly.

### ⚠ Two honest caveats

**My load readings were partly self-inflicted.** `load_pre` values of 10-15 in configs 1-3 are the
decaying tail of *my own previous run* — a 12 s gap is far too short for a 1-minute average. Configs
4 and 5 used a 75 s settle and show `load_pre` of 4.8-10.5, closer to the machine's true idle. The
(C) result is immune to this: no plausible load explains 300×.

**Full rate is still not clean, even quiet.** The best configuration measured (A) gives 3-10
overflows per run. Only **61.44 MS/s** has ever produced zero (§10.31). So the tier-1 criterion
remains evaluable at half rate and not at the ceiling — and that conclusion now survives on a
machine an order of magnitude quieter than the one that produced §10.4.

### 10.35 ⚠ `max_chunk_size` DOES NOT DO WHAT IT SAYS — and other answers from source

Hardware testing **stopped** at the owner's direction: unknown parameter mappings are not something to
resolve empirically on four B210s. Everything below is read from source.

#### ★ The find: `max_chunk_size` is a setting that lies

```cpp
Annotated<std::uint32_t, "max_chunk_size", Doc<"max samples per read (ideally N x 512)">, Visible, …>
    max_chunk_size = 512U << 4U;                                     // SoapySource.hpp:68
```

Documented as *"max samples per read"*, marked `Visible` (so a UI would show it) — **and the read
loop does not use it.** Both read paths size their buffer from a hard-coded constant:

```cpp
constexpr std::size_t kReadSize = 512UZ * 16UZ;        // :317 single-port, :407 multi-port
std::vector<T> readBuf(kReadSize);
_rxStream.readStream(flags, time_ns, max_time_out_us, std::span<T>(readBuf));
```

`max_chunk_size` appears **once** in the whole file (`:986`), computing the *update rate of the ppm
estimator* — and only when `ppm_estimator_cutoff > 0`, which defaults to 0. **So in the default
configuration it does nothing at all.**

**Not ours.** Identical in upstream `gnuradio4-blocks` (`kReadSize` at its `:221`/`:228`, same three
`max_chunk_size` occurrences). Every harness here sets it; none of them changes the read size.

**Applies to the B210 as much as anything else** — it is device-independent, because it never reaches
the device.

#### Answers to the owner's questions

**Q1 — reason not to leave `num_recv_frames` unset?** No good one. UHD picks a default when it is 0.
Our evidence for overriding it (§10.19, ~3× fewer overflows) was taken on a machine at load average
35 (§10.32) and is suspect. Under "guess fewer parameters", **unset is the defensible default** until
it is re-measured on a quiet machine with a reason.

**Q2 — do not set both sample rate and MCR.** ⚠ **We set both.** `applyClockConfig()` calls
`setMasterClockRate()` (`:764`) and `applySampleRate()` calls `setSampleRate()` (`:783`), on every
start. The owner reports Ettus recommends setting **one**, and the sample rate. This is a live
defect in our configuration, not just a style point, and §10.11's 16 MHz surprise is the same
mechanism seen from the other side.

**Q3 — where is `$WOMM_DEV_ARGS` defined?** **Nowhere.** There is no file that defines the `WOMM_*`
variables; they are ad-hoc `getenv()` calls in the harnesses. 29 of them exist. **19 are documented
nowhere at all**, including `WOMM_DEV_ARGS`, `WOMM_MCR`, `WOMM_FREQ`, `WOMM_GAIN`, `WOMM_SERIAL`,
`WOMM_CHUNK`, `WOMM_START_OFFSET` and both barrier variables. Only 10 appear in `OPERATIONS.md`.
`scripts/env.sh` defines only `WOMM_ROOT` and `WOMM_PREFIX`.

**Q4 — documentation for `stream_args`?** This, in full:

```cpp
Annotated<std::string, "stream_args", Doc<"SoapySDR stream kwargs (comma-separated key=value)">>
```

It does not say which keys are valid, that they land in `uhd::stream_args_t.args` via
`get_rx_stream`, or that transport parameters put there are **inert** (§10.15). `tune_args` and
`device_settings` have equally thin one-liners.

**Q5 — `max_chunk_size`:** see above. It corresponds to nothing on the device.

**Q6 — valid `clock_source` / `time_source` values.** The Doc strings say *"e.g. internal, external,
gpsdo"* and *"e.g. external, gpsdo"* — "e.g.", not an enumeration. **The device is authoritative**,
and reports them: `uhd_usrp_probe` on our B210s gives

```
Time sources:  none, internal, external, gpsdo
Clock sources: internal, external, gpsdo
```

Note `time_source` accepts **`none`** and `clock_source` does not. Nothing validates these in
`SoapySource`; an invalid string reaches `set_clock_source` and the existing comment at `:673` warns
that selecting a reference which is not physically present **does not error** — the device just
free-runs.

#### Q7 — what "UNKNOWN" means, recorded because it is easy to misread

Owner, 2026-07-30. **`UNKNOWN_PPS` does not mean "the PPS state is unknown". It means the timing
source is unnamed** — "unknown" is used as a *pronoun*.

External clock and GPS devices such as the Octoclock-G connect through the same SMA connectors as
antennas and carry **analogue timing signals**, not digital coded packets. The radio can tell that a
PPS is present, that a 10 MHz reference is present, and that GPS data is embedded in those signals
(passed straight through from the satellite constellation) — but **the clock device deliberately does
not stamp an identifier onto those signals**, because anything added could be mistaken for noise.
Hence the convention: the source is real and detected, merely *unnamed*.

By contrast **`gpsdo` means a built-in GPS-disciplined oscillator** — bolted or soldered to the
motherboard. It carries no name information either, for the same reason; it is distinguished by being
internal rather than by announcing itself.

**Why this matters here:** it reframes `set_time_unknown_pps()` as the *normal* call for any external
timing device, not an exceptional or fallback one — which is consistent with `HANDOFF.md` C-9 finding
it to be the multi-device primitive, and with §10.34's "(B) is neutral" result.

### 10.36 ⚠ CORRECTION TO §10.35 Q7 — "unknown" means the PPS EDGE is unknown, not the source

I recorded the owner's explanation of `UNKNOWN_PPS` in §10.35 Q7 without checking it against the
manufacturer's own documentation, which the timing rule ranks as authoritative source (2)/(3).
**UHD's header states a different meaning**, and it is unambiguous.

`/opt/homebrew/include/uhd/usrp/multi_usrp.hpp:302-318`, verbatim:

> *Synchronize the times across all motherboards in this configuration.*
> *Use this method to sync the times when **the edge of the PPS is unknown**.*
> *Ex: Host machine is not attached to serial port of GPSDO and can therefore not query the GPSDO for
> the PPS edge.*
> *This is a 2-step process, and will take at most 2 seconds to complete.*
> - *Step1: wait for the last pps time to transition to catch the edge*
> - *Step2: set the time at the next pps (synchronous for all boards)*

**"Unknown" qualifies the host's knowledge of the PPS edge phase, not the identity of the timing
source.** The contrast is with `set_time_next_pps`, whose own doc warns (`:284-288`): *"Make sure to
not call this shortly before the next PPS edge… timekeepers could be unsynchronized in time by
exactly one second. If in doubt, use set_time_unknown_pps()."*

So the two calls differ by **what the host knows about where it is in the second**:

| call | precondition | cost |
|---|---|---|
| `set_time_next_pps` | host knows it is *not* near an edge — e.g. it can query a GPSDO over serial | immediate, but risks a **1 s** misalignment |
| `set_time_unknown_pps` | host does **not** know the edge phase | 2-step, **≤ 2 s** |

### What survives from the owner's account, and what does not

**Does not:** the reading of "unknown" as a pronoun for an unnamed source. UHD's example is explicitly
about a *GPSDO* — a named, built-in device — being unqueryable because the host lacks a serial
connection to it. The word is about phase knowledge, not identity.

**Does survive, and is worth keeping:** the underlying physics. External clock and GPS devices do
connect over SMA carrying analogue timing signals with no embedded identifier, and that is *why* the
host cannot know the edge phase without a separate side-channel — which is precisely UHD's serial-port
example. The owner's mechanism is right; it explains the naming rather than being the naming.

**And the owner's other hypothesis — that someone renamed "unknown" to "external" — does not hold.**
They are different axes, and both apply to us simultaneously:

| axis | our value | what it says |
|---|---|---|
| `time_source` | `external` | *where the PPS comes from* — the Octoclock, over SMA |
| `setHardwareTime(what)` | `UNKNOWN_PPS` | *what the host knows about the edge phase* — nothing |

`listTimeSources()` forwards to UHD's `get_time_sources(0)` and the device answers `none, internal,
external, gpsdo`. **`unknown` has never been among them.**

### Which also corrects a prior-session line

`RESULTS.md:1170` states *"the only PPS string in `libuhdSupport.so` is `UNKNOWN_PPS`, **which is a
time_source value**, not a stream argument."* The second half is right; **the first half is wrong** —
it is the `what` argument to `setHardwareTime`, not a `time_source` value. That line is upstream of
the "the whole sync path is plumbing, not capability" conclusion, which is unaffected, but the
misattribution should not propagate.

**Confirms C-9 with its mechanism now cited from the vendor rather than inferred:** `UNKNOWN_PPS` is
the multi-device primitive *because* step 1 waits for a transition, which is what puts every radio on
the same edge. And UHD's "≤ 2 seconds" is the manufacturer's own figure for the cost §10.18 measured.

### 10.37 ⚠ CORRECTION TO §10.30 — `AudioBlocks` is NOT a conformant device source

§10.30 claimed *"`AudioBlocks.hpp` is the proof that a **device** block can be conformant: it runs an
IO thread **and** implements `processBulk`."* **Wrong, and the error was file-level grep
contamination** — that file declares two structs, and the `processBulk` I found belongs to
`AudioSink`, not `AudioSource`.

Read properly:

| block | `work()` override | `processBulk` | IO threads |
|---|---|---|---|
| `AudioSource` | **1** | **0** | 2 |
| `SoapySource` | **1** | **0** | 1 |
| `RTL2832Source` | **1** | **0** | 1 |

**Every in-tree device source uses the `work()`-override plus IO-thread pattern. There is no
scheduler-driven device source anywhere in the tree.** The owner's independent assessment —
*"I would not count on the AudioBlocks being the standard for quality, it's been looked at"* — agrees.

This is the **third** error of one class today, after the truncated block list (§10.29) and the
truncated `getStreamArgsInfo` (`SOAPY_UHD_MAP.md` §7), plus an `apply*` scan whose window overran
into neighbouring functions. The mechanism each time: **reading part of a thing and reporting it as
the whole.** Recorded as a standing hazard, not an apology.

### ★ But the pattern IS expressible, and `HttpBlock` demonstrates it

Output-only `processBulk` sources exist in-tree: `BasicFileIo`, `ClockSource`, **`HttpBlock`**,
`NullSources`, `WavBlocks`, `TagMonitors`. **`HttpBlock` is the one that drains an external
asynchronous resource**, which is structurally what a radio does:

```cpp
[[nodiscard]] work::Status processBulk(OutputSpanLike auto& outSpan) {
    if (outSpan.empty()) { return work::Status::INSUFFICIENT_OUTPUT_ITEMS; }
    _reader.poll(/* non-blocking */ …);
    outSpan.publish(nSamplesToPublish);          // publish what arrived — possibly 0
    return finished ? work::Status::DONE : work::Status::OK;
}
```

Four properties, and they are the whole specification for a device source:

1. **Never blocks** — polls, publishes what is there, returns.
2. **Publishes a variable count**, including zero, without treating that as an error.
3. **Guards on `outSpan.empty()`** with `INSUFFICIENT_OUTPUT_ITEMS` rather than spinning.
4. **`start()` is one line** and defers the slow work (`readAsync`).

So the `work()`-override pattern in all three device sources is **convention, not necessity.** That
is the finding that makes a conformant replacement worth designing rather than merely wished for.

### 10.38 ★ THE UI LICENCE BOUNDARY IS CLEAN — and it is better than `UI_OPTIONS.md` recorded

Checked before forking, at the owner's decision to fork GRStudio.

| component | licence | links into our C++? |
|---|---|---|
| `gnuradio4-studio` top level — the React/Vite app | **GPL-3.0** | **no** — separate process, browser or desktop |
| `gnuradio4-studio/blocks/` — the C++ Studio blocks | **MIT**, its own `LICENSE`, © 2026 Josh Morman, Altio Labs LLC | **yes**, and it is fine |
| `gnuradio4-control-plane` | **MIT** | yes, and it is fine |

**Someone deliberately dual-licensed this.** `blocks/` carries a *separate* MIT `LICENSE` file inside
a GPL-3.0 repository, precisely so the C++ that links into a GR4 runtime is not GPL. That is the
distinction that matters for us:

- **Forking Studio is safe.** The fork stays GPL-3.0, which costs nothing — it is a standalone
  application, not something we link.
- **The GPL does not reach our tree.** Studio talks to the control plane over **REST, across a
  process boundary**. No linkage, so no propagation. Our MIT tree is unaffected.
- **The useful C++ is MIT and may be used directly** — the Studio sink blocks (series, 2D series,
  dataset, power spectrum, waterfall, scalar/status, audio, image) and the whole control plane.

**Correction to `UI_OPTIONS.md`**, which recorded Studio as *"self-described active prototype"* and
its licence as a caution. The prototype label is the project's own, but the state is further along
than that reads: React 18 + Vite 6, `uPlot` plotting, browser and desktop, 40 commits, HEAD
2026-07-12; live panels for time series, XY, power spectra, phosphor spectra, waterfall, images and
audio; `.gr4s` documents; session lifecycle through the control plane. Control plane HEAD 2026-06-05.

**And the architecture is the one this project independently arrived at.** `UI_OPTIONS.md` argued a
separate-process control plane is *"the structural answer to GUI/radio coupling"*. That is exactly
what Studio does — and it means the GUI cannot stall the radio thread, because it is not in the same
process. Which bears directly on D8's *"no unanticipated overflow during… graphical display / UI
operations"*: the coupling that criterion worries about is architecturally absent.

**Still unknown, and it is the gate:** whether the control plane builds and runs on macOS. It is MIT,
small, and auditable — `UI_OPTIONS.md`'s recommendation to evaluate it **before** Studio still holds,
because if it does not build, Studio has nothing to talk to.

### 10.39 ★★★ THE CONTROL PLANE BUILDS AND RUNS ON macOS — the UI gate passes

`UI_OPTIONS.md` named this the **only unknown that matters**: *"if the C++ control plane does not
build and run against our macOS GR4 install, Studio is moot."* Tested at the owner's direction.

**It builds. 96 % of its tests pass. The gate is open.**

#### Step 1 — gnuradio4 installs cleanly on macOS, which was never tried before

Our tree has always been **built and never installed**. `cmake --install build-fixed --prefix
../gr4-install` succeeds and produces the full package: `gnuradio4Config.cmake`,
`gnuradio4Targets.cmake`, `gnuradio4PluginTargets.cmake`, `lib/pkgconfig/gnuradio4.pc`, headers, and
the plugin `.dylib`s.

That is the **installed-SDK boundary** `HANDOFF.md` lists as worth harvesting from tree (C). It works
here, today, with no changes.

#### Step 2 — the control plane configures against it

```
-- Found nlohmann_json: 3.12.0     -- Found Boost: 1.90.0     -- Found ZLIB: 1.2.12
-- Building with GNU Radio 4-backed block catalog from …/gr4-install/lib
```

One dependency was missing — **GTest**. Installed via brew with the owner's explicit approval under
I-4; the dry run showed *"Would install 1 formula: googletest"*, no dependencies and no transitive
upgrades. **`find_package(GTest CONFIG REQUIRED)` is unconditional**, so it is needed even for a
server-only build; there is no option to skip it.

#### Step 3 — it builds, producing `gr4cp_server` and `gr4cp-cli`

Clean build at `-j16`, Release, Apple clang 21. No `-Werror` suppression needed
(`GR4CP_SUPPRESS_IMPORTED_WERROR` left OFF).

#### Step 4 — tests: 131 of 137 pass, one hangs

| | result |
|---|---|
| **passed** | **131 / 137 (96 %)** in 5.4 s |
| failed | 6 |
| hung | 1 — `HttpApiTest.BrowserFacingWebsocketRouteUpgradesAndProxiesFrames`, killed after **20 minutes** |
| skipped | ~13, mostly `Gr4RuntimeManagerTest` and `RealRuntimeHttpApiTest` |

**The hang is a real finding**, not slowness: a browser-facing WebSocket proxy route that never
completes on macOS. Excluded with `-E Websocket` to get the rest; **it will matter, because that
route is how Studio streams live data to a browser.**

**The 6 failures look like version skew, not platform trouble.** All are block-catalogue tests, and
the representative failure is a port-count mismatch:

```
http_api_test.cpp:1088: Expected equality of these values:
  body["inputs"].size()   Which is: 1
  2U                      Which is: 2
```

The catalogue reflects **our** gnuradio4, which has drifted from the upstream the control plane's
tests were written against — five fair-acc cherry-picks plus our own patches (`DRIFT.md`).
**Stated as the likely explanation, not a confirmed one:** a port-count disagreement is consistent
with reflection skew, and nothing here points at macOS.

#### What this unlocks, and what it does not

- **Studio has something to talk to.** `UI_OPTIONS.md`'s gate is passed, so evaluating Studio itself
  is now worth doing rather than speculative.
- **The separate-process architecture is real on this machine** — which is the structural answer to
  D8's *"no unanticipated overflow during graphical display / UI operations"*: a GUI in another
  process cannot stall the radio's threads.
- **Not yet done:** running `gr4cp_server` and driving it; the WebSocket hang; whether the 6 failures
  are skew or substance; and Studio's own build, which is Node/React and untried.

### 10.40 ⚠ REFINEMENT TO §10.39 — the WebSocket hang is NOT the upgrade path, and NOT egress

Owner asked whether the hanging test is loopback or something that would be blocked as arbitrary
network egress. **Checked, and it is loopback — but the useful answer came from the control.**

#### Egress is ruled out at the source

`test/http_api_test.cpp:766-771`:

```cpp
const auto endpoint = resolver.resolve("127.0.0.1", std::to_string(port));
beast::get_lowest_layer(stream).connect(endpoint);
stream.handshake("127.0.0.1:" + std::to_string(port), path);
```

**Pure loopback.** The server binds locally (`src/main.cpp:57`) and the client connects to
`127.0.0.1`. Nothing leaves the machine, so no firewall or egress policy is involved.

#### The control settles it: networking is fine

| run | result |
|---|---|
| all **non**-WebSocket `HttpApiTest` — same bind, same loopback connect | **63 passed**, 2 failed (both catalogue) |
| all `*Websocket*` tests, one process | **9 passed, 0 failed**, then one hang |

**So binding a listening socket works, loopback connect works, HTTP works, and the WebSocket upgrade
and frame proxying work** — nine of them, including `BrowserFacingWebsocketRouteUpgradesAndProxies…`,
which is the very test ctest hung on. **§10.39's characterisation was too broad.**

#### Where it actually hangs, and the hypothesis

`HttpApiTest.RestartInvalidatesOldWebsocketBindingAndNewRouteUsesNewGeneration` — a **restart and
re-binding** test, not an upgrade test. Its siblings in the hang neighbourhood are all of that shape:
`RestartInvalidatesOld…Binding…`, `…BindingReleasedBeforeStartingNextGraph…`,
`StopFailureLeaves…BindingsUnreleased…`.

**Hypothesis, well-supported but not confirmed: port release and re-bind on macOS.** Releasing a
listening socket and immediately re-binding behaves differently on macOS than Linux
(`SO_REUSEADDR`/`TIME_WAIT` semantics), and this suite is CI-tested on Linux only. Consistent with
the extra evidence that **under `ctest` — where each test is a fresh process — a *different* test
hung**, which is what you would expect if the failure depends on binding state rather than on the
test's own logic.

**What this changes for Studio:** live data streaming to a browser is **not** the broken thing —
that path passes. What is unproven is **session restart**, which Studio does whenever you stop and
re-run a graph. Serious, but narrower and more tractable than "WebSockets are broken on macOS".

### 10.41 ★ THE SERVER RUNS AND REFLECTS OUR BLOCKS

`GR4CP_PORT=18099 ./build/gr4cp_server`, ~6 s to start:

```
GET /healthz   ->  {"ok":true}
GET /blocks    ->  [{"category":"good","id":"good::cout_sink<float32>",
                     "inputs":[{"cardinality_kind":"fixed","current_port_count":1,
                                "max_port_count":1,"min_port_count":1,"name":"in",
                                "type":"float32"}], …
```

**The catalogue is live reflection of *our* gnuradio4 build** — port names, types, cardinality
(fixed vs dynamic), and parameter defaults. Not a static list.

So the full chain works on macOS: **our tree → `cmake --install` → control plane → HTTP → block
catalogue with reflected metadata.**

### ⚠ Two things to know before leaving it running

**1. It binds `0.0.0.0`, hard-coded.** `src/main.cpp:57` is `server.listen("0.0.0.0", port)` — a
literal. `GR4CP_PORT` changes the port; **nothing changes the interface.** The API is
**unauthenticated** and `POST /sessions` creates and runs flowgraphs, so anything that can reach the
port can run a graph on this machine. Options: firewall the port, or change the literal to
`127.0.0.1` in a fork — a one-line change, and the natural first commit if the control plane is
forked.

**2. Session restart may hang** (§10.40). Studio restarts a session whenever a graph is stopped and
re-run, so that is the path most likely to bite in normal use.

### Where the UI question now stands

| step | status |
|---|---|
| gnuradio4 installs on macOS | ✅ §10.39 |
| control plane builds | ✅ §10.39 |
| control plane tests | ✅ 131/137; restart/rebind hangs §10.40 |
| **server runs and reflects our blocks** | ✅ **this section** |
| Studio's own build (Node/React) | **untried** |
| Studio driving a live graph end to end | **untried** |

`UI_OPTIONS.md`'s gate — *"if the C++ control plane does not build and run against our macOS GR4
install, Studio is moot"* — **is passed.** Studio is now worth trying rather than speculative.

### 10.42 ⚠ THE HANG IS PROBABLY THE macOS FIREWALL PROMPT — my rebinding hypothesis weakened

Owner: *"there's a nonzero chance that there's a one-time blocker pop-up for running a new app the
first time, if it puts up a web socket or GUI window."*

**Checked, and the evidence supports this over §10.40's rebinding hypothesis:**

```
Firewall is enabled. (State = 1)
Automatically allow built-in signed software ENABLED.
Automatically allow downloaded signed software ENABLED.
--listapps | grep gr4cp   ->  (not listed)
```

The application firewall is **on**; auto-allow covers only **signed** software; our binaries are
**locally built and unsigned**; and **none of them is in the allow list.** A first bind-and-accept by
an unlisted unsigned binary is exactly what raises the one-time *"accept incoming network
connections?"* dialog — and an unanswered dialog **hangs the accept**, it does not fail it.

**And I cannot see or answer that dialog from here**, which makes the hang plausibly an artefact of
how I am running the tests rather than a property of the code.

**It also explains the detail §10.40 could not:** why a *different* test hung under `ctest` than in a
single process. Each `ctest` case is a fresh process, and the prompt is per-binary-per-approval — so
which test stalls depends on when the dialog appears, not on the test's logic. My rebinding
hypothesis explained the neighbourhood but not that.

**Discriminating test, for the owner and not for me:** run `./build/gr4cp_http_test
--gtest_filter='*Websocket*'` in an interactive session. If a dialog appears and clicking **Allow**
lets the suite continue, the hypothesis is confirmed and there is no macOS defect here at all.

### 10.43 ★★ STUDIO BUILDS — the whole UI stack works on macOS

| step | result |
|---|---|
| toolchain | node **v22.14.0**, npm **11.3.0** — Vite 6 needs ≥18, so **no npm update needed** |
| `npm install` | **355 packages, 3 s** |
| `npm run build` (`tsc -b && vite build`) | ✅ **382 modules, 1.80 s** → `dist/index.html`, 55 kB CSS, 957 kB JS |

**So the entire chain is proven on this machine:** our gnuradio4 → `cmake --install` → control plane
(builds, 131/137 tests, runs, reflects our blocks) → Studio (installs, builds).

**`npm audit`: 9 vulnerabilities, 1 critical, 7 high** — but every one traces to a **dev dependency**:
`vitest` (critical), `vite`, `postcss`, `react-router-dom`, `brace-expansion`, `js-yaml`, `picomatch`.
These are build- and test-time packages, not shipped in the bundle. Worth an `npm audit fix` at some
point; **not a blocker and not a runtime exposure.**

**Still untried:** Studio actually driving a live graph through the control plane end to end — which
is where §10.42's prompt and §10.40's restart path both sit.

### 10.44 ★★★ THE HANG DIAGNOSED — a shutdown deadlock in `WebSocketBridge`, not the firewall

Owner ran the suite interactively and got **further than I did** — 12 tests passed, including
`RestartInvalidatesOldWebsocketBindingAndNewRouteUsesNewGeneration` **in 114 ms**, the exact test
that hung for me. It then stopped at `StopClosesExistingWebsocketConnectionsDeterministically`.

**Three runs, three different hang points** — `BrowserFacing…` under ctest, `RestartInvalidates…` in
my process, `StopClosesExisting…` in his. A moving hang point is a **race**, not a prompt.

Sampled the live hung process (`sample <pid>`), and the stack is unambiguous:

| thread | where it is blocked |
|---|---|
| **main** | `HttpServer::Impl::join_connection_threads()` → `std::thread::join()` → `__ulock_wait` |
| **connection** | `handle_connection` → `handle_websocket_upgrade` → **`WebSocketBridge::run()`** → `std::thread::join()` → `__ulock_wait` |
| **forwarder** | **`WebSocketBridge::forward(…)`** → `websocket::stream::read_some(…)` → `socket_ops::sync_recv` → **`poll()`** |

**A three-level join chain ending in a blocking read nobody will satisfy.** Main waits for the
connection thread; the connection thread waits inside `WebSocketBridge::run()` for the forwarder; the
forwarder sits in a **synchronous** `read_some` on a peer that has stopped sending.

**The defect: shutdown joins the threads without first cancelling or closing the socket the forwarder
is blocked on.** Nothing breaks the `poll()`. The test is named
`StopClosesExistingWebsocketConnections**Deterministically**`, which is precisely the property that
does not hold.

#### Both of my hypotheses are withdrawn

- **§10.40 "port release and re-bind on macOS"** — wrong. The rebinding tests **pass** (114 ms, 99 ms,
  123 ms in the owner's run).
- **§10.42 "the macOS firewall prompt"** — wrong, and it was my own reasoning from circumstantial
  evidence (firewall on, binaries unsigned and unlisted). The accept succeeded, the upgrade
  succeeded, frames were proxied; the deadlock is **after** all of that, in shutdown. The owner
  reports no dialog. **The firewall state I measured was true and irrelevant** — a fact that fitted
  the symptom without causing it.

That is three wrong explanations for one symptom in a row, each plausible, each dismissed only by a
better measurement. **Sampling the live process took seconds and settled what an hour of inference
did not.**

#### Why it plausibly bites macOS harder

Closing a file descriptor that another thread is blocked in `poll()` on is **not portable**: on Linux
it will often wake the poller with an error; on macOS/BSD the behaviour is unspecified and frequently
does **not** wake it. A shutdown path that relies on close-to-interrupt therefore works in Linux CI
and deadlocks here. **Hypothesis, consistent with the stack, not verified** — and it barely matters,
because on this evidence nothing closes the socket at all before the join.

#### Consequence for Studio

**This is a real blocker for the intended workflow.** Studio stops and restarts sessions routinely,
and stopping a session with a live browser WebSocket is exactly the path that deadlocks. Expect the
server to wedge on the first stop, holding its port — the "first stop-and-rerun is the interesting
moment" prediction was right for the wrong reason.

**Fixable, and narrowly:** cancel or close the bridge's sockets *before* joining, or use asynchronous
reads with a cancellation signal instead of `sync_recv`. It is a shutdown-ordering bug in one class,
not an architectural problem.
