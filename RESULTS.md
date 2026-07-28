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
