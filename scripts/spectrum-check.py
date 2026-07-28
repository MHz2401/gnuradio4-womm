#!/usr/bin/env python3
"""Detect the transmitted picket comb and report per-radio LO calibration.

SIGNAL: a complex sum spaced 40 kHz apart about f_C, amplitude rolling off toward
zero by f_C +/- 250 kHz. So ~13 lines across a 500 kHz span - which is why the
capture runs at 0.512 MS/s (+/-256 kHz): it just covers the comb.

CALIBRATION IS MEASURED BY FITTING, NOT BY PICKING A PEAK. Pickets sit at
f = spacing*n + offset for integer n, so a least-squares line through (n, f) gives
spacing from the slope and the LO offset from the intercept, using every picket at
once. That matters here because the B2xx leaves a DC-offset artefact at exactly
f_C - precisely where the tallest picket also lands. A fit over the outer pickets
is immune to one contaminated line; "find the tallest peak" is not.

The radios share a 10 MHz reference, so their sample clocks are exact and
identical. Any residual offset measured here is therefore LO/synthesiser
calibration error, not clock error - separable only because the radios are locked.

Predictions, each falsifiable:
  * spacing ~= 40 kHz on every channel   - it is the transmitted signal, not noise
  * offsets MATCH within a radio         - ch0/ch1 share one LO
  * offsets DIFFER across radios         - independent LOs. Equal offsets across
                                           radios would suggest duplicated data.

usage: scripts/spectrum-check.py <dir> --centre 2401e6 --rate 0.512e6 [--spacing 40e3]
"""

import argparse
import glob
import os
import sys

import numpy as np

DC_GUARD_HZ = 300.0  # the DC artefact is ~1 bin at 1 Hz resolution; keep this tight


def find_pickets(spec, freqs, spacing_hz, snr_floor_db=20.0):
    """Local maxima well above the noise floor, thinned to one per half-spacing."""
    noise = np.median(spec)
    if noise <= 0:
        return np.array([], dtype=int)
    thresh = noise * (10.0 ** (snr_floor_db / 20.0))

    cand = np.where((spec[1:-1] > spec[:-2]) & (spec[1:-1] >= spec[2:]) & (spec[1:-1] > thresh))[0] + 1
    cand = cand[np.abs(freqs[cand]) > DC_GUARD_HZ]
    if cand.size == 0:
        return cand

    # One peak per half-spacing window: keep the tallest, drop its shoulders.
    keep, order = [], cand[np.argsort(spec[cand])[::-1]]
    for i in order:
        if all(abs(freqs[i] - freqs[j]) > spacing_hz * 0.5 for j in keep):
            keep.append(i)
    return np.array(sorted(keep))


def analyse(path, rate_hz, nfft, spacing_hz):
    raw = np.fromfile(path, dtype=np.complex64)
    if raw.size == 0:
        return None
    n = min(nfft, 1 << int(np.floor(np.log2(raw.size))))
    x = raw[:n]

    # Clipping is judged on RAW samples: an overdriven front end still produces a
    # clean-looking spectrum, so the spectrum cannot reveal it.
    peak_iq = float(max(np.abs(x.real).max(), np.abs(x.imag).max()))

    spec = np.fft.fftshift(np.abs(np.fft.fft(x * np.hanning(n))))
    freqs = np.fft.fftshift(np.fft.fftfreq(n, d=1.0 / rate_hz))

    pk = find_pickets(spec, freqs, spacing_hz)
    out = {"n": n, "res_hz": rate_hz / n, "peak_iq": peak_iq,
           "n_pickets": int(pk.size), "noise": float(np.median(spec))}
    if pk.size < 3:
        return out

    f = freqs[pk]
    idx = np.round((f - f[np.argmax(spec[pk])]) / spacing_hz)  # picket index about the tallest
    A = np.vstack([idx, np.ones_like(idx)]).T
    (slope, intercept), *_ = np.linalg.lstsq(A, f, rcond=None)
    resid = f - (slope * idx + intercept)

    # A COMB MUST FIT. The thinning above keeps peaks >= half a spacing apart, so
    # indexing them against the expected spacing yields a slope near it even for
    # pure noise - the detector would otherwise manufacture the answer it is looking
    # for. Residual is what separates a real comb (a few Hz) from noise that merely
    # got binned into a plausible-looking ladder (thousands of Hz). Measured on a
    # silent capture: fit rms 11 kHz, 27% of spacing.
    fit_rms = float(np.sqrt(np.mean(resid ** 2)))
    is_comb = (pk.size >= 5
               and fit_rms < 0.05 * spacing_hz
               and abs(slope - spacing_hz) < 0.20 * spacing_hz)

    out.update({
        "is_comb": is_comb,
        "spacing_hz": float(slope),
        "offset_hz": float(intercept),
        "fit_rms_hz": fit_rms,
        "span_khz": float((f.max() - f.min()) / 1e3),
        "snr_db": float(20.0 * np.log10(spec[pk].max() / out["noise"])),
    })
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dir")
    ap.add_argument("--centre", type=float, required=True)
    ap.add_argument("--rate", type=float, required=True)
    ap.add_argument("--spacing", type=float, default=40e3)
    ap.add_argument("--nfft", type=int, default=1 << 20)
    a = ap.parse_args()

    files = sorted(glob.glob(os.path.join(a.dir, "capture_*_ch*.bin")))
    if not files:
        print(f"no capture_*_ch*.bin in {a.dir}", file=sys.stderr)
        return 1

    print(f"centre {a.centre/1e6:.6f} MHz   rate {a.rate/1e6:.6f} MS/s   "
          f"expect {a.spacing/1e3:.0f} kHz picket spacing\n")
    print(f"{'file':<26} {'res':>6} {'pk':>3} {'spacing':>10} {'LO offset':>11} "
          f"{'fit rms':>8} {'SNR':>7} {'pk|IQ|':>7}  verdict")
    print("-" * 104)

    rows = {}
    for path in files:
        base = os.path.basename(path)
        r = analyse(path, a.rate, a.nfft, a.spacing)
        if r is None:
            print(f"{base:<26}  empty file")
            continue
        if "spacing_hz" not in r:
            print(f"{base:<26} {r['res_hz']:>5.1f}H {r['n_pickets']:>3}  "
                  f"too few pickets - no comb detected (peak|IQ| {r['peak_iq']:.3f})")
            continue
        clip = " CLIP" if r["peak_iq"] > 0.99 else ""
        verdict = "COMB" if r["is_comb"] else "no comb (fit too poor)"
        if r["is_comb"]:
            rows[base] = r
        print(f"{base:<26} {r['res_hz']:>5.1f}H {r['n_pickets']:>3} {r['spacing_hz']:>9.1f} "
              f"{r['offset_hz']:>+10.1f} {r['fit_rms_hz']:>7.1f} "
              f"{r['snr_db']:>6.1f}d {r['peak_iq']:>7.3f}{clip}  {verdict}")

    if not rows:
        print("\nNo comb detected in any channel. Nothing transmitting, or the")
        print("signal is outside the captured span / below the noise floor.")
        return 1

    per_radio = {}
    for name, r in rows.items():
        per_radio.setdefault(name.split("_")[1], []).append(r)

    print("\n=== LO calibration offset, per radio ===")
    for serial, rs in sorted(per_radio.items()):
        offs = [r["offset_hz"] for r in rs]
        print(f"  {serial:<10} " + "  ".join(f"{o:+10.1f} Hz" for o in offs)
              + f"   within-radio spread {max(offs)-min(offs):9.1f} Hz"
              + ("   <- ch0/ch1 agree" if max(offs) - min(offs) < 100 else "   <- ch0/ch1 DISAGREE"))

    means = {s: float(np.mean([r["offset_hz"] for r in rs])) for s, rs in per_radio.items()}
    if len(means) > 1:
        v = list(means.values())
        spread = max(v) - min(v)
        print(f"\n  across radios: spread {spread:.1f} Hz -> "
              f"{'DIFFER (independent LOs)' if spread > 50 else 'SUSPICIOUSLY EQUAL - check for duplicated data'}")

    sp = [r["spacing_hz"] for r in rows.values()]
    print(f"  picket spacing: min {min(sp):.1f}  max {max(sp):.1f} Hz "
          f"(expect {a.spacing:.0f})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
