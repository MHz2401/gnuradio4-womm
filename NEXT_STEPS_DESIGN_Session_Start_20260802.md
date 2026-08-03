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

### 1. ★ Going direct to UHD does NOT solve the multi-device problem

**The one-mboard limit is UHD's, not Soapy's.** `b200_impl.cpp:309` hardcodes
`const fs_path mb_path = "/mboards/0"`, and `b200_find` refuses the `addr0=…,addr1=…`
multi-device syntax outright. So **four USB B210s are four `device` objects and four
`multi_usrp` objects whichever API you use.**

⇒ *"use `multi_usrp` commands to issue the same command to all radios"* is limited by the
driver, not by the wrapper. `ALL_MBOARDS` covers **one B210**. Multi-mboard aggregation belongs
to the **networked** USRPs (X300 and friends), which build one device from several addresses.

⇒ **The broadcast loop does not disappear.** Measured this session: per-radio
`set_time_unknown_pps` → **6.000000 s** epoch spread; external REF+PPS with no zeroing →
**8.509223 s**; one transition detected then broadcast → **0.000000 s**. Plan for the broadcast;
do not expect the API to remove it.

### 2. ★ `assert_no_tx.cmake` is BLIND to a pure-UHD transmit path — fix before any Sink work

It matches `SoapySink|writeStream|SOAPY_SDR_TX`. A UHD TX path presents as `tx_streamer`,
`get_tx_stream`, `send`, `tx_metadata_t` — **none of which it matches.** Every RX-only harness
would keep passing while linking a transmit path. **This is a safety regression, and it is the
first thing to fix in the new tree**, alongside `assert_tx_gated.cmake`, which needs the same
symbol list.

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
