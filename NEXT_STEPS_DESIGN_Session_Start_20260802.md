# NEXT SESSION — `USRPSource` / `USRPSink`, time-provenance-first

**Owner strategy, 2026-08-02. Supersedes `NEXT_STEPS_DESIGN_Session_Start_20260731.md`.**
**Read `TIME_PROVENANCE.md` first. It is the point of this exercise.**

---

## The strategy, as given

Build **`USRPSource`** and **`USRPSink`** with:

- **pure state-machine semantics** around `LifeCycle.hpp` — `init()` for initialisation,
  `start()` for start, as developed and proven this session
- **all relevant time measurements in `uhd_time_t`**, with **time-provenance as the primary
  decision-driver at every level** of a `multi_usrp`
- **wall-clock time restricted** to tests and pre-`INITIALISED` operations, and there only for a
  *binary* decision: an operation ran longer/shorter than expected, kill it and inspect
- **measurements restricted to reported sample offset and clock time** — including counting
  samples between tags in a capture. **Signal content is not relevant** unless the owner is
  running a beacon for that purpose
- **parameters and operations following GR 3.11 `UHD: USRP Source` / `UHD: USRP Sink`**
- **radio operations direct via the USRP/UHD API**, using `multi_usrp` to issue one command to
  all radios where possible; **the single-command timed form always preferred over issuing
  several commands**

**One extension, ours:** *fixed-interval tag reporting* — period specifiable as an **(external
clock) time interval**, with a checkbox to snap it to the **nearest USRP frame boundary**.

**Tiers:** this block fully operational is **Tier 1**, with context-dependent usable UI to
exercise milestones. Tier 2 stays GR4 UI. **Soapy-based blocks for UHD and HackRF drop to
Tier 3 or lower.**

### Code-reuse rules for this work

| source | rule |
|---|---|
| **GR 3.11 UHD Source/Sink** | **may reuse code** where it exemplifies time-provenance-first and does not break the GR4 scheduler model |
| **GR4 SoapySDR block** | **no reuse of code or parameters without the owner's explicit permission** |

⚠ This is a **deliberate change** from the standing "ideas yes, source no" rule
(`HANDOFF.md`, `DESIGN_UhdSource.md` §11b), and it is scoped to GR 3.11's UHD block only.
GR 3.11 is **GPL-3.0**, so reused code makes the result a GPL derivative — fine while never
distributed, and it must be recorded in `DRIFT.md` Category E with the file and line reused.

### Why: "works" has two halves

Throughput at full rate was always the stated criterion. **Time-provenance-first was the
unstated one** — an experience-based assumption, non-obvious to anyone who has not been bitten.
This block exists to exemplify it. That is the "Works" in Works on My Machine.

---

## ⚠ ADDENDA — things that will bite at the start of next session

### 1. ⏸ OPEN QUESTION: one `multi_usrp` per radio, or one across all four?

**Answer this by reading GR 3.11's `UHD: USRP Source/Sink`, not by reasoning from Soapy.**

What is established: `b200_impl.cpp:309` hardcodes `const fs_path mb_path = "/mboards/0"`, so a
B210 presents **one motherboard with two frontends**.

⚠ **Do not carry my earlier framing forward.** I argued from `b200_find` refusing the
`addr0=…,addr1=…` multi-device syntax — but that is **networked-USRP syntax and irrelevant
here**. A B210 is USB, addressed by `serial=`, and never had an address to begin with. The
refusal is not evidence about B210 aggregation; it is evidence about a mechanism B210s do not
participate in.

**So the real question is open**: is **launching four separate `multi_usrp` objects** the right
thing? **It may well be.** Find worked examples of the multi-device commands *for the B210
specifically* — one mboard, two frontends, no addresses — rather than generalising from X300-era
material.

⚠ **What SoapyUHD does is NO LONGER RELEVANT.** GR 3.11's `UHD: USRP Source/Sink` is the new
standard. It may face this same question; **whatever it does, we do.**

What remains true regardless, because it was measured on this hardware: per-radio
`set_time_unknown_pps` → **6.000000 s** epoch spread; external REF+PPS with no zeroing →
**8.509223 s**; one transition detected then broadcast to all → **0.000000 s**. Whatever object
model is chosen, the epoch must end up shared, and that is the check.

### 2. TX safety — the actual rule, and the mechanism that is merely ours

**⚠ Correcting an attribution I got wrong.** `assert_no_tx.cmake` is **not** an owner
requirement. It is a mechanism a previous session invented, and I wrongly presented it as the
safety guarantee and told the owner to "fix it first". **The owner's actual rule is
behavioural**, stated 2026-08-02:

> **Claude — or agents in general, or unlicensed people — should not do transmit operations.**
> **Do not write automated tests for TX.**

That is the rule. It is about **who acts**, and about **not automating emission**. It does not
depend on any build check, and no build check can discharge it.

**Fact worth stating, verified 2026-08-02:** before 2026-08-01 **no transmit-capable binary had
ever existed in this repository.** All seven pre-existing harnesses link zero TX symbols;
`assert_no_tx.cmake` was asserting the absence of something never present. **`womm_uhd_tx`,
created by Claude on 2026-08-01 (commit `3a8b4f6`), is the first** — 5 TX symbols, built and
gated, never run. Authorised at the time ("build tx side-by-side, just don't run it without me
here"), but it moved the tree from *transmission structurally absent* to *transmission
present-but-gated*. If that is not wanted, deleting `womm_uhd_tx.cpp`, `womm_tx_arm.hpp` and
`assert_tx_gated.cmake` returns it; `UhdSink.hpp` alone links nothing until included.

**And the boundary it actually encodes, verified 2026-08-02.** Owner asked whether a cmake gate
makes sense only if tests exist that do TX. Three do:

| | TX symbols | talks to |
|---|---|---|
| `qa_SoapyIntegration`, `qa_SoapyLoopback`, `qa_SoapyRaiiWrapper` | 721 / 1 / 1 | **software loopback** (`SoapyLoopbackModule.cpp`) — never a radio |
| all seven pre-existing `womm_*` harnesses | 0 | real radios |
| `womm_uhd_tx` | 5 | real radios; gated, never run |

So TX code *is* exercised in tests, against a fake device. The gate therefore encodes a real
boundary — **hardware-touching binaries carry no transmit path; test binaries may** — and that
boundary predates the gate. **No TX test was ever disabled or commented out**; the only two
`SKIPPED` lines in the sdr test CMake are ABI-mismatch gates on the loopback module.

`assert_no_tx.cmake` is one implementation of the second half: it greps the linked binary for
`SoapySink|writeStream|SOAPY_SDR_TX`. **Note if we keep relying on it**: a pure-UHD transmit path
presents as `tx_streamer`, `get_tx_stream`, `send`, `tx_metadata_t` — none of which it matches —
so it would pass a UHD TX binary silently. Same for `assert_tx_gated.cmake`, also ours.

**Fix the symbol lists if we keep the mechanism; drop it if it is not earning its keep.** Either
way it is a convenience, not the safeguard, and it must never be cited as the reason something
is safe to run.

### 3. `"UNKNOWN_PPS"` is stale vocabulary — use `"external"`

Current API and GR 3.11+ use `"external"`. `"UNKNOWN_PPS"` survives in `SoapyUHDDevice.cpp:874`
and older examples. Look values up via `https://files.ettus.com/manual/functions_<letter>.html`
— the site *search* is broken since Ettus changed hosts; the per-letter index is reliable.

### 4. Licence position, stated so it is not rediscovered

UHD is **GPL-3.0**; linking it directly is the same exposure we already carry through SoapyUHD,
only more direct. Reusing GR 3.11 source adds a second GPL derivation. The owner has ruled
**Tier 1 outranks licence purity**, and MIT purity is a `wosem` concern (Works On Someone Else's
Mac) that we are not working on. Record, do not re-litigate.

---

## What carries over from this session

**Directly reusable, API-agnostic:**

| item | note |
|---|---|
| **`TIME_PROVENANCE.md`** | the whole point; nothing in it is Soapy-specific |
| **`init()` vs `start()` split** | the fix for the serial-bring-up stall. Blocking `init()` is safe because `Graph::emplaceBlock` calls it where **no scheduler and no timeout exist** |
| **the `LifeCycle.hpp:238` core fix** | ⚠ **required regardless of driver.** Without it the `init()` hook is *structurally unusable* — the no-arg hook hides `Block::init(progress, domain)`, and `using` makes `&TDerived::init` ambiguous. Both horns compiler-verified |
| **`UhdRing`** | SPSC ring, driver-agnostic, keep as-is (rename) |
| **the epoch discriminator** | `get_time_last_pps()`: same edge → sub-ms, different edges → whole seconds. ~1000× separation, so it is an instrument |
| **`womm_tx_arm` + `assert_tx_gated.cmake`** | keep; update the symbol list per addendum 2 |
| **harness shape** | INIT-phase vs START-phase timing; per-channel tag/offset checks; the boundary-aliasing guard |
| **`B210_sizing.md`** | 8176-byte USB3 frame, ~2040 samples, "just under a power of two". `get_max_num_samps()` returns it at runtime — **this is what the frame-boundary checkbox rounds to** |

**Becomes obsolete:**

- `SoapyRaiiWrapper` dependency
- `patches/womm/SoapyUHD-0003` (`SYNC_DEVICES`) — its *logic* moves into `USRPSource`; its
  *discoverability* half was a Soapy-specific fix and is not needed against the UHD API
- `UhdSource`/`UhdSink` as Soapy-based blocks → Tier 3

**Still untested and must not be assumed working:**

- **resync has never fired** — implemented, never triggered. Provoke with a low
  `num_recv_frames`. An untriggered recovery path is not a working one
- **`UhdSink` has never transmitted** — the `readStreamStatus`/`recv_async_msg` underflow path
  is unexercised, and needs the owner at the console

---

## Recommended sequencing

1. **`assert_no_tx.cmake` symbol list first.** Safety gate before any TX-capable code exists.
2. **`USRPSource` skeleton on the proven lifecycle** — `init()` blocking device bring-up,
   `start()` arming only, `processBulk` draining the ring. This part is already proven at
   120 MS/s; only the layer beneath it changes.
3. **Epoch + broadcast**, per addendum 1, with the last-PPS discriminator wired into the harness
   from the outset — not added afterwards.
4. **Fixed-interval tag reporting**, seconds in the API, snapped to `get_max_num_samps()` when
   the checkbox is set.
5. **`USRPSink`** — build, gate, do not run unattended.
6. **Provoke the failure modes**: overflow → resync; then TX underflow with the owner present.

**Test discipline, carried forward:** before trusting any check, ask *what it would report if
the thing being tested were false.* Two checks written this session read back literals we had
written and would have looked perfect either way. And prefer an out-of-band witness — the owner
reading front-panel LEDs caught a whole day of runs pointed at a capped `RX2` port while every
counter read perfect.
