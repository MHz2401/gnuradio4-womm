# HANDOFF_SUPPLEMENT — reconciliation with prior systems, and a correction

Companion to `HANDOFF.md`, which stands as written. This exists because the owner supplied
performance figures from two systems he built previously, and **those figures contradict the
headline conclusion of §8.6 in `RESULTS.md`.** That contradiction is the most useful thing in this
document; everything else is supporting detail.

Written 2026-07-27, at the end of the session that produced Phases 6-8 of `RESULTS.md`.

---

## 1. Units — answering the question directly, because I was inconsistent

The owner asked whether our Msps figures are complex or real. They are **both, in different
places, and I did not label them consistently.** That is a defect in my reporting, not in the
measurements.

| figure | type | file |
|---|---|---|
| **2416 Msps** DSP peak | **real `float`** | `core/benchmarks/womm_bm_scaling.cpp` (`using T = float`) |
| **~44 MS/s** radio ingest cap | **`std::complex<float>`** | `womm_b210_sweep.cpp`, `womm_bmax.cpp` |

One `complex<float>` is two floats, so as raw data movement 44 MS/s complex = **88 M
float-equivalents/s**. Note that complex *arithmetic* is worse than 2x real: a complex multiply is
four real multiplies plus two adds, and SIMD lanes halve. So a complex chain costs roughly 3-4x a
real one per sample, not 2x.

**Every future figure should carry its type.** A bare "Msps" in this project has meant two things.

### Answers to the four numbered questions

1. **Mixed, as above** — the DSP number is real float, the radio number is complex.
2. **Yes, you are counting correctly.** 6 channels x 20 MHz = **120 MS/s complex**, and that is
   directly comparable to our ~44 MS/s complex. **MCM is 2.7x our measured ceiling.**
3. **240 M float-equivalents/s** is the right figure to set against the 2416 Msps real-float DSP
   number. The ratio is unchanged; only the units differ.
4. **Your 4th-radio expectation is consistent with the rest.** If MCM ran 120 MS/s complex without
   strain, it was not near a hardware limit, and ~160 MS/s complex is a reasonable extrapolation.

---

## 2. ⚠ THE CORRECTION — MCM refutes "the cap is a kernel/USB limit"

`RESULTS.md` §8.6 concluded that the ~40-44 MS/s aggregate ingest cap "sits below the process
boundary" and that "no amount of gr4 tuning will move this number", on the strength of three
separate processes hitting the same ceiling as one.

**That conclusion does not survive contact with the MCM figures.** The MCM ran **120 MS/s complex
across three B210s on this same machine**, under GNU Radio 3.9 and Python, with three independent
flowgraph processes. If the macOS USB stack or an OS-level limit capped aggregate ingest at ~44
MS/s, the MCM could not have existed.

**So the cap is ours, not the platform's.** The exclusion logic in §8.6 was sound as far as it went
— separate processes really do have separate UHD/SoapyUHD instances and separate library locks —
but it was reasoning from absence within a single family of configurations. One data point from
outside that family invalidates the conclusion. **This is the correct reading; §8.6's "not a gr4
defect" line should be treated as withdrawn.**

### What I should have tested and did not

The single most likely confound, in hindsight, is that our radio harness carries **eight
multiply/divide pairs per radio chain** while the MCM did essentially no per-sample arithmetic —
it vectorised into 4k blocks, tagged, and published to ZMQ.

We never ran three radios at **depth 0**. We ran:

- 1 radio, depth 0, at 4 and 16 MS/s — both fine, never pushed to find its ceiling
- 3 radios, **depth 8** — the ~44 MS/s cap

So **"ingest cap" may be a misnomer for what is actually a complex-arithmetic DSP-and-scheduler
cap at depth 8.** That single experiment — 3 radios, `kRadioDepth = 0`, rate swept — would settle
it and takes minutes. It is now the first item in §5 below.

Supporting arithmetic, which makes this plausible rather than certain: our 2416 Msps is *real*
float through 16 blocks. Converting for complex cost (~3-4x per sample) puts a complex chain
somewhere near 600-800 Msps through the same depth. Three radios at 44 MS/s through 16 blocks is
only ~132 MS/s of complex traffic — comfortably under that estimate, which argues *against* the
DSP explanation. But the estimate is a back-of-envelope conversion between two different
benchmarks, and the MCM number is a measurement. **Trust the measurement; run the experiment.**

---

## 3. What the MCM and The Abomination say about coupling

The owner's conclusion from GR 3.x was that "separate processes and threads are not actually
separated", evidenced most sharply by The Abomination: buffer overflow triggered by **resizing a
GUI slider widget** — not by changing the value it controlled. Meanwhile the MCM, which ran heavier
graphics in *separate* processes, stayed clean.

Our GR4 data both supports and complicates that:

- **Supports:** something couples work that should be independent. Three radios in one gr4 process
  cap at the same aggregate as one radio, and dividing the rate differently does not help.
- **Complicates:** our three-*process* test hit essentially the same ceiling (40.89 vs 43.83
  MS/s). If the coupling were inside gr4's process, separate processes should have escaped it. They
  did not — but per §2 the MCM shows separate processes *can* reach 120 MS/s on this machine, so
  our three-process result is more likely explained by a shared per-process limitation we carried
  into all three (same harness, same depth-8 chain, same settings) than by a platform limit.

**The honest statement is that we have not yet demonstrated the "ground-up implementation removes
hidden interprocess dependencies" claim, and we have not refuted it either.** What we have is a
ceiling we cannot yet attribute.

### The MCM's "secret sauce" is a design rule worth adopting

> All GR parts were written assuming GR behaves as a single-threaded, single-process system, with
> no blockers in the path — e.g. the ZMQ PUB node does not block.

That rule is directly applicable here and is not currently followed by our harness. It also aligns
with something measured independently this session: gr4's multi-threaded workers **never back off**
(`Scheduler.hpp:797` gates the progress-blocking path on `singleThreadedBlocking`, which is
compiled out for `multiThreaded`). A starved gr4 worker spins at full rate. Any blocking call
placed in a gr4 processing path therefore burns a core rather than yielding it.

---

## 4. Oversubscription — the owner's joblib/loky observation, and ours

The owner notes that under joblib/loky a worker blocked on I/O is not released to its *siblings*,
though it does yield to external processes: "the blocked processor becomes available to anyone
except the job that launched it." For a real-time system that is precisely backwards.

**We measured the gr4 analogue of this, and it is severe.** gr4's default CPU pool is
`hardware_concurrency()` **per process** — 24 here. Three gr4 processes therefore claim **72
workers on 24 cores**, and because those workers spin rather than block, the result is not graceful
degradation:

| 3 radios, 3 processes | aggregate |
|---|---|
| default pools (24 threads each = 72) | **3.54 MS/s** |
| explicitly sized (8 threads each = 24) | **40.89 MS/s** |

**A 12x throughput loss from thread accounting alone.** This is a deployment landmine, not a
benchmark artefact: any multi-process gr4 design oversubscribes the machine by the process count
unless every process sizes its pool explicitly. There is no automatic coordination between gr4
processes, and none is planned as far as I can see.

Related and also measured: 24 threads is worse than 16 even in a *single* process (921 vs 962
Msps), because `hardware_concurrency()` counts the 8 efficiency cores, and a straggler on a weaker
vector pipe gates a strided pipeline.

**Recommendation:** treat `hardware_concurrency()` as wrong on this machine in all cases. Size CPU
pools to the performance-core count divided by the number of concurrent gr4 processes.

---

## 5. Ranked next steps, revised in light of the above

1. **★ Three radios at depth 0, rate swept.** Settles whether the ~44 MS/s figure is an ingest cap
   or a complex-DSP/scheduler cap at depth 8. Minutes of work; invalidates or confirms the largest
   open finding. **Do this before anything else.**
2. **Match the MCM's configuration exactly** — 3 radios x **2 channels** x 20 MHz complex, minimal
   per-sample work. If gr4 cannot reach 120 MS/s where GR 3.9 did, that is a concrete, reproducible
   gr4 regression against a known-good baseline on identical hardware, and it is the single most
   valuable bug this project could produce.
3. **A non-gr4 control.** Plain `SoapySDRUtil` or a small libuhd program streaming three devices,
   to establish the platform ceiling independently of gr4. §8.6 needed this and did not have it.
4. Only then: the remaining items in `HANDOFF.md` §7.3.

---

## 6. Caveats on everything above

- The MCM comparison is **not** apples-to-apples: different GR generation, different threading
  model, 2 channels/radio vs 1, minimal DSP vs depth 8, ZMQ hand-off vs in-process sinks. It is
  strong evidence that ~44 MS/s is not a platform limit; it is **not** evidence about which of
  those differences matters.
- Our overflow counts remain **indicators, not measurements** — an overflow says samples were
  dropped, not by what. Every threshold figure in this project is only valid as a repeated
  relative comparison under otherwise-identical conditions.
- The B210 rate ceiling per radio is an **LO/front-end sync constraint**, per the owner: ~32 MS/s
  with two front ends against a 56 MS/s max clock. Our measured single-FE figure is ~43 MS/s
  sustained, with everything above ~40 in a stochastic marginal band. Ettus documents flakiness
  above 40 MS/s requiring on-device buffer tuning.
- Nothing in this project has yet tested **synchronised** multi-radio operation. All figures are
  free-running. The Octoclock-G path is plumbing that exists but is unwired — see `HANDOFF.md`
  §7.3 item 2 and `RESULTS.md` §8.4.
