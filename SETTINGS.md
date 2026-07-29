# SETTINGS — every name/value in the pristine harnesses, for owner categorisation

Extracted from `75f9bb3` — the harnesses **as they stood at the start of the 2026-07-29 session**,
before any change made that day. These encode experiments that were learned one at a time, and the
reasons are not all recorded.

**Owner to categorise each row:** `known-good` · `probably-good` · `less-bad-than-others` · `unsure`.
Fill the last column; anything marked `unsure` becomes a candidate experiment rather than an
assumption.

## ⚠ First, the thing that makes this table necessary

**Soapy and Ettus use the same parameter names in different places, and the wrong place fails
silently.** Measured 2026-07-29: `num_recv_frames=1024` passed as **stream args** has *no effect
whatsoever* — identical to not setting it. The same value in **device args** does work. Ettus's own
wording is "as part of the device args".

So the "where" column is not documentation trivia; it is the difference between a setting and a
no-op. Every row below is grouped by where it actually lands.

---

## A · Device args — reach `multi_usrp::make()` and the transport

Built into `device_parameter`, merged into the kwargs passed to `soapy::Device::make`.

| setting | pristine value | used by | note | category |
|---|---|---|---|---|
| `driver` | `uhd` | both | | |
| `serial` | per radio | both | from `$B210U00…03`, never committed | |
| `num_recv_frames` | **not set** | neither | ⚠ the only place it works, and no harness has ever set it here | |
| `recv_frame_size` | **not set** | neither | ⚠ the value Ettus actually names in its known-issues text | |

## B · Stream args — reach `uhd::stream_args_t.args` via `get_rx_stream()`

| setting | pristine value | used by | note | category |
|---|---|---|---|---|
| `num_recv_frames` | `1024` | **MP only** | ⚠ **measured to have no effect here.** MT set nothing, so the two harnesses differed by a setting that does nothing — a real difference with an inert effect | |

## C · Block settings — gr4 `SoapySource`, applied through Soapy setter calls

| setting | MP (`womm_rx_hold`) | MT (`womm_mt_test`) | note | category |
|---|---|---|---|---|
| `master_clock_rate` | `30.72e6` | `30.72e6` | the documented **dual-channel maximum**. `0` (auto) makes the device pick 16 MHz, which caps dual-channel at 8 MS/s — so this is load-bearing for full rate | |
| `sample_rate` | `15.36e6` (= MCR/2) | `15.36e6` | decimation 2, the minimum | |
| `num_channels` | `2` | `2` | | |
| `frequency` | `2401e6` | `2401e6` | non-round MHz less crowded; `womm-scan.sh` uses `2401.1042e6` | |
| `rx_gains` | `20.0` dB | `20.0` dB | | |
| `rx_bandwidths` | `= sample_rate` | `= sample_rate` | bandwidth tied to rate | |
| `rx_antennae` | `TX/RX` | `TX/RX` | `RX2` also allowed. Selecting TX/RX as an RX antenna does not enable transmit | |
| `max_time_out_us` | `1000000` (1 s) | `1000000` | vs the block default of 1000 µs | |
| `max_overflow_count` | `0` (never stop) | `0` | | |
| `max_chunk_size` | `8192` (512<<4) | `8192` | | |
| `emit_timing_tags` | **`true`** | **`false`** | ⚠ **harnesses disagree.** MT comments "depth 0 and throughput only: no tag cost"; MP wants the device's own time back | |
| `emit_meta_info` | **`true`** | **`false`** | ⚠ same disagreement | |
| `tag_interval` | **`1.0`** s | *(unset)* | MP: one tag/s rather than one per chunk (~1875/s at this rate) | |
| `clock_source` | `external` (when `WOMM_EXTCLK=1`) | same | | |
| `time_source` | `external` (when `WOMM_EXTCLK=1`) | same | | |
| `start_time_offset` | `5.0` s | `5.0` s | with external time; else 0.1 s default | |
| `reference_clock_rate` | *(unset)* | *(unset)* | Octoclock is 10 MHz | |

## D · Harness / host level — never reach the radio

| setting | MP | MT | note | category |
|---|---|---|---|---|
| `WOMM_THREADS` | `6` per process ×4 | `24` in one process | 24 total either way. §9.14: a **cliff**, not a gradient — 16 silently fails at full rate | |
| topology | 4 processes, 1 radio each | 1 graph, 1 scheduler, 4 radios | | |
| launch stagger | 3 s apart | n/a (serial bring-up in one `start()`) | | |
| sink type | `CountingSink` / `BasicFileSink` / `TagSink` | `CountingSink` / `BasicFileSink` | | |

## E · `scripts/womm-scan.sh` — the capture path, and it disagrees with both

| setting | value | note | category |
|---|---|---|---|
| `WOMM_MCR` | **`30e6`** | ⚠ **not `30.72e6`** — a round MCR, unlike either harness | |
| `WOMM_RATE` | `0.5e6` | capture, not throughput | |
| `WOMM_FREQ` | `2401.1042e6` | deliberately non-round | |
| `WOMM_GAIN` | `20` | | |
| `WOMM_MAX_SEC` | `120` | backstop | |

---

## Questions this inventory raises on its own

1. **`emit_timing_tags` / `emit_meta_info` disagree between the two harnesses** — MP on, MT off. Both
   were "the working configuration" for their respective results, so one of the two full-rate
   results was taken with tags and one without.
2. **`womm-scan.sh` uses MCR 30e6 while both harnesses use 30.72e6.** Deliberate, or drift?
3. **`recv_frame_size` has never been set anywhere**, in any place, despite being the value Ettus
   names explicitly in its B200 known-issues text.
4. **`reference_clock_rate` is never set** although an external 10 MHz reference is in use.
5. **`max_time_out_us` is 1 s** against a block default of 1 ms — a 1000× departure, reason unrecorded.
