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
through the scheduler:

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
                            "device bring-up runs on an IO thread and processBulk drains it.">;
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
init thread      → make device, apply settings, activate stream, then run the read loop
processBulk()    → drain the ring; if not ready, publish 0 and return OK
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

## 8 · Data path and threading

One IO thread per device, exactly as now — the thread was never the problem. The change is where the
samples go:

| | today | proposed |
|---|---|---|
| IO thread | writes **directly** to `out.streamWriter()` | writes to a **lock-free SPSC ring** owned by the block |
| scheduler | calls `work()`, which returns 0 samples forever | calls `processBulk`, which **drains the ring** |
| back-pressure | none — the port is written behind the scheduler | natural: the ring fills, the read loop sees it and counts an overflow |

**Open question (§12):** whether the ring is a second copy or whether `processBulk` can hand the IO
thread an output span to fill directly. The second is faster and harder; the first is obviously
correct. **Start with the first.**

## 9 · Instrumentation — carry over, it is the one part that is proven

Keep from this session's work on `SoapySource`, unchanged in substance: counters for overflow,
timeout, corruption and stream error; event tags carrying **`device_time_ns`**, not host time;
`lo_lock_ms` per channel. And the honest labelling: **underflow does not exist on RX** — SoapyUHD
never returns it — so the receive-side starvation event is **timeout**.

Add what `SoapySource` lacks: **report the load average** with any throughput figure. Every number in
`RESULTS.md` Phase 10 had to be qualified because no harness recorded it (§10.32).

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

## 12 · Open questions — to settle before writing code

1. **Ring vs. direct span fill** (§8). Correctness first, performance second.
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
