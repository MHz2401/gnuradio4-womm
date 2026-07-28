#!/usr/bin/env bash
# Compare device clocks across N radios from womm_rx_hold logs.
#
# The discriminator is coarse ON PURPOSE. Radios that latched the same PPS edge
# agree to within the read skew - tens of milliseconds, set by how far apart the
# logs were sampled and by the 533 us chunk quantisation of the device clock.
# Radios that latched DIFFERENT edges are an integer second apart. Those two
# hypotheses are 1000x apart, so sampling noise cannot carry one into the other.
#
# That margin is the whole point: an earlier instrument here compared cumulative
# sample counts, where the effect was ~900 samples against ~300 000 of skew, and
# it could not tell locked from free-running. See RESULTS.md 9.7 and 9.9.
#
# usage: scripts/epoch-check.sh <logdir> [prefix]     (default prefix: ep_)

set -euo pipefail

dir="${1:?usage: epoch-check.sh <logdir> [prefix]}"
prefix="${2:-ep_}"

# No mapfile: macOS ships bash 3.2, where it does not exist.
logs=()
for f in "$dir"/"$prefix"*.txt; do
    [ -e "$f" ] && logs+=("$f")
done
[ "${#logs[@]}" -gt 0 ] || { echo "no ${prefix}*.txt in $dir"; exit 1; }

echo "=== lock status ==="
locked=()
for f in "${logs[@]}"; do
    serial=$(basename "$f" .txt); serial=${serial#"$prefix"}
    n=$(grep -c 'device_time ' "$f" || true)
    if grep -q 'free-running' "$f"; then
        printf '  %-10s REF NOT LOCKED\n' "$serial"
    elif [ "$n" -eq 0 ]; then
        printf '  %-10s no device timestamps (HAS_TIME absent, or never streamed)\n' "$serial"
    else
        printf '  %-10s locked, %s device timestamps\n' "$serial" "$n"
        locked+=("$f")
    fi
done

[ "${#locked[@]}" -ge 2 ] || { echo; echo "need >=2 locked radios to compare; have ${#locked[@]}"; exit 0; }

ref="${locked[0]}"
refname=$(basename "$ref" .txt); refname=${refname#"$prefix"}
echo
echo "=== epoch offsets against $refname ==="
echo "    |offset| << 1 s  -> same PPS edge, shared epoch"
echo "    |offset| ~= 1 s  -> different edges"
echo

worst=0
for f in "${locked[@]:1}"; do
    name=$(basename "$f" .txt); name=${name#"$prefix"}
    line=$(paste <(grep 'device_time ' "$ref" | awk '{print $2}') \
                 <(grep 'device_time ' "$f"   | awk '{print $2}') \
           | awk '$1 && $2 {d=$1-$2; n++; s+=d; if(mn==""||d<mn)mn=d; if(mx==""||d>mx)mx=d}
                  END{ if(n) printf "%d %.6f %.6f %.6f", n, s/n, mn, mx }')
    read -r n mean mn mx <<<"$line"
    verdict=$(awk -v m="$mean" 'BEGIN{ a=(m<0?-m:m); print (a<0.5)?"SAME EDGE":"DIFFERENT EDGE" }')
    printf '  %-10s n=%-4s mean %+0.6f s   min %+0.6f   max %+0.6f   %s\n' \
        "$name" "$n" "$mean" "$mn" "$mx" "$verdict"
    worst=$(awk -v w="$worst" -v m="$mean" 'BEGIN{a=(m<0?-m:m); print (a>w)?a:w}')
done

echo
awk -v w="$worst" -v k="${#locked[@]}" 'BEGIN{
    printf "  %d radios compared, worst |offset| %.3f ms -> %s\n", k, w*1000,
        (w<0.5) ? "ALL ON A SHARED EPOCH" : "NOT ALL ON ONE EDGE" }'
