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
| **libsoundio** | `github.com/andrewrk/libsoundio` | latest release — **to confirm** | required unconditionally; provides the CoreAudio path |
| **SoapySDR** | `github.com/pothosware/SoapySDR` | `0.8.1` — matches the ABI the gr4 wrapper checks and the version previously installed here | gr4's **only** radio path |
| **SoapyUHD** | `github.com/pothosware/SoapyUHD` | latest compatible with UHD 4.10.0.0 — **to confirm** | the only route to the three attached B210s |

I will report the exact resolved tag, commit SHA and archive checksum for each **before** building,
and record them in `BUILD_JOURNAL.md`.

### 4c. Struck from the manifest

- **UHD FPGA/firmware images** — no longer needed. `/opt/homebrew/share/uhd/images/` holds 32
  files including `usrp_b200_fpga.bin` and `usrp_b200_fw.hex`; three B210s enumerate.
- **A newer CMake** — not needed. Installed 3.28.1 clears the real floor of 3.28
  (`blocklib_generator/CMakeLists.txt:1`; the top-level says 3.27, docs say 3.25).
- **brew `llvm@20`** — held in reserve. Apple clang 21 is tried first at zero cost; this becomes a
  manifest item only if that fails.

### 4d. Separate approval, not a dependency

- **`upstream` remote** (`github.com/gnuradio/gnuradio4`, fetch-only, never push). Needed only for
  future resyncs — since you forked this morning, `DRIFT.md` already starts empty without it.
- **Sandbox network egress** for UDP — unrelated to dependencies; needed only if Ethernet
  Octoclock-G work proceeds.

---

## 5. Provenance findings (§7 requires these be raised, not footnoted)

1. **`libsoundio` is effectively unmaintained and is a personal repository.** It is required
   unconditionally by every build. Its own CMake is old enough that the tree once carried a patch
   bumping `cmake_minimum_required` from 2.8.5 → 3.5 for CMake 4.x compatibility
   (`patches/libsoundio-cmake4.diff`). **This is the weakest link in the dependency tree** and it
   sits on the critical path. We are unaffected today (CMake 3.28.1), but it is the item most
   likely to become unbuildable on a future toolchain.
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

1. Fetch §4a/§4b artifacts; record resolved SHAs + checksums in `BUILD_JOURNAL.md`.
2. Build the isolated prefix. Run the isolation proof. **GATE 2.**
3. Phase 3: stock-config baseline build + full ctest, Apple clang 21 first.
