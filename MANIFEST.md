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

### 4a. Supplied by CMake's own pins — set `GR_USE_FETCHCONTENT_DEPS=ON`

No version choice by me; these are the project's pins from §2. They land in the build tree, not on
the system.

| Artifact | Ref | Verification |
|---|---|---|
| Boost.UT | `53e17f2…` (full SHA) | git object hash is self-verifying |
| vir-simd | `v0.4.4` | record resolved commit SHA at fetch; re-check on every rebuild |
| cpr | `1.14.1` | record resolved commit SHA at fetch |

### 4b. Built from source into the isolated prefix — **versions are my proposal, to confirm**

| Artifact | Upstream | Proposed ref | Why needed |
|---|---|---|---|
| **cpp-httplib** | `github.com/yhirose/cpp-httplib` | `v0.18.1` — *matching the tree's own Emscripten pin* | required, no native fetch path (§2 item 3) |
| **SoapySDR** | `github.com/pothosware/SoapySDR` | `0.8.1` — matches the ABI the gr4 wrapper checks and the version previously installed here | gr4's **only** radio path |
| **SoapyUHD** | `github.com/pothosware/SoapyUHD` | latest compatible with UHD 4.10.0.0 — **to confirm** | the only route to the three attached B210s |

I will report the exact resolved tag, commit SHA and archive checksum for each **before** building,
and record them in `BUILD_JOURNAL.md`.

### 4c. libsoundio — VENDORED, not fetched (revised per owner direction)

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

### 4c. Struck from the manifest

- **UHD FPGA/firmware images** — no longer needed. `/opt/homebrew/share/uhd/images/` holds 32
  files including `usrp_b200_fpga.bin` and `usrp_b200_fw.hex`; three B210s enumerate.
- **A newer CMake** — not needed. Installed 3.28.1 clears the real floor of 3.28
  (`blocklib_generator/CMakeLists.txt:1`; the top-level says 3.27, docs say 3.25).
- **brew `llvm@20`** — held in reserve. Apple clang 21 is tried first at zero cost; this becomes a
  manifest item only if that fails.

### 4e. Separate approvals — decided

- **`upstream` remote** (`github.com/gnuradio/gnuradio4`, fetch-only, never push) — **APPROVED**,
  subject to the snapshot-first policy in §8.
- **Sandbox UDP egress** — **granted on a whitelist basis when needed**, conditional on this
  session no longer listing `codpcl_LCS` as the project working directory. Not needed until
  Ethernet Octoclock-G work begins, which is currently out of scope.

### 4f. Emscripten — assessed, no action

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
