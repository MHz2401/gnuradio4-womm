# DRIFT

Local deviations from upstream, and what would make each unnecessary.

**Baseline:** `origin/main` @ `44275ed` (`4.0.0-RC2-13`). The fork was created 2026-07-25 from
`gnuradio/gnuradio4`, so **all 880 commits and all four branches are upstream's**. Everything
listed here is ours. `blocks/sdr` is upstream code, not local work.

Forward compatibility is a stated objective: prefer upstream-shaped changes (CMake options,
toolchain files, documented flags) over patches to project source. Where a source patch is
unavoidable, keep it small, isolated, and listed here.

---

## Category A — new files (no upstream conflict, trivially reconcilable)

| Path | Purpose | Removal cost |
|---|---|---|
| `MANIFEST.md` | Dependency & Provenance Manifest for GATE 1 | delete |
| `BUILD_JOURNAL.md` | append-only decision log | delete |
| `DRIFT.md` | this file | delete |
| `RESULTS.md` | baseline vs final, scaling curves | delete (not yet created) |
| `PORTABILITY.md` | what to re-check on other Macs | delete (not yet created) |
| `scripts/env.sh`, `scripts/build.sh`, `scripts/verify.sh` | isolated toolchain activation, reproducible build, verification | delete (not yet created) |

These never conflict on rebase. No reconciliation path needed.

## Category A2 — planned, NOT yet done (pending GATE 1)

Listed so it is not mistaken for completed work.

| Planned | Shape | Reconciliation path |
|---|---|---|
| Vendor `libsoundio` into `third_party/libsoundio/` with its unmodified `LICENSE`, built by **our own** CMake rules following the `exprtk` pattern (`CMakeLists.txt:466-481`) | new files + one `CMakeLists.txt` block; CoreAudio backend only | unnecessary if upstream vendors it, gains a `GR_ENABLE_AUDIO` opt-out, or a native CoreAudio backend replaces it |

Rationale in `MANIFEST.md` §4c. Side effect: bypassing libsoundio's own 2.8.5-era CMake makes
`patches/libsoundio-cmake4.diff` permanently moot rather than merely dead.

## Category B — modifications to upstream files

| # | File | Change | Why | What would make it unnecessary |
|---|---|---|---|---|
| B-1 | `.gitignore` | added `.womm-prefix/` and `data/` | upstream ignores `build*/` and `out/` but not an artifact directory | upstream adopting a conventional artifact-directory ignore |

## Category C — configuration-only deviations (no source change)

| # | Deviation | Upstream default | Why |
|---|---|---|---|
| C-1 | `GR_USE_FETCHCONTENT_DEPS=ON` | `OFF` (`CMakeLists.txt:210`) | supplies Boost.UT, vir-simd and cpr from the project's own pins instead of requiring system packages that do not exist on macOS. Documented upstream option — **not** drift in the patch sense. |

---

## Category D — deliberate departures from `CLAUDE.md`

Authorised by the project brief: *"where those conventions optimize for backwards compatibility or
lower-end hardware, 'works fast and stable on this machine' wins"* — logged rather than silently
taken.

### D-1 — build parallelism exceeds `-j6`
`CLAUDE.md:433` mandates: *"Limit parallelism to 6 cores… Never use `-j` without a number or with
values exceeding 6. GR4 template-heavy translation units consume significant memory;
oversubscription causes swapping and OOM kills."*

That constant is calibrated for a 16–32 GB developer laptop. This machine has **192 GiB and 24
cores**; the stated failure mode cannot occur here at any plausible job count. The job count will
be set from **measured peak RSS per translation unit** on the first build, not guessed.

*Unnecessary when:* upstream expresses the limit as a memory-derived formula rather than a constant.

### D-2 — Clang + libc++ instead of GCC-first
`CLAUDE.md:248` lists *"GCC 15+ (libstdc++), Clang 20+ (libc++)"* and `CLAUDE.md:438` ranks GCC 15
as the primary development compiler.

Three reasons to invert that here, all macOS-specific:
1. Upstream's own macOS CI uses Homebrew `llvm@20` + libc++ (`.github/workflows/ci-macos.yml:42,76-78`),
   not GCC. The GCC-first ordering describes the Linux workflow.
2. `blocklib_generator/tools/CMakeLists.txt:13` **unconditionally forces `-stdlib=libc++` under
   Clang**, ignoring the `GR4_USE_LIBCXX` option — so the Clang path is the one that is actually
   exercised.
3. The SoapySDR stdlib-ABI guard (`blocks/sdr/test/CMakeLists.txt:30-64`) detects mismatch by
   shelling out to **`ldd`**, which does not exist on macOS. The guard is **silently inert here**,
   so a libstdc++/libc++ mismatch against a libc++-built SoapySDR would surface as confusing link
   or runtime failures with nothing catching it. Matching libc++ avoids the whole class.

*Unnecessary when:* upstream's macOS guidance is written down (INSTALL.md's macOS section is
currently an empty stub ending in a colon), or the ABI guard gains a Mach-O path.

---

## Upstream defects observed but **not** patched

Recorded so they are not mistaken for our drift, and so they can be reported upstream later if
wanted. None are fixed at this stage.

| Observation | Location |
|---|---|
| `patches/libsoundio-cmake4.diff` is dead code — zero references tree-wide | `patches/` |
| CMake floor documented three ways: 3.25 (README, DEVELOPMENT.md), 3.27 (top-level), 3.28 (real, via `blocklib_generator`) | `CMakeLists.txt:1`, `blocklib_generator/CMakeLists.txt:1` |
| INSTALL.md's macOS section is an empty stub ending in a colon | `INSTALL.md:53-56` |
| `cpr` patch applied with `\|\| true` — a failed patch is silently ignored | `CMakeLists.txt:633` |
| Double-mapped buffer fallback on macOS is silent — no warning, no log, no `static_assert` | `CircularBuffer.hpp:777-783` |
| Every SoapySDR source file is excluded from all CI (no CI image installs SoapySDR) | `blocks/sdr/CMakeLists.txt:9` |
| macOS CI runs only on push-to-main and `workflow_dispatch`, never on PRs | `.github/workflows/ci-macos.yml` |
