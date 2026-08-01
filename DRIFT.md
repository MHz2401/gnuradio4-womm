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

### Checked against both upstreams — all five are novel, 2026-07-27

Scanned at the owner's suggestion, because a high-impact finding is worth checking against the
better-staffed fork before assuming it is new. Three trees compared: ours, `fair-acc/main`
(`92278b6`), and `gnuradio/gnuradio4-core` (`c35f5c8`, the split-repo direction gnuradio.org is
moving to — alongside `-library`, `-blocks`, `-studio`).

| Defect | ours | fair-acc | gnuradio4-core |
|---|---|---|---|
| G-1 Linux-only mmap gate | fixed | unfixed | unfixed |
| G-2 dead code / resource override | fixed | **unfixed** | **unfixed** |
| G-3 missing release fence | fixed | **unfixed** | **unfixed** |
| G-4 mmap hole race | fixed | **unfixed** | **unfixed** |
| G-5 deallocate unmaps half | fixed | **unfixed** | **unfixed** |
| G-6 tag ring sized per-sample | **was unfixed** | **FIXED** | unfixed |

**All five are present in both upstreams.** G-1 is expected — fair-acc dropped macOS ARM64
(`ac59533`), so a Linux-only gate costs them nothing. But **G-3, G-4 and G-5 are latent on Linux**
in both trees: they are unreachable only because of G-2, and would bite the moment anyone fixed it.
G-4 in particular is a silent-corruption bug, not merely a crash.

Line anchors as of the scan: fair-acc `Port.hpp:930` (G-2), `CircularBuffer.hpp:119` (G-4),
`:153` (G-5), `:382`/`:408`/`:410` (G-3). gnuradio4-core `Port.hpp:856`, `CircularBuffer.hpp:156`,
`:352`/`:378`/`:380`.

### G-6 — the tag ring was sized per-sample (fair-acc had already fixed this; we had not)

The one place the traffic ran the other way. `Port::resizeBuffer` gave the tag buffer the **same
element count** as the stream buffer, but a tag ring holds only tags still *in flight*, never a slot
per sample. At `sizeof(Tag) == 128` on Apple ARM64 a 65536-sample edge cost **16 MiB of tags against
256 KiB of stream**, and that single term was most of a graph's footprint.

Capped at `std::min(min_size, kDefaultBufferSize)`, matching fair-acc's approach (`92278b6`).
Measured on the 16-chain graph: **peak RSS 6.92 GiB → 2.27 GiB (3.05×)**, page reclaims 423 210 →
139 367, throughput unchanged (2409 vs 2416 Msps, within noise).

This closes the tier-2 item that §4.3's withdrawn "leak" turned out to actually be.

### Reconciliation path

**All three are upstream-shaped and all three are ours** (original work, no LGPL provenance), so
unlike the Category E cherry-picks they carry no licensing obstacle to being offered upstream. G-2
and G-3 are platform-neutral bug fixes that benefit Linux; G-1 removes a Linux assumption rather
than adding a platform branch.

*Unnecessary when:* upstream fixes the resource-override test, moves the fence, and widens the gate.

**Reverse:** `git revert` the Category G commits. Note G-3 must not be reverted while G-2 stands —
that combination is the data-race one.

---

## Category H — multi-channel receive was impossible (two defects, both ours to report)

`blocks/sdr/include/gnuradio-4.0/sdr/SoapySource.hpp`,
`blocks/sdr/include/gnuradio-4.0/sdr/SoapyRaiiWrapper.hpp`. Found 2026-07-27 by the E0.0
hold-open harness, at the owner's suggestion that a single-channel run could be masquerading as a
two-channel one. It was worse than that: **`num_channels > 1` had never worked on real hardware at
all**, and every throughput figure this project has recorded is single-channel.

Same shape as Category G: the second defect was unreachable until the first was fixed.

### H-1 — `start()` cannot express a timed start, so UHD refuses the stream

`SoapySource::start()` called `_rxStream.activate()` **bare** — `flags=0, timeNs=0, numElems=0`,
which SoapySDR maps to "stream now". UHD **rejects** that when one streamer covers several
channels:

> `RuntimeError: Invalid recv stream command - stream now on multiple channels in a single streamer will fail to time align.`

Reproduced outside gr4 with `SoapySDRUtil --rate=15.36e6 --channels="0,1" --direction=RX`, so it is
a UHD contract, not a gr4 bug — but gr4 had **no way to satisfy it**. `RESULTS.md` §8.4 had already
recorded the missing `start_time` setting as a *synchronisation* gap; it is in fact a hard
prerequisite for two-channel operation.

Fix: new `start_time_offset` setting (seconds, 0 = start now). When `num_channels > 1` or the
offset is set, `start()` zeroes the device clock and arms the stream 0.1 s ahead. Single-channel
behaviour is unchanged — verified by re-running the 1-channel `womm_bmax` path (19.81 MS/s, PASS).

### H-2 — `setHardwareTime()` segfaults on its own default argument

Exposed by H-1, and **found on hardware, not by any test**. `SoapyRaiiWrapper.hpp:487` passed
`event.empty() ? nullptr : event.c_str()`. The C shim forwards that straight into
`Device::setHardwareTime(long long, const std::string&)`, so a **null pointer is constructed into a
`std::string`** — `strlen(nullptr)`, `EXC_BAD_ACCESS at 0x0`. The sibling `getHardwareTime` at
`:484` already passes `c_str()` unconditionally, so the wrapper contradicted itself.

Fix: pass `event.c_str()`; an empty string is the documented "no event" value. The other
`empty() ? nullptr` in the file (`:840`, `setupStream`) is a `SoapySDRKwargs*` where null **is** the
documented convention — correct, left alone.

`setHardwareTime` and `getHardwareTime` were both **entirely uncalled** before this session
(`RESULTS.md` §8.4), which is why a crash on the default argument survived in shipped code.

### Result

`womm_rx_hold`, one B210, 2 channels, depth 0, MCR 30.72 MHz:

| | rate | verdict |
|---|---|---|
| ch0 | 15.36 MS/s | LIVE |
| ch1 | 15.36 MS/s | LIVE |

Sample counts identical between channels across the whole run — the timed start aligned them.

### ⚠ The B2xx two-channel ceiling, measured

UHD refuses `master_clock_rate > 30.72 MHz` when two RX channels are active, and per-channel rate
is MCR/decimation. So a B210 sustains **30.72 MS/s aggregate across both channels** — this is the
"~32 MS/s with two front ends" figure in `HANDOFF.md`, which is therefore **an aggregate per radio
and is correct**. Requesting 20 MS/s per channel pins MCR at 40 MHz and is rejected outright; with
MCR pinned to 30.72 UHD silently delivers 15.36.

### Reconciliation path

Both are **ours** (original work, no LGPL provenance) and both are upstream-shaped: H-2 is a
one-token null-pointer fix, H-1 adds a setting that the wrapper already had primitives for
(`activate(flags,timeNs,numElems)` at `:750`, `setHardwareTime` at `:486`). Neither carries a
licensing obstacle to being offered upstream.

*Unnecessary when:* upstream wires a timed start into `SoapySource::start()` and stops passing
`nullptr` to `setHardwareTime`.

**Reverse:** `git revert` the Category H commit. H-2 must not be reverted while H-1 stands — that
combination is the segfault.

---

## Category J — SoapyUHD divergence: multi-device sync + discoverable settings (2026-08-01)

**Owner authorised modifying SoapyUHD** after the licence question was settled: Tier 1 "Works
On My Mac" outranks Tier 3 licence purity, and `wosem` (Works On Someone Else's Mac) is a later
epoch we are not working on. ⚠ **SoapyUHD is GPL-3.0**, so this is a GPL derivative. Fine while
never distributed; it must not be absorbed into MIT-licensed code.

**Carried as `patches/womm/SoapyUHD-0003-multi-device-sync-and-discoverable-settings.patch`**,
in the existing patch-series style: `vendor/SoapyUHD` stays byte-exact and
`scripts/verify-vendor.sh` still passes (confirmed after the change).

### What it adds, and why it belongs in the driver rather than in our block

**1. `SYNC_DEVICES` — one command, every radio.** A USB B2xx is one device with one mboard
(`b200_impl.cpp:309` hardcodes `/mboards/0`; `UHDSoapyDevice.cpp:215` does the same on the
reverse bridge), so a `multi_usrp` can never span several of them and `ALL_MBOARDS` cannot
reach them. Separate devices had no way to receive one command together, and **every**
application driving more than one B2xx has had to rebuild the same host-side loop — as ours
did. The patch keeps a process-wide registry of open UHD devices, detects **one** PPS
transition, and broadcasts `set_time_next_pps()` to all of them so they latch the **same** edge.

Measured on four B210s with `get_time_last_pps()`, before and after:

| | last-PPS spread |
|---|---|
| per-radio `UNKNOWN_PPS` | 6.000000 s |
| external REF+PPS, no zeroing at all | 8.509223 s |
| our host-side loop | 0.000000 s |
| **driver `SYNC_DEVICES`** | **0.000000 s** |

`UhdSource` now contains **no timed-command machinery at all** — one
`writeSetting("SYNC_DEVICES", "0")`.

**2. `getSettingInfo()` / `writeSetting()` / `readSetting()` implemented.** Upstream leaves all
three to `SoapySDR::Device`'s defaults, which are `return ArgInfoList()`, a silent `return;`
and `return ""`. Two consequences fixed: the driver's magic strings were **undiscoverable**
(`setHardwareTime` accepts `"PPS"`, `"UNKNOWN_PPS"`, `"CMD"` and no enumeration said so —
finding `UNKNOWN_PPS` cost roughly a quarter of four sessions), and there was no extension
point for a cross-device operation. Both are now declared and readable.

⚠ **Still true upstream and NOT changed by us:** `writeSetting` on any *other* key remains the
base-class silent no-op, so `device_settings` on a UHD device is accepted and discarded without
error. `UhdSource` deliberately does not expose that setting.

### Reversal

Delete the patch file and rebuild the prefix. `UhdSource::armAllRadios()` would then need its
host-side loop restored — kept in git history at the commit before this one.

---

## Category I — event instrumentation on the receive path (additive, 2026-07-29)

`blocks/sdr/include/gnuradio-4.0/sdr/SoapySource.hpp`. Added while building the S2-3 instrument,
because the tier-1 criterion is stated in terms of overflow and underflow and **only overflow was
counted**. Purely additive: no existing behaviour changes, and every addition is independently
removable.

| # | change | why | removal cost |
|---|---|---|---|
| I-a | `_timeoutCount` | `SOAPY_SDR_TIMEOUT` was discarded by a bare `continue` at both read loops. On UHD this is `ERROR_CODE_TIMEOUT` — the receive-side starvation event, and the closest true analogue to underflow on RX | delete the field, its reset, and two `fetch_add` lines |
| I-b | `_underflowCount` + `SOAPY_SDR_UNDERFLOW` case | Soapy defines underflow as a **write** condition (`Errors.h:65`) and SoapyUHD never returns it on RX, so this is defensive — another driver may. Without the case it falls to `default:`, which stops the graph on a condition never characterised | delete the case; behaviour reverts to stopping |
| I-c | `_corruptionCount`, `_streamErrorCount` + tags before stopping | both were fatal and reported only through the error channel, so a consumer could not place the event in the sample stream. `SOAPY_SDR_STREAM_ERROR (-2)` is specifically `LATE_COMMAND` or `BROKEN_CHAIN` (`vendor/SoapyUHD/SoapyUHDDevice.cpp:323-324`); the message now names both | delete the counters and the two `emitEventTag` calls |
| I-d | `emitOverflowTag()` → `emitEventTag(key)` carrying `device_time_ns` | host timestamps carry scheduling jitter far larger than the effect being attributed, so on a disciplined multi-radio set-up the device clock is the only one on which radios are comparable. `rx_overflow` is unchanged as a key and stays canonical (`Tag.hpp:206`) | revert to the two-branch literal form |
| I-e | `waitForLoLock()` retains duration in `_loLockMs`; **per-channel deadline** | the duration was discarded on success and reported only on failure. ⚠ **This one is a behaviour change**: the deadline was previously computed once for the whole loop, so channel 1 got whatever channel 0 left and a slow channel 0 could time out a channel that had not yet been given a chance | restore the single shared `deadline`; drop `_loLockMs` and its tag |
| I-f | `lo_lock_ms` in the timing tag meta-info | surfaces I-e downstream, alongside the existing `device_time_ns` | delete the three-line block |

**Licence:** all six are ours, written here. No fair-acc provenance, so Category E does not apply.

**What this does NOT change:** no counter alters control flow except I-b, which replaces a stop with
a count. Overflow handling, the "keep reading rather than restart the stream" decision, and the
`max_overflow_count` threshold are untouched.

Harnesses that report the counters: `womm_ops` (new), `womm_mt_test`, `womm_rx_hold`. Before this,
`womm_rx_hold` reported none — which is how "zero overflows" came to be attached to full-rate
results that had never been counted (`RESULTS.md` §10.1).
