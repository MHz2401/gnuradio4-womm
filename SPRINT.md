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

## Sprint 2 — "make the guarantees structural, and decide the upstream"

**Proposed goal:** convert sprint 1's *observed* properties into *guaranteed* ones, and take the
upstream decision that sprint 1 deferred.

### Backlog, ordered

| # | Item | Definition of done |
|---|---|---|
| S2-1 | **Define the cross-process arming barrier.** It is currently undefined — see `RESULTS.md` §9.13. It exists as code in `womm_bmax.cpp:233-246` and nowhere else | a written contract: what it guarantees, the bound on the last-arrival window, failure modes, and what happens on timeout. Then an implementation meeting it, then PPS-edge agreement demonstrated across ≥5 runs |
| S2-2 | **Sensible tag defaults.** `tag_interval` is now sample-derived (§9.12); the remaining question is what the *default* should be for a block that does not know its rate at construction | a default that yields ~1 tag/s at any rate without the caller computing it, and a `qa_` test pinning the interval in samples |
| S2-3 | **Soak.** Every tier-1 figure so far is ≤60 s | 4 radios × 2 channels × 1 h, zero overflows, epoch offsets bounded throughout, RSS flat |
| S2-4 | **Upstream decision.** Sprint 1 established the landscape but took no decision | the patch series of `DRIFT.md` restructured into portability classes, `scripts/replay-onto.sh` reproducing `HEAD` from the base exactly, and a recorded decision on (A)/(B)/(C) |
| S2-5 | **`qa_SoapySource` gain test** — the one ctest failure, 101/102 | diagnosed at source level, or a stated reason it is not worth fixing |

### Explicitly NOT in sprint 2

- **`B_max`.** Owner: already done to the practical limit. Some actions — retuning among them —
  *must* drop samples; a ballast bisection past that point measures the wrong thing.
- **UHD from source.** Closed as a decision, not debt: brew is acceptable where the formula has a
  solid, well-reviewed build chain. A source build would drag in Boost + libusb for provenance only.
- Anything automating **transmit**. Standing rule; the owner keys the transmitter.

### Open questions carried forward

Deliberately short. "Why was the old ~44 MS/s figure what it was" is **not** on this list: under the
authoritative-sources rule it is a prior-session timing statement, so it is neither evidence nor a
question. It was replaced, not explained, and that is sufficient.

- Is the 39.8 ms epoch agreement stable across launch timings, or an artefact of this stagger? (S2-1)
- What explains the residual 4.1 Hz between radios — genuine LO synthesis differences, or
  measurement floor? At 1.72 ppb it is near the limit of a 1 s capture.

---

## NEEDS AN OWNER DECISION — goals I cannot source

Listed because building on an unsourced goal is how the last three sessions produced work nobody
asked for. Each of these appears in project docs as an objective or acceptance criterion, and I
cannot trace it to the owner, to a manufacturer specification, or to an upstream statement. Several
predate this session and may be prior-session invention — the same failure mode as the
"cross-process arming barrier", which I used twice as though it named something defined.

| # | The statement | Why I am stuck |
|---|---|---|
| Q1 | **"Fully operational means processing capacity approximately linear-proportional to hardware capacity"** — the tier-1 definition of done | Not measurable as written. Proportional to *what* — core count, ingest ceiling, memory bandwidth? Ingest now sits at the hardware maximum with ~29 % of the machine idle. Does that satisfy it, fail it, or is it about DSP scaling instead? |
| Q2 | **"The soak test is the yardstick for done"** | There is no soak test, and no definition of one, anywhere in the repo. I invented "1 h, zero overflows, RSS flat" for S2-3 out of nothing. Duration and pass criteria are yours to set |
| Q3 | **"5-clean-run stability gate"**, recorded as "currently unmeetable" | Whose gate? Still binding? It is currently blocked by `qa_BasicFileIo` nondeterminism, which may itself no longer be true after the condvar fix |
| Q4 | **Tier 2 = "UI dev; blocks; UI/reconfiguration paths"** | Is a UI actually wanted? `gnuradio4-studio` is GPL-3.0 (against an MIT core) and requires `gr4cp_server`. That is a large, licence-entangled scope to carry on an assumption |
| Q5 | **S2-4's patch-series restructure** (`DRIFT.md` into portability classes, `replay-onto.sh`) | **This was my proposal**, from the planning phase — not yours. It is the durable move if we ever change base, and pure overhead if we never do |
| Q6 | **"Upstream contribution is tier 3 or lower"** | Is contribution intended *at all*? The rules also say no PRs to upstream, and the tree is LGPL-derived while `LICENSE` says MIT (I-8). Tier 3 implies "later"; "never" is also a coherent answer and changes what we bother making portable |
| Q7 | **`qa_SoapySource` gain test**, the one failure at 101/102 | Cause is known (out-of-range gain value). Is a known-cause failure acceptable, or does 102/102 matter? |

**Not blocking sprint 2.** S2-1, S2-2, S2-3 and S2-5 can proceed regardless; only S2-4 depends on an
answer (Q5, Q6). Q1 and Q2 matter for knowing when the project is *finished*, not for what to do next.
