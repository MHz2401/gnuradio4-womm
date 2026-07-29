# DRAFT — is multi-process actually necessary, or did we condemn multi-threading on bad evidence?

**Status: draft for review, not patched into the project docs.** Written 2026-07-28 at the owner's
request. If accepted, the natural homes are `SPRINT.md` (as a sprint-2 item) and a short note in
`RESULTS.md`.

---

## 1. Correction: it was four processes, not five. There is no main flowgraph.

The 122.88 MS/s run was a **bash `for` loop** launching four independent `womm_rx_hold` processes,
staggered 3 s apart. Verified in the source:

- `womm_rx_hold.cpp:228-229, 274` — each process builds **its own** `gr::Graph`, with **one**
  `SoapySource<complex<float>, 2>`, and **its own** `gr::scheduler::Simple<multiThreaded>`.
- The launcher (`scripts/womm-scan.sh`, or the shell loop before it) constructs no graph at all.

**So: four processes, four graphs, four schedulers, fully independent.** Nothing coordinates them at
runtime except the shared Octoclock PPS — which is hardware, not software. There is no fifth
process and no owning flowgraph.

## 2. Does gr4 support a flowgraph owning sub-processes?

**No evidence of it.** Searching `core/include/gnuradio-4.0/` for `fork`, `posix_spawn`, `execv`,
`std::system` and `popen` returns nothing but `std::system_error` exception types. The graph model
is in-process: blocks, a scheduler, and thread pools. This matches the owner's recollection of
gr3.x.

*Caveat on confidence:* this is "no subprocess machinery in core", established by search, not an
exhaustive audit of the whole tree or of upstream intent. The nearest thing that exists is
`gnuradio4-control-plane`, which manages **sessions** over REST — but that is an external service
supervising graphs, not a graph owning processes.

## 3. ⚠ The mis-attribution is real, specific, and untested

**The single-process multi-radio model has never been run in the configuration that works.**

| harness | topology | channels | DSP depth | result |
|---|---|---|---|---|
| `womm_bmax` | **1 process, 1 graph, 1 scheduler, N radios** | **1** (`:116`) | 8 | ~40-44 MS/s aggregate |
| `womm_rx_hold` | **N processes, 1 radio each** | **2** | 0 | **122.88 MS/s, ratio 1.0000** |

Every figure that made multi-process look necessary came from `womm_bmax` — the **single-process**
harness — and every one of those runs was **single-channel at depth 8**, i.e. crippled by the same
bug that made all pre-2026-07-27 measurements half-blind.

Worse, the ~44 MS/s figure was then *explained* in terms that implicate the shared scheduler:
`RESULTS.md` §8.3 profiled `cleanupZombieBlocks`/`adoptBlocks` mutex contention among workers
sharing one graph, and §8.6 concluded a global cap. §8.3's own conclusion was that the contention
was a **symptom, not the cause** — workers spinning with nothing to do — but the shape of the
evidence still pointed at "radios in one scheduler contend".

**Nothing has ever tested 2 channels × depth 0 in a single process.** The two variables that
produced 122.88 MS/s were changed at the *same time* as the topology.

## 4. ★ THE EXPERIMENT WAS RUN. Multi-process is NOT necessary.

`blocks/sdr/src/womm_mt_test.cpp` — new file, reversible by deletion. 4 radios × 2 channels,
depth 0, **ONE graph, ONE scheduler, one process**. Everything held at the working configuration;
only the topology changed.

| aggregate target | threads | result |
|---|---|---|
| 15.36 MS/s | 16 | 15.31, ratio 0.9968 |
| 30.72 MS/s | 16 | 30.72, **ratio 1.0000** |
| 61.44 MS/s | 16 | 61.44, **ratio 1.0000** |
| 122.88 MS/s | 16 | **FAILED** — overflows, then `stream error: -2` |
| **122.88 MS/s** | **24** | **122.24, ratio 0.9948** |

**The full-rate failure at 16 threads was my own pool under-sizing, not a topology limit.** The
multi-process run used 6 threads × 4 processes = 24 total; 16 in one process is not the same
experiment. Matched at 24, single-process reaches **122.24 MS/s against multi-process's 122.88 — a
0.5 % difference.**

### What this settles

- **Multi-process is not required.** One ordinary gr4 graph carries four radios and eight channels
  at essentially full rate.
- **The owner's mis-attribution hypothesis is confirmed.** Single-process was condemned on
  single-channel depth-8 evidence, and my own first attempt then failed for an unrelated
  pool-sizing reason. Neither had anything to do with the topology.
- **S2-1 is largely dissolved, not solved.** The cross-process start barrier is the cost of a
  topology we do not need. Four radios started inside one process arm within one code path.

### What it does NOT settle

- **Depth 0 only.** No DSP in the graph. The scheduler-contention concern from `RESULTS.md` §8.3 was
  measured with depth-8 chains and is untested in this topology at full rate.
- **One run per point**, not repeated. The 0.5 % gap is not established as real or as noise.
- **Startup overflows still occur** at every rate — visible as `O` markers while later radios
  initialise and earlier ones already stream. Survivable at 24 threads, fatal at 16. Nothing here
  makes bring-up clean; it makes it survivable.
- **Fault isolation is genuinely lost.** One radio's stream error now takes the whole graph down —
  which is exactly what happened at 16 threads. In four processes, three would have survived.

## 5. Recommendation, revised by the result

**Make single-process the default and keep multi-process as the fault-isolation option.** Ordinary
in-process gr4 is simpler for everything downstream, including any UI, and it removes an entire
class of orchestration problem.

Two follow-ups this creates, both cheap:

1. **Re-run with DSP depth** — the one variable still held at zero, and the one §8.3 implicated.
2. **Pool sizing is now load-bearing and undocumented.** 16 threads silently fails at full rate
   where 24 works. That is a cliff, not a gradient, and nothing warns you.

## 6. Why this matters more than it looks

**The entire cross-process barrier problem exists *because* we are multi-process.** S2-1 — which the
owner identifies as one of the core console tasks of the MCM, and a hard problem — is the cost of a
topology we may not need. In a single process, four radios arm inside one code path within the same
second; the PPS-edge agreement we currently *observe* would come much closer to being structural,
without a filesystem-based barrier at all. **Solving S2-1 and eliminating the need for it are both
on the table, and we have not checked which is cheaper.**

**The oversubscription finding argues FOR single-process, not against it.** `RESULTS.md` records
3 processes × 24 default threads = 72 workers on 24 cores, costing 12× throughput (3.54 vs 40.89
MS/s) until pools were sized by hand. That is a *consequence* of multi-process: one process sizes
its pool once, correctly, and the trap cannot occur.

**What would still favour multi-process,** stated fairly so the experiment is not rigged:

- **Fault isolation** — one radio's crash does not take the others down. Real, and not a performance
  argument.
- **The MCM's original reason may not apply.** It was built on GR 3.x. If its multi-process design
  was working around a 3.x limitation, that constraint may simply not exist in gr4 — which is
  precisely the claim this whole project set out to test.

## 7. Original recommendation, superseded by §5

Run the experiment **before** investing in S2-1. If single-process reaches full rate, the barrier
specification is work we may never need, and gr4 integration for everything downstream — including
any UI — becomes ordinary in-process gr4 rather than an orchestration problem.

**Suggested wording for `SPRINT.md`, if accepted:**

> **S2-0 (do first): is multi-process necessary?** 4 radios × 2 channels × depth 0 in ONE graph and
> ONE scheduler, `WOMM_THREADS` sized once, against the recorded 122.88 MS/s. Every prior
> single-process figure was taken single-channel at depth 8 and is void under the
> authoritative-sources rule. **S2-1 is contingent on this result** — a barrier is only needed if
> multi-process is.
