# n Top priorities for the next session: 

(owner's document, from Claude Code summary)

wrapping current session soon, current stamp is 20260728 1900H

1. Evaluate gnuradio4-control-plane on macOS — MIT, 24 commits, auditable in an afternoon. It's the only unknown gating the whole UI question: if the C++ control plane won't build against our macOS GR4 install, Studio is moot. (UI_OPTIONS.md)

2. Define "Usable UI" — coined in D10, still undefined. The deliverable is the definition, not an implementation. Best asked as: does it run the radios, or watch them?

3. S2-1: specify the cross-process arming barrier before implementing it — it's currently code in one harness and nothing else.  (UPDATED - owner's note, it's well-defined in context, forward-def was added to RESULTS.md)

4. S2-3: apply the diagnostic procedure to retune / gain change / settings change at capacity. Lazy evaluation — the catalogue of anticipated drop-causes accretes from real triggers.

5. S2-2: tag-interval default for a block that doesn't know its rate at construction. The conversion is right; the default still assumes the caller thought about it.

6. S2-4: document per-platform parameter conventions (B210 / RTL-SDR / HackRF). MANUAL.md covers B210; the others are open, and gain having no universal convention is the trap.

7. Blocks have no status list — 44 registered types, and registration means constructible, not working, tested, or Mac-clean. Scoping that is unstarted.

8. Re-check qa_BasicFileIo determinism — recorded as flaky from before the Category F condvar fix, never re-verified. It may already be stale.