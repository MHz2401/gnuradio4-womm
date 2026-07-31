# BUILD_JOURNAL

Append-only. Every entry: what was decided, why, the evidence, and how to reverse it.
Machine: Mac Studio M2 Ultra (16 P + 8 E, 192 GiB), macOS 26.5.2 (25F84), 16 KB pages.
Repository root: `/Users/whom/dvel/ghzhub/GR4-fork/gnuradio4-womm`.

---

## 2026-07-25 — Phase 0, reconnaissance (read-only)

No installs, builds, or network fetches. One compiler flag probe (`-fsyntax-only`, output to
`/dev/null`, no artifacts).

**Baseline established.** Tree is `4.0.0-RC2-13-g44275ed`, not RC1 as originally assumed. RC1
`4661f02` (2026-03-13), RC2 `c946b14` (2026-04-28). Single remote `origin` (personal fork); no
`upstream`. Complete ref set is `main` + 4 remote branches + 2 tags — verified via
`git for-each-ref`, `packed-refs` (7 lines), and the absence of `.git/FETCH_HEAD`.

**Isolation baseline captured.** `which -a` for `cc c++ clang clang++ gcc g++ cmake ninja make
python3 git pkg-config`, taken in a fresh unactivated shell. All C/C++ compilers resolve to
`/usr/bin/*`. This is the "before" half of the isolation proof; the "after" diff runs at GATE 2.

**Findings that shaped the plan** (full detail and citations in the Phase 0 recon report):
- Zero-copy double-mapped buffers are compiled out on macOS (`CircularBuffer.hpp:30-53`, gated on
  the Linux-only `__NR_memfd_create`); the fallback memcpys every published sample
  (`:352-378`) and doubles buffer memory (`:300-305`). Selection is silent `if constexpr`.
- All thread affinity/priority/QoS is compiled out on Darwin — 15 `not defined(__APPLE__)` guards
  in `thread_affinity.hpp`, each with an empty `#else`. No `QOS_CLASS_*` anywhere in the tree.
- `multiThreaded` scheduler workers never sleep (`Scheduler.hpp:687-704`); block→thread assignment
  is strided round-robin (`:1378-1385`), maximising cross-thread handoffs.
- `core/benchmarks/bm_Scheduler.cpp:92` hard-pins the CPU pool to 2 threads, so the existing
  benchmark suite cannot observe this machine's 24 cores.
- No VOLK, no hand-written NEON/AVX — all SIMD via `vir-simd`. No cross-architecture asymmetry.

**Retraction, same day.** An earlier reading of "UHD images directory empty, no hardware attached"
was taken during a concurrent `brew upgrade` and was **wrong**. Corrected by running
`/opt/homebrew/bin/uhd_find_devices` at the user's instruction: 32 image files present, **three
B210s enumerate**. The Octoclock-G did not appear and UHD logged
`Device discovery error: send: Broken pipe` — attributable to this agent's sandbox being restricted
from UDP egress, not to a machine fault. Recorded so the earlier claim is not relied upon.

---

## 2026-07-25 — Phase 1, decisions taken

### D1. Mission reframed — the GNU Radio 3.x comparison is struck
**Why:** brew's GR 3.10 was deprecated and tap-sourced, and has been uninstalled; GR 3.9
(radioconda) is gone and was never a valid benchmark; 3.9 flowgraphs would not port to 4.0. There
is no 3.x baseline to measure against and reconstructing one is impractical.
**Replaced by:** the absolute criterion — does throughput scale with parallel chain count — read in
**multiples of real device rate** (61.44 MS/s B210; 0.5 GS/s high-end reference) rather than
synthetic ops/s or memory-bandwidth fraction.
**Reverse:** n/a (scoping decision).

### D2. Baseline branch = `origin/main`; two working branches created
**Why:** the three sibling branches offer nothing — `marcus/single-header-wf` differs by one CI
workflow file, `prune_ci` is 13 behind/8 ahead on CI pruning, `multiport-uncertain` is 61 behind
and 6648 lines behind on `blocks/sdr`. `main` is a strict superset. Provenance table starts empty.
**Done:** `womm/m2ultra-validated` and `womm/m2ultra-wip`, both at `44275ed`. `main` untouched and
still tracking `origin/main`.
**Reverse:** `git branch -D womm/m2ultra-validated womm/m2ultra-wip`.

### D3. Toolchain = Clang + libc++, Apple clang 21 attempted first
**Why:** CMake has **no compiler-version gate** at all; the real floor is implicit via `<format>`
(82 files), `<print>` (32), `<expected>` (25), plus `__PRETTY_FUNCTION__`-parsing reflection in
`meta/include/gnuradio-4.0/meta/reflection.hpp:137-183` whose `static_assert`s fire if the format
shifts. Apple clang 21 costs one configure and installs nothing. Corroborating evidence: brew's
UHD 4.10 reports itself built with `Clang version 21.0.0` (libc++), so the stdlib matches.
**Not GCC**, despite `CLAUDE.md:248` ranking it first — see DRIFT.md D-2.
**Fallback:** brew `llvm@20`, which upstream macOS CI uses (`ci-macos.yml:42,76-78`) and which is
keg-only, so it shadows nothing in `PATH`.
**Reverse:** change one constant in `scripts/build.sh`.

### D4. Dependencies via isolated prefix, not Homebrew
**Why:** `/opt/homebrew` holds 231 formulas; `brew install` can upgrade transitive dependencies.
The user removed a tap-sourced stack for provenance reasons, so inheriting tap provenance is the
wrong direction. Full detail in `MANIFEST.md`.
**Reverse:** `rm -rf .womm-prefix`.

### D5. Clean-shutdown verification promoted to an acceptance criterion
**Why:** the thread pool is a process-lifetime singleton whose workers are created eagerly and, in
`multiThreaded`, never sleep. "Graph stopped" and "threads gone" are therefore not the same event,
and a shutdown bug on this machine means a pegged box. Checked from outside the process, **before**
any long soak rather than after.
**Reverse:** n/a (test policy).

**Flag probe result (evidence for a closed risk):** `-march=native` is **accepted** by Apple clang
21 on arm64. `bench/CMakeLists.txt:14` and `core/benchmarks/CMakeLists.txt:22` apply it
unconditionally with no Apple branch; the build-failure risk is closed. Whether it is a *no-op*
still needs a `-###` check before any A/B comparison.

---

## Pending — GATE 1
`MANIFEST.md` awaiting approval. Nothing fetched or installed.

---

## 2026-07-25/26 — Phase 2 complete, Phase 3 baseline

### D6. Prefix moved OUTSIDE the repository
`.womm-prefix/` inside the source tree broke gnuradio4's configure: `gr-libsoundio` is an exported
INTERFACE target and CMake rejects INTERFACE include/link paths located under the source dir
("prefixed in the source directory"). Prefix is now `../womm-prefix`, a sibling.
**Why this way:** the alternative was patching upstream CMake to wrap paths in `$<BUILD_INTERFACE:>`
— upstream drift for a purely local layout choice.
**Reverse:** change `WOMM_PREFIX` in `scripts/env.sh` and `PREFIX` in `scripts/build-prefix.sh`.

### D7. SoapyUHD patched — two changes, three lines
`patches/womm/SoapyUHD-0001-cxx17-for-uhd-4.10.patch` and `-0002-boost-190-lexical-cast.patch`.
Upstream's `set(CMAKE_CXX_STANDARD 14)` cannot be overridden by `-D`; UHD 4.10 headers need C++17.
Boost 1.90 no longer provides `lexical_cast` transitively.
**Reverse:** delete the patch files; `stage_patched()` applies whatever is present.

### Two errors made and corrected, recorded so they are not repeated
1. A `sed` insertion of the Boost include mangled `SoapyUHDDevice.cpp` — the file had no
   `#include <boost` to anchor on, so the empty address made `a\` append after **every** line:
   1162 copies, file doubled. It compiled cleanly because include guards make repeats no-ops.
   Redone with an asserted-unique anchor. **Lesson: verify line counts after scripted edits.**
2. The first GATE 2 isolation proof used `env -i`, which stripped the environment so the login
   shell never rebuilt the Homebrew/CMake.app PATH. It reported four false "differences".
   **Lesson: a proof harness that does not reproduce the baseline's conditions proves nothing.**

### Vendoring integrity — two silent corruptions caught
- `core.autocrlf=input` stripped CR from vendored files on commit (180 CRLF on disk, 0 in the
  blob). Fixed with `.gitattributes: vendor/** -text`.
- Plain `git add vendor` dropped 59 files; gnuradio4's own `.gitignore` rule `lib/` swallowed all
  of `vendor/SoapySDR/lib/`, leaving an unbuildable snapshot. Fixed by force-adding.
- `scripts/verify-vendor.sh` now checks committed content against `vendor/MANIFEST.lock`, so both
  classes fail loudly.

### GATE 2 — isolation proven
`which -a` for 12 tools in an unactivated shell is byte-identical to the Phase 0 baseline. No
`WOMM_PREFIX`/PATH leakage. Nothing written outside the prefix. No formula installed or linked.
Brew count 231 -> 198 is the owner's own `gnuradio` uninstall taking orphaned deps; every brew
command run here was read-only (`list`, `info`, `--prefix`, `--version`, `autoremove --dry-run`).

### Phase 3 baseline — see RESULTS.md
Build 603 s, 1850 targets, 0 errors, **0 compiler warnings**, peak RSS 11.4 GiB across 16 jobs.
Tests 100/101. The single failure, `qa_SoapySource`, is environmental: the test hardcodes
`{"device", "rtlsdr"}` and has no skip guard. B210 sample streaming confirmed end to end.

---

### D8. Tier-1 acceptance criterion replaced — capacity is no longer the question

**Decided by the owner, 2026-07-28.** Supersedes the "processing capacity approximately
linear-proportional to hardware capacity" criterion, which the owner wrote himself and now
withdraws: *"it made sense in a more pessimistic era, but that era is over — four radios currently
run at full capacity without making much of a dent in the system performance meter."*

**New tier-1 criterion:** *no loss of lock, and no **unanticipated** overflow or underflow, during
operations.* It applies particularly to **channel switching, calibration, and graphical
display / UI operations** — all of which now appear to be within reach for the first time.

The word **unanticipated** is load-bearing. Some operations *must* drop samples — retuning among
them — and a drop around a deliberate reconfiguration is expected behaviour, not a defect. The
criterion is about surprises, not about zero drops.

**"The soak test is the yardstick for done" is retired as redundant** — it meant an extended run at
full capacity, which has been done, and it is subsumed by the criterion above.

**The "5-clean-run stability gate" is met and closed.** It was a prior agent's
reasonable-at-the-time goal — all radios for 10 s without a crash, five times running — set without
knowing that only one channel was ever lit. It is no longer "currently unmeetable".

*Reverse:* nothing to reverse; this is a goal change, not a code change.

---

### D9. Camp chosen — gnuradio.org, with LGPL permitted as a near-term expedient

**Executive decision by the owner, 2026-07-28.**

- **If we must choose sides, we are in the gnuradio.org camp** — and we probably will have to,
  eventually. That means MIT, and trees (A)/(C).
- **Near term: get things done first.** Work with LGPL code where it is the fast path to something
  working, but **do not irrevocably bake it in**.
- The owner prefers MIT to LGPL-3.0, but **"Works on My Mac" is the prevailing principle** and
  outranks the licence preference for now.

**Already satisfiable.** `DRIFT.md` Category E lists the five fair-acc cherry-picks individually and
records that reverting the four post-relicense ones restores an MIT-only tree. So "not baked in" is
a property we currently have and must not lose — every future LGPL-derived change must stay
separately revertible and be listed there.

**Upstream contribution is intended**, but with a hard constraint: *no consideration in support of a
PR or contribution may constrain the implementation of the "Works on My Mac" goal.*

*Reverse:* revert the four post-relicense picks per `DRIFT.md` Category E to restore MIT-only.

---

### D10. A UI is now in scope, and "Usable UI" is a tier-1 item

**Decided by the owner, 2026-07-28.** Previously the owner "did not dare to hope". Expectations have
been reset by this sprint's results, and the question has changed from *whether* to *what*.

⚠ **"Usable UI" is coined but NOT YET DEFINED.** It is recorded here as a term awaiting a
definition, per the standing rule that shorthand must carry its definition or a pointer to one —
see `HANDOFF.md`, "WHAT DO YOU MEAN BY '19 % WALL COST'". **Defining it is the next discussion, and
that definition is a deliverable, not a preamble.** Until then, do not build against it.

The owner, on scope: *"It's not scope creep until you have enough information to set expectations,
and mine have been reset."* And on the finish line: *"I can't define 'Works on my Mac' at this time,
but I'll know it when I see it"* — while being certain it requires a Usable UI.

*Reverse:* n/a — scope decision.
