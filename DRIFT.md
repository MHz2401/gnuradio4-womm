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

---

## Category E — cherry-picked upstream fixes (provenance table, §4)

**Source: `fair-acc/gnuradio4`, branch `main`.** Fetch-only remote `upstream-fair`; never merged
wholesale. Snapshot `womm-known-good/2026-07-26` tagged before the fetch, per `MANIFEST.md` §8.

### ⚠ Licensing — the tree is no longer purely MIT

`gnuradio/gnuradio4` (our base, `44275ed`, 2026-06-07) is **MIT**. `fair-acc/gnuradio4` relicensed
to **LGPL-3.0-or-later with linking exception** in `8c1dcb4` (2026-06-08, *"Putting the GNU back
into GNU Radio 4.0"*) — one day after our baseline. Four of the five picks postdate that.

**This tree therefore contains LGPL-derived code while `LICENSE` still says MIT.** Accepted
deliberately by the owner: this build is never distributed (§4 — no PR, no push, no upstream
contribution), and LGPL obligations attach to distribution rather than to private use. **If that
ever changes, this section is the thing to read first.** Reverting the four post-relicense picks
(all but `9728d03`) restores an MIT-only tree.

| Pick (ours) | Source SHA | Date | Licence era | Subject | Touches | Why taken |
|---|---|---|---|---|---|---|
| `e929237` | `9728d03` | 2026-05-14 | **MIT** | RT-safe housekeeping for CircularBuffer of aggregate T | Buffer, CircularBuffer, DataSet, Message, Port, Block, qa | prerequisite for the later picks; adds `qa_BlockHouseKeeping` |
| `ef36879` | `83722e1` | 2026-06-23 | LGPL | Fix deadlock when processing kGraphGRC Set messages | Scheduler | a deadlock in the message path |
| `bc4fa45` | `e6f0d92` | 2026-06-27 | LGPL | Schedulers call `init()` when `reset()`ing | Scheduler | restart correctness — a rate sweep restarts repeatedly |
| `86ce450` | `1240bc3` | 2026-06-23 | LGPL | **Prevent watchdog thread leak** | Scheduler | **the blocker.** Watchdogs sleeping during a scheduler restart accumulate and keep running |
| `321a20b` | `2b85bf5` | 2026-06-23 | LGPL | perf: drop per-round `shared_ptr` churn in CircularBuffer scalar queries | CircularBuffer | hot-path allocation churn |

**Order matters.** `1240bc3` and `2b85bf5` conflict if applied first; `83722e1` and `e6f0d92`
move `Scheduler.hpp` toward fair-acc's state and both then apply cleanly. `2b85bf5` conflicts only
in `.gitignore`, resolved in favour of ours to keep the vendor entries.

**Reverse:** `git revert` the five merge-side commits, or reset to `womm-known-good/2026-07-26`.

### E-1 — libc++ portability shim (ours, required by the above)

`9728d03` calls `shrink_to_fit()` on `property_map` (`std::unordered_map`). That is a **libstdc++
extension, not standard**; libc++ does not provide it. Five build errors on Apple clang 21.

fair-acc reverted macOS ARM64 support in `ac59533` (2026-05-04); this commit landed ten days
later. Having stopped building against libc++, libstdc++-only code is no longer caught there — in
violation of their own `CLAUDE.md:249` ("only use features available in both").

Fix: `gr::meta::shrinkIfSupported()` in `meta/include/gnuradio-4.0/meta/utils.hpp`, used in
`Message.hpp` and `DataSet.hpp`; `CircularBuffer.hpp` uses the equivalent inline `if constexpr`.
The helper **must** be a template — a requires-expression inside a non-templated function has no
substitution context and hard-errors instead of yielding `false`. That is exactly why `Message`
(a plain struct) failed while `DataSet` and `CircularBuffer` (class templates) did not.

*Upstream-shaped:* this is the fix upstream would need to take to restore libc++ support.

*Unnecessary when:* upstream drops the call, guards it itself, or restores libc++ CI.

---

## Category F — the thread-pool condvar fix (our most significant change)

`core/include/gnuradio-4.0/thread/thread_pool.hpp`. **Net −4 lines.** Full measurements in
`RESULTS.md` ("THE FIX").

### What was wrong upstream

`worker()` declared a **function-local** `std::mutex` per thread and waited on the *shared*
`_condition` while holding it. POSIX requires all concurrent waiters on one condition variable to
use the **same** mutex. Linux tolerates the violation; macOS returns EINVAL. Upstream's response
was a macOS-only `sleep_for(10 µs)` polling loop (`#if defined(__APPLE__)`), which made every
gnuradio4 process on this machine issue ~2.4 M `nanosleep` syscalls/s from 24 eagerly-created
idle workers — ~13 cores permanently in the kernel.

### What we changed

1. `std::mutex _conditionMutex` added beside `_condition`.
2. Per-thread local mutex removed; the wait takes the shared mutex in a narrow scope.
3. **The `#if defined(__APPLE__)` branch is deleted** — one blocking path for all platforms.
4. Task submission notifies under the shared mutex, closing a lost-wakeup window worth up to
   `keepAliveDuration` (10 s).

### Effect

| Measure | Before | After |
|---|---|---|
| system CPU (`qa_BasicFileIo`) | 71.54 s | **0.30 s** (238×) |
| involuntary context switches | 2,974,438 | **3,817** (779×) |
| total CPU | 74.25 s | **1.58 s** (47×) |
| serial ctest | 100/102, `qa_BasicFileIo` Timeout | **101/102** |
| suite wall time | 461.68 s | **180.56 s** (2.6×) |
| scaling peak | 348.8 Msps | **439.7** (+26 %) |
| B210 ceiling | 26.68 MS/s | **32.50** (+22 %) |

Costs 19 % wall time on one short single-threaded test — inherent condvar wakeup latency versus a
poll that is already spinning. Does not generalise: the full suite got 2.6× faster.

### Reconciliation path

**This is upstream-shaped and should be offered upstream**, licensing permitting — it removes a
platform branch rather than adding one, and fixes a genuine POSIX violation rather than working
around it. *Unnecessary when:* upstream adopts a shared-mutex condvar. Note the fix is **ours**
(original work), so unlike the LGPL cherry-picks it carries no licensing obstacle to contribution.

**Reverse:** `git revert` the fix commit; nothing else depends on it.
