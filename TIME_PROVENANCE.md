# TIME AND PROVENANCE — read this before you compute anything from a timestamp

**Owner, 2026-08-01. If a future session internalises one document, make it this one.**

---

## The thesis, in one paragraph

If you compute phase — or delay, or correlation, or anything else that treats two samples as
simultaneous — from a stream of `uhd_time_t` values on radios you have kept in sync, **you know
the limits of your accuracy**. The moment you mix those aligned values with a time that came
from the system clock, or from a different time base, or that is relative to an event you are
not tracking, **your calculation is wrong and nothing will tell you so**. The numbers will look
fine. The units will match. The result will be plausible. It will be wrong.

**Every time value must carry its provenance, and values from different provenances must never
be combined.** That is the whole document. The rest is the detail needed to obey it.

---

## 1 · What the radios are for, and what they are not

**The radios and their tags are the source of truth for time and for how much data was
captured.** There is nothing more accurate available to check them against:

- A host "high-precision nanosecond" clock on arm64 or amd64 is really a **1/30th to 1/50th of
  a microsecond clock** — 20–35 ns of real granularity. Trivially demonstrated by histogramming
  the digits it emits.
- A computer gets better time only by attaching a GPS or a high-performance clock, and the
  fundamental problem with those is that **they must connect through the computer's bus**,
  which reintroduces exactly the jitter they were meant to remove.

**That is why the external clock connects to each radio directly**, by SMA, with cables of
sub-millimetre matched length. Every centimetre of length difference shifts relative phase by
about **1/30 ns**. The distribution is that accurate, and the accuracy lives in the cabling and
the radios — not in the host.

⇒ **Do not "verify" a radio timestamp against a host timestamp.** You will measure the host.
⇒ **Do not re-derive what the device reported.** Carry it.

---

## 2 · Start-up order, and what is immutable

From `RUN_Start_GR310_OneUnit_TxRx.log` — a real GR 3.10 console trace, and worth reading in
full before designing anything against this stack:

1. **CONFIGURATION** — the user sets parameters. *Before the start button.*
2. **COMPILE** — the graphical flowgraph is generated into a script (Python there; **C++ in our
   configuration**).
3. **RUN** — that script executes and instantiates the configured flowgraph.
4. **INITIALIZATION** — *only now* do the radios initialise. Detect, transport, CODEC control,
   radio control, register loopback self-test. Seconds per radio.
5. **radio t = 0** — everything initialised. Nothing has told it to stream.
6. **CONSUMPTION** — a consumer attaches; the radio numbers samples from 0 and tags the start.

**These are set before the flowgraph or the schedulers are instantiated, and are immutable
thereafter:**

- clock source settings
- sample rate
- buffer sizes
- **the interval between tags** (expressed upstream in buffer-size units)

> **Wanted improvement**: express the tag interval in **seconds** in the GUI — a unit everybody
> understands — rather than in buffers, which nobody does. The conversion to a whole number of
> buffers is the block's job, not the operator's.

The standard external-clock setup is two calls, and nothing more:

```cpp
usrp->set_clock_source("external");   // the 10 MHz REF
usrp->set_time_source("external");    // the PPS
```

With several radios, both the UHD and the SoapySDR Source/Sink then issue
[`multi_usrp::set_time_unknown_pps(time_spec)`](https://files.ettus.com/manual/classuhd_1_1usrp_1_1multi__usrp.html#a413014bf3aea4a8ea2d268b4a3b390e9).
**That sequence is the equivalent of what we want.**

---

## 3 · After start-up, only two quantities matter

Everything else is bookkeeping. Once the radios are configured and running, you care about:

| quantity | what it is |
|---|---|
| **`Offset`** | the **64-bit sample index**. How many samples since sample 0 was sent. An exact integer |
| **`rx_time` / `tx_time`** | the device's time, as `uhd_time_t` |
| **`o` / `u`** | overflow / underflow alerts, and only the **unexpected** ones matter |

**`rx_time` and `tx_time` are the time since the radios completed initialisation. They are not
reset.** That is their epoch, and it is the only one they have.

### The invariant that defines a synchronised array

> **RX channels must have the same sample index at the same `rx_time`.
> TX channels must have the same sample index at the same `tx_time`.**

This is not a quality metric to be optimised toward. It either holds or the array is not an
array. **When the sample counts do not match, stop looking at anything else** — phase, epoch,
correlation and every other cross-radio quantity are meaningless until it does.

### Why: the SIMD paradigm

UHD's model — which Soapy follows — is that **one command goes to all the radios and each knows
what to do with it.** A broadcast command carries a time, and every radio acts on it at what
*its own* clock says is T. So if the clocks agree, **the sample index is a shared address
space** and the array is one instrument. If they do not, one command silently becomes N
different actions and sample *n* addresses nothing coherent.

---

## 4 · Expected versus unexpected desynchronisation

**"Unexpected" is load-bearing and always has been.**

| situation | expected? | response |
|---|---|---|
| a channel is changed **independently** — not via a `multi_usrp` timed command | **yes** | none. It may be out of sync; you asked for that |
| a deliberate retune, calibration, reconfiguration | **yes** | none. Some operations must drop samples |
| **mismatched sample index after overflow or underflow** | **NO** | **resync** |

In normal operation, mismatched sample indices on channels that are supposed to be synchronised
happen **only** unexpectedly, after an over/underflow.

**Why resync is the only answer, and not merely a good idea:** a radio that drops samples has
**lost its place in the shared index**. It keeps streaming. Its counters look healthy. And
sample *n* on that radio is no longer sample *n* on the others. Counting and carrying on leaves
a **silently misaligned lane** — worse than a visible stop, because everything computed
downstream is wrong and nothing says so. Alignment is a property of **the set**, so the whole
array resyncs together.

**To resync:** issue `set_time_unknown_pps(T)` again, `T` usually `0.0`. It is relative to *when
the radios resync* — simultaneous across radios, but non-deterministic in absolute terms.

---

## 5 · ⚠ THE PROVENANCE TABLE — the part that actually bites

`T` for USRP/UHD is a **`uhd_time_t`: `{int full_secs, double frac_secs}`** — not an integer,
not a float. The split exists because a `double` holding seconds-since-epoch (~1.8e9) resolves
only to ~0.24 µs; splitting it keeps the fractional part exact regardless of magnitude. **The
float part has sub-nanosecond accuracy when clocked.**

| provenance | what it measures | safe to combine with |
|---|---|---|
| **`rx_time` / `tx_time` from tags** | device time since **that radio's initialisation**, on the shared clock | each other, across radios **that share a clock and were synced together** |
| **`Offset` (sample index)** | exact integer count from sample 0 | `rx_time` **on the same radio**, via the exact sample rate |
| `get_time_last_pps()` | device time at that radio's last PPS edge | other radios' same call — this is the **epoch discriminator** |
| host `steady_clock` / `system_clock` | the **host**, at 20–35 ns granularity | **nothing on this list** |
| any value computed by extrapolating from an anchor | your arithmetic, not the device | **nothing** — it is not a measurement |
| a time "relative to an event" | that event, if you are tracking it | **nothing, unless you are tracking it** |

**The rule: two time values may be combined only if they share a provenance. If you cannot name
the provenance of a value, you cannot use it in a calculation.**

### The failure mode, concretely

You are computing phase across four radios. You have kept the clocks in sync. Your `rx_time`
values are good to sub-nanosecond. Then somewhere in the chain a value arrives that was:

- read from the host clock (→ 20–35 ns of granularity, plus scheduling jitter), or
- extrapolated from an anchor rather than reported by the device (→ your arithmetic, silently
  substituted for a measurement), or
- relative to an event you are not tracking (→ an unknown constant offset), or
- from a radio that resynced while the others did not (→ a whole-second offset)

…and the result is still a number, still in seconds, still plausible. **Nothing in the type
system, the units, or the magnitude will flag it.**

---

## 6 · ⚠ Traps found in this stack, with evidence

Each of these was found the hard way, most of them by the owner catching a wrong claim.

**`set_command_time(T)` — real, current, and still the wrong thing to reach for.**

*Status, checked against the headers actually installed here:* present in **UHD 4.10.0.0** at
`multi_usrp.hpp:342` as a **pure virtual**, so every `multi_usrp` implementation must provide
it. **No deprecation marker** anywhere in the header — `UHD_DEPRECATED` and `deprecated` do not
appear. Its doc string is **byte-identical to 4.6.0.0**, so the semantics have not drifted. It
is not going away.

⚠ **But the owner could not find it in the UHD and USRP Manual for 4.10.0.0-0-g2af4ddb9
(https://files.ettus.com/manual/).** A function you can call but cannot look up is its own
category of hazard — the same shape as `UNKNOWN_PPS`, which is real, load-bearing, and appears
in no enumeration or manual page. **Treat "in the header but not in the manual" as a reason for
extra care, not as reassurance.**

⚠ **And the substantive danger, which is why we do not use it.** Its own doc string says: *"the
time at which the next command will activate"*, and **"If the time spec is late, the command
will be activated upon arrival."** A late timed command **executes anyway, silently** — nothing
in the result distinguishes "ran at T" from "ran whenever it got there". It is also unclear
whether its `T` shares the time base of the `rx_time`/`tx_time` that arrive on tags, which is
precisely the provenance question this document exists to make you ask.

**Prefer `set_time_unknown_pps()`, whose semantics are unambiguous and which is documented.**
Many timed commands in the UHD API *look* like this one; the resemblance is not a reason to
trust an unverified one, and "I recognise this description" is not the same as "I can cite the
current page for it".

**Timed commands are atomic units, implemented in hardware — but atomic PER DEVICE.** They use
**two PPS ticks**: one to establish unambiguously where you are, and the command acts on the
**next**, so the target edge cannot fall either side of a boundary for different radios. That is
why they cost seconds. **Do not decompose them** into poll-transition + set + sleep; the device
does it correctly in hardware.

**But a USB B2xx is one device with one mboard.** `b200_impl.cpp:309` hardcodes `/mboards/0`,
and `UHDSoapyDevice.cpp:215` does the same on the reverse bridge, so a `multi_usrp` can never
span four USB B210s and `ALL_MBOARDS` cannot reach them. Multi-mboard aggregation belongs to the
**networked** drivers (`addr0=…,addr1=…`). ⇒ **Four B210s are strangers to each other in
software. All of their coordination arrives over the Octoclock cables.** That is why the
cable-length discipline is load-bearing: no software layer exists that could compensate.

**External REF gives RATE agreement, not EPOCH agreement.** Measured, four B210s:

| | `get_time_last_pps()` spread |
|---|---|
| external REF+PPS selected, **no zeroing** | **8.509223 s** |
| per-radio `UNKNOWN_PPS` (each latches a different edge) | **6.000000 s** |
| one transition detected, then broadcast to all | **0.000000 s** |

A shared 10 MHz means the clocks *tick* together; it does not give them a common *origin*.
**Corroborating signature:** an un-zeroed radio reports a **fractional** last-PPS time; a radio
zeroed *on* an edge reports an **exact integer**, because every later edge then lands on a whole
second by construction.

---

## 7 · How to build an instrument rather than a formality

Two checks written this session read back a literal we had written, and **would have looked
perfect whether or not the thing they claimed to measure was true**:

- *"cross-radio spread at the first tag = 0 ns"* — every radio is armed at the same literal
  instant **on its own clock**, so it reports that literal back either way.
- *"device time exactly 1 s between tags"* — computed as `anchor + samples/rate`, i.e.
  arithmetic on the same counter that indexes the tag. Guaranteed by construction.

The check that replaced them works because **its two hypotheses are ~1000× apart**:
`get_time_last_pps()` reads each radio's own clock reporting its own last edge — same edge gives
sub-millisecond agreement, different edges give **whole seconds**.

> **Choose measurements whose competing hypotheses differ by more than the noise, and validate
> in both directions.** An instrument only ever shown to say "no" has not been shown to say
> "yes". Before trusting a check, ask: *what result would this produce if the thing I am testing
> were false?* If the answer is "the same one", it is not an instrument.

**And ask the device rather than inferring.** *"Did underflows occur?"* and *"can we read the
sample index and time from the tags?"* are direct queries to an authoritative source. *"Let me
use a wall clock over several runs to count samples"* and *"let me measure the average load"*
are inferences from noisy proxies — and every retraction this project has recorded came from
that second kind. (Load, for a modern digital SDR, is algebra:
`sample_rate × bytes_per_sample × number_of_channels`. Nothing needs estimating.)

---

## 8 · The short version, for the top of the next session

1. **The radios' tags are the truth.** Carry what the device reported; never re-derive it, never
   check it against the host clock.
2. **Only two quantities matter after start-up**: the 64-bit sample index, and `rx_time`/`tx_time`
   — plus `o`/`u` for the *unexpected* ones.
3. **Same sample index at the same `rx_time`, across every synchronised channel.** If that fails,
   stop; everything downstream is meaningless.
4. **Desync is expected after an independent change and unexpected after over/underflow.** For
   the unexpected case the only answer is **resync the whole array**.
5. **Never mix time bases.** If you cannot name a value's provenance, you cannot compute with it.
6. **Do not rebuild what the driver does.** Timed commands are hardware primitives. This session
   rebuilt them four times and deleted them four times.

**Related**: `HANDOFF.md` (the SIMD section), `NAMEMAP.md` (verified UHD ↔ SoapyUHD ↔ SoapySDR
names, and what is *not* discoverable), `B210_sizing.md` (frames and buffers),
`RUN_Start_GR310_OneUnit_TxRx.log` (the start-up trace), `DRIFT.md` Category J (our SoapyUHD
divergence).
