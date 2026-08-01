# DESIGN — `UhdSource`, a conformant GR4 block over SoapyUHD

**Status: design for review. Nothing implemented.**
**Decision (owner, 2026-07-30): SoapySDR as implemented on this site — verification status ABANDONED.**

## 1 · Why, in one paragraph

`SoapySource` cannot be trusted parameter by parameter. Verified, not asserted: two settings are
declared and never read (`tune_args`, `ppm_tag_threshold`); one is documented "max samples per read"
and reaches nothing (`max_chunk_size`); transport keys placed where the driver *says* they are valid
are silently ignored on USB; twelve settings are start-only and a runtime change to them is accepted,
staged and discarded without warning; and the block overrides `work()` to return zero samples forever
while an IO thread writes to the output port behind the scheduler's back. Detail in
[PARAMS.md](PARAMS.md). Fixing it in place was rejected by the owner — it is in-tree, and changing it
invites objection without removing the design.

## 2 · Scope

**In:** a receive-only GR4 block wrapping **SoapyUHD**, targeting the B210 on this machine.
**Out:** transmit, in any form. Non-UHD devices. Replacing `SoapySource` (it stays; this is additive).
**Fallback if SoapyUHD proves unworkable:** wrap the **Ettus UHD C++ API** directly, losing Soapy's
device abstraction but removing a layer whose universal API is the documented source of the ambiguity
(`SOAPY_UHD_MAP.md`).

## 3 · Dependency policy — fork SoapySDR separately

SoapyUHD depends on SoapySDR. Per the owner: **references to SoapySDR must use a separate fork**, so
breaking changes here cannot affect this or other projects.

| tree | today | proposed |
|---|---|---|
| SoapySDR | `vendor/SoapySDR` @ `1551ea0d`, byte-identical to pothosware | **fork to `MHz2401/SoapySDR`**, vendor from the fork, keep the SHA pin |
| SoapyUHD | `vendor/SoapyUHD` @ `2a5d381f`, byte-identical + 2 build-time patches | **fork to `MHz2401/SoapyUHD`**, same |

Both currently reproduce byte-for-byte from upstream (`RESULTS.md` §10.26), so the fork starts as a
pure rename with no divergence — and `scripts/verify-vendor.sh` keeps proving that until we
deliberately change something. **MIT throughout**, so D9's licence preference is unaffected.

## 4 · The pattern to copy, and the one to avoid

**Avoid:** all four in-tree device blocks (`SoapySource`, `SoapySink`, `RTL2832Source`,
`AudioSource`) override `work()` and have no processing method of their own. The owner's assessment
of `AudioBlocks` — *"I would not count on it being the standard for quality"* — is confirmed by
reading it (`RESULTS.md` §10.37).

**Copy: `HttpBlock`** — the one in-tree block that drains an **external asynchronous resource**
through the scheduler. It is **in-tree and MIT**, so its patterns may be followed directly:

```cpp
void start() { openReader(); }                       // one line; readAsync defers the work

[[nodiscard]] work::Status processBulk(OutputSpanLike auto& outSpan) {
    if (outSpan.empty()) { return work::Status::INSUFFICIENT_OUTPUT_ITEMS; }
    _reader.poll(/* non-blocking */ …);
    outSpan.publish(nSamplesToPublish);              // possibly 0, not an error
    return finished ? work::Status::DONE : work::Status::OK;
}
```

Four required properties: **never block**; **publish a variable count including zero**; **guard on
`outSpan.empty()`**; **`start()` returns immediately**.

## 5 · Structure

Canonical member order per `CLAUDE.md` §2. `struct`, not `class`.

```cpp
GR_REGISTER_BLOCK("gr::blocks::uhd::UhdSource",     gr::blocks::uhd::UhdSource, ([T], 1UZ), [ std::complex<float>, std::complex<short> ])
GR_REGISTER_BLOCK("gr::blocks::uhd::UhdDualSource", gr::blocks::uhd::UhdSource, ([T], 2UZ), [ std::complex<float>, std::complex<short> ])

template<typename T, std::size_t nPorts = 1UZ>
struct UhdSource : gr::Block<UhdSource<T, nPorts>> {
    using Description = Doc<"Receive-only UHD source. start() returns immediately; "
                            "device bring-up runs on a thread, then processBulk receives directly.">;
    …ports… …settings… GR_MAKE_REFLECTABLE(…) …private state…
    void start(); void stop();
    [[nodiscard]] work::Status processBulk(OutputSpanLike auto& outSpan);
    void settingsChanged(const property_map&, property_map&, property_map&);
};
```

## 6 · Parameters — derived, not inherited

**Rule: every parameter must be justified by (a) the device declaring it, (b) UHD documenting it, or
(c) a measurement here. Nothing is carried over from `SoapySource` on faith.**

**Device args** — reach `multi_usrp::make()`, so anything affecting the transport belongs here:
`serial`, `master_clock_rate` (F-2: as a device arg, so UHD does not default to 16 MHz),
`num_recv_frames`, `recv_frame_size`. Exposed as one `device_args` string plus typed conveniences.

**Stream args** — reach `get_rx_stream()`. Only what SoapyUHD declares and USB honours: `spp`
(the real samples-per-read knob, which `max_chunk_size` pretended to be), `WIRE`, `peak`, `fullscale`.

**Tune args** — reach `tune_request_t.args`, and **must actually be wired**, unlike `SoapySource`'s
dead field: `mode_n` (`integer` / `fractional`), `int_n_step`.

**Block settings**, each runtime-changeable or explicitly documented as start-only: `sample_rate`
(⚠ **set the rate, not the MCR** — F-1), `frequency`, `rx_gains`, `rx_bandwidths`, `rx_antennae`,
`clock_source`, `time_source`, `start_time_offset`, `max_time_out_us`.

**Validated at start, not assumed:** `clock_source`/`time_source` against
`listClockSources()`/`listTimeSources()`, rejecting an unlisted value instead of letting the device
free-run (F-4); antenna against `listAntennas()`; gain against `getGainRange()`.

## 7 · Lifecycle

```
start()          → spawn init thread, return immediately          (≪1 ms)
init thread      → make device, apply settings, activate stream, mark ready, exit
processBulk()    → recv() straight into the output span; if not ready, publish 0 and return OK
stop()           → request stop, join, deactivate, release
```

**Timed start is mandatory for multi-channel** (H-1): `activate()` must pass `SOAPY_SDR_HAS_TIME`
with a `time_spec`, or UHD refuses a multi-channel streamer. Not optional, not a synchronisation
luxury.

**Because `start()` no longer blocks, four radios initialise concurrently** — which is what the
serial `forEachBlock` traversal in upstream's `SchedulerBase::start()` prevents today
(`RESULTS.md` §10.28). **This is the fix for the ~9 s serial bring-up, at block level, with no core
change.** It does not provide the *rendezvous* that GR 3.10's `thread::barrier` gives (§10.23); that
remains open and is a separate piece of work.

## 8 · Data path — GR 3.10's mechanisms, GR4's threading

Owner: *mechanisms for high-performance streaming can probably be modelled on GR 3.10 blocks.*
Correct, and it overturns the ring design this section previously proposed.

**`gr-uhd/lib/usrp_source_impl.cc:614-623` — the reference implementation.**
⚠ **QUOTED FOR ANALYSIS ONLY — GNU Radio 3.10 is GPL-3.0. Do not copy these lines into this tree.**

```cpp
int usrp_source_impl::try_work(int noutput_items, …, gr_vector_void_star& output_items) {
    // In order to allow for low-latency:
    // We receive all available packets without timeout.
    size_t num_samps = _rx_stream->recv(
        output_items, noutput_items, _metadata, _recv_timeout, _recv_one_packet);
```

**`recv()` writes straight into the flowgraph's own output buffers.** No IO thread. No intermediate
ring. **No copy.** The scheduler's thread does the receive, inside `work()`.

| | `SoapySource` today | my earlier proposal | **GR 3.10, adopted** |
|---|---|---|---|
| receive runs on | IO thread | IO thread | **scheduler thread, in `processBulk`** |
| samples land in | port writer, behind the scheduler | SPSC ring, then copied | **the output span, directly** |
| copies | 1 | 2 | **0** |

### The four behaviours that make it work, all from the reference

| condition | GR 3.10 | line |
|---|---|---|
| **timeout** | `return 0` — *"its ok to timeout, perhaps the user is doing finite streaming"* | `:649-651` |
| **overflow** | set `_tag_now`, count, rate-limited log, publish an async message, `return -1` → **`work()` retries**, up to `_num_overflow_retries = 10` | `:653-674` |
| **other error** | warn and `return num_samps` — keep whatever arrived | `:676-678` |
| **normal** | if `_tag_now`, emit `rx_time` / `rx_rate` / `rx_freq` at the current sample offset | `:628-646` |

### ★ The element I had missed entirely: re-tag after overflow

`_tag_now = true` is set **on overflow** (`:654`), as well as at start (`:84`, `:108`) and on an
explicit `tag` command. So **the chunk following any discontinuity carries a fresh time/rate/frequency
tag.**

That is exactly the owner's description of correct behaviour — a beginning-of-stream tag carrying
time and sample position, re-anchored whenever the sample-to-time mapping breaks. **A consumer can
therefore always map samples to device time**, which is the whole point of the disciplined set-up.
`SoapySource` publishes a bare `rx_overflow` flag and never re-anchors.

### Latency posture

`_recv_timeout = 0.1 s`, `_recv_one_packet = true` (`:37-38`) — return as soon as one packet is
available rather than waiting to fill the buffer. Settable at runtime via `set_recv_timeout()`.

### ⚠ BUT — owner's caution, and it is decisive

*"Revising the design to the GR 3.10 model may result in effectively building GR 3.20 rather than
GR4."*

**Correct, and it invalidates a straight port.** GR 3.10 can `recv()` into the output buffer and
block for `_recv_timeout` because of **thread-per-block**: that block owns its thread
(`scheduler_tpb`, §10.23), so blocking costs only its own latency. **GR4 has a worker pool.**
Blocking inside `processBulk` holds a *shared* worker and starves every other block assigned to it.
The mechanism is safe in 3.10 *because of* an architecture GR4 does not have.

### ★ Which explains why all four device blocks override `work()`

This is not laziness, and the earlier framing of it as an anti-pattern was too glib. **GR4's
worker-pool model offers a device source no good option:**

| option | cost |
|---|---|
| block in `processBulk` waiting on the device | holds a shared pool worker — starves other blocks |
| poll with ~zero timeout | burns a worker spinning — the problem `DRIFT.md` Category F already fixed once |
| side thread writing to the port | never blocks the scheduler, but **bypasses it entirely** — what all four do |

**Every in-tree device block chose the third.** That is a rational response to a real gap, not a
failure of care.

### The synthesis — take the mechanisms, not the threading model

| take from GR 3.10 | leave in GR 3.10 |
|---|---|
| **re-tag after overflow** (`_tag_now`) so the stream is re-anchored at every discontinuity | `recv()` on the scheduler's thread |
| **bounded retry** on overflow rather than one-shot | a 100 ms blocking timeout |
| tags carrying **time / rate / frequency** at the sample offset | thread-per-block assumptions generally |
| timeout treated as **normal**, publish 0, not an error | |

**So the IO thread stays** — but it delivers through `processBulk` rather than writing to the port
behind the scheduler. That is the GR4-native shape, and it is what §8 proposed before the GR 3.10
detour:

```
IO thread          : recv() into an SPSC ring, may block freely — it is our thread
processBulk(outSpan): drain the ring, never block, publish what is there (possibly 0)
```

**One copy, not zero** — the price of not owning a scheduler thread. Reducing it to zero would mean
the IO thread writing into a span obtained from the port, which is what `SoapySource` does today and
is precisely the bypass we are trying to remove. **Correctness before the copy.**

## 9 · Instrumentation — carry over, it is the one part that is proven

Keep from this session's work on `SoapySource`, unchanged in substance: counters for overflow,
timeout, corruption and stream error; event tags carrying **`device_time_ns`**, not host time;
`lo_lock_ms` per channel. 

**OWNER COMMENT**: sorry I missed this BS-ish and misleading statement text:  
~~And the honest labelling: **underflow does not exist on RX** — SoapyUHD
never returns it — so the receive-side starvation event is **timeout**.~~ 
Here is a quote directly from a **source of truth**, the Ettus hardware manual, which simple web search finds at
https://files.ettus.com/manual/page_general.html#:~:text=as%20a%20result.-,Overflow/Underflow%20Notes,-Note%3A%20The
> **Underrun notes**   
> When transmitting, the device consumes samples at a constant rate. Underflow occurs when the host does not produce  
> data fast enough. When UHD software detects the underflow, it prints a "U" to stdout, and pushes a message packet into the async message stream.


~~Add what `SoapySource` lacks: **report the load average** with any throughput figure. Every number in
`RESULTS.md` Phase 10 had to be qualified because no harness recorded it (§10.32).~~
**OWNER COMMENT:** If you're a hacker bit-banging a laser printer then you might need to "estimate the sample rate," 
of a non-radio that you have coerced into producing radio emissions.  However, there's no modern SDR, including hackrf
or Flipper, that needs to "estimate the sample rate."  SoapySDR dates back to when there were no SDRs and you needed 
to hack what you have. Parameters like this are silly for a modern digital SDR platform, but have many wonderful 
uses by friends and neighbors all over the world who like to make radios out of non-radios. If you can get signal 
from a negighbor's hair-dryer, you're proabably not concerned with 'measuring the load average.'    
   
SoapySource (and all other drivers) lack 'meausuring the load averager' for this reason. If you're using a modern  
digital SDR and want to know the load average, you multipy the values you entered into the device:   
`Total_Load_Of_Run = sample_rate * bytes_per_sample * number_of_channels * how_long_channels_the_channels_ran`   
`Load_Average = Total_Load_Of_Run / how_long_channels_the_channels_ran`   
The `Load_Average` failure mode: _"I don't know `how_long_channels_the_channels_ran`, so this is hard."_    
Solution: AL-GEBRA!  `Load_Average = sample_rate * bytes_per_sample * number_of_channels`  

## 10 · Testing

- `qa_UhdSource.cpp`, Boost.UT, per `CLAUDE.md` §7, with skip guards when no device is present.
- **Hardware-free tests**: settings validation and rejection, tag key presence, ring drain under
  synthetic fill, `processBulk` publishing 0 without error, start-returns-immediately.
- **RX-only gate**: `assert_no_tx.cmake` on the linked binary, as for every harness here.
- **The null control** (`OPERATIONS.md`) is the acceptance test: at the operating point where the
  current block reaches zero attributed events, the new one must too.

## 11 · Migration

Additive. `SoapySource` is untouched; `UhdSource` lives beside it in `blocks/sdr/`. Harnesses select
via an env switch until the new block is proven, then default over. **Nothing in `DRIFT.md` is
reverted by this** — the Category I instrumentation stays useful for both.

## 11b · ⚠ LICENCE RULE — ideas yes, source no

**Owner, 2026-07-30: from fair-acc you may plagiarise ideas and procedures, but do not cut and
paste. LGPL vs MIT.**

| source | licence | what may be taken |
|---|---|---|
| **this tree / `gnuradio/gnuradio4` / the separated repos** | MIT | **anything** — `HttpBlock`, `ClockSource`, `NullSources` are safe to follow line for line |
| **`fair-acc/gnuradio4`**, including its WiP branches | **LGPL-3.0** | **ideas and procedures only.** Read `ian/779-convert-exceptions-to-expected`, `ian/fix-tsan-qa-scheduler-messages`, `onnx_integration` for approach — then write our own |
| **GNU Radio 3.10** (`gr-uhd`, `gr-zeromq`, `scheduler_tpb`) | **GPL-3.0** | **ideas and procedures only**, and stricter — GPL would be viral on distribution |

**Practical test before writing a line:** if the result would be recognisable as *their* code rather
than ours — same identifiers, same structure, same ordering — it is a copy, whatever the intent.
Re-expressing a mechanism after understanding it is not.

**What this design takes, and it is all in this category:** re-tag after a discontinuity; bounded
retry on overflow; treat timeout as normal; tag with time, rate and frequency at the sample offset;
defer slow work out of `start()`. **Procedures, every one — no source.**

**Already-known exposure:** `HANDOFF.md` I-8 records that four cherry-picks postdate fair-acc's
relicensing, so the tree is *"MIT on paper, LGPL-derived in fact"* (`DRIFT.md` Category E). That is
existing and separately revertible. **This rule prevents adding to it.**

## 12 · Open questions — to settle before writing code

1. ~~Ring vs. direct span fill~~ — **settled by GR 3.10's `usrp_source`: recv directly into the output span, no ring, no IO thread in steady state (§8).**
2. **Does `processBulk` see enough of the output span** to publish a full device chunk, or does the
   scheduler's sizing force fragmentation? Determines the ring's shape.
3. **Which of `SoapySource`'s remaining features are wanted at all** — DC blocker, ppm estimator,
   frequency correction, IQ balance? Each is a parameter to justify or drop. **Default: drop, and
   re-add on demand.**
4. **SoapyUHD or raw UHD?** SoapyUHD keeps a device abstraction we do not currently need, and its
   universal API is the documented source of the ambiguity. Raw UHD is one layer thinner and maps
   directly onto the manufacturer's documentation. **Worth deciding before, not during.**
5. **ZMQ** — the owner reports gr4 is planning one, going to an upcoming architecture working group.
   A ZMQ source faces the identical problem this design solves (draining an external async transport
   through the scheduler), so **the pattern chosen here should be one that generalises**, and the
   working group's outcome is worth knowing before committing.
