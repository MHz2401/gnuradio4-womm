# HANDOFF — gnuradio4-womm

**Read this before doing anything else.** You are continuing work from multiple sessions. 

---

## FIRST — verify where you are

In the past, the session has attached to an unrelated project (`codpcl_LCS`). That project is **deprecated
and irrelevant**. The path you **should** be in is:
`.../GR4-fork/gnuradio4-womm/`  
and the correct current-branch is: `origin/womm/m2ultra-wip`
Origin: `https://github.com/MHz2401/gnuradio4-womm`

---

## WHAT THIS PROJECT IS

Produce the most stable and performant working build of **gnuradio4** on a Mac Studio M2 Ultra,
with a working path to **USRP B210** hardware. Not headed for a PR; never pushed. The owner is
Walter, an experienced SDR user who reads code, checks claims, and
supplies useful measurements of his own. Treat his hypotheses as evidence to test, not as
instructions — he has been right and wrong, and says so either way.

Machine: M2 Ultra, 16 P + 8 E cores, 192 GiB, macOS 26.5.2 (25F84), **16 KiB pages**, 128 B cache
line. Three B210s attached over USB 3. Full Xcode, Apple clang 21.

---

### ⚠ RF TRANSMISSION and U.S. LAW — READ THIS

- **IT IS AGAINST FEDERAL LAW TO TRANSMIT WITHOUT A LICENSE ON MOST RADIO FREQUENCIES**  
- _IT'S ALSO AGAINST FEDERAL LAW TO TRANSMIT (RX) **WITH** MOST LICENSE TYPES ON MOST FREQUENCIES._
  - _This is one reason why GNURadio hardcodes automatable tests for the receive-only (RX-only) RTL/SDR._
- **DO NOT AUTOMATE TRANSMISSION (TX) TESTS: all automated test code should be RX-only.** 

---

## PRIORITY TIERS — the owner's ordering. 

| Tier | Scope | Contents |
|---|---|---|
| **1** | **radio running** — performance while a device streams | Primary goal is **fully operational** gnuradio4 variant that works on this machine. |
| **2** | radio off | UI dev; blocks; UI/reconfiguration paths |
| **3+** | everything else | upstream contribution, public-repo polish |

- **"Fully operational" means processing capacity approximately linear-proportional to hardware
capacity.** Until that holds, assume undiscovered surprises: yesterday's independent issue becomes
tomorrow's dependency and vice versa. Do not treat any tier as finished early.

- **Upstream contribution is tier 3 or lower** — not for lack of value, but because we are not done
with a working local system, and premature contribution locks in an incomplete picture.

**The soak test is the yardstick for "done"** in both tier 1 (radio on) and tier 2 (radio off),
judged against the linear-proportionality criterion above.

**Making things work at capacity outranks formal QA for a public repo.**

## WHEN TO PUSH

- **at every gate / milestone** — and always before ending a session;
- **after anything expensive to recreate** 
- **before anything risky** — rebase, reset, upstream fetch/merge, or a large refactor;
- **whenever more than a few commits have accumulated.** If in doubt, push.

---

## WHAT DO YOU MEAN BY "19 % WALL COST" ?

We all make up jargon as we work because it saves time.  Tell everyone what it means.

---

## RULES THAT BIND (FROM PROJECT EXPERIENCE)

We're working with the early RC of an open-source project that includes unmaintained dependencies.

- **No green-washing.** Never disable a test, or add `|| true`, or blanket `-Wno-error`, or 
  narrow the test set to make things pass. A diagnosed failure is a result.
- **No network fetch** without explicit discussion and approval. Source only from hosts with 
  active third-party malware monitoring.
- **PUSH REGULARLY to `origin`.**, which is a fork, at regular times and/or work increments.   
- **no pull requests to upstream** (`gnuradio/gnuradio4*` and `fair-acc/gnuradio4`).
- **Report regressions as prominently as wins.** Label anything not actually measured.

---

## THIS SESSION: Project Owner's Statement (NEW)

In this session, we want to understand the current state of the claim that `gnuradio4` removes 
the process-blocking characteristics that I've worked around in the past: prior sessions indicate
the old problems are alive and well on Mac/arm64, Here are the claims:  

- https://www.gnuradio.org/news/2026-03-22-gr4-release-candidate-1/

I don't think the GR or Fair-Acc teams are making baseless claims, but the claims are becoming 
harder to fulfill because of unfortunate politics since that page was posted.  It's important 
to review the 'competing' gnuradio4 projects:

(A) GNURadio.org: https://github.com/gnuradio/gnuradio4 
(B) FAIR Accelerator Center: https://github.com/fair-acc/gnuradio4 
(C) Also GNURadio.org (componentized, more-recently-updated): 
- https://github.com/gnuradio/gnuradio4-core   
- https://github.com/gnuradio/gnuradio4-library   
- https://github.com/gnuradio/gnuradio4-blocks   
- https://github.com/gnuradio/gnuradio4-studio (early UI, may be "optional by necessity" for a running system).    

This project was originally forked from (A) https://github.com/gnuradio/gnuradio4, but it's starting 
to appear that - despite being the only one actively publicized - (A) is more _stale_ than (C) or (B).
Overall, (B) may be best-architected at the current time, but is also no longer supporting Mac (due 
to license politics) and is by far the most-likely to drift into unaligned needs of its sponsoring 
organization.  
No matter the choice, a lot of work will be needed to put the 'womm' in `gnuradio4-womm`.   
And t`womm` is an acronym that defines the mission: _**"Works On My Machine."**_.

**TASK: Please review the 'componentized' project (C) and compare it with the history and revisions of the 
local (current) project - the latter will implicitly provide a picture of the state-of-affairs with (A)
and (B).  Assess and recommend the path to success, and propose your design approach.** 

The remainder of this document are primarily tech notes from prior sessions that should accellerate your
view of (A) and (B).  

## Known Issues and Cautions

- Runtime graph reconfiguration is a runtime component (tier 1)
  - gr4 is designed to modify a flowgraph **while it runs** — it is not UI-only.
- **device-touching tests are NOT parallel-safe.** `DeviceRegistry::findOrCreate` shares one
  device instance per kwargs, so `qa_SoapyIntegration` and `qa_SoapyLoopback` interfere under
  `ctest -j`. Run them serially.
- **measure the streaming interval, never total elapsed.** Device init (~2.5 s on a B210)
  and graph construction both dwarf short runs. This error invalidated two separate measurements.
- **the test suite is not deterministic.** `qa_BasicFileIo` varies 8.8 s → 48 s → timeout on
  identical code. The 5-clean-run stability gate is currently unmeetable.

## Current state — what works

These items were current in the recent past, maintain truth.

- Builds clean: 1850 targets, ~600 s at `-j16`, **0 errors, 0 compiler warnings**.
- **ctest 101/102 (serial).** The one failure is `qa_SoapySource`'s "gain" test — **cause is an
  out-of-range gain value, NOT an AGC defect** (an earlier session claimed the latter; withdrawn).
  On a B2xx, RX2 tops out at ~76 dB and TX/RX at ~88 dB; an out-of-range value returns `nan`.
  Undiagnosed at source level; do not treat it as a SoapyUHD bug. Its two *missing-device*
  failures (rtlsdr, lime) now skip cleanly.
- **B2xx RF defaults that are known-good:** RX gain **20 dB**, centre **2401 MHz** (legal for
  amateur and WiFi). Antenna gain ranges differ — RX2 ≠ TX/RX (the device spells it TX/RX; "RX/TX" is not a name it knows) — so quote the probe section, not
  just a number.
- **Run ctest SERIALLY** (invariant I-11). Device tests are not parallel-safe.
- Five upstream correctness fixes cherry-picked (watchdog leak, message-path deadlock,
  init-on-reset, CircularBuffer churn, RT-safe housekeeping) — provenance in `DRIFT.md` Category E.
- One portability fix of ours: `gr::meta::shrinkIfSupported()` in `meta/…/meta/utils.hpp`
  (libc++ lacks `unordered_map::shrink_to_fit`, a libstdc++ extension).
- **Hardware reaches the radio**: `SoapySDRUtil --probe="driver=uhd"` initialises a B210 (FPGA
  16.0, fw 8.0). All three enumerate.


### ★ CONFIRMED — solid results, 2026-07-27 evening

Stated separately because this project has accumulated far more retractions than confirmations, and
these are neither hypothetical nor single-trial. Full detail in `RESULTS.md` Phase 9,
`DRIFT.md` Category H.

| # | Result | How it is known |
|---|---|---|
| C-1 | **Two-channel receive works.** First time in this project. | Owner read the front-panel LEDs on **all three** units, A and B frontends, both on `TX/RX` — out-of-band evidence no software defect can fake — agreeing with independent per-channel counters. |
| C-2 | **Two-channel receive had NEVER worked, for two reasons, both now fixed.** | H-1: `start()` called `activate()` bare, and UHD refuses "stream now" on a multi-channel streamer. Reproduced **outside gr4** with `SoapySDRUtil --channels="0,1"`. H-2: `setHardwareTime` passed `nullptr` for an empty event → `strlen(nullptr)` → SIGSEGV, found in the macOS crash report. H-2 was unreachable until H-1 was fixed. |
| C-3 | **92.16 MS/s across 3 radios / 6 channels. Ratio 1.0000. Zero overflows. 43 s.** | 43 one-second samples per channel from `CountingSink`, three separate processes, `WOMM_THREADS=8` each. CPU sampled independently: ~42 % user, ~15 % sys, **~43 % idle**. |
| C-4 | **A B2xx does 30.72 MS/s aggregate on two channels** (15.36 per channel). | UHD's own refusals: MCR 40 rejected outright, MCR pinned 30.72 silently delivers 15.36, auto-MCR fails at `activate()`. Matches the owner's Nyquist reasoning and the long-recorded "~32 MS/s" figure, which is an **aggregate per radio**. |
| C-5 | **`UNKNOWN_PPS`, not `PPS`, is the multi-device sync primitive.** | `vendor/SoapyUHD/SoapyUHDDevice.cpp:873-874` — `"PPS"` → `set_time_next_pps()`, `"UNKNOWN_PPS"` → `set_time_unknown_pps()`. Only the latter waits for a PPS *transition* first, so every radio latches the same edge. |
| C-6 | **The antenna is `TX/RX`, never `RX/TX`.** | Device: `Antennas: TX/RX, RX2`. Our own RX-only allow-list had it reversed and would have rejected the correct name; unnoticed because every prior run used RX2. Corrected in both harnesses; verified receiving. |
| C-7 | **There is no "demand lock on start" flag.** Poll `ref_locked`. | SoapyUHD forwards `set_clock_source` to UHD with no lock logic; Ettus document loop-until-locked precisely because nothing can be set. Sampling the sensor once reads `false` on a good reference. |
| C-9 | **Content verified — the transmitted comb is on all 8 channels.** | 13 pickets each, spacing 40001.3-40001.4 Hz against 40000 transmitted, fit rms 3.2-3.5 Hz (~0.008 % of spacing). Closes the disclaimer §9.10 carried. Detector validated in BOTH directions first: synthetic comb recovered to 0.1 Hz, pure noise rejected. `RESULTS.md` §9.11. |
| C-10 | **★ Four locked radios agree to 1.72 ppb.** | Measured LO offsets +12914.2 to +12918.3 Hz — spread **4.1 Hz out of 2.401 GHz**. I predicted they would DIFFER; that carried a free-running assumption into a locked configuration. With a shared 10 MHz, LO error is common-mode, so this is the *transmitter's* offset seen identically by four receivers. Free-running TCXOs (+/-2 ppm) would have scattered by kHz. **This is the number that says the four radios can be treated as one instrument.** |
| C-11 | **Four radios, 8 channels, 122.88 MS/s, ratio 1.0000, one PPS epoch.** | Worst epoch offset 39.8 ms against a one-second discriminator; ~29 % of the machine idle. Exceeds the MCM's 120 MS/s with two properties it never established. `RESULTS.md` §9.10. |
| C-8 | **SoapyUHD/UHD are already correctly paired.** | `MANIFEST.md:99` — vendored SoapyUHD is master `2a5d381f`, 20 commits past the 0.4.1 tag, chosen because master adds UHD 4.8+ support. Our two patches total 31 lines of build hygiene, no API shims. Brew's UHD stays: the owner accepts brew where the formula has a solid build chain with many eyes on it. **Closed as a decision, not debt.** |

**Instruments that had to be fixed before they could be believed — all the same family.** Each
returned a plausible answer regardless of input, and each was caught by a control rather than by
inspection. Build the control first.

| instrument | failure | caught by |
|---|---|---|
| cross-process count drift (§9.7) | "wanders, no offset" for locked *and* free-running alike; ~300 000-sample skew against a ~900-sample effect | running it on a known-locked pair |
| comb detector, v1 (§9.11) | reported 40530 Hz spacing on **silence** — peak-thinning forces ~40 kHz spacing, so the fit manufactured it | running it on a silent capture |
| comb detector, v2 (§9.11) | rejected 3 of 4 radios by assuming every peak belongs to the comb; ISM interferers dragged the fit | offsets clustering correctly *despite* rejection |

**The general rule this session earned:** choose measurements whose competing hypotheses differ by
more than the noise, and validate in **both** directions — an instrument proven only against silence
has been shown to say "no", not to say "yes".

**Retracted this session, so nobody rebuilds them:** `RESULTS.md` §8.6 (global ingest cap) and §8's
bottleneck claim; and **my cross-process count-difference drift test**, which reports "wanders, no
offset" for locked *and* free-running pairs alike — the log-sampling skew between independent
processes is ~300 000 samples against a ~900-sample effect. Lock is established by the device's
`ref_locked` sensor; **epoch** alignment needs the device timestamps in the timing tags, and host-side
counters cannot get there at any sampling discipline.

### Next-step ordering — REWRITTEN 2026-07-27 (prior to 1130 Pacific)

This is the next-step-list from the prior session: some items are done, some were deferred, and 
items' priorities can change as the result of a turn.

1. ~~Tag-buffer sizing~~ **DONE.** Capped at `min(min_size, kDefaultBufferSize)`, matching
   fair-acc. Peak RSS **6.92 → 2.27 GiB**, throughput unchanged. See `DRIFT.md` G-6.
1b. ~~THE TIER-1 BOTTLENECK IS SDR INGEST~~ **WITHDRAWN 2026-07-27 evening. There is no ingest
   cap.** Three radios x two channels sustain **92.16 MS/s aggregate, ratio 1.0000 to the hardware
   maximum, zero overflows, 43 s** — 2.1x the "~40-44 MS/s however it is divided" figure, and at
   the physical limit of the hardware. `RESULTS.md` §8.6 and §8's bottleneck claim are both
   withdrawn; see §9.6. The old number came from a configuration that was single-channel at DSP
   depth 8 in one graph; the new one is two-channel at depth 0 in three processes. **Refuted, but
   not attributed** — three variables moved at once. Attributing it is E0.1, still open.

1c. ~~Set `clock_source`/`time_source` to `external`~~ **DONE, and it works on 2 of 3 radios.**
   `WOMM_EXTCLK=1` sets both. `31FE7A2` and `32FCD05` report `ref_locked=true` and stream full
   rate; **`32FCCF7` reads `false` after 3000 ms and refuses to run** — isolated to one unit with
   two controls on the same code path, so it is hardware/cabling. Bisect by swapping its 10 MHz
   cable with a known-good one. See `RESULTS.md` §9.7.

2. **`B_max` hardware harness (tier 1)** — built, `blocks/sdr/src/womm_bmax.cpp`; still needs the
   ballast sweep, and **now has a valid configuration to run at**: 3 radios x 2 channels =
   92.16 MS/s live ingest with **~43 % of the machine idle**. `B_max` = the largest M synthetic
   ballast chains with zero overflows for T seconds. This is the tier-1 linear-proportionality
   yardstick and it is now answerable. RX-only by construction — the TU must not include
   `SoapySink`, and `assert_no_tx.cmake` on the linked binary is the check.
3. **Make the harness survive a diagnosable misconfiguration.** Every configuration error found
   this session terminated the process via an *uncaught* exception out of the scheduler
   (`Scheduler.hpp:496`) rather than reporting: bad MCR, unlocked reference, `activate()`
   STREAM_ERROR. Refusing to run is right; crashing is not. Same defect shape as the old
   overflow-throw item.
4. ~~Verify the B210 ceiling~~ **DONE, both configurations.** Single channel sustains **~43 MS/s**
   (the old 32.5 figure was an artefact of a rate list that never bracketed 32-56). **Two channels
   cap at 15.36 MS/s each, 30.72 MS/s aggregate per radio** — UHD refuses `master_clock_rate`
   above 30.72 MHz with two RX channels and the per-channel rate is MCR/decimation. That is the
   "~32 MS/s with two front ends" figure, it is an **aggregate per radio**, and it is correct.
   Owner confirms the Nyquist relation to the sample clock. See `RESULTS.md` §7.1 and §9.4.
5. **Serialise device tests** — ctest `RESOURCE_LOCK` or a fixture; upstream-shaped, no patch.
6. Deferred: default pool size is `hardware_concurrency()` = 24 on this machine, which puts 8
   workers on utility cores; 16 threads measured faster than 24. `PORTABILITY.md`.

**Do NOT spend time on:** `_nWorkersInWork` cache-line padding or the graph-global `progress`
counter. Profiled at ≤2.5 % and not visible respectively — both are amortised over large
per-`work()` chunks. Revisit only if something else stops dominating.


## Prior deliverables and where things live

Update as necessary.  Most of these are or should be updated.

| File | Contents |
|---|---|
| `BUILD_JOURNAL.md` | append-only decisions D1–D7, with rationale and how to reverse |
| `RESULTS.md` | all measurements, including the retraction in Phase 3.5c |
| `DRIFT.md` | every local deviation; Category E is the cherry-pick provenance + licensing |
| `MANIFEST.md` | dependency provenance, §8 upstream sync policy |
| `scripts/env.sh` | the only thing that activates the prefix |
| `scripts/build-prefix.sh` | builds all vendored deps into the prefix |
| `scripts/verify-vendor.sh` | proves committed vendor trees reproduce |
| `scripts/vendor.sh` | re-fetches vendored deps at pinned SHAs |

Still to write: `scripts/build.sh`, `scripts/verify.sh`, `PORTABILITY.md`.

---

## INVARIANTS

Discuss with user before varying ferom these practices.

| # | Invariant | Why |
|---|---|---|
| I-2 | **Toolchain is Apple clang 21 + libc++.** Not GCC | builds the tree with **0 errors, 0 compiler warnings** under project-wide `-Werror`; matches brew UHD's own libc++ build |
| I-3 | **All dependencies are vendored** in `vendor/`, SHA-pinned, built into the isolated prefix. Nothing from Homebrew | `MANIFEST.md`; `scripts/verify-vendor.sh` proves the committed trees reproduce |
| I-4 | **Never `brew install`** without asking first. | `/opt/homebrew` holds the owner's live 198-formula stack; transitive upgrades would perturb it. |
| I-5 | **`-j16` is fine**, despite `CLAUDE.md:433` mandating `-j6` | measured: peak 11.4 GiB across 16 compilers = 6 % of RAM. |
| I-6 | **Vendored files must be `git add -f`** | repo `.gitignore` rule `lib/` silently swallows all of `vendor/SoapySDR/lib/` |
| I-7 | **`.gitattributes: vendor/** -text`** must stay | `core.autocrlf=input` otherwise rewrites vendored bytes and breaks verification |
| I-8 | **The tree is MIT on paper, LGPL-derived in fact** | four cherry-picks postdate fair-acc's relicensing; fine while never distributed. `DRIFT.md` Category E |
| I-9 | **THIS MIGHT CHANGE:** Baseline is `origin/main` @ `44275ed`** (`4.0.0-RC2-13`), not RC1 | the three sibling branches are CI/math only; `main` is a strict superset |
| I-10 | **fair-acc is NOT a tracking target, but it IS worth reading** | it reverted macOS ARM64 support (`ac59533`), so never merge it wholesale — cherry-pick individual fixes. The split is a licence dispute, not a technical one: gnuradio.org insists on MIT, fair-acc on LGPL-3.0. `gnuradio/gnuradio4` (our base) periodically forks from the better-staffed fair-acc, so our tree is somewhat stale by construction. See I-14 |
| I-14 | **Check a high-impact finding against ALL upstreams before assuming it is novel** — and before inventing a fix | `fair-acc/main` is better-staffed and sometimes ahead: it had already fixed the tag-ring sizing we had not. It is also *behind* on all five ring-buffer defects. `gnuradio/gnuradio4-core` (+`-library`, `-blocks`, `-studio`) is the split-repo direction gnuradio.org is moving to and is worth the same check.  They have also stopped supporting
the mac platform. See `DRIFT.md` Category G |


## PLACEHOLDER — HANDOFF.md revisionsfill before session end

For the next session.

<!-- Complete a revised HANDOFF.md at ~70% context. -->




## - - - REFERENCE ONLY - - -

Below here: not 'required reading' but _search this for the name a module before working on it_.  

## APPENDIX: (CLOSED or STALE or WRONG) and USEFUL

The items below are **old**, many are stale, some are wrong, all are **educational**. 

### (REF) Performance, measured

**⚠ The whole earlier curve is retracted.** It sized the pool to the chain count, so every point
varied workload *and* decomposition together, and its 1-chain point was a different regime (one job,
topological order, no cross-thread edge). "1→2 chains gains nothing" was an **artefact of the axes**.
The 168.8 Msps single-chain figure was also still setup-diluted at 20 M samples/chain.

Current, windowed steady state, `--chains`/`--threads` independent, one cell per process:

| chains | threads | Msps | ×B210 |
|---|---|---|---|
| 1 | 1 | 476 | 7.8× |
| 16 | 8 | 1754 | 28.5× |
| 16 | 16 | **2416** | **39.3×** |

One chain on one core sustains **7.8× a B210** through eight multiply/divide stages; the machine
peaks near **2416 Msps**. 16-thread scaling is **6.90×** — still sublinear, but the dominant term
was the buffer mirror copy, not the scheduler. Harness: `core/benchmarks/womm_bm_scaling.cpp`,
one cell per invocation (`--chains N --threads M --window SEC`).

---

#### (REF)  ~~THE BUG~~ — RESOLVED, was my harness. See RESULTS.md Phase 5.

**Not a gnuradio4 defect.** `kDurationSec=2.0` was shorter than the B210's ~2.5 s bring-up, so
`requestStop()` fired before the reader thread was scheduled. End-to-end now works: B210 sustains
**16 MS/s complex lossless**, saturating at ~25-27 MS/s. Original text kept below for the
ruled-out list, which remains useful.

#### (REF) (historical) `SoapySource` delivers zero samples

The block yields **0 samples** from a real B210 *and* from the synthetic `LoopbackDevice`. A
`ConstantSource` through the identical harness does 573 Msps, so the measurement is sound.

Harness: `blocks/sdr/src/womm_b210_sweep.cpp`. Reproduce with **no hardware**:

```bash
cd /Users/whom/dvel/ghzhub/GR4-fork/gnuradio4-womm && source scripts/env.sh && \
SOAPY_SDR_PLUGIN_PATH="$PWD/build-fixed/blocks/sdr" \
  ./build-fixed/blocks/sdr/src/womm_b210_sweep 1e6 0 loopback
```
Args: `<rate> <depth> <device> [min]`. `device=constant` runs the known-good control.

**Already ruled out — do not redo:**

| Hypothesis | How it died |
|---|---|
| DSP chain at fault | depth 0 (source→sink direct) behaves identically |
| Silent connect failure | all `graph.connect` results checked |
| `complex<float>` unsupported | registered at `Math.hpp:27` |
| Device init failure | UHD log shows B210 detected, USB 3, clock set |
| Scheduler error | `runAndWait()` returns **success** |
| Wrong channel/antenna | `num_channels=1`, `rx_antennae={"RX2"}` — no change |
| `activateStream` burst semantics | `SoapySource.hpp:182` calls `activate()` with `flags=0, timeNs=0, numElems=0` — continuous, correct |
| Settings-driven double acquisition | tested with device-only config — **still zero** |

**Best remaining lead.** `start()` does not activate the stream directly; it defers via
`DeviceRegistry::registerActivation` (`SoapyRaiiWrapper.hpp:249-266`), which fires the callback
only when `pendingUsers` reaches 0. `findOrCreate` sets 1 on creation (`:245`) and **increments**
on reuse (`:224`); `registerActivation` decrements **once** (`:259`). Any extra acquisition wedges
it forever. Both that path and the `!_device.get()` early return in `start()` are **silent**.

**NEXT STEP (planned, ~5 min):** add a print inside the activation lambda and one on the early
return in `SoapySource::start()` (~`:178-190`). Three-way split:
1. callback never fires → hunt the extra `pendingUsers` increment;
2. callback fires but no samples → look at `ioReadLoop` / `readStream`;
3. `start()` returns early → look at `reinitDevice()`.

Why this was never caught upstream: every Soapy file is excluded from all CI
(`blocks/sdr/CMakeLists.txt:9` guards on `if(TARGET SoapySDR)` and no CI image installs it);
`qa_SoapySource` needs an absent RTL-SDR; `qa_SoapyIntegration` self-skips; `qa_SoapyLoopback`
tests the raw device, not the block. The block-level path appears never to have run to completion.

### (REF) ★ ROOT CAUSE FOUND — macOS thread-pool polling burns ~13 cores

**The headline result. Read RESULTS.md "ROOT CAUSE" before anything else.**

`thread_pool.hpp:635-657` is a macOS-only `sleep_for(10us)` polling loop, added to work around a
libc++ `condition_variable::wait_for` EINVAL. Every other platform blocks on a condvar. The pool
eagerly spawns `hardware_concurrency()` = 24 workers, so **every gr4 process on this machine**
issues ~2.4 M `nanosleep` syscalls/s whether or not work exists.

Measured on `qa_BasicFileIo` — a file test that never uses the pool:
**5.46 s wall, 2.71 s user, 71.54 s system, 2.97 M involuntary context switches.**
~13 cores permanently in the kernel.

Causally confirmed: 10 us -> 2 ms cut system time **97.4 %** (71.54 s -> 1.85 s) but made wall
time **70 % worse**. So a bigger constant is NOT the fix.

**THE FIX — DONE.** Shared `_conditionMutex`, per-thread local mutex removed, `__APPLE__` branch
deleted, submission notifies under the lock. **Net −4 lines.** See `DRIFT.md` Category F.
Results: system CPU 238× lower, context switches 779× lower, total CPU 47× lower, serial ctest
100/102 → **101/102** with the `qa_BasicFileIo` hang **gone**, suite 2.6× faster, scaling peak
+26 %, B210 ceiling +22 % (26.7 → 32.5 MS/s). Costs 19 % wall on one short test (inherent condvar
latency); does not generalise.

Explains: the owner's system-vs-user observation, the 3 M context switches in the scaling
benchmark, and why `qa_BasicFileIo` hangs ~33 % of the time **when run alone** but passes under
`-j8` (idle workers get more CPU when nothing competes). Possibly the original §0 anomaly — a
MacBook Air polls with 8 threads, an Ultra with 24; both throttle on the syscall storm. Not
proven, since §0 was measured on GNU Radio 3.x.

Experiment reverted; `thread_pool.hpp` is byte-identical to upstream.

### (REF)  ~~Graph lifecycle leaks memory~~ — WITHDRAWN. There is no leak.

Measured directly: a **single** 16-chain graph cycle peaks at **6.92 GiB**. The recorded 8 GiB
across *seven* cycles is therefore consistent with memory being released every cycle — a leak would
have shown ~42 GiB. The evidence was already in the old table and went unread: page reclaims were
1,710,937 at 1 M samples/chain and 1,764,674 at 20 M — **flat**, i.e. a fixed allocation cost.

**The real cause is static over-allocation, dominated by one line.** `Port::resizeBuffer` passes the
**same element count** to the stream and the tag buffer, and `Tag` is `alignas(kCacheLine)` = **128 B
on Apple ARM64**. A connected `float` port at 65536 gets 256 KiB of stream and **16 MiB of tags** —
64×, and 4× worse than the same graph on Linux/x86-64. `Tag` is not trivially copyable, so tag
buffers stay on the copying path and were untouched by the Category G buffer fix.

**Open, tier 2:** decouple tag-buffer sizing from stream-buffer sizing. Nothing here needs 65536
tags in flight per port. See `RESULTS.md` §6.6.

### (REF) Deferred, in priority order

1. Parallel scaling: 2.07× from 16 chains. Suspects, untested: strided block→thread partitioning
   (`Scheduler.hpp:1378-1385`), absent Darwin QoS (`thread_affinity.hpp`, 15 no-op sites),
   macOS mirror-`memcpy` (`CircularBuffer.hpp:352-378`).
2. ~~UHD provenance~~ **CLOSED 2026-07-27 — accepted, not debt.** Owner: brew is fine where the
   formula has a solid build chain with many eyes on it, and UHD qualifies. A source build would
   also drag in Boost + libusb (the prefix has no real Boost — only Boost.UT's `ut.hpp`), for
   provenance only. Deliberate exception to I-3.
3. `PORTABILITY.md` not yet written.

---

### (REF) Mistakes already made — do not repeat

Recorded because each cost real time.

1. **A benchmark that measured setup, not steady state.** The original scaling curve reported a
   ceiling of 0.44× a B210 and "cannot sustain one radio." Wrong by ~13×. `/usr/bin/time -l`
   showed system time and page-fault counts *identical* at 1 M and 20 M samples/chain. **Always
   check `time -l` before believing a throughput number.**
2. **A wedged graph at 1717 % CPU.** A radio source never finishes, so a sink's `n_samples_max`
   cannot end the graph — 17 cores spun until killed. Use `requestStop()` after a fixed duration
   *plus* a hard backstop. `qa_SoapySource.cpp:335` uses a watchdog for the same reason.
3. **`sed`/Python patching without verification.** One insertion appended an include after *every*
   line of a 1162-line file; it compiled because include guards made repeats no-ops. **Assert a
   unique anchor and check line counts after any scripted edit.**
4. **An isolation proof using `env -i`**, which stripped the environment so the login shell never
   rebuilt PATH, producing four false differences. **A proof harness must reproduce the baseline's
   conditions.**
5. **`std::ignore = graph.connect(...)`** hid connection failures. Always check.
6. Recommending fair-acc as the tracking upstream before noticing it had dropped macOS.

---



