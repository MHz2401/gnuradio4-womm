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

### Rules that bind (from the owner's original brief)

- **No green-washing.** Never disable a test, add `|| true`, blanket `-Wno-error`, or narrow the
  test set to make things pass. A diagnosed failure is a result; a hidden one is a lie.
- **No network fetch** without explicit approval. Source only from hosts with active third-party
  malware monitoring (this ruled out a self-hosted gitea mid-session).
- **Never push. Never touch `main`.** Commit often on the working branch.
- **Ask before the first command that touches hardware** in a new session.
- **Report regressions as prominently as wins.** Label anything not actually measured.

---

## 3. Current state — what works

- Builds clean: 1850 targets, ~600 s at `-j16`, **0 errors, 0 compiler warnings**.
- **ctest 101/102.** The one failure, `qa_SoapySource`, is environmental: it hardcodes
  `{"device", "rtlsdr"}` (`:322`) with no skip guard and no RTL dongle is attached.
- Five upstream correctness fixes cherry-picked (watchdog leak, message-path deadlock,
  init-on-reset, CircularBuffer churn, RT-safe housekeeping) — provenance in `DRIFT.md` Category E.
- One portability fix of ours: `gr::meta::shrinkIfSupported()` in `meta/…/meta/utils.hpp`
  (libc++ lacks `unordered_map::shrink_to_fit`, a libstdc++ extension).
- **Hardware reaches the radio**: `SoapySDRUtil --probe="driver=uhd"` initialises a B210 (FPGA
  16.0, fw 8.0). All three enumerate.

### Performance, measured

Steady state, 20 M samples/chain, median + spread over 7 runs:

| chains | Msps | ×B210 rate |
|---|---|---|
| 1 | 168.8 | 2.75× |
| 16 | 348.8 | 5.68× |

A single chain sustains **2.75× a B210's full rate** through eight multiply/divide stages.
Parallel scaling is poor — 16 chains give only **2.07×**, and 1→2 gains nothing — but absolute
capability is comfortable. Harness: `core/benchmarks/womm_bm_scaling.cpp`.

---

## 4. OPEN — the live problem

### 4.1 THE BUG: `SoapySource` delivers zero samples

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

### 4.2 Second open item: graph lifecycle leaks memory

Seven graph builds → **8 GiB peak RSS** and **1.7 M page reclaims** against ~64 MB of live sample
data. Buffers are not released between graph lifecycles. Fixed cost per build, not per sample.
This dominates any build-run-teardown cycle. Not yet diagnosed.

### 4.3 Deferred, in priority order

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

### 7.1 Outcome of the activation diagnostic
*(which of the three branches in §4.1 it landed on, and what followed)*

### 7.2 Any new invariants
*(promote from OPEN to INVARIANT anything settled today)*

### 7.3 Revised next-step ordering
*(what the next session should do first, given today's result)*
