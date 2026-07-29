# OPERATIONS — the tier-1 criterion as a diagnostic procedure

The tier-1 acceptance criterion (`BUILD_JOURNAL.md` D8) is:

> **no loss of lock, and no _unanticipated_ overflow or underflow, during operations**

particularly **channel switching, calibration, and graphical display / UI operations**.

**"Unanticipated" is load-bearing.** Some operations *must* drop samples — retuning among them — and
a drop around a deliberate reconfiguration is expected behaviour, not a defect. The criterion is
about surprises, not about zero drops.

This document is the **procedure** for deciding which a given drop was. It is deliberately not an
enumeration of causes: listing every condition that can produce an overflow up front is open-ended,
and the catalogue at the end of this file is a *by-product* of running the procedure, not an input
to it. An entry that is never triggered is an entry nobody needed to write.

---

## The procedure

**When activity X produces an overflow or underflow:**

1. **Establish the baseline is quiet first.** See "Precondition" below. This step is not optional and
   is the one most easily skipped.
2. **Review what the radio was actually asked to do** — not what the harness intended. Settings
   applied, messages delivered, graph edits, file rollovers, and anything else on the same thread as
   the read loop.
3. **Establish whether those operations cause the event _always_, or _only under condition Y_**,
   using: the driver's own counters and tags; manufacturer documentation (Ettus/UHD, pothosware,
   Great Scott Gadgets); the source of the driver in use; or the owner.
4. **Record the answer here**, in the catalogue, with the evidence that produced it.

An event that survives step 3 without an explanation is **unanticipated**, and is a tier-1 defect.

---

## Precondition — a quiet baseline, established before any operation is injected

**An operation's cost cannot be measured against a baseline that is already dropping samples.** If
the radios overflow continuously with nothing being asked of them, no injected operation is
attributable: its effect is buried in the background rate.

So before any operation measurement:

- Run `womm_ops null` — the **control**. Identical code path, timing and windows, with no operation
  injected. It must report **zero** attributed events.
- If the control is not clean, the operating point is wrong and **no operation result taken there
  means anything.** Reduce rate, or fix the cause, and re-establish the control first.

`womm_ops` enforces this in two directions at once, because an instrument shown only a retune has
been shown to say "yes", not to say "no":

| control | what it rules out |
|---|---|
| `WOMM_OP=null` | the harness, the windows and the plumbing producing events on their own |
| untargeted radios in every run | an event being attributed to an operation when it was global |

## Two things that are not the same claim

**A sample-rate ratio near 1.0 is not "lossless".** A ratio of 0.994 at 122.88 MS/s is roughly
700 000 samples per second discarded by the device. Ratio and event counts answer different
questions, and a run that reports only the ratio cannot distinguish "kept up" from "nearly kept up
while overflowing throughout". Report both; `womm_mt_test` and `womm_rx_hold` now do.

**UHD's console markers are not a counter.** UHD prints `O` on overflow and `D` on a dropped packet
directly to stderr. They are a useful smell, but they are unlabelled, interleaved with `[INFO]`
lines, and cannot be attributed to a radio in a multi-device process. Use `_overflowCount` and the
`rx_overflow` tag.

## What the instrument reports, and on which clock

`SoapySource` counts every condition SoapyUHD can return on a receive stream
(`vendor/SoapyUHD/SoapyUHDDevice.cpp:318-324`):

| counter | UHD condition | tagged? |
|---|---|---|
| `_overflowCount` | `ERROR_CODE_OVERFLOW` | `rx_overflow` |
| `_timeoutCount` | `ERROR_CODE_TIMEOUT` | no — see below |
| `_corruptionCount` | `ERROR_CODE_BAD_PACKET`, `ERROR_CODE_ALIGNMENT` | `rx_corruption` |
| `_streamErrorCount` | `ERROR_CODE_LATE_COMMAND`, `ERROR_CODE_BROKEN_CHAIN` | `rx_stream_error` |
| `_underflowCount` | — never produced by UHD on RX | `rx_underflow` |

**On "underflow".** Soapy defines underflow as a *write* condition (`Errors.h:65`) and SoapyUHD
never returns it on a receive stream, so `_underflowCount` is defensive — another driver may. The
receive-side analogue, the host asking for samples and being given none, is **timeout**.

**Timeout is counted but deliberately not tagged.** An overflow marks a discontinuity in delivered
samples and so has a sample offset to anchor to; a timeout delivered no samples at all and has no
position in the stream to attach a tag to.

**Event tags carry `device_time_ns`, not host time.** Host timestamps carry scheduling jitter far
larger than the effect being attributed, so on a disciplined multi-radio set-up the device clock is
the only one on which radios are comparable.

## Running it

```bash
source scripts/env.sh
WOMM_EXTCLK=1 ./build-fixed/blocks/sdr/src/womm_ops null
```

Then, only once that is clean, one of `retune-far`, `retune-near`, `gain`, `bandwidth`.

| variable | meaning |
|---|---|
| `WOMM_OP` | operation, if not given as `argv[1]` |
| `WOMM_RATE` | per-channel sample rate; lower it until the control is clean |
| `WOMM_THREADS` | CPU pool size, sized once for the whole machine (default 24) |
| `WOMM_EXTCLK=1` | external 10 MHz + PPS |
| `WOMM_DIRECT=1` | inject via `settings().setStaged()` instead of the scheduler message port |
| `WOMM_FAR`, `WOMM_NEAR`, `WOMM_ALT_GAIN` | operation targets |

Operations reach the block through the **scheduler's message port** by default — the path a UI or
`gnuradio4-control-plane` drives, which is what makes this a test of D10's "usable UI" and not just
of the block. `WOMM_DIRECT=1` bypasses the plumbing so the two costs can be separated.

---

## Catalogue of anticipated causes

Each entry arrives from step 3 with the evidence that put it there. **Nothing is listed here
speculatively.**

### A-1 · Startup: radios brought up serially while earlier ones already stream

**Condition:** any multi-radio graph, at any rate, during bring-up only.
**Always or conditional:** always, in a single process.
**Evidence:** a B210 takes ~2.5 s to initialise and, with an external time source, blocks a further
~2 s in `set_time_unknown_pps()`. In one graph the first radio is streaming while the last is still
initialising, so it overflows with nothing draining it. `RESULTS.md` §9.14 recorded the same effect
("survivable at 24 threads, fatal at 16").
**Anticipated.** Excluded from measurement by the readiness gate plus warm-up window, not by fixing
it. In a multi-process topology each process brings up its own radio and the effect does not arise.

### A-2 · A live retune blocks the read loop

**Condition:** any `frequency` change while streaming.
**Always or conditional:** **not yet established — measurement pending.**
**Evidence so far:** `applyChangedSettings()` is the first statement of `ioReadLoop()`
(`SoapySource.hpp:282`, `:363`) and `SoapySource::work()` never calls it (`:253`), so
`setCenterFrequency` executes on the same thread as `readStream` and its latency is charged directly
against the device ring. Mechanism identified; cost not yet measured. **This is what `womm_ops
retune-far` exists to answer**, and it is not an entry until it has a number.

---

## Known limitation — runtime LO settling is not directly observable

`applyFrequency()` (`SoapySource.hpp:750`) does **not** call `waitForLoLock()`; that runs only from
`reinitDevice()` (`:620`). So after a runtime retune the synthesiser has not necessarily settled,
and samples are invalid for an interval this instrument cannot see — the harness cannot poll
`lo_locked` itself, because the read loop is the only thread that may touch the device.

What **is** measured is the **boot** settle time, per channel, in `_loLockMs`, reported by
`womm_ops` at startup and carried on every timing tag as `lo_lock_ms`. It is available on every boot
**regardless of what is on the air**, so it needs no transmitter.

Measuring runtime settle would mean making `applyFrequency()` wait for lock — which would block the
read loop and cause the very overflows being measured. That is a deliberate design question, not an
oversight, and it is open.
