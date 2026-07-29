# HANDOFF — gnuradio4-womm

**Read this before doing anything else.** You are continuing work from multiple sessions.

You are in `.../GR4-fork/gnuradio4-womm/` on branch `womm/m2ultra-wip`, origin
`https://github.com/MHz2401/gnuradio4-womm`. Confirm it; do not assume it.

**Read `SPRINT.md` next.** It holds the current sprint, the milestones already hit with their
measurements, and a table of goals awaiting an owner decision. This document is standing context;
`SPRINT.md` is what to do.

---

## ⚠ `CLAUDE.md` IS UPSTREAM'S FILE, NOT THE OWNER'S

**`CLAUDE.md` in this repository was inherited from `gnuradio/gnuradio4`.** It is the GNU Radio 4
project's own style guide, written for contributors to the upstream tree. **It is not Walter's
personal or local instruction file, and it was not written with this fork in mind.**

Most of it is aligned and worth following — naming, struct layout, the documentation policy, the
AI-anti-pattern list. But some of it is a **non-sequitur in this project's context**, and where it
conflicts, the owner's instructions and this document win. Known conflicts, all deliberate:

| `CLAUDE.md` says | here | why |
|---|---|---|
| limit builds to `-j6` | **`-j16`** | I-5: measured peak 11.4 GiB across 16 compilers = 6 % of RAM |
| GCC 15 is the primary compiler | **Apple clang 21 + libc++** | I-2: upstream's own macOS CI uses clang/libc++, and GCC is not the path here |
| conventions aimed at upstream PRs and public-repo QA | **"Works On My Mac" prevails** | D9: no consideration in support of a PR may constrain it |
| nothing about secrets | **serials, absolute paths and site details stay out of the repo** | it is someone else's file; it does not cover this |

**When `CLAUDE.md` and this document disagree, this document is authoritative.** When in doubt,
ask — do not silently follow upstream's guide into a decision the owner has already made
differently.

---

## WHAT THIS PROJECT IS

Produce the most stable and performant working build of **gnuradio4** on a Mac Studio M2 Ultra,
with a working path to **USRP B210** hardware. The owner is Walter, an experienced SDR user who
reads code, checks claims, and supplies useful measurements of his own. Treat his hypotheses as
evidence to test, not as instructions — he has been right and wrong, and says so either way.

**`womm` is the mission, spelled out: "Works On My Machine."**

Machine: M2 Ultra, 16 P + 8 E cores, 192 GiB, macOS 26.5.2, **16 KiB pages**, 128 B cache line.
**Four B210s** over USB 3 on separate XHCI controllers, disciplined by a common **Octoclock-G**
(10 MHz + PPS, GPS-disciplined). Full Xcode, Apple clang 21.

---

### ⚠ RF TRANSMISSION and U.S. LAW — READ THIS

- **IT IS AGAINST FEDERAL LAW TO TRANSMIT WITHOUT A LICENSE ON MOST RADIO FREQUENCIES**  
- _IT'S ALSO AGAINST FEDERAL LAW TO TRANSMIT (RX) **WITH** MOST LICENSE TYPES ON MOST FREQUENCIES._
  - _This is one reason why GNURadio hardcodes automatable tests for the receive-only (RX-only) RTL/SDR._
- **DO NOT AUTOMATE TRANSMISSION (TX) TESTS: all automated test code should be RX-only.** 

---

## PRIORITY TIERS

| Tier | Scope | Contents |
|---|---|---|
| **1** | **radio running** — performance while a device streams | a fully operational gnuradio4 that works on this machine |
| **2** | radio off | UI dev; blocks; UI/reconfiguration paths |
| **3+** | everything else | upstream contribution, public-repo polish |

**Tier-1 acceptance (owner, 2026-07-28 — `BUILD_JOURNAL.md` D8):** *no loss of lock, and no
**unanticipated** overflow or underflow, during operations.* It applies particularly to **channel
switching, calibration, and graphical display / UI operations**.

**"Unanticipated" is load-bearing.** Some operations *must* drop samples — retuning among them — and
a drop around a deliberate reconfiguration is expected behaviour, not a defect. The criterion is
about surprises, not about zero drops.

This **replaces** "processing capacity approximately linear-proportional to hardware capacity",
withdrawn by its own author: four radios now run at full capacity without denting the machine, so
capacity is no longer the question. **"The soak test is the yardstick"** is retired with it, as
redundant — it meant an extended run at capacity, which is done.

- **A UI is in scope, and "Usable UI" is a tier-1 item** (D10). ⚠ The term is **coined but not yet
  defined** — do not build against it until it is. Defining it is `SPRINT.md` S2-5.
- **Upstream contribution is intended but subordinate** (D9): no consideration in support of a PR
  may constrain the implementation of "Works on My Mac".
- **Do not treat any tier as finished early.** Yesterday's independent issue becomes tomorrow's
  dependency and vice versa.

---

## WHEN TO PUSH

Push at every gate or milestone, and always before ending a session. Push after anything expensive
to recreate, and before anything risky — rebase, reset, upstream fetch/merge, large refactor.
Whenever more than a few commits have accumulated: push. **If in doubt, push.**

---

## WHAT DO YOU MEAN BY "19 % WALL COST" ?

We all make up jargon as we work because it saves time.  Tell everyone what it means.

**Coining shorthand is fine and often necessary — just carry its definition, or a pointer to one,
every time.** The failure is a term that travels without either.

**Worked example, 2026-07-28.** I wrote "a cross-process arming barrier would make the shared epoch
structural" in two `RESULTS.md` sections. The owner asked whether that was defined anywhere. It was
not: it existed as an unnamed code fragment in one harness and nowhere else. **Undefined jargon lets
you refer to a thing you have not built, and then reason as though you had.**

---

## RULES THAT BIND (FROM PROJECT EXPERIENCE)

We're working with the early RC of an open-source project that includes unmaintained dependencies.

- **No green-washing.** Never disable a test, add `|| true`, blanket `-Wno-error`, or narrow the
  test set to make things pass. A diagnosed failure is a result.
- **No network fetch** without explicit discussion and approval. Source only from hosts with active
  third-party malware monitoring.
- **PUSH REGULARLY to `origin`** — it is our fork.
- **No pull requests to upstream** (`gnuradio/gnuradio4*`, `fair-acc/gnuradio4`).
- **Report regressions as prominently as wins.** Label anything not actually measured.
- **⚠ NO SERIALS, ABSOLUTE PATHS, OR SITE DETAILS IN THE REPOSITORY.** Radio serials, capture
  directories, RF parameters for a specific test, and anything else that identifies this machine or
  this site are **semi-secrets** and stay out of version control. Radios are named through
  `$B210U00 … $B210U03` in the operator's shell; output directories are given on the command line.
  This is *not* inherited from `CLAUDE.md`, which is someone else's file and does not cover it.

### ⚠ AUTHORITATIVE SOURCES FOR TIMING (owner, 2026-07-28)

**All prior-session statements about timing are void.** Only these count, in order:

1. tests run in the 2026-07-27/28 session;
2. manufacturer specifications;
3. current statements in docs, comments or sites maintained by the authors of the software and
   drivers in use — gnuradio.org, fair-acc, pothosware, Ettus, Great Scott Gadgets.

A figure from a prior session is not evidence, and is not a question to be resolved either.

---

## UPSTREAM LANDSCAPE — provenance, politics, and what to read

Three trees claim to be GNU Radio 4. They are not interchangeable, and the split is a **licence
dispute, not a technical one**: gnuradio.org insists on MIT, fair-acc on LGPL-3.0.

| | tree | runtime advance since 1 Jun 2026 | macOS | licence |
|---|---|---|---|---|
| **(A)** | [`gnuradio/gnuradio4`](https://github.com/gnuradio/gnuradio4) — our base | none | CI on push-to-main only | MIT |
| **(B)** | [`fair-acc/gnuradio4`](https://github.com/fair-acc/gnuradio4) | **52 commits, real features** | **removed** (`ac59533`) | LGPL-3.0 |
| **(C)** | [`-core`](https://github.com/gnuradio/gnuradio4-core) + [`-library`](https://github.com/gnuradio/gnuradio4-library) + [`-blocks`](https://github.com/gnuradio/gnuradio4-blocks) + [`-studio`](https://github.com/gnuradio/gnuradio4-studio) + [`-control-plane`](https://github.com/gnuradio/gnuradio4-control-plane) | none | `-core` only | MIT; `-studio` **GPL-3.0** |

**(C) is not fresher than (A) — it is (A) repackaged.** Verified 2026-07-27: 13 of 16 sampled
`-core` commit subjects exist in (A) verbatim, dated earlier; the three that do not are the split
mechanics themselves. `-core`'s `thread_pool.hpp` still carries the `__APPLE__` polling loop
byte-identical to (A). Everything (C) added since is CI, packaging and SDK-image work — real, and
entirely infrastructure. **Neither (A) nor (C) has advanced runtime code since 30 May 2026.**

Two qualifiers on (C)'s macOS story: `-blocks` and `-library` have **no macOS CI at all**, and
`-blocks` CI builds against a prebuilt *Linux container* SDK image. Our SDR path lives in `-blocks`,
where SDR is `GR4_ENABLE_SDR=OFF` and CI-untested.

**★ CAMP CHOSEN (owner, 2026-07-28 — `BUILD_JOURNAL.md` D9): gnuradio.org.** That means MIT, and
trees (A)/(C). Near-term policy is **get things done first** — work with LGPL code where it is the
fast path, but **do not irrevocably bake it in**. `DRIFT.md` Category E already keeps every
fair-acc pick separately revertible; that property must not be lost. The owner prefers MIT to
LGPL-3.0, but **"Works on My Mac" outranks the licence preference** for now.

**Consequence:** no upstream will converge with this fork. (A) is over; (C) is (A) plus packaging;
(B) has the code but deleted the platform we need — and our two largest wins *are* macOS enablement.
Worth harvesting from (C): its **installed-SDK boundary**, and **`-control-plane`** (a REST service
for session lifecycle and live block settings **in a separate process** — upstream's architectural
answer to GUI/radio coupling).

Owner's note, worth keeping: **non-round MHz are less crowded than round ones.** 2401.0 MHz sits in
WiFi channel 1 and the ISM traffic is visible in captures.

---

## ★ CONFIRMED — measured, not assumed

Stated separately because this project has accumulated more retractions than confirmations. Detail
in `RESULTS.md` Phase 9, `DRIFT.md` Category H; milestones in `SPRINT.md`.

| # | Result | How it is known |
|---|---|---|
| C-1 | **Two-channel receive works** — first time in this project | Owner read the front-panel LEDs on all four units (A/B frontends, `TX/RX`) — out-of-band evidence no software defect can fake — agreeing with independent per-channel counters |
| C-2 | **It had never worked, for two reasons, both fixed** | H-1: `start()` called `activate()` bare; UHD refuses "stream now" on a multi-channel streamer, reproduced **outside gr4**. H-2: `setHardwareTime` passed `nullptr` → `strlen(nullptr)` → SIGSEGV, unreachable until H-1 was fixed |
| C-3 | **4 radios, 8 channels, 122.88 MS/s, ratio 1.0000** | 43 s; CPU ~53 % user / ~18 % sys / **~29 % idle**. ⚠ **"zero overflows" WITHDRAWN 2026-07-29** — no counter existed; it was inferred from the ratio. Measured with one, the same topology overflows 9–14 times per radio. The *rate* stands. `RESULTS.md` §10.1 |
| C-4 | **A B2xx does 30.72 MS/s aggregate on two channels** (15.36 per channel) | UHD's own refusals; Nyquist relation to the sample clock, per owner |
| C-5 | **All four radios share one PPS epoch** | worst offset **39.8 ms** against a one-second discriminator |
| C-6 | **Content verified — transmitted comb on all 8 channels** | 13 pickets each, spacing 40001.3–40001.4 Hz vs 40000 transmitted, fit rms **3.2–3.5 Hz** |
| C-7 | **★ Four locked radios agree to 1.72 ppb** | LO offsets spread **4.1 Hz** of 2.401 GHz. With a shared 10 MHz, LO error is common-mode, so this is the *transmitter's* offset seen identically. **The number that says four radios can be treated as one instrument** |
| C-8 | **Tag placement is sample-deterministic** | exactly 516096 samples between tags (63 chunks); device-clock deltas 1.008000000 s exactly |
| C-9 | **`UNKNOWN_PPS`, not `PPS`, is the multi-device sync primitive** | `vendor/SoapyUHD/SoapyUHDDevice.cpp:873-874`. Only `set_time_unknown_pps()` waits for a PPS *transition* first, so every radio latches the same edge |
| C-10 | **The antenna is `TX/RX`, never `RX/TX`** | device reports `Antennas: TX/RX, RX2`. Our own allow-list had it reversed and would have rejected the correct name |
| C-11 | **There is no "demand lock on start" flag** — poll `ref_locked` | SoapyUHD forwards `set_clock_source` to UHD with no lock logic; Ettus document loop-until-locked. Sampling once reads `false` on a good reference |

### Instruments that had to be fixed before they could be believed

Each returned a plausible answer regardless of input. Each was caught by a control, none by
inspection. **Build the control first.**

| instrument | failure | caught by |
|---|---|---|
| cross-process count drift | "wanders, no offset" for locked *and* free-running alike; ~300 000-sample skew against a ~900-sample effect | running it on a known-locked pair |
| comb detector v1 | reported 40530 Hz spacing on **silence** — thinning forces ~40 kHz spacing, so the fit manufactured it | running it on a silent capture |
| comb detector v2 | rejected 3 of 4 radios by assuming every peak belongs to the comb; ISM interferers dragged the fit | offsets clustering correctly *despite* rejection |

> **Choose measurements whose competing hypotheses differ by more than the noise, and validate in
> both directions.** An instrument proven only against silence has been shown to say "no", not to
> say "yes".

The epoch check works because its two hypotheses are 1000× apart. The drift test failed because its
two hypotheses were 300× *closer* than the noise.

---

## Current state

- Builds clean: 1850 targets, ~600 s at `-j16`, **0 errors, 0 compiler warnings** under `-Werror`.
- **ctest 101/102 (serial), and the one failure is NOT a defect.** `qa_SoapySource`'s gain test
  hardcodes an RTL-SDR and a gain around 1000 — out of range for a B210, which the test was never
  written for. Upstream's tests cover one platform, and **gain has no universal convention**: a
  value that suits an RTL-SDR suits neither a B2xx nor a HackRF. On a B2xx, RX2 tops out at ~76 dB
  and `TX/RX` at ~88 dB; out of range returns `nan`. Quote the probe section, never a remembered
  number. Withdrawn as a work item by the owner; the residue is a documentation task (`SPRINT.md`
  S2-4).
- **Run ctest SERIALLY.** Device tests are not parallel-safe: `DeviceRegistry::findOrCreate` shares
  one device instance per kwargs, so `qa_SoapyIntegration` and `qa_SoapyLoopback` interfere.
- Five upstream correctness fixes cherry-picked from fair-acc; our own fixes are in `DRIFT.md`
  Categories F (thread-pool condvar), G (double-mapped ring, five defects), H (multi-channel RX).
- **Hardware reaches the radio**: four B210s enumerate, lock to the Octoclock, and stream.

### Known cautions

- **Measure the streaming interval, never total elapsed.** Device init (~2.5 s per B210) and graph
  construction dwarf short runs. This error invalidated two separate measurements.
- **Some actions must drop samples** — retuning among them. Dropped samples around a deliberate
  reconfiguration are expected behaviour, not a defect.
- **Runtime graph reconfiguration is a tier-1 concern**, not a UI one: gr4 is designed to modify a
  flowgraph *while it runs*.
- **The test suite is not fully deterministic.** `qa_BasicFileIo` has historically varied widely;
  re-check whether that survived the Category F condvar fix before treating it as current.

---

## INVARIANTS

Discuss with the owner before departing from these.

| # | Invariant | Why |
|---|---|---|
| I-2 | **Toolchain is Apple clang 21 + libc++**, not GCC | builds the tree with 0 errors, 0 warnings under project-wide `-Werror`; matches brew UHD's own libc++ build |
| I-3 | **Dependencies are vendored** in `vendor/`, SHA-pinned, built into the isolated prefix — **except UHD** | `MANIFEST.md`; `scripts/verify-vendor.sh` proves the committed trees reproduce. UHD stays on brew by owner decision: brew is acceptable where the formula has a solid, well-reviewed build chain |
| I-4 | **Never `brew install`** without asking | `/opt/homebrew` holds the owner's live stack; transitive upgrades would perturb it |
| I-5 | **`-j16` is fine** despite `CLAUDE.md`'s `-j6` | measured: peak 11.4 GiB across 16 compilers = 6 % of RAM |
| I-6 | **Vendored files need `git add -f`** | repo `.gitignore` rule `lib/` silently swallows `vendor/SoapySDR/lib/` |
| I-7 | **`.gitattributes` `vendor/** -text` must stay** | `core.autocrlf=input` otherwise rewrites vendored bytes and breaks verification |
| I-8 | **The tree is MIT on paper, LGPL-derived in fact** | four cherry-picks postdate fair-acc's relicensing; fine while never distributed. `DRIFT.md` Category E |
| I-11 | **Run device tests serially** | not parallel-safe; see Current state |
| I-14 | **Check a high-impact finding against ALL upstreams before assuming it is novel** | fair-acc is better-staffed and sometimes ahead — it had already fixed a tag-ring sizing issue we had not, while being *behind* on all five ring-buffer defects. Track **(B) for code, (C) for packaging and macOS CI, (A) not at all** |

---

## Mistakes already made — do not repeat

Recorded because each cost real time.

1. **A benchmark that measured setup, not steady state.** Always check `time -l` before believing a
   throughput number.
2. **A wedged graph at 1717 % CPU.** A radio source never finishes, so a sink's `n_samples_max`
   cannot end the graph. Use `requestStop()` after a fixed duration *plus* a hard backstop.
3. **`sed`/Python patching without verification.** One insertion appended an include after *every*
   line of a 1162-line file. **Assert a unique anchor and check line counts after any scripted edit.**
4. **An isolation proof using `env -i`**, which stripped the environment so the login shell never
   rebuilt PATH, producing four false differences. A proof harness must reproduce the baseline's
   conditions.
5. **`std::ignore = graph.connect(...)`** hid connection failures. Always check the result.
6. **Recommending fair-acc as the tracking upstream** before noticing it had dropped macOS.
7. **Measuring one channel while believing two were running.** Every figure before 2026-07-27 was
   single-channel; the harness could not tell two live channels from one live and one dead, because
   both read as half rate. The owner caught it by asking. **An aggregate counter cannot detect its
   own blind spot — find an out-of-band check.**
8. **Reading a sensor before the hardware could answer.** `ref_locked`, sampled once immediately
   after `setClockSource()`, reported `false` on a good reference and sent the owner to check
   cabling. Poll to a deadline.
9. **A banner that printed a compiled default while the radio used a runtime value.** Caught in a
   dry run. Print what was actually requested, not what the constant says.

---

## Where things live

| File | Contents |
|---|---|
| `WoMM_README.md` | **cheat sheet: everything unique to this fork, and where it is** |
| `MANUAL.md` | **one-page user manual: `womm-scan` CLI and known-good B210/Soapy parameters** |
| `SPRINT.md` | **current sprint, milestones, and goals awaiting an owner decision** |
| `UI_OPTIONS.md` | UI research across the three trees, feeding D10 |
| `RESULTS.md` | every measurement, including the retractions |
| `DRIFT.md` | every local deviation; Category E is cherry-pick provenance + licensing |
| `MANIFEST.md` | dependency provenance, §8 upstream sync policy |
| `BUILD_JOURNAL.md` | append-only decisions D1–D7, with rationale and how to reverse |
| `scripts/env.sh` | the only thing that activates the prefix |
| `scripts/build-prefix.sh` | builds all vendored deps into the prefix |
| `scripts/verify-vendor.sh` | proves committed vendor trees reproduce |
| `scripts/vendor.sh` | re-fetches vendored deps at pinned SHAs |
| `scripts/epoch-check.sh` | N-radio PPS-epoch comparison from `womm_rx_hold` logs |
| `scripts/spectrum-check.py` | comb detection and per-radio LO calibration from IQ captures |
| `scripts/womm-scan.sh` | the demo CLI — capture from all present radios, optionally analyse |

Harnesses live in `blocks/sdr/src/`: `womm_rx_hold` (hold-open multi-channel RX; capture and tag
modes), `womm_bmax` (ballast/deadline probe), `womm_b210_sweep` (rate sweep). All are RX-only by
construction — `assert_no_tx.cmake` asserts on the **linked binary** that no transmit symbol is
present, because a comment cannot make that guarantee.

Still to write: `scripts/build.sh`, `scripts/verify.sh`, `PORTABILITY.md`.

---

## ⚠ ALWAYS REVIEW PAST DECISIONS AND ATTRIBUTIONS

**This is the most transferable lesson the project has produced.** A conclusion is only as good as
the configuration it was measured in, and configurations change underneath conclusions.

**The worked example (`RESULTS.md` §9.14).** Multi-process was treated as necessary for most of this
project. It was not. Every figure supporting it came from runs that were **single-channel at DSP
depth 8** — crippled by a bug nobody knew about — and the explanation offered at the time implicated
the shared scheduler. When the bug was fixed and full performance existed, one process reached
**within 0.5 %** of four. The topology had been convicted on evidence about something else entirely.

The same shape appears three more times in this project: the "~44 MS/s ingest cap", the retracted
count-drift instrument, and a comb detector that reported a signal on silence. In each case a
plausible explanation attached itself to an observation and then survived unexamined.

**So, on entering a session:**

- **Re-test the load-bearing conclusions**, especially ones inherited from a prior session. Cheap to
  re-run, expensive to build on.
- **When a bug is fixed, ask what that bug was previously blamed for.** Fixing the single-channel
  defect invalidated far more than the figures it directly touched.
- **Ask who measured a claim, in what configuration, and whether that configuration still exists.**
- Under the authoritative-sources rule, **a prior-session figure is not evidence** — but it may still
  be silently shaping a decision. Those are the ones to hunt.
