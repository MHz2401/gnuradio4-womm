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

---

## Category G — the double-mapped ring buffer (three defects, one of them ours to report)

`core/include/gnuradio-4.0/CircularBuffer.hpp`, `core/include/gnuradio-4.0/Port.hpp`,
`core/test/qa_buffer.cpp`. Full measurements in `RESULTS.md`.

Found by profiling: **33 % of all CPU time in one worker of a 16-thread run was
`_platform_memmove`** under `~OutputSpan()`, against 42 % in the actual SIMD DSP. That is the
`CircularBuffer` mirror copy, and it runs on **every publish** — not, as previously recorded here,
on every wrap.

### G-1 — the mmap gate tested a Linux syscall number

`#ifdef __NR_memfd_create` gated the whole double-mapped implementation. That is a **Linux syscall
number**, so every other POSIX system silently fell back to the copying path. Darwin has no
`memfd_create` but has POSIX shared memory, which is all this needs: an `shm_open` object
`shm_unlink`ed immediately after creation is the exact equivalent of an anonymous `memfd`, and
nothing is left in the global namespace even on a crash. Two Darwin specifics: names are capped at
`PSHMNAMLEN` (31 incl. the leading slash), and the second mapping needs `MAP_FIXED` because Darwin
treats a bare address as advisory — safe, because the hole was just `munmap`ed. **Linux behaviour
is unchanged.**

### G-2 — the double-mapped path was dead code on EVERY platform

The larger defect, and not macOS-specific. `Graph::connect` always passes `edge._dataResource` to
`Port::resizeBuffer` (`Graph.hpp:695`), and for a host-domain edge that is
`std::pmr::get_default_resource()` — **non-null**. `resizeBuffer` treated any non-null pointer as an
override and so never consulted `BufferType`'s own `DefaultAllocator()`. Net effect: **no
graph-connected port has ever used the double-mapped ring, on any platform**, and Linux users are
paying this copy too. `Graph` spells "no override" as the default resource while `Port` tested for
`nullptr`; `resizeBuffer` now accepts both spellings.

### G-3 — the double-mapped path published without a release fence

**⚠ The path had FIVE defects.** G-2 is why: with the double-mapped allocator unreachable from any
graph, nothing downstream of it was ever exercised, so G-1 and G-3..G-5 could all sit there
indefinitely. Enabling it turned each one up in turn.


Exposed by G-2's fix, and **found on hardware, not by any test**. The `gr::atomicThreadFence()`
before `_claimStrategy.publish(...)` sat *inside* the `if (!_isMmapAllocated)` branch, so the
double-mapped path published with no fence at all. On a weakly ordered ISA the payload stores may
become visible *after* the cursor update, letting a reader observe published-but-unwritten samples.
Harmless on x86's TSO, and invisible everywhere because of G-2. Moved out of the branch.

**How it surfaced:** a B210 at 16 MS/s overflowed at DSP depth ≥ 4 while depth 0–2 were clean, and
throughput could not explain it (238 Msps available against a 16 MS/s need). The full serial `ctest`
was green across three runs with this bug present — **only streaming hardware caught it.** That is
the strongest argument in this project so far for the owner's insistence on hardware in the loop.

### G-4 — the allocator raced any other `mmap` in the process

Exposed by fixing G-3. `do_allocate_internal` mapped 2×, `munmap`ed the upper half, and re-mapped
the object into the resulting **hole**. In that window the hole belongs to nobody, so any other
thread's `mmap` — including the ones libmalloc issues for large allocations — can be placed there.
The re-map then either lands elsewhere (the address check throws) or, with `MAP_FIXED`, **silently
clobbers the victim's mapping.** Measured: intermittent **SIGBUS in ~20 % of 16-thread runs**, and
in principle silent cross-buffer corruption rather than a crash.

Replaced with reserve-then-overlay: reserve the whole 2× range `PROT_NONE`, then `MAP_FIXED` both
halves into a range this process already owns. No hole ever exists, so nothing can take it. Also
simpler than what it replaces — one loop instead of a map/unmap/remap dance and its address check.

### G-5 — the mirror half of every buffer leaked

`do_allocate(required_size)` maps `2 * required_size`; `do_deallocate(p, size)` is called with that
same `required_size` and did `munmap(p, size)` — **unmapping only half**. Every buffer free leaked
its mirror mapping: an unbounded address-space leak that also fragments the space G-4's reservation
has to be placed into, making G-4 progressively more likely in a long-lived process.

### Effect

| Measure | Before | After |
|---|---|---|
| 1 chain, 1 thread | 291 Msps | **454** |
| 16 chains, 16 threads | 985 Msps | **2390** (2.43×) |
| peak, as a multiple of a B210 | 16× | **38.9×** |
| 16-thread scaling | 4.16× | **7.42×** |
| stream buffer allocation | 2× logical size | **1×** |

The copy is memory-bandwidth-bound, so the cores were contending for it — it was flattening the
scaling curve, not merely taxing each core. Lands within 3 % of a mirror-elision probe, confirming
the copy was the whole cost.

### Test change — declared, because it is a test being relaxed

`qa_buffer`'s "power-of-2 fast path preservation" asserted `size == 1024` exactly. A double-mapped
ring cannot be smaller than one page, so the element floor is `page_size/sizeof(T)`: exactly 1024 on
4 KiB pages, **4096 on Apple silicon's 16 KiB**. That assertion encoded a page size rather than the
property it is named for. Rewritten to the real invariant — never shrunk below the request, still a
power of two, a whole number of pages — **not deleted, and not weakened**: it now also asserts page
alignment, which it never checked before. The heavy suites (`WrapAroundAndEdgeCases`, 822 588
asserts; `CursorCacheStaleness`, 815 636) now exercise the double-mapped path and pass.

### Reconciliation path

**All three are upstream-shaped and all three are ours** (original work, no LGPL provenance), so
unlike the Category E cherry-picks they carry no licensing obstacle to being offered upstream. G-2
and G-3 are platform-neutral bug fixes that benefit Linux; G-1 removes a Linux assumption rather
than adding a platform branch.

*Unnecessary when:* upstream fixes the resource-override test, moves the fence, and widens the gate.

**Reverse:** `git revert` the Category G commits. Note G-3 must not be reverted while G-2 stands —
that combination is the data-race one.
