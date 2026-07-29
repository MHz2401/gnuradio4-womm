#!/usr/bin/env bash
# womm-scan - capture IQ from every available B210 and optionally analyse it.
#
# RECEIVE ONLY. The binary this drives links no transmit path; assert_no_tx.cmake
# asserts that on the linked binary itself, because a comment cannot guarantee it.
#
# NO SERIALS IN THIS FILE. Radios are named by the environment variables B210U00..
# B210U03, which live in the operator's shell and not in the repository. Serials,
# absolute paths and site details are semi-secrets and stay out of version control.
#
# usage: scripts/womm-scan.sh -o DIR [-f HZ] [-t SEC] [-r HZ] [-g dB] [-a]

set -euo pipefail

FREQ=2401.1042e6
RATE=0.5e6
TIME=0.2
GAIN=20
OUT=""
ANALYSE=0
FAILED=0

usage() {
    sed -n '2,12p' "$0" | sed 's/^# \{0,1\}//'
    cat <<'USAGE'

  -o DIR   output directory. REQUIRED, and must already exist.
  -f HZ    centre frequency        (default 2401.1042e6)
  -r HZ    sample rate = bandwidth (default 0.5e6)
  -t SEC   capture duration, clipped to [0.001, 1.0]   (default 0.2)
  -g dB    RX gain                 (default 20)
  -a       also run the spectrum analysis (otherwise capture only)
  -h       this help
USAGE
    exit "${1:-0}"
}

while getopts ":f:r:t:g:o:ah" opt; do
    case "$opt" in
        f) FREQ="$OPTARG" ;;
        r) RATE="$OPTARG" ;;
        t) TIME="$OPTARG" ;;
        g) GAIN="$OPTARG" ;;
        o) OUT="$OPTARG" ;;
        a) ANALYSE=1 ;;
        h) usage 0 ;;
        *) echo "unknown option -$OPTARG" >&2; usage 2 ;;
    esac
done

[ -n "$OUT" ] || { echo "ERROR: -o DIR is required" >&2; usage 2; }
[ -d "$OUT" ] || { echo "ERROR: output directory does not exist: $OUT" >&2; exit 2; }

# Clip rather than reject: the bounds exist because below ~1 ms there is less than one
# chunk to return, and above 1 s the volume grows without telling you anything new.
TIME=$(python3 -c "print(f'{min(1.0, max(0.001, float(\"$TIME\"))):.6f}')")

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="$ROOT/build-fixed/blocks/sdr/src/womm_rx_hold"
[ -x "$BIN" ] || { echo "ERROR: not built: $BIN" >&2; exit 1; }

# shellcheck disable=SC1091
source "$ROOT/scripts/env.sh" >/dev/null 2>&1
export SOAPY_SDR_PLUGIN_PATH="$ROOT/build-fixed/blocks/sdr"

# Named radios, intersected with what is actually present, so a disconnected or
# powered-down unit drops out silently instead of failing the run.
VISIBLE=$(SoapySDRUtil --find="driver=uhd" 2>/dev/null | sed -n 's/.*serial = //p' | tr -d ' ')
WANT=""
for v in "${B210U00:-}" "${B210U01:-}" "${B210U02:-}" "${B210U03:-}"; do
    [ -n "$v" ] || continue
    if printf '%s\n' "$VISIBLE" | grep -qx "$v"; then
        WANT="$WANT $v"
    else
        echo "  note: \$B210Uxx names $v, which is not present - skipping"
    fi
done
# No named radios configured at all: fall back to everything visible, so the demo
# still runs on a machine where the operator has not set the variables.
if [ -z "$WANT" ]; then
    echo "  note: no B210U00..03 set (or none matched); using all visible radios"
    WANT=$(printf '%s\n' "$VISIBLE" | tr '\n' ' ')
fi
[ -n "$(echo "$WANT" | tr -d ' ')" ] || { echo "ERROR: no radios found" >&2; exit 1; }

FC_MHZ=$(python3 -c "print(f'{float(\"$FREQ\")/1e6:.4f}')")
MS=$(python3 -c "print(f'{float(\"$TIME\")*1000:.0f}')")
STAMP=$(date +%Y%m%dT%H%M%S)
NCH=2
SUFFIX="_fc${FC_MHZ}MHz_${NCH}ch_${MS}ms_${STAMP}"

echo "womm-scan  centre ${FC_MHZ} MHz   rate/BW $(python3 -c "print(f'{float(\"$RATE\")/1e6:.4f}')") MHz   ${TIME}s   gain ${GAIN} dB"
echo "  radios:$WANT"
echo "  out:    $OUT"
echo

for s in $WANT; do
    # Collect first, print after. Piping straight to the terminal leaves a dangling
    # label when a radio dies without emitting a matching line, and reports silence as
    # though it were success.
    out=$(WOMM_SERIAL="$s" WOMM_FREQ="$FREQ" WOMM_RATE="$RATE" WOMM_GAIN="$GAIN" \
          WOMM_MCR=30e6 WOMM_EXTCLK="${WOMM_EXTCLK:-1}" \
          WOMM_CAPTURE_SEC="$TIME" WOMM_CAPTURE_DIR="$OUT" WOMM_CAPTURE_SUFFIX="$SUFFIX" \
          WOMM_MAX_SEC=120 "$BIN" </dev/null 2>&1 \
          | grep -E "verdict|free-running|SHORT CAPTURE" || true)
    if [ -z "$out" ]; then
        printf '  %-10s NO RESULT - radio produced no verdict (crash, or no samples)\n' "$s"
        FAILED=1
    else
        printf '  %-10s %s\n' "$s" "$(echo "$out" | head -3 | tr '\n' ' ')"
    fi
done

echo
ls -l "$OUT"/capture_*"$SUFFIX".bin 2>/dev/null \
  | awk '{printf "  %s  %.2f MB\n", $NF, $5/1e6}' || echo "  (no files written)"

[ "$FAILED" -eq 0 ] || echo "  ⚠ at least one radio produced no result"

if [ "$ANALYSE" -eq 1 ]; then
    echo
    "$ROOT/scripts/spectrum-check.py" "$OUT" --centre "$FREQ" --rate "$RATE"
fi
