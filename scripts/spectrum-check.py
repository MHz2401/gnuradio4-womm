#!/usr/bin/env python3
"""Find the AM sidebands in womm_rx_hold captures and report per-radio LO calibration.

The radios share a 10 MHz reference, so their SAMPLE clocks are exact and identical.
Any residual frequency offset measured here is therefore LO/synthesiser calibration
error, not clock error - the two are separable only because the radios are locked.

Three predictions, each falsifiable:
  * offsets DIFFER across radios      - independent LOs. Identical offsets would be
                                        evidence of duplicated data, not four captures.
  * offsets MATCH within a radio      - ch0 and ch1 share one LO, so they should agree
                                        with each other while differing from other radios.
  * amplitudes may differ freely      - different antennas, same frequencies.

usage: scripts/spectrum-check.py <dir> --centre 2401.1042e6 --rate 0.512e6
"""

import argparse
import glob
import os
import sys

import numpy as np

# The B2xx leaves a DC-offset spike at the tuned centre. It is an artefact of the
# direct-conversion front end, not signal, and it is usually the strongest bin in
# the capture - so exclude a guard band around it before hunting for sidebands.
DC_GUARD_HZ = 2000.0


def analyse(path, centre_hz, rate_hz, nfft):
    raw = np.fromfile(path, dtype=np.complex64)
    if raw.size == 0:
        return None
    n = min(nfft, 1 << int(np.floor(np.log2(raw.size))))
    x = raw[:n]

    # Clipping is checked on the RAW samples, not the spectrum: an overdriven front
    # end still produces a clean-looking peak, so the spectrum cannot reveal it.
    peak_iq = float(max(np.abs(x.real).max(), np.abs(x.imag).max()))

    win = np.hanning(n)
    spec = np.fft.fftshift(np.abs(np.fft.fft(x * win)))
    freqs = np.fft.fftshift(np.fft.fftfreq(n, d=1.0 / rate_hz))

    mask = np.abs(freqs) > DC_GUARD_HZ
    idx = np.where(mask)[0]
    if idx.size < 4:
        return None

    order = idx[np.argsort(spec[idx])[::-1]]
    first = order[0]

    # The second peak must be a genuinely separate feature, not the shoulder of the
    # first: skip anything within 1 kHz of the peak already found.
    second = next((i for i in order[1:] if abs(freqs[i] - freqs[first]) > 1000.0), None)
    if second is None:
        return None

    f1, f2 = sorted((float(freqs[first]), float(freqs[second])))
    noise = float(np.median(spec[idx]))
    return {
        "n": n,
        "res_hz": rate_hz / n,
        "f_lo": f1,
        "f_hi": f2,
        "sep_hz": f2 - f1,
        "mid_off_hz": (f1 + f2) / 2.0,
        "snr_db": 20.0 * np.log10(spec[first] / noise) if noise > 0 else float("inf"),
        "peak_iq": peak_iq,
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dir")
    ap.add_argument("--centre", type=float, required=True)
    ap.add_argument("--rate", type=float, required=True)
    ap.add_argument("--nfft", type=int, default=1 << 20)
    a = ap.parse_args()

    files = sorted(glob.glob(os.path.join(a.dir, "capture_*_ch*.bin")))
    if not files:
        print(f"no capture_*_ch*.bin in {a.dir}", file=sys.stderr)
        return 1

    print(f"centre {a.centre/1e6:.6f} MHz   rate {a.rate/1e6:.6f} MS/s   "
          f"DC guard +/-{DC_GUARD_HZ/1e3:.1f} kHz\n")
    print(f"{'file':<26} {'res':>7} {'f_lo':>11} {'f_hi':>11} {'sep':>10} "
          f"{'mid_off':>10} {'SNR':>7} {'pk|IQ|':>7}")
    print("-" * 96)

    rows = {}
    for f in files:
        base = os.path.basename(f)
        r = analyse(f, a.centre, a.rate, a.nfft)
        size_mb = os.path.getsize(f) / 1e6
        if r is None:
            print(f"{base:<26} {'-':>7}  no usable spectrum ({size_mb:.1f} MB)")
            continue
        rows[base] = r
        clip = "  CLIP" if r["peak_iq"] > 0.99 else ""
        print(f"{base:<26} {r['res_hz']:>6.1f}H {r['f_lo']:>+10.1f} {r['f_hi']:>+10.1f} "
              f"{r['sep_hz']:>9.1f} {r['mid_off_hz']:>+9.1f} {r['snr_db']:>6.1f}d "
              f"{r['peak_iq']:>7.3f}{clip}")

    if not rows:
        return 1

    # Group by radio serial: capture_<serial>_ch<N>.bin
    per_radio = {}
    for name, r in rows.items():
        serial = name.split("_")[1]
        per_radio.setdefault(serial, []).append(r)

    print("\n=== calibration: midpoint offset from tuned centre, per radio ===")
    for serial, rs in sorted(per_radio.items()):
        offs = [r["mid_off_hz"] for r in rs]
        spread = max(offs) - min(offs)
        print(f"  {serial:<10} " + "  ".join(f"{o:+9.1f} Hz" for o in offs)
              + f"   within-radio spread {spread:8.1f} Hz")

    means = {s: float(np.mean([r["mid_off_hz"] for r in rs])) for s, rs in per_radio.items()}
    if len(means) > 1:
        vals = list(means.values())
        print(f"\n  across radios: min {min(vals):+.1f} Hz  max {max(vals):+.1f} Hz  "
              f"spread {max(vals)-min(vals):.1f} Hz")
        print("  PREDICTION across radios: offsets DIFFER (independent LOs). "
              f"-> {'DIFFER' if max(vals)-min(vals) > 50 else 'SUSPICIOUSLY EQUAL'}")

    seps = [r["sep_hz"] for r in rows.values()]
    print(f"\n  sideband separation: min {min(seps):.1f} Hz  max {max(seps):.1f} Hz  "
          f"(expect ~20000 for a 10 kHz AM tone)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
