# CLAUDE.md — FORKED gnuradio4-WoMM

**_Gnuradio4_** is a modern SDR platform that has not previously had ~~great~~ Mac support.

**_WoMM_** is an acronym for _"Works on my Mac"_, and it's not a marketing slogan: **it means *this* Mac**.

One day, this code may run on other Macs: _that day is not today, but I own other Macs_.


## MAP-of-FORKS (local directories)

```zsh
$ tree -L 1 ..
..
├── gnuradio4-blocks-womm/
├── gnuradio4-control-plane-womm/
├── gnuradio4-studio-womm/
├── gnuradio4-womm/        #`<--- You are here`
├── gnuradiot/
├── gr4-install/
└── womm-prefix/
```


## ★★★ READ FIRST: `TIME_PROVENANCE.md`

**Owner, 2026-08-01: if getting future sessions aligned with one perspective is worth anything,
it is this one.**

The radios and their tags are the **source of truth** for time and for how much data was
captured — there is nothing more accurate to check them against, and the host's "nanosecond"
clock is really a 20–35 ns clock. After start-up only two quantities matter: the **64-bit sample
index** and **`rx_time`/`tx_time`**, plus `o`/`u` for *unexpected* over/underflow. Every
synchronised channel must show **the same sample index at the same `rx_time`**; when that fails,
stop, because everything downstream is meaningless.

**And the trap that motivates the whole document:** mix a value from the radios' time base with
one from the host clock, or a different base, or relative to an untracked event, and **the
calculation is wrong while looking entirely plausible** — right units, right magnitude, no
error. If you cannot name a value's provenance, you cannot compute with it.

**→ `TIME_PROVENANCE.md`.** Read it before writing anything that touches a timestamp.

**→ `NEXT_STEPS_DESIGN_Session_Start_20260802.md`** is the current strategy: build
`USRPSource`/`USRPSink` **direct on the USRP/UHD API**, time-provenance-first, on the
`init()`/`start()` lifecycle proven 2026-08-01. Soapy-based blocks drop to Tier 3. Read its
**ADDENDA** before writing code — two of them will bite immediately.

---

### Preface:

**SEE ALSO: `HANDOFF-ADDITIONAL-HISTORICAL-DETAILS.md`**.  This is a new sprint and
epoch after a strategic milestone.  As was done after the first milestone, this
`HANDOFF.md` includes some material from Owner.  The `HANDOFF-ADDITIONAL-...md` file
contains the rest of compiled notes kept from session-to-session, and you're
welcome to merge the two as you see fit for the next HANDOFF.md to the next session.  
The prior session included a _first-run of gnuradio4-studio (UI)_,
and the decision of what the final systems should be architecturally after
sevral sessions of design, experiment, and redesign. Overview and references for new tasks,
beginning this session, are below.

## MISSION

Produce a stable and performant working build of **gnuradio4** on a Mac
Studio M2 Ultra, capable of:
1. easily accommdating four-to-six **USRP B210** devices and equivalent loads with
2. each radio (and radio-wrapping _block_) using non-blocking multithreading for operations including
3. start, run, PPS/GPS, sync, retune, burst, tag-processing, and ...
    1. specifically,, free of unnecessary blocking operations currently displayed (on any host
       type) in all current gnuradio4 SDR blocks (on start, for example), and
    2. free of 'unanticipated' overflow and underflow (discussed below).
4. Block-based and code-based signal processing and analysis, waveform display,
   spectral decomposition, and communications tasks, which are basic promises and
   premises of gnuradio4.
5. List of things that definitively **do not** meet our goals:
    1. Anything in any `gnuradio4` source tree that matches `r'[S|s]oapy*\.?pp'` is unusable.
    2. That emphatically includes matching code **we've** worked on: it's how we know.

## WHOM

The Owner (didn't write this part and) is an experienced SDR user who writes code,
reads code, checks claims, and supplies independent measurements of his own. One important
measurement to use as a benchmark is that _all the performance goals above have been
achieved by the user on this Mac_, but not in gnuradio4.  He prefers to offer hypotheses and
evidence to test — he has been right and wrong, and says so either way.

## TARGET SYSTEN

**Mac:** 2023 MacStudio, M2 Ultra, 16 P + 8 E cores, 192 GiB, macOS 26.5.2, etc.

**RF HW** Nominal setup: 4 x Etus/NI B210 (USB 3) with Octoclock-G time source.

---


## YOU: Welcome to a Journey of Disvovery!

1. Please read the following sections and learn why this is an environment where
   **_checking assumptions_** is super-important (_is this gun loaded?_).

2. Always consider that with RF systems "_I don't know what this parameter does,
   but I'll run with it to find out!_" might turn out to be expensive or illegal.
---

## Beware of Unrepresenttive Examples

All current examples of radio blocks in **_gnuradio4_** code currently use the **_RTL-SDR_**,
which costs less than about a week of a Claude or ChatGPT subscription, and is not able to
transmit.
1. Most radios are a lot more expensive than the RTL-SDR, and
2. Transmitting has special considerations ...

---

### ⚠ RF TRANSMISSION and U.S. LAW — READ THIS

- **IT IS AGAINST FEDERAL LAW TO TRANSMIT (TX) WITHOUT A LICENSE ON MOST RADIO FREQUENCIES**
- _IT'S ALSO AGAINST FEDERAL LAW TO TRANSMIT (TX) **WITH** MOST LICENSE TYPES ON MOST FREQUENCIES._
    - _This is one reason why GNURadio hardcodes automatable tests for the receive-only (RX-only) RTL/SDR._
- **DO NOT AUTOMATE TRANSMISSION (TX) TESTS: all automated test code should be RX-only.**

---

## PRIORITY TIERS: WHAT IS MOST IMPORTANT

| Tier   | Scope                                                  | Contents                                                                            |
|--------|--------------------------------------------------------|-------------------------------------------------------------------------------------|
| **0**  | **do not burn the Owner's radios**                     | UHD devices are expensive. WRONG PARAMETERS CAN DO PHYSICAL DAMAGE.                 |
| **1**  | **radio running** — performance while a device streams | A **fully operational** gnuradio4 that works on this Mac, and has a **_Usable UI_** |
| **2**  | **radio off** - component performance                  | UI dev; blocks; UI/reconfiguration paths                                            |
| **3+** | **everything** else                                    | upstream contribution, public-repo polish                                           |

- A **USABLE UI** may be task- and function-specific: gr4studio looks very cool,
  but it's also newer-than-new: we can't turn it into a blocker.

- **Fully operational** means *no loss of lock, and no **unanticipated** overflow or underflow,
  during operations.* It applies particularly to **channel switching, calibration, and graphical
  display / UI operations**.
    
    - **"Unanticipated" is load-bearing.** Some operations *must* drop samples — retuning among them — and
      a drop around a deliberate reconfiguration is expected behaviour, not a defect. The criterion is
      about surprises, not about zero drops.

- **Upstream contribution is subordinate**: no consideration in support of a PR
  may constrain the implementation of "Works on My Mac".

---

## ★ THE SIMD PARADIGM — WHY ALIGNMENT IS THE WHOLE POINT

**See `TIME_PROVENANCE.md` for the full treatment — start-up order, the immutables, the
provenance table, and how to tell an instrument from a formality. This section is the summary.**

**Owner, 2026-08-01. This is the model to design against, and most of the day's mistakes
came from not having it.**

UHD's paradigm — which Soapy follows, for the B210 and for other platforms — is that **you
issue ONE command to all the radios at the same time and they each know what to do with
it.** There are special forms where they differ (listening or transmitting on different
frequencies), but it is very much **a SIMD-like paradigm for radio commands.**

**The consequence, and it is the reason alignment matters at all.** A broadcast command
carries a time, and every radio acts on it at what *its own* clock says is T. So:

- If the clocks agree, **the sample index is a shared address space**: sample *n* is the
  same physical instant on every channel of every radio, and the array is **one
  instrument**.
- If they do not, one broadcast command silently becomes N different actions, sample *n*
  addresses nothing coherent, and you have four radios rather than an instrument. This is
  what C-7 means by *"the number that says four radios can be treated as one instrument."*

**So identical sample counts are not pedantry — they are the observable that says the lanes
are in step.** Do not explain a spread away. The owner's rule: *when the samples are not
aligned, stop looking at anything else.* It makes no sense to discuss phase, epoch, or any
cross-radio quantity before that holds.

### Why an unexpected over/underflow means RESYNC, and nothing else

**Owner: when you get unexpected underflows, the only thing to do is resync.** The reason
follows from the paradigm rather than from the lost samples: a radio that drops samples has
**lost its place in the shared index.** It keeps streaming, its counters look healthy, and
sample *n* on that radio is no longer sample *n* on the others. Counting and continuing
leaves a **silently misaligned lane**, which is worse than a visible stop — every
cross-radio quantity computed afterwards is wrong and nothing says so.

Alignment is a property of **the set**, so the whole array resyncs together. Deliberate
discontinuities — a retune — are *anticipated* and must not trigger it.

### Timed commands are ATOMIC UNITS — do not decompose them

**Owner:** timed commands are **atomic units**, used by both the Soapy and the UHD driver and
**implemented in hardware** in the B210 and other UHD devices. Other radios do the same; it
is a common paradigm. They cost seconds because they use **two PPS ticks** — the first tells
you unambiguously where you are, the command acts on the **next**, so the target edge cannot
fall either side of a boundary for different radios. The same trick is used at start-up.

**The rule: hand the device a command and a time. Do not compute PPS boundaries, round to
whole seconds, poll for edges, or sleep.** All of that is the device's job, in hardware, and
it already does it correctly. This session rebuilt it four separate times — arming, retune,
resync, and boundary rounding — and every one of them was deleted again.

**But atomicity is PER DEVICE, and that is the one place work remains.** `set_time_unknown_pps`
is exactly right for one `multi_usrp` (covering all *its* mboards). Four separate B210s are
four separate `multi_usrp` objects, so it degenerates into four independent atomic commands,
each latching whichever edge it happens to reach. **Measured 2026-08-01** with the last-PPS
discriminator:

| approach | last-PPS spread across four radios |
|---|---|
| `UNKNOWN_PPS` issued per radio | **6.000000 s** — different edges, not one instrument |
| detect ONE transition, then broadcast `PPS` to all | **0.000000 s** — one shared epoch |

So the multi-device case is exactly the SIMD shape: **one detection, then the same command to
every radio.** That is the only part we implement, and only because the atomic primitive does
not span devices.

**The discriminator itself matters as much as the result.** `get_time_last_pps()` reads each
radio's own clock reporting its own last edge: same edge → sub-millisecond, different edges →
**whole seconds**. Two hypotheses ~1000× apart, which is what makes it an instrument. Contrast
the two checks it replaced, both of which read back a literal we had written and would have
come out identical either way: the "0 ns cross-radio spread at the first tag" (every radio is
armed at the same literal instant **on its own clock**) and "device time exactly 1 s between
tags" (arithmetic on the same counter that indexes the tag).

### The startup log — read it before designing against the driver

**`RUN_Start_GR310_OneUnit_TxRx.log`** — GR 3.10, one B200, a Sink and a Source, UHD 4.6.0.0,
two consecutive runs. Sixty lines of console output that corrected five confident wrong
inferences in a row, and the single most useful artefact of the session. It shows, in order:
configuration *before* the start button → flowgraph compiled → process launched → **then**
device initialisation (detect, USB 3, CODEC, radio control, register loopback self-test) →
auto-MCR resolving (16 MHz default, then 40 MHz to serve the requested 10 MS/s) → PPS time
sync → **radio t = 0** → free-running with no consumer → charts instantiate and consumption
begins, at which point the radio numbers samples from 0 and tags the start.

**Standing practice earned from it: before designing against a subsystem, get a console dump
of it running.** Reasoning about a mechanism is not a substitute for looking at a trace, and
it is cheaper to ask for the dump than to retract.

---

## WHAT DO YOU MEAN BY "19 % WALL COST" ?

We all make up jargon as we work because it saves time.  Tell everyone what it means.

**Coining shorthand is fine and often necessary — just carry its definition, or a pointer to one,
every time.** The failure is a term that travels without either.

**Worked example, 2026-07-28.** I wrote "a cross-process arming barrier would make the shared epoch
structural" in two `RESULTS.md` sections. The owner asked whether that was defined anywhere. It was
not: it existed as an unnamed code fragment in one harness and nowhere else. **Undefined jargon lets
you refer to a thing you have not built, and then reason as though you had.**

---

## WHEN TO PUSH

Push at every gate or milestone, and always before ending a session. Push after anything expensive
to recreate, and before anything risky — rebase, reset, upstream fetch/merge, large refactor.
Whenever more than a few commits have accumulated: push. **If in doubt, push.**

---

## CODE & DOCUMENTATION STYLE GUIDE(S)

_**FORKED gnuradio4-WoMM**_ expects all AI AGENTS and HUMANS to follow
the `CLAUDE.md` and  `AGENTS.md` syle guides found in the **_gnuradio4_** project urls
that match the regular expression:

`r'https://github.com/gnuradio/gnuradio4(-[a-z]+)*')`

1. Please note that not all projects guides are the same: some are
   purpose-specific.  The others are not _machine-specific_ like this one.
2. Also note that there are organizations besides **_gnuradio.org_** who
   have similar github urls, but _they do not support my Mac_ (or any Mac).  
   Fair-Acc is the primary example (and is LGL3, which GR4 cannot use).
    1. No, that's not a typo: the GNU people cannot use the GNU license.

---

## MAP-of-FORKS (local directories)  

```zsh
..
├── gnuradio4-blocks-womm/
├── gnuradio4-control-plane-womm/
├── gnuradio4-studio-womm/
├── gnuradio4-womm/        #`<--- You are here`
├── gnuradiot/
├── gr4-install/
└── womm-prefix/
```

---

## RULES THAT BIND (FROM PROJECT EXPERIENCE)

We're working with the early RC of an open-source project that includes unmaintained dependencies.

- **All findings must go through human review** by the owner.
- **No green-washing.** Never disable a test, add `|| true`, blanket `-Wno-error`, or narrow the
  test set to make things pass. A diagnosed failure is a result.
- **No network fetch** without explicit discussion and approval. Source only from hosts with active
  third-party malware monitoring.
- **PUSH REGULARLY to `origin`** — it is our fork.
- **⚠ IDEAS YES, SOURCE NO.** From **fair-acc** (LGPL-3.0) and **GNU Radio 3.10** (GPL-3.0) you may take
  ideas and procedures; **never cut and paste**. This tree is MIT. In-tree gnuradio4 code is MIT and may
  be followed directly. Test: if the result is recognisable as *their* code — same identifiers, structure,
  ordering — it is a copy. See `DESIGN_UhdSource.md` §11b.
  **⚠ SCOPED EXCEPTION, owner 2026-08-02:** code **may** be reused from **GR 3.11's `UHD: USRP
  Source/Sink`** where it exemplifies time-provenance-first and does not break the GR4 scheduler
  model. GR 3.11 is GPL-3.0, so that makes a GPL derivative — record each reuse in `DRIFT.md`
  Category E with file and line. No reuse from the GR4 SoapySDR block without explicit permission.
- **No pull requests to upstream** (`gnuradio/gnuradio4*`, `fair-acc/gnuradio4`).
- **Report regressions as prominently as wins.** Label anything not actually measured.
- **⚠ NO SERIALS, ABSOLUTE PATHS, OR SITE DETAILS IN THE REPOSITORY.** Radio serials, capture
  directories, RF parameters for a specific test, and anything else that identifies this machine or
  this site are **semi-secrets** and stay out of version control. Radios are named through
  `$B210U00 … $B210U03` in the operator's shell; output directories are given on the command line.
  This is *not* inherited from `CLAUDE.md`, which is someone else's file and does not cover it.

### ⚠ AUTHORITATIVE SOURCES FOR PERFORMANCE, TIMING, AND ASSERTIONS THAT 'X' CAUSES 'Y'

If you are working on a problem, and find a claim from a prior session stating the
solution to that problem, it may **seem** authoritative, but if it were **correct**
you would not be working on the same problem **now**.

#### Sources of Truth

1. the user, who will truthfully state their degree of confidence in a claim.
2. for device operations: manufacturer specs; Ettus, Great Scott Gadgets, software and drivers — gnuradio.org,
   pothosware, original authors, etc.
3. KNOWN CURRENT ITEMS: These Are Not Stale
    1. `TIME_PROVENANCE.md` : ★★★ time, provenance, and what "works" actually means. **Read first.**
    2. `NEXT_STEPS_DESIGN_Session_Start_20260802.md` : **current strategy (what are we doing this session?)**
    3. `NAMEMAP.md` : verified UHD ↔ SoapyUHD ↔ SoapySDR names, each row with how it was verified
    4. ⚠ `DESIGN_UhdSource.md` is **SUPERSEDED** — it designs a SoapyUHD-based block, now Tier 3

---

### = = = = = = = = = = = = = =

## BEGIN:

### = = = = = = = = = = = = = =

## What are we doing in this session?

**→ `NEXT_STEPS_DESIGN_Session_Start_20260802.md`** — read it, and read `TIME_PROVENANCE.md`
before it.

**Build `USRPSource` and `USRPSink` directly on the USRP/UHD API**, time-provenance-first, on the
`init()`/`start()` lifecycle proven 2026-08-01. Parameters and operations follow **GR 3.11
`UHD: USRP Source` / `UHD: USRP Sink`**. One extension of ours: fixed-interval tag reporting,
period given as an external-clock time interval, optionally snapped to the nearest USRP frame
boundary.

**Tier 1** = this block fully operational, plus usable UI to exercise the milestones.
**Tier 3** = anything Soapy-based, including the `UhdSource`/`UhdSink` built on 2026-08-01.

⚠ **`DESIGN_UhdSource.md` is SUPERSEDED.** It designs a SoapyUHD-based block and its §12 open
questions were answered or overtaken during 2026-08-01. Keep it for the parameter analysis and
the licence table; do not follow its plan.

### The three problems that started this, and where they stand

| | problem | status |
|---|---|---|
| **(A)** | half a radio — RX-only, no Sink | **Sink designed and built** (Soapy-based, Tier 3). `USRPSink` is the Tier-1 version, unbuilt |
| **(B)** | hardware risk from undocumented / misidentifiable parameters | **`NAMEMAP.md`** — every parameter verified against the device, with how it was verified |
| **(C)** | blocking on `start()` | **SOLVED.** Initialisation belongs in `init()`, which `Graph::emplaceBlock` calls where no scheduler and no timeout exist. Measured: INIT ~11 s for four radios, blocking, with nothing waiting on it; first sample ~0.5 s after the scheduler starts. Needed a one-line core fix at `LifeCycle.hpp:238`, without which the `init()` hook is structurally unusable |
