# RESULTS

Machine: Mac Studio M2 Ultra, 16 P + 8 E cores, 192 GiB, macOS 26.5.2 (25F84), 16 KiB pages.
Repository root: `/Users/whom/dvel/ghzhub/GR4-fork/gnuradio4-womm`.

---

## Phase 3 — stock baseline

**Configuration.** `origin/main` @ `44275ed` (`4.0.0-RC2-13`), no local patches to gnuradio4.
Apple clang 21.0.0, `-DCMAKE_BUILD_TYPE=Release`, Ninja, all dependencies from the isolated
prefix. `GR_USE_FETCHCONTENT_DEPS=OFF` — no build-time downloads. Stock options otherwise:
`WARNINGS_AS_ERRORS=ON`, `GR_ENABLE_HTTP=ON`, `ENABLE_TESTING=ON`, `ENABLE_EXAMPLES=ON`,
sanitizers off, TBB off.

### Build

| Metric | Value |
| --- | --- |
| Targets | 1850 |
| Wall time | **603 s** (`-j16`) |
| Compiler errors | **0** |
| **Compiler warnings** | **0** |
| Linker warnings | 126, all one message (see below) |
| Peak compiler RSS, all 16 jobs | **11.4 GiB** (~730 MiB/TU) |

**Apple clang 21 builds gnuradio4 cleanly.** Three Phase-0 risks are closed by this:

- The `__PRETTY_FUNCTION__`-parsing reflection in `meta/…/reflection.hpp:137-183` has
  `static_assert`s that fire if the format shifts. They did not.
- `-Werror` is project-wide and Apple clang 21 is newer than the clang 20 upstream CI tests.
  **Zero compiler warnings**, so nothing to trip on.
- `blocklib_generator/tools/CMakeLists.txt:13` forces `-stdlib=libc++` unconditionally under
  Clang; the nested host-tool build succeeded.

**The 126 linker warnings are one repeated upstream wart**, not ours:
`ld: warning: ignoring duplicate libraries: 'core/libgnuradio-core.a'` — `libgnuradio-core.a`
appears twice on the link line because targets link core both directly and transitively. Benign.
Logged in `DRIFT.md` as an upstream observation; not patched.

**Job-count evidence (`DRIFT.md` D-1).** `CLAUDE.md:433` mandates `-j6`, citing swapping and OOM
kills. Measured peak across **16** concurrent compiler processes was 11.4 GiB — **6 % of this
machine's RAM**. The stated failure mode cannot occur here. The override is justified by
measurement, not assumption.

### Tests

`ctest --output-on-failure --timeout 300 -j8`

| Metric | Value |
| --- | --- |
| Tests run | 101 |
| **Passed** | **100** |
| Failed | 1 — `qa_SoapySource` |
| Wall time | 71.9 s |

Notably passing: **`qa_SoapyIntegration` (36.9 s)** and `qa_SoapyLoopback`, `qa_SoapyRaiiWrapper`.
Every SoapySDR source file in this repository is excluded from all upstream CI
(`blocks/sdr/CMakeLists.txt:9` guards on `if(TARGET SoapySDR)`, and no CI image installs it), so
these had **never been compiled or run on any platform** before this build.

#### The one failure — environmental, not a gnuradio4 defect

`qa_SoapySource.cpp:322` hardcodes `{"device", "rtlsdr"}`. No SoapyRTLSDR module is installed
(we built only SoapyUHD) and no RTL-SDR hardware is attached, so device creation fails at
`SoapyRaiiWrapper.hpp:334` and the assertion at `:339` trips.

**This test has no skip guard.** `qa_SoapyIntegration` self-skips when its device is absent
(`[SKIP] no LimeSDR device found`); `qa_SoapySource` does not, and hard-fails instead. That
inconsistency is an upstream test-design issue.

Building SoapyRTLSDR would **not** fix it — with no dongle attached, device creation would still
fail. The test presumes hardware.

Not disabled, not skipped, not worked around. Reported as a failure with diagnosis.

#### Secondary observation — possible watchdog thread leak

Run standalone rather than under ctest, `qa_SoapySource` **hangs for >300 s** instead of failing.
The test creates `createWatchdog(sched, 6s)` and then `watchdogThread.join()`s it
(`qa_SoapySource.cpp:335,343`). The hang is consistent with the watchdog thread never
terminating after the scheduler errors out.

This matters beyond one test: it is the same class of problem as the clean-shutdown acceptance
criterion (`DRIFT.md`/plan §4.C) — the scheduler's threads not winding down when a graph stops.
Flagged as a lead for Phase 3.5, not yet diagnosed.

### Hardware — B210 streaming confirmed

Through the isolated prefix: gnuradio4 `SoapySource` → SoapyUHD (`libuhdSupport.so`, ABI
`v0.8-3`) → UHD 4.10.0.0 → **USB 3** → B210.

```
[INFO] [B200] Detected Device: B210
[INFO] [B200] Operating over USB 3.
[INFO] [B200] Register loopback test passed
[INFO] [B200] Asking for clock rate 32.000000 MHz... Actually got clock rate 32.000000 MHz.
Basic API test - deviceDriver 'uhd'
  0: ret = 16384, flags(4) = [HAS_TIME], time_ns = 1286488250
  …
  6: ret = 16384, flags(4) = [HAS_TIME], time_ns = 1384792250
Basic API test - deviceDriver 'uhd' -- DONE
```

16384 samples per read, timestamps advancing 16.384 ms — consistent with 1 MS/s. All three B210s
enumerate (`SoapySDRUtil --find`).

---

## Not yet measured

No performance claim is made here. Phase 3.5 has not run: no scaling curve, no ablation, no
comparison. The build-time and RSS figures above are single observations, not medians over
repeated runs, and are labelled as such.
