# Dependency & Provenance Manifest — GATE 1

**Repository root:** `/Users/whom/dvel/ghzhub/GR4-fork/gnuradio4-womm`
**Base commit:** `44275ed` (`4.0.0-RC2-13`) · **Branch:** `womm/m2ultra-wip`
**Status: AWAITING APPROVAL. Nothing in this document has been fetched or installed.**

Every claim in §1–§3 was verified by reading this checkout, with line citations. Items in §4 are
*proposals* I must choose, and are marked as such — their exact tags and checksums will be
confirmed and reported back **before** anything is installed.

---

## 1. What a default configure does

`GR_USE_FETCHCONTENT_DEPS` is **`OFF`** by default (`CMakeLists.txt:210`). Verified: a default
native configure performs **zero network access**. Only Emscripten builds fetch unconditionally.

There are **no** `ExternalProject_Add`, **no** CPM, and **no git submodules** anywhere in the tree.

## 2. In-tree fetch points — all four, verbatim

| # | Name | Repository | Pinned ref | Ref kind | Gate (verified) |
|---|---|---|---|---|---|
| 1 | `ut` (Boost.UT) | `https://github.com/boost-ext/ut.git` | `53e17f25119598c6458d30351b260193096ba67e` | **full 40-char SHA — immutable** | `:489` `if(EMSCRIPTEN OR GR_USE_FETCHCONTENT_DEPS)` |
| 2 | `vir-simd` | `https://github.com/mattkretz/vir-simd.git` | `v0.4.4` | tag | `:489`, same gate |
| 3 | `cpp-httplib` | `https://github.com/yhirose/cpp-httplib.git` | `v0.18.1` | tag | `:525` **`if(EMSCRIPTEN)` ONLY** |
| 4 | `cpr` | `https://github.com/libcpr/cpr.git` | `1.14.1` | tag | `:620` `if(GR_USE_FETCHCONTENT_DEPS)` inside `if(CURL_FOUND)` |

**Nothing is floating.** No branch names, no `HEAD`, no `master`/`main` refs. That is a good
result for this gate.

**Critical detail — item 3.** `cpp-httplib`'s FetchContent is gated on **`EMSCRIPTEN` alone**
(`CMakeLists.txt:525`), *not* on `GR_USE_FETCHCONTENT_DEPS`. Turning that option on therefore does
**not** supply cpp-httplib for a native macOS build; the `else()` branch demands it from the system
and hard-fails at `:536`/`:546`. This is why §4 exists.

## 3. Hard requirements with no fetch path

Each of these terminates configure with `FATAL_ERROR` if absent. **None are currently present on
this machine** (verified: no `boost/ut.hpp`, `vir/simd.h`, `httplib.h`, `soundio/soundio.h`
anywhere; `pkg-config` finds no `libsoundio` or `cpr`).

| Dependency | Discovery | Fails at | Covered by `FETCHCONTENT_DEPS=ON`? |
|---|---|---|---|
| Boost.UT | `find_package(ut CONFIG)` → `find_path(boost/ut.hpp)` | `:511` | ✅ yes |
| vir-simd | `find_path(vir/simd.h)` | `:520` | ✅ yes |
| **cpp-httplib** | `find_package(httplib CONFIG)` → `pkg_search_module(cpp-httplib httplib)` | `:536`, `:546` | ❌ **no — native builds must supply it** |
| **libsoundio** | `pkg_search_module(soundio libsoundio)` → `find_path`/`find_library` | `:581` | ❌ **no fetch path exists at all** |
| libcurl | `find_package(CURL)` | `:615` | n/a — ✅ satisfied by the macOS SDK (8.7.1) |
| cpr | `find_package(cpr CONFIG)` | `:647` | ✅ yes |
| SoapySDR | `find_package(SoapySDR CONFIG)` — **not** `REQUIRED` | — | ❌ optional; silently disables all radio blocks |

`libsoundio` is **required unconditionally for every non-Emscripten build** (`:557`, `:581`) — there
is no `OFF` switch. It cannot be skipped even though audio is item 2 on the hardware priority list.

## 4. Artifacts to be fetched — APPROVAL REQUESTED

Route: **isolated project-local prefix, zero Homebrew perturbation.** Rationale — `/opt/homebrew`
holds 231 formulas, `brew install` can upgrade transitive dependencies, and you have just
deliberately removed a tap-sourced stack for provenance reasons. Every artifact below gets a
recorded URL, ref and checksum instead of inheriting a tap's provenance.

### 4a. Copy-and-carry — vendored header-only sources (revised per owner direction)

**`GR_USE_FETCHCONTENT_DEPS` stays `OFF`. There is no FetchContent in this build at all.**

Owner's reasoning, adopted: (A) a pinned SHA guarantees *integrity*, not *availability* — immutable
things still disappear; (B) processor changes may require source changes; (C) the code may need to
change *now*. Point (C) is load-bearing here: the project compiles `-Werror`, and Apple clang 21 is
newer than the clang 20 upstream CI tests, so a third-party header emitting a new diagnostic is a
hard build stop. Vendored sources are patchable in-tree with a `DRIFT.md` line; fetched sources
would have to be patched at configure time — the `|| true` hack upstream already resorts to for cpr
(`CMakeLists.txt:633`).

All three are header-only, verified from what CMake looks for:

| Artifact | Upstream | Ref | CMake discovery | Size |
|---|---|---|---|---|
| **Boost.UT** | `github.com/boost-ext/ut` | `53e17f25119598c6458d30351b260193096ba67e` (full SHA, from the tree) | `find_path(… boost/ut.hpp)` `:509` | single header |
| **vir-simd** | `github.com/mattkretz/vir-simd` | `v0.4.4` (from the tree) | `find_path(… vir/simd.h)` `:517` | headers only |
| **cpp-httplib** | `github.com/yhirose/cpp-httplib` | `v0.18.1` (matching the tree's Emscripten pin) | `find_package(httplib CONFIG)` `:532` → pkg-config | single header |

Vendored under `third_party/`, each with its upstream `LICENSE` unmodified, following the existing
`magic_enum` pattern (`CMakeLists.txt:450-463`). Resolved commit SHA and checksum recorded in
`BUILD_JOURNAL.md`. If none is multi-megabyte the whole set is a handful of files.

### 4b. Built from source into the isolated prefix — **versions are my proposal, to confirm**

Compiled here from fetched, checksummed source. **No binaries are downloaded.** Building with our
own compiler is what makes the ABI match by construction — it is the mechanism that avoids the
libstdc++/libc++ mismatch, not an accident of it.

**FETCHED — actual pins below. Both are master commits, not release tags.** Investigating the
tags first (owner's "pessimism saves time") changed both choices:

| Artifact | Pinned SHA | Rejected | Why the tag was wrong |
|---|---|---|---|
| **SoapySDR** | `1551ea0d39ce546b32a15808b9b1241018a89fc8` (2026-01-02) | `soapy-sdr-0.8.1` (2021-07-25, 109 commits behind) | 0.8.1 lacks `ea8b3c1 Fix macOS rpath installation` and `101d1f4 os-x-clang-fix`, plus newer-CMake compat |
| **SoapyUHD** | `2a5d381f68fd05d5b3c0e7db56c36892ea99b4ae` (2025-10-05) | `soapy-uhd-0.4.1` (2020-09-20, 20 commits behind) | **0.4.1 predates UHD 4.8 and would not build against our UHD 4.10.** master adds `post_input_action`/`post_output_action` for UHD 4.8+ (`ryanvolz`, PR #61), newer-CMake compat, and the C++14 the UHD headers require |

Verified post-fetch: `post_input_action`/`post_output_action` present in
`vendor/SoapyUHD/UHDSoapyDevice.cpp:711,863`; CMake floors are now range syntax
(`2.8.12...3.10`, `3.3...3.10`), so the antique-CMake problem is gone from both.

**ABI consequence, accepted deliberately.** master moves `SOAPY_SDR_ABI_VERSION` from `"0.8"` to
`"0.8-3"` (`vendor/SoapySDR/include/SoapySDR/Version.h`). Harmless while we build SoapySDR *and*
every module ourselves — they are self-consistent. It would bite only if a **prebuilt third-party
module** compiled against `"0.8"` were mixed in; such a module will refuse to load. Relevant later
if HackRF is approached via a packaged SoapyHackRF rather than a source build.

**On "unreleased master" as a risk:** in a copy-and-carry model we pin an exact SHA either way, so
tag-vs-branch is not the safety property — the pinned, checksummed, locally-carried tree is.

**Patch risk remains open.** Both are being compiled against a macOS 26 SDK and clang 21 for the
first time. Exposure is bounded — each builds as its own project with its own flags, so gr4's
`-Werror` does not reach them — and any patch gets reported before it is applied, not fixed quietly.

### 4c. libsoundio — VENDORED at master, NOT at any release tag

**FETCHED. Pinned to `49a1f78b50eb0f5a49d096786a95a93874a2592a`** (master, 2023-07-05) —
*"Updating support for coreaudio to macos 12. kAudioObjectPropertyElementMaster ->
kAudioObjectPropertyElementMain"*. Verified: 31 occurrences of `kAudioObjectPropertyElementMain`
in `vendor/libsoundio/src/coreaudio.c`. LICENSE is MIT (Expat), © 2015 Andrew Kelley, intact.

**The newest-looking tags are a trap and were rejected.** `2.0.1-5/6/7` (April 2024) postdate
master but live on the **`zig-pkg` branch** — 25 commits of Zig build-system packaging. Confirmed
with `git merge-base --is-ancestor`: **`2.0.1-7` does NOT contain master's CoreAudio fix.** Picking
by version number would have silently produced a build broken on modern macOS. `macos9` (2015) and
`v2` (2016) are both dead ends.

Selecting an untagged commit costs nothing here: we pin the SHA ourselves.

**Changed from §4b at the owner's direction.** libsoundio is *not* to be pulled from an upstream
URL at build time. Instead: take one well-marked MIT-licensed snapshot, vendor it, and
self-maintain it only insofar as underlying libraries (i.e. CoreAudio) change.

Engineering rationale: an MIT grant on a *received copy* cannot be retroactively revoked. A future
release could be relicensed, but the snapshot we take under MIT remains MIT. Vendoring therefore
converts an open-ended dependency on a third party's future licensing into a one-time,
permanently-granted one. The `LICENSE` file is vendored alongside the source, unmodified, so the
grant travels with the code.

**Implementation follows the existing in-tree `exprtk` pattern** (`CMakeLists.txt:466-481`):
vendored upstream sources plus **our own** `add_library(... STATIC ...)` and compile options,
bypassing upstream's build system entirely.

Three consequences, all favourable:
1. We never invoke libsoundio's `cmake_minimum_required(VERSION 2.8.5)`, so the CMake-4.x
   incompatibility disappears permanently and `patches/libsoundio-cmake4.diff` becomes moot rather
   than merely dead.
2. We compile **only the CoreAudio backend** plus core files — roughly `soundio.c`, `util.c`,
   `os.c`, `channel_layout.c`, `ring_buffer.c`, `dummy.c`, `coreaudio.c`. ALSA, PulseAudio, JACK
   and WASAPI backends are never compiled. Smaller auditable surface, less to maintain. *(Exact
   file list to be confirmed against the snapshot; the shape is what matters here.)*
3. `pkg_search_module` / `find_library` discovery is bypassed, so the `FATAL_ERROR` at
   `CMakeLists.txt:581` is satisfied by our own target rather than by a system package.

*Longer term, and explicitly not this project's job:* a clean CoreAudio wrapper that replaces
libsoundio entirely would remove the dependency. Noted as a direction, not scheduled.

**Approval still needed** for the one-time fetch of that snapshot — it is a network fetch like any
other, and its resolved commit SHA, checksum and `LICENSE` text will be recorded before use.

### 4d. cpr and `GR_ENABLE_HTTP` — OPEN DECISION, needs your call

Not previously raised. `GR_ENABLE_HTTP` defaults to **`"ON"`** (`CMakeLists.txt:222`), which
requires **libcurl** (satisfied by the macOS SDK, 8.7.1) **and cpr** (absent). Two ways forward:

| Option | Effect | Cost |
|---|---|---|
| **(a) Build cpr `1.14.1` from source into the prefix** *(default)* | HTTP blocks build and are tested; nothing excluded | one more dependency; cpr is a real library, not header-only |
| **(b) Set `GR_ENABLE_HTTP=OFF`** | drops cpr *and* libcurl; `blocks/http` is excluded | **excludes a module from the build — needs your explicit approval per the project rules.** Coverage loss is modest: `qa_HttpBlock` is already not built on macOS (`blocks/http/test/CMakeLists.txt:1`) |

I am **not** proposing the third path, `GR_ENABLE_HTTP=OPTIONAL`: with libcurl found but cpr
missing it disables HTTP with no diagnostic (`:643-650`) — a silent fallback, which is precisely
what the brief forbids.

**Note:** cpp-httplib is required **regardless** of `GR_ENABLE_HTTP` — its `FATAL_ERROR` at `:536`
and `:546` sits outside the HTTP block entirely. So option (b) does not remove it; it stays in §4a.

Default is **(a)** unless you say otherwise — it keeps the "no exclusions" rule intact, and for an
SDR performance project the HTTP blocks are irrelevant either way, so the honest tiebreak is
"don't disable things to make the build easier."

### 4e. Struck from the manifest

- **UHD FPGA/firmware images** — no longer needed. `/opt/homebrew/share/uhd/images/` holds 32
  files including `usrp_b200_fpga.bin` and `usrp_b200_fw.hex`; three B210s enumerate.
- **A newer CMake** — not needed. Installed 3.28.1 clears the real floor of 3.28
  (`blocklib_generator/CMakeLists.txt:1`; the top-level says 3.27, docs say 3.25).
- **brew `llvm@20`** — held in reserve. Apple clang 21 is tried first at zero cost; this becomes a
  manifest item only if that fails.

### 4f. Separate approvals — decided

- **`upstream` remote** (`github.com/gnuradio/gnuradio4`, fetch-only, never push) — **APPROVED**,
  subject to the snapshot-first policy in §8.
- **Sandbox UDP egress** — **granted on a whitelist basis when needed**, conditional on this
  session no longer listing `codpcl_LCS` as the project working directory. Not needed until
  Ethernet Octoclock-G work begins, which is currently out of scope.

### 4g. Emscripten — assessed, no action

`EMSCRIPTEN` is **not an option and cannot be "pulled in" or "cut out."** Verified: no
`option()` for it exists anywhere; CMake sets the variable only when the Emscripten toolchain file
is used. On a native build it is false, and all Emscripten code is behind `#if
defined(__EMSCRIPTEN__)` compile-time branches that never compile. Keeping it costs **zero**;
removing it would mean deleting upstream code for no benefit — pure drift.

**Verdict: leave it alone.** The only way it touches us is that `cpp-httplib`'s FetchContent is
gated `if(EMSCRIPTEN)` (§2 item 3), which is an upstream gap in *native* dependency handling rather
than a cost imposed by Emscripten.

---

## 5. Provenance findings (§7 requires these be raised, not footnoted)

1. **`libsoundio` is effectively unmaintained and is a personal repository** — required
   unconditionally by every build, and therefore on the critical path. Its own CMake is old enough
   that the tree once carried a patch bumping `cmake_minimum_required` from 2.8.5 → 3.5 for CMake
   4.x compatibility (`patches/libsoundio-cmake4.diff`). It was the weakest link in the dependency
   tree. **RESOLVED by §4c** — vendoring a pinned MIT snapshot and compiling it with our own build
   rules removes both the build-system fragility and the exposure to future relicensing.
2. **`patches/libsoundio-cmake4.diff` is dead code** — zero references tree-wide. It implies a
   libsoundio FetchContent path existed and was removed, leaving libsoundio system-only.
3. **Three of four in-tree pins are tags, not SHAs** (`vir-simd`, `cpp-httplib`, `cpr`). Tags are
   re-taggable upstream. Not "floating" in the branch sense, but weaker than the `ut` SHA pin.
   Mitigation: record the resolved commit SHA at first fetch and assert it on every rebuild.
4. **`cpr`'s patch is applied with `|| true`** (`CMakeLists.txt:633`) — a failed patch is silently
   ignored, producing a differently-configured cpr with no diagnostic. Worth asserting the patch
   actually applied.
5. **`vir-simd`, `cpp-httplib`, `libsoundio` and ExprTk are personal repositories** rather than
   organisations. Widely used, but noting it per §7.
6. **Vendored, never fetched:** `magic_enum` 0.9.3 and ExprTk (pinned at a full SHA in
   `third_party/download_external_deps.sh`, which is **not** invoked by any CMake code).

---

## 6. Toolchain isolation design

**Prefix:** `<repo>/.womm-prefix/` — inside the workspace, gitignored, deletable without trace.
Nothing is written to `/opt/homebrew`, `/usr/local`, or any system path.

**Activation:** exactly one sourceable script, `scripts/env.sh`, which prepends only
`.womm-prefix/bin` and sets `CMAKE_PREFIX_PATH` / `PKG_CONFIG_PATH`. No edits to `~/.zshrc`,
`~/.zprofile`, `/etc/paths`, or any login shell config. No `brew link`.

**Isolation proof.** Baseline `which -a` for `cc c++ clang clang++ gcc g++ cmake ninja make python3
git pkg-config` was captured in a fresh, unactivated shell during Phase 0 — all C/C++ compilers
resolve to `/usr/bin/*`. After installation the same capture is repeated in a **fresh unactivated
shell** and diffed. **Any movement stops the work and gets reported.**

**Rollback.** `rm -rf .womm-prefix build*` plus `git checkout main` restores this machine exactly.
No system state is modified, so there is nothing else to undo.

---

## 7. What happens after approval

1. Fetch §4a/§4b/§4c artifacts; record resolved SHAs + checksums in `BUILD_JOURNAL.md`.
2. Build the isolated prefix. Run the isolation proof. **GATE 2.**
3. Phase 3: stock-config baseline build + full ctest, Apple clang 21 first.

---

## 8. Upstream sync policy (approved)

Governs the fetch-only `upstream` remote. **Check often, update rarely and deliberately.**

**Before any fetch or merge from upstream** — snapshot the current known-good downstream state
first, so there is always a labelled point to return to:

```
git tag womm-known-good/<YYYY-MM-DD>  womm/m2ultra-validated
git branch womm-snapshot/<YYYY-MM-DD> womm/m2ultra-validated
```

Cheap, and no case has been found where it is impractical.

**Cadence.** The *check* is periodic and read-only — compare `upstream/main` to our base and
summarise what landed. The *update* is event-driven, triggered by one of:
- **mass of code** — enough has accumulated that deferring makes reconciliation harder than doing it, or
- **impact of functionality** — something lands that materially affects this machine, this
  toolchain, or the four Darwin divergences in the recon report.

**Explicitly not a trigger:** volume alone. Platform work irrelevant to an M2 Ultra — new
single-board-computer variants, other-architecture backends, CI changes for platforms we do not
build — is skipped regardless of line count, and the decision is recorded rather than silently taken.

**Never:** push to any remote; merge into `main`; rebase or rewrite anything already published.
`main` stays a clean mirror of `origin/main`.
