# SPRINT — gnuradio4-womm

Formal sprint record. Sprint 1 is written **retroactively** on 2026-07-28, at the owner's request,
covering work already done; sprint 2 is planned forward from it.

**AUTHORITATIVE SOURCES FOR TIMING (owner, 2026-07-28).** All prior-session statements about
timing are void. Only these count, in order:

1. tests run in the 2026-07-27/28 session,
2. manufacturer specifications,
3. current statements in docs, comments or sites maintained by the authors of the software and
   drivers in use — gnuradio.org, fair-acc, pothosware, Ettus, Great Scott Gadgets.

A figure from a prior session is not evidence, and is not a question to be resolved either.

Rules this document follows, because the project has been bitten by each:

- A milestone is **measured**, with the measurement named. "Works" is not a milestone.
- **Retractions are listed as prominently as wins.** A withdrawn result is a result.
- Anything not actually measured is labelled as such.

---

## Sprint 1 — "does the radio path work at all, and can we trust what says so"

**Ran:** 2026-07-25 → 2026-07-28 (the tier-1 hardware portion, 2026-07-27/28).
**Goal, as it turned out:** the sprint began aimed at upstream selection and ended aimed at tier-1
hardware, because the first measurement taken invalidated the premise of the second.

### Milestones hit

| # | Milestone | Measurement |
|---|---|---|
| M1 | **Two-channel receive works** — first time in the project | Owner read the front-panel LEDs on all four units (A/B frontends, `TX/RX`), agreeing with independent per-channel counters |
| M2 | **Four radios, eight channels, 122.88 MS/s** | ratio **1.0000** to the hardware maximum, zero overflows, 43 s, ~29 % of the machine idle |
| M3 | **All four radios on one PPS epoch** | worst offset **39.8 ms** against a one-second discriminator (`scripts/epoch-check.sh`) |
| M4 | **Content verified — transmitted comb on all 8 channels** | 13 pickets each, spacing 40001.3-40001.4 Hz vs 40000 transmitted, fit rms **3.2-3.5 Hz** |
| M5 | **Four locked radios agree to 1.72 ppb** | LO offsets +12914.2 to +12918.3 Hz, spread **4.1 Hz** of 2.401 GHz |
| M6 | **Tag placement is sample-deterministic** | exactly 516096 samples between tags (= 63 chunks), device-clock deltas 1.008000000 s exactly |
| M7 | **Upstream landscape established** | (C) `-core` = (A) filtered to `core/` + split + packaging; 13 of 16 sampled commits present in (A) verbatim. Neither has advanced runtime code since 30 May 2026 |

**Against where the sprint started:** `HANDOFF.md` opened with "★ THE TIER-1 BOTTLENECK IS NOW SDR
INGEST" at ~40-44 MS/s. It closes at **122.88 MS/s, 2.8x that, at the hardware ceiling**, with
content and timing verified. Nothing was optimised to achieve it.

### Defects fixed — every one in code that was fully plumbed and never wired

| # | Defect | Found by |
|---|---|---|
| H-1 | `start()` called `activate()` bare; UHD refuses "stream now" on a multi-channel streamer | reproducing outside gr4 with `SoapySDRUtil --channels="0,1"` |
| H-2 | `setHardwareTime` passed `nullptr` for an empty event → `strlen(nullptr)` → SIGSEGV | macOS crash report; unreachable until H-1 was fixed |
| — | the device timestamp was read from every `readStream` and **discarded** | reading the code while looking for a host-independent clock |
| — | `ref_locked` sampled once, before the PLL could acquire | it reported `false` on a good reference |
| — | RX-only antenna allow-list said `RX/TX`; the device spells it `TX/RX` | asking the device |

### Retractions — listed deliberately

| Withdrawn | Why |
|---|---|
| "Total SDR ingest caps at ~40-44 MS/s" (`RESULTS.md` §8, §8.6) | measured 122.88 MS/s at the hardware ceiling |
| Cross-process count-drift instrument (§9.7) | reports "wanders, no offset" for locked *and* free-running alike |
| Comb detector v1 (§9.11) | reported 40530 Hz spacing on **silence** |
| "Check your cabling" (§9.7) | my `ref_locked` read was premature. *One* radio was genuinely disconnected |
| My prediction that LO offsets would differ across radios (§9.11) | carried a free-running assumption into a locked configuration |

### The rule this sprint earned

Three of the failures above are the same bug wearing different clothes: **an instrument that
returns a plausible answer regardless of input.** Each was caught by a control, none by inspection.

> Choose measurements whose competing hypotheses differ by more than the noise, and validate in
> **both** directions. An instrument proven only against silence has been shown to say "no", not to
> say "yes".

The epoch check works because "same edge" and "different edge" are 1000x apart. The drift test
failed because its two hypotheses were 300x *closer* than the noise.

---

## Sprint 2 — "operations under load, without surprises"

**Goal (owner, D8):** *no loss of lock, and no **unanticipated** overflow or underflow, during
operations* — particularly **channel switching, calibration, and UI operations**. This replaces the
linear-proportionality criterion, which the owner withdrew: four radios now run at full capacity
without denting the machine, so capacity is no longer the question.

**"Unanticipated" is load-bearing.** Retuning *must* drop samples. A drop around a deliberate
reconfiguration is expected behaviour; the criterion is about surprises, not about zero drops.

**S2-3 is a PROCEDURE, not an enumeration** (owner, 2026-07-28). Listing every condition that can
cause an overflow up front is open-ended — the set is countable but possibly not finite, and
attempting it first is the expensive way to start. **Evaluate lazily instead:**

> When activity X produces an overflow or underflow: review the operations the radio was asked to
> perform, then use web search, documentation search, and/or ask the owner to establish whether
> those operations cause overflow/underflow **always**, or **only under condition Y**.

The catalogue of anticipated causes is then a *by-product* of running the procedure — it accretes as
conditions are actually met, and each entry arrives with the evidence that put it there. An entry
that is never triggered is an entry nobody needed to write.

### Backlog, ordered

| # | Item | Definition of done |
|---|---|---|
| S2-1 | **Define the cross-process arming barrier**, then implement it. Currently undefined — `RESULTS.md` §9.13; it exists as code in `womm_bmax.cpp:233-246` and nowhere else | a written contract — what it guarantees, the bound on the last-arrival window, failure modes, timeout behaviour — then an implementation meeting it, then PPS-edge agreement across ≥5 runs |
| S2-2 | **Tag defaults.** `tag_interval` is sample-derived (§9.12); the open part is the *default* for a block that does not know its rate at construction | ~1 tag/s at any rate without the caller computing it, and a `qa_` test pinning the interval in samples |
| S2-3 | **★ Operations under load** — the tier-1 criterion as a DIAGNOSTIC PROCEDURE, not an enumeration | a written procedure (below) plus a first pass applying it to retune, gain change and settings change on 4 radios × 2 channels at capacity |
| S2-4 | **Document parameter conventions per platform** — replaces the withdrawn gain-test item | gain, antenna and rate conventions recorded for B210 / RTL-SDR / HackRF, with ranges and the fact that **gain has no universal convention** stated plainly |
| S2-5 | **Define "Usable UI"** (owner, D10 — now a tier-1 item) | a written definition. It is coined but undefined; per the standing jargon rule, **do not build against it until it is defined.** Separate discussion; the definition is the deliverable |

### Explicitly NOT in sprint 2

- **`B_max`.** Owner: already done to the practical limit. Some actions — retuning among them —
  *must* drop samples; a ballast bisection past that point measures the wrong thing.
- **UHD from source.** Closed as a decision, not debt: brew is acceptable where the formula has a
  solid, well-reviewed build chain.
- **The `DRIFT.md` patch-series restructure.** Demoted by D9: near-term policy is get-things-done
  first. The property it was meant to protect — LGPL changes staying separately revertible — is
  already held by `DRIFT.md` Category E and must simply not be lost.
- **`qa_SoapySource`'s gain test.** Withdrawn by the owner: not a defect. The test hardcodes an
  RTL-SDR and a gain of ~1000, which is out of range for a B210 — hardware the test was never
  written for. Upstream's tests cover one platform, and gain has no universal convention. Folded
  into S2-4 as a documentation task.
- Anything automating **transmit**. Standing rule; the owner keys the transmitter.

### Open questions carried forward

Deliberately short. "Why was the old ~44 MS/s figure what it was" is **not** on this list: under the
authoritative-sources rule it is a prior-session timing statement, so it is neither evidence nor a
question. It was replaced, not explained, and that is sufficient.

- Is the 39.8 ms epoch agreement stable across launch timings, or an artefact of this stagger? (S2-1)
- What explains the residual 4.1 Hz between radios — genuine LO synthesis differences, or
  measurement floor? At 1.72 ppb it is near the limit of a 1 s capture.
- What is a "Usable UI"? (S2-5 — the term exists, the definition does not.)

---

## Decisions taken — the sprint-1 open questions are all answered

Owner, 2026-07-28. Full text in `BUILD_JOURNAL.md` D8–D10.

| was | resolution |
|---|---|
| Q1 linear-proportionality | **Withdrawn by its own author.** Replaced by the no-unanticipated-overflow criterion above (D8) |
| Q2 the soak test | **Retired as redundant** — it meant an extended run at capacity, which is done, and is subsumed by D8 |
| Q3 5-clean-run stability gate | **Met and closed.** A prior agent's goal, set without knowing only one channel was ever lit |
| Q4 is a UI wanted | **Yes, and "Usable UI" is now tier 1** (D10). The term is coined but undefined — defining it is S2-5 |
| Q5 patch-series restructure | **Demoted** (D9). Get things done first; keep LGPL separably revertible |
| Q6 upstream contribution | **Intended** — but no consideration in support of a PR may constrain "Works on My Mac" (D9) |
| Q7 gain test | **Not a defect.** Withdrawn; folded into S2-4 as documentation |

---

## Blocks — backlog and light research (2026-07-28)

### What the demo actually uses: four block types, and no DSP at all

`womm_rx_hold` emplaces exactly four: `SoapySource`, `CountingSink`, `BasicFileSink`, `TagSink`.
Source straight to sink. **There is no flow-graph in the demo worth the name** — which is the
owner's point: it is not GR without blocks. `womm_bmax` adds `MultiplyConst`/`DivideConst` as
ballast, but that is synthetic load, not signal processing.

### ⚠ CORRECTION — the "ISM traffic" was an inference, not a detection

I wrote that extra spectral peaks were "2401 MHz ISM traffic" in `RESULTS.md` §9.11 and in session
notes. **That was not measured.** What was measured: radios with better antennas detected 14-17
peaks against a 13-line comb, and the extras failed the comb fit. Attributing them to WiFi came
from *knowing 2401 MHz sits in WiFi channel 1*, not from identifying anything.

They could equally be LO spurs, images, harmonics of the transmitted comb, or another emitter
entirely. **No block in the demo classifies a signal**, because the demo contains no classifier —
the comb detector is a numpy script working on a file after the fact.

Recorded as a correction because it is the same species as the retracted instruments: a plausible
label attached to an unexplained observation. The honest statement is "peaks that are not part of
the comb, origin unidentified".

### In-tree blocks — 44 registered types across 8 modules

| module | count | module | count |
|---|---|---|---|
| testing | 9 | electrical | 6 |
| math | 8 | filter | 5 |
| basic | 7 | timing | 2 |
| sdr | 6 | fft | 1 |

Enumerated from `GR_REGISTER_BLOCK` declarations. **There is no status field** — registration says a
block exists and is constructible, not that it works, is tested, or is Mac-clean. A status list would
have to be built, and the honest axes are: has a `qa_` test / exercised on this platform / exercised
with real hardware. Backlog item, not yet scoped.

### `BufferToTagRatioFromPeriod` — assessment: **probably not a block**

Proposed: given sample rate, buffer size and a desired period, output the nearest integer number of
buffers between tags; lightweight to compare.

**The computation is already implemented, inside `SoapySource`.** §9.12's sample-gated tag emission
computes exactly this: `ceil(rate x interval / chunk)`, measured at 63 chunks for 0.512 MS/s with
8192-sample chunks. It is three lines in the read loop.

**Why a block is the wrong shape for it, as stated:** gr4 blocks are stream processors — they
consume and produce samples. This consumes three *settings* and produces one *number*, which is
configuration, not dataflow. Expressed as a block it would have no meaningful ports, and would add
graph topology to do arithmetic.

**The owner's own caution is the right one, and points at the case where it IS a block.** If the
rate changes *at runtime* — retune, resampling, a settings message — then something must recompute
the interval and push it to the tagging block. That is a genuine dynamic-flowgraph concern, gr4 has
the message machinery for it (`Scheduler.hpp` handles settings and graph-edit messages), and a small
block that watches `sample_rate` and emits a settings update is expressible and useful.

**Recommendation:** keep it a helper function in the settings path for now — that is where the logic
already lives and it costs nothing. Revisit as a block **only** when a runtime rate change actually
needs to drive tag interval automatically. That condition has not arisen; per the S2-3 principle,
evaluate it lazily.
