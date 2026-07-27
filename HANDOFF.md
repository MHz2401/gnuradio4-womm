# HANDOFF — gnuradio4-womm

**Read this before doing anything else.** You are continuing work that is 26 commits deep. Most
of the expensive discovery is already done and recorded. Re-deriving it wastes the session.

---

## 0. FIRST COMMAND — verify where you are

The session may open attached to an unrelated project (`codpcl_LCS`). That project is **deprecated
and irrelevant**; do not read, modify, or commit to it. All paths below are absolute for this
reason — **never trust a relative path in this project.**

Run this first. If it does not print `ROOT OK`, stop and tell the user:

```bash
cd /Users/whom/dvel/ghzhub/GR4-fork/gnuradio4-womm && \
  test -f CLAUDE.md && test -d core/include/gnuradio-4.0 && \
  echo "ROOT OK  $(git branch --show-current)  $(git log --oneline -1)"
```

Then activate the toolchain (source, never execute):

```bash
cd /Users/whom/dvel/ghzhub/GR4-fork/gnuradio4-womm && source scripts/env.sh
```

| Thing | Absolute path |
|---|---|
| Repo | `/Users/whom/dvel/ghzhub/GR4-fork/gnuradio4-womm` |
| Isolated prefix | `/Users/whom/dvel/ghzhub/GR4-fork/womm-prefix` (deliberately **outside** the repo) |
| Baseline build (stock) | `<repo>/build-baseline` |
| Current build (fixed) | `<repo>/build-fixed` |
| Working branch | `womm/m2ultra-wip` |
| Snapshot to fall back to | tag `womm-known-good/2026-07-26` |

---

## 1. What this project is

Produce the most stable and performant working build of **gnuradio4** on a Mac Studio M2 Ultra,
with a working path to **USRP B210** hardware. Not headed for a PR; never pushed. The owner is
Walter (`walter@hacktuary.ai`), an experienced SDR user who reads code, checks claims, and
supplies useful measurements of his own. Treat his hypotheses as evidence to test, not as
instructions — he has been right and wrong, and says so either way.

Machine: M2 Ultra, 16 P + 8 E cores, 192 GiB, macOS 26.5.2 (25F84), **16 KiB pages**, 128 B cache
line. Three B210s attached over USB 3. Full Xcode, Apple clang 21.

---

## 1b. PRIORITY TIERS — the owner's ordering. Respect it.

**"Fully operational" means processing capacity approximately linear-proportional to hardware
capacity.** Until that holds, assume undiscovered surprises: yesterday's independent issue becomes
tomorrow's dependency and vice versa. Do not treat any tier as finished early.

| Tier | Scope | Contents |
|---|---|---|
| **0** | **the work exists and is backed up** — outranks everything, including tier 1 | commits pushed to `origin`; nothing of value living only on this machine |
| **1** | **radio running** — performance while a device streams | thread QoS; scaling plateau; anything that costs throughput with hardware live |
| **2** | radio off | graph-lifecycle memory; config-time costs; UI/reconfiguration paths |
| **3+** | everything else | upstream contribution, public-repo polish |

**Upstream contribution is tier 3 or lower** — not for lack of value, but because we are not done
with a working local system, and premature contribution locks in an incomplete picture.

**`scripts/build.sh` and `scripts/verify.sh` missing is a *symptom* that tiers 1–2 are unfinished
— but the converse does not hold.** Expect to need both *before* reaching fully-operational.

**The soak test is the yardstick for "done"** in both tier 1 (radio on) and tier 2 (radio off),
judged against the linear-proportionality criterion above.

**Making things work at capacity outranks formal QA for a public repo.**

### Tier 0 in practice — when to push

Losing work costs more than any tier-1 result is worth. Push:

- **at every gate / milestone** — and always before ending a session;
- **after anything expensive to recreate** — a 10-minute build, a measurement run, a diagnosis
  that took several experiments;
- **before anything risky** — rebase, reset, upstream fetch/merge, or a large refactor;
- **whenever more than a few commits have accumulated.** If in doubt, push.

Cheap and reversible; the failure mode it prevents is not. Licensing of *this fork* is tier 3 —
the owner renamed it `gnuradio4-womm` deliberately, and final licensing is sorted out later. **Do
not let a licensing question block a backup push.**

---

## 1c. GLOSSARY — terms used loosely in prior sessions

Written because a successor will otherwise assume the owner has internalised shorthand he has seen
once. Prefer these full phrasings over the shorthand.

| Term | Means |
|---|---|
| **wall time** | elapsed real-world clock time, as opposed to CPU time consumed |
| **user / system time** | CPU seconds spent in the program vs in kernel calls. Can exceed wall time when threads run in parallel — 71 s of system time in a 5 s run means ~13 cores were in the kernel |
| **involuntary context switch** | the OS preempted a thread that still wanted to run. Millions per second indicates thrash, not work |
| **condvar** | `std::condition_variable`. Lets a thread *block* until signalled, instead of repeatedly waking to check |
| **lost wakeup** | a signal sent while the waiter is between "checked the condition" and "went to sleep", so it sleeps anyway |
| **livelock** | threads are running but no progress is made — distinct from deadlock, where they are stopped |
| **steady state** | the regime after start-up costs are amortised. A benchmark too short to reach it measures set-up, not throughput |
| **setup-dominated** | a measurement where construction/initialisation exceeds the work being timed. Caused two wrong conclusions here |
| **spread** | half the range between fastest and slowest run. A "±0.02 s spread" means run-to-run variation, not measurement error |
| **drift** | our local deviations from upstream source. Tracked in `DRIFT.md` |
| **cherry-pick** | copying one upstream commit onto our branch without merging its whole history |
| **ABI** | binary-level compatibility between separately-compiled code. Mismatches appear as link errors or crashes, not compile errors |
| **SFINAE / `requires`** | compile-time feature detection. Used here to skip a function that does not exist on this standard library |
| **keg-only** (Homebrew) | installed but deliberately not linked into `PATH`, so it shadows nothing |
| **poured from bottle** (Homebrew) | installed as a precompiled binary rather than built here — relevant to provenance |
| **strided round-robin** | work assigned as 1st→thread A, 2nd→thread B, 3rd→thread A… so *adjacent* pipeline stages land on *different* threads |
| **oversubscription** | more worker threads than usable cores, so they contend rather than add throughput |

### The "19 % wall cost" — clarified, since prior wording was sloppy

Prior sessions called it a *regression*. That was the wrong word: nothing returned to an earlier
state. It is a **trade**, and here is the whole of it.

`qa_BasicFileIo` — one short, largely single-threaded test — took **5.46 s** with upstream's
10 µs polling loop and **6.48 s** after our condvar fix. That single number is the entire cost.

Answering the natural questions directly:

- **Is it an artefact of testing?** Largely yes. It is one short test with little parallel work.
- **Does it indicate a real-world bottleneck?** No, and the evidence runs the other way: the full
  test suite got **2.6× faster**, scaling throughput **+26 %**, and the B210 ceiling **+22 %**.
  Every workload with actual parallelism improved.
- **Is the test↔runtime relationship known?** Partly. Four measurements, three strongly improved.
- **Does it mean something is untested?** It reveals *why* the trade exists: the polling loop
  bought 10 µs task-pickup latency by burning ~13 cores continuously. A workload that submits many
  tiny tasks and has little parallel work is the one case where paying those cores is worth it.
- **What changed, precisely?** Better: CPU 47×, suite time 2.6×, scaling +26 %, radio ceiling
  +22 %, and an intermittent hang eliminated. Worse: this one test's wall time, by 1.02 s.

**It is a latency-versus-throughput trade, not a bottleneck.**

### Runtime graph reconfiguration — it IS a live capability

Checked, not assumed. The scheduler handles `kEmplaceBlock`, `kRemoveBlock`, `kReplaceBlock`,
`kEmplaceEdge`, `kRemoveEdge` and `kGraphGRC` as messages (`Scheduler.hpp:57-69`), so gr4 is
designed to modify a graph **while it runs** — this is not merely a UI/config-time concern.

**But the 8 GiB / 1.7 M page-reclaim measurement is of full graph construct-and-destroy cycles,
which is a config-time cost (tier 2).** Whether *incremental* runtime edits leak is **untested**
and would be tier 1. Those are different code paths; do not conflate them.

---

## 2. INVARIANTS — settled. Do not re-open without a reason.

Evidence for every one of these is in `BUILD_JOURNAL.md` (decisions D1–D7) and `RESULTS.md`.

| # | Invariant | Why |
|---|---|---|
| I-1 | **Baseline is `origin/main` @ `44275ed`** (`4.0.0-RC2-13`), not RC1 | the three sibling branches are CI/math only; `main` is a strict superset |
| I-2 | **Toolchain is Apple clang 21 + libc++.** Not GCC | builds the tree with **0 errors, 0 compiler warnings** under project-wide `-Werror`; matches brew UHD's own libc++ build |
| I-3 | **All dependencies are vendored** in `vendor/`, SHA-pinned, built into the isolated prefix. Nothing from Homebrew | `MANIFEST.md`; `scripts/verify-vendor.sh` proves the committed trees reproduce |
| I-4 | **Never `brew install`** | `/opt/homebrew` holds the owner's live 198-formula stack; transitive upgrades would perturb it |
| I-5 | **`-j16` is fine**, despite `CLAUDE.md:433` mandating `-j6` | measured: peak 11.4 GiB across 16 compilers = 6 % of RAM |
| I-6 | **Vendored files must be `git add -f`** | repo `.gitignore` rule `lib/` silently swallows all of `vendor/SoapySDR/lib/` |
| I-7 | **`.gitattributes: vendor/** -text`** must stay | `core.autocrlf=input` otherwise rewrites vendored bytes and breaks verification |
| I-8 | **The tree is MIT on paper, LGPL-derived in fact** | four cherry-picks postdate fair-acc's relicensing; fine while never distributed. `DRIFT.md` Category E |
| I-9 | **fair-acc is NOT a tracking target** | it reverted macOS ARM64 support (`ac59533`). Cherry-pick individual fixes only |
| I-10 | **gr4 source drift must stay small** and every change logged in `DRIFT.md` | forward-compat is a stated objective; currently 14 files |

### ⚠ RF TRANSMISSION SAFETY — read before any hardware test

The owner holds an amateur radio licence. **Claude does not, and cannot transmit on his behalf
without his knowledge.**

- **PRE-NOTIFY the owner of the exact frequency, bandwidth, gain and duty cycle of ANY test that
  could key a transmitter, and wait for explicit approval.** Not "SDR testing" — the actual
  numbers and the band they fall in.
- **Default to RX-only.** A B210 and a HackRF both transmit. Configure test graphs so TX is
  impossible by construction (no `SoapySink`, no TX antenna selected), not merely unused.
- **Never widen frequency range, sweep gain, or enable TX to "improve coverage"** without the
  owner deciding, band by band. Automating an emission does not make it legal, and a licensed
  operator carries the consequences.
- **Bands are not interchangeable.** GPS, aeronautical, emergency and public-safety allocations
  are near-instantly attributable and can be catastrophic to jam. Being near an airport makes
  this concrete rather than theoretical.
- **Do not commit RF parameters** (frequency, gain, antenna, TX enable) to any file that could
  reach a public repository. Keep them in a gitignored local config. A committed test that another
  contributor edits becomes someone else's emission, on someone else's licence.

This is why upstream's `qa_SoapySource` hardcodes an **RTL-SDR: it is receive-only.** That is
plausibly a deliberate safeguard for unattended CI runners, not an oversight. Treat it as one.

### Rules that bind (from the owner's original brief)

- **No green-washing.** Never disable a test, add `|| true`, blanket `-Wno-error`, or narrow the
  test set to make things pass. A diagnosed failure is a result; a hidden one is a lie.
- **No network fetch** without explicit approval. Source only from hosts with active third-party
  malware monitoring (this ruled out a self-hosted gitea mid-session).
- **PUSH REGULARLY to `origin`.** An earlier session read "never push" as absolute and left 35
  commits existing only on this machine for two days. That was wrong. The rule means **no pull
  requests to upstream** (`gnuradio/gnuradio4`, `fair-acc/gnuradio4`) — it never meant "do not back
  up your own work". See tier 0 below.
  - **Do:** `git push -u origin <our-branch>` — our branches, our fork.
  - **Never:** push `main`; force-push; push to `upstream-gr`/`upstream-fair` (fetch-only); open a
    PR upstream.
- **Ask before the first command that touches hardware** in a new session.
- **Report regressions as prominently as wins.** Label anything not actually measured.

---

## 3. Current state — what works

- Builds clean: 1850 targets, ~600 s at `-j16`, **0 errors, 0 compiler warnings**.
- **ctest 101/102 (serial).** The one failure is `qa_SoapySource`'s "gain" test — **cause is an
  out-of-range gain value, NOT an AGC defect** (an earlier session claimed the latter; withdrawn).
  On a B2xx, RX2 tops out at ~76 dB and RX/TX at ~88 dB; an out-of-range value returns `nan`.
  Undiagnosed at source level; do not treat it as a SoapyUHD bug. Its two *missing-device*
  failures (rtlsdr, lime) now skip cleanly.
- **B2xx RF defaults that are known-good:** RX gain **20 dB**, centre **2401 MHz** (legal for
  amateur and WiFi). Antenna gain ranges differ — RX2 ≠ RX/TX — so quote the probe section, not
  just a number.
- **Run ctest SERIALLY** (invariant I-11). Device tests are not parallel-safe.
- Five upstream correctness fixes cherry-picked (watchdog leak, message-path deadlock,
  init-on-reset, CircularBuffer churn, RT-safe housekeeping) — provenance in `DRIFT.md` Category E.
- One portability fix of ours: `gr::meta::shrinkIfSupported()` in `meta/…/meta/utils.hpp`
  (libc++ lacks `unordered_map::shrink_to_fit`, a libstdc++ extension).
- **Hardware reaches the radio**: `SoapySDRUtil --probe="driver=uhd"` initialises a B210 (FPGA
  16.0, fw 8.0). All three enumerate.

### Performance, measured

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

## 4. OPEN — the live problem

### 4.1 ~~THE BUG~~ — RESOLVED, was my harness. See RESULTS.md Phase 5.

**Not a gnuradio4 defect.** `kDurationSec=2.0` was shorter than the B210's ~2.5 s bring-up, so
`requestStop()` fired before the reader thread was scheduled. End-to-end now works: B210 sustains
**16 MS/s complex lossless**, saturating at ~25-27 MS/s. Original text kept below for the
ruled-out list, which remains useful.

#### (historical) `SoapySource` delivers zero samples

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

### 4.2 ★ ROOT CAUSE FOUND — macOS thread-pool polling burns ~13 cores

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

### 4.3 ~~Graph lifecycle leaks memory~~ — WITHDRAWN. There is no leak.

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

### 4.4 Deferred, in priority order

1. Parallel scaling: 2.07× from 16 chains. Suspects, untested: strided block→thread partitioning
   (`Scheduler.hpp:1378-1385`), absent Darwin QoS (`thread_affinity.hpp`, 15 no-op sites),
   macOS mirror-`memcpy` (`CircularBuffer.hpp:352-378`).
2. UHD provenance — brew's `uhd 4.10` is a poured bottle, the one non-source-verified dependency.
3. `PORTABILITY.md` not yet written.

---

## 5. Mistakes already made — do not repeat

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

## 6. Deliverables and where things live

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

## 7. PLACEHOLDER — fill before session end

<!-- Complete these at ~90% context. Leave the headings; replace the bodies. -->

### 7.1 Outcome of the activation diagnostic — RESOLVED
Instrumenting `ioReadLoop` showed `state=STOPPED` at a 2 s duration and `state=RUNNING` with
`ret=8192` reads at 12 s. **My harness, not gnuradio4.** End-to-end now works; see RESULTS.md
Phase 5. All instrumentation reverted, `SoapySource.hpp` byte-identical to upstream.

**B210 sustains 16 MS/s complex lossless** through 8 DSP stages; saturates ~25-27 MS/s where UHD
reports overflow. That ceiling is NOT the DSP layer (168 Msps single-chain) — it is
USB/UHD/`ioReadLoop`.

### 7.2 New invariants
- **I-11: device-touching tests are NOT parallel-safe.** `DeviceRegistry::findOrCreate` shares one
  device instance per kwargs, so `qa_SoapyIntegration` and `qa_SoapyLoopback` interfere under
  `ctest -j`. Run them serially.
- **I-12: measure the streaming interval, never total elapsed.** Device init (~2.5 s on a B210)
  and graph construction both dwarf short runs. This error invalidated two separate measurements.
- **I-13: the test suite is not deterministic.** `qa_BasicFileIo` varies 8.8 s → 48 s → timeout on
  identical code. The 5-clean-run stability gate is currently unmeetable.

### 7.3 Next-step ordering — REWRITTEN 2026-07-27, the old list is done or void

Items 1 (condvar fix) and 4's scaling entry are complete; §4.2's fix landed and the scaling plateau
turned out to be the buffer, see `DRIFT.md` Category G and `RESULTS.md` §6.

1. **Tag-buffer sizing (tier 2, cheap, large).** `Port::resizeBuffer` gives the tag buffer the same
   *element* count as the stream buffer; at `sizeof(Tag) == 128` that is 16 MiB per connected port.
   This is the whole of the ~6.9 GiB-per-graph footprint. Decouple the two.
2. **`B_max` hardware harness (tier 1).** N radio chains + M synthetic ballast chains in ONE graph
   and scheduler; `B_max` = the largest M with zero overflows for T seconds. Radios alone cannot
   load this machine (3 × ~32 MS/s against 2416 Msps), so they serve as a *deadline probe*. A
   threshold resolvable by bisection beats a noisy Msps figure. RX-only by construction — the TU
   must not include `SoapySink`, and the `nm -C` gate on the linked binary is the check.
3. **Verify the single-channel B210 ceiling.** The recorded 32.5 MS/s may be the 2×2 figure; 1×1
   should reach ~61.44 MS/s. If so the "ceiling" is a configuration artefact, not a transport limit,
   and every `B_max` figure anchored to it is anchored wrongly.
4. **Serialise device tests** — ctest `RESOURCE_LOCK` or a fixture; upstream-shaped, no patch.
5. Deferred: default pool size is `hardware_concurrency()` = 24 on this machine, which puts 8
   workers on utility cores; 16 threads measured faster than 24. `PORTABILITY.md`.

**Do NOT spend time on:** `_nWorkersInWork` cache-line padding or the graph-global `progress`
counter. Profiled at ≤2.5 % and not visible respectively — both are amortised over large
per-`work()` chunks. Revisit only if something else stops dominating.
