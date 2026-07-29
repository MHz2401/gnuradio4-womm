# UI OPTIONS — research feeding D10 ("Usable UI")

Research only. **"Usable UI" is still undefined** (`BUILD_JOURNAL.md` D10); this document lists what
exists so the definition can be written against reality rather than hope. Surveyed 2026-07-28
against the three trees named in `HANDOFF.md`.

Status uses the owner's ladder — *proposed but not…*, *designed but not…*, *implemented but not…*,
*running but not…*, *working but not…* — with **Mac compatible** as a separate add-on, because it
cuts across all five.

---

## Summary

| option | tree | status | Mac | licence |
|---|---|---|---|---|
| **In-tree `Drawable`** | (A)/(C) | **implemented but barely populated** — the hook exists; one consumer, and it draws to a console | **yes** (builds here today) | MIT |
| **`gnuradio4-studio` + `-control-plane`** | (C) | **running but self-described "active prototype"** | **unknown, plausible** — see below | GPL-3.0 / MIT |
| **`opendigitizer`** | (B) | **working but not ours** — FAIR's digitizer platform, not a gr4 UI | **no** | LGPL-3.0 |

---

## 1. In-tree `Drawable` — (A) and (C), already in our tree

The block model carries UI machinery: `Drawable<UICategory, "renderer">` appears in `Block.hpp`,
`BlockModel.hpp` and `annotated.hpp`. A block declares itself drawable and names a renderer.

**Status: implemented but barely populated.** Tree-wide, **exactly one block uses it** —
`ImChartMonitor` (`blocks/testing/…/ImChartMonitor.hpp:23`), declaring
`Drawable<UICategory::Content, "console">`. The renderer is a **console/ASCII chart**
(`algorithm/…/ImChart.hpp`), not a GUI.

- **Mac compatible: yes.** It is in our tree and builds clean here today.
- **Distance to "Usable UI": large.** The seam is real and the block-side contract exists; there is
  no window, no interaction, no layout. Everything above the seam would be ours to write.
- **Value regardless of choice:** it is the mechanism by which *any* gr4 UI learns what a block
  wants to draw. Worth understanding before adopting anything else.

## 2. `gnuradio4-studio` + `gnuradio4-control-plane` — (C), the gnuradio.org camp

Two repos, and the split matters:

**`gnuradio4-control-plane`** (MIT, 24 commits): a **C++ REST service** for session lifecycle —
create, inspect, start, stop, restart, delete — plus **live block-settings updates on running
sessions** and a read-only block catalogue. Requires a GNU Radio 4 installation with CMake package
files; validates plugin loading at startup.

**`gnuradio4-studio`** (GPL-3.0-or-later; its `blocks/` MIT, 65 commits): browser **and** desktop
Studio. Self-described **"an active prototype"**. Implemented today: graph editor with block
browsing/placement, run/stop/restart/link/delete of sessions, application layouts with live panels
and controls, visualisation panels (time series, XY, power spectra, **waterfall**, image, audio),
adjustable running-block parameters, `.gr4s` save/open, and first-party studio blocks.

Deliberate limitation, quoted: *graph edits are local until Run submits a snapshot, and running
sessions can drift from the editor state after further edits.*

**Status: running but a prototype, and dependent on a backend.** Needs a control plane at
`http://localhost:8080`, Node.js/npm to build, `GR4_PREFIX_PATH` for the desktop launcher.

- **Mac compatible: unknown, and the risk is not where it looks.** The web/desktop front end is
  Node plus a browser — unremarkable on macOS. **The Mac question is the C++ control plane**, which
  needs a GR4 install with CMake package files. We have a working GR4 install; nobody has tried.
  Neither repo advertises macOS CI.
- **★ The architecture is the point.** Studio is a *separate process* talking REST. That means two
  things the owner should weigh:
  1. **GPL-3.0 does not propagate to our MIT core.** Studio is a client over a network protocol, not
     a linked library. Only the control plane links gr4, and it is MIT.
  2. **It structurally cannot reproduce "The Abomination".** The failure that motivated this whole
     project — a GUI window resize stalling a radio — is impossible across a process boundary. This
     is upstream's architectural answer to the exact problem, and it is the reason to take it
     seriously even as a prototype.

## 3. `opendigitizer` — (B), FAIR

**Not a gr4 UI.** It is FAIR's digitizer platform — *"modernising FAIR's time- and frequency-domain
digitizer infrastructure"* for accelerator diagnostics and fault identification. GNU Radio 4 is a
component inside it, alongside **OpenCMW**. 434 commits, active, LGPL-3.0. UI is **ImGui** natively
plus **WebAssembly/Emscripten** in the browser.

**Status: working but not ours.** Mature relative to the others, and aimed at a different problem.

- **Mac compatible: no.** It depends on `fair-acc/gnuradio4`, which reverted macOS ARM64 support
  (`ac59533`). No macOS mention; Linux is the implied target.
- **Wrong camp under D9.** LGPL-3.0, and D9 puts us with gnuradio.org/MIT.
- Adopting it means adopting OpenCMW and FAIR's accelerator-shaped assumptions as well.

---

## Recommendations

**Anti-recommend `opendigitizer`.** Wrong camp (LGPL vs D9's MIT), wrong platform (built on the tree
that deleted macOS ARM64), and it carries OpenCMW plus a domain model aimed at accelerator
diagnostics. Worth *reading* for its ImGui/WASM approach; not worth adopting.

**Recommend a scoped evaluation of `-control-plane` first, and only then `-studio`.** In that order,
because the control plane is the load-bearing part and the cheap test:

1. It is **MIT**, in our camp, and small (24 commits) — auditable in an afternoon.
2. It is the **only unknown that matters**: if the C++ control plane does not build and run against
   our macOS GR4 install, Studio is moot. If it does, Studio is a Node app pointed at a URL.
3. It independently answers the session's founding question — a separate-process control plane with
   live settings updates is *the* structural answer to GUI/radio coupling, whether or not we ever
   run Studio.

**Note the in-tree `Drawable` seam as a fallback, not a plan.** It is Mac-native and already builds,
but one console-charting block is a very long way from usable. It becomes interesting only if the
control-plane evaluation fails.

### What this does not decide

**Nothing here defines "Usable UI"** — that is still D10 and still owed. This document exists so the
definition can be written against three concrete options rather than in the abstract. The obvious
next question, and the one worth the owner's time: *is a Usable UI something that runs the radios,
or something that watches them?* Studio does both, and the two have very different acceptance bars.
