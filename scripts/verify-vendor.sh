#!/usr/bin/env bash
# Verify that what git actually stores reproduces the vendored snapshots
# recorded in vendor/MANIFEST.lock.
#
# This checks the committed content, not the working tree, so it catches the
# two failure modes that already bit once:
#   - EOL normalisation altering bytes (fixed by .gitattributes "vendor/** -text")
#   - .gitignore silently dropping files (fixed by force-adding)
#
# No arguments. Exit 0 if every digest matches.
set -euo pipefail

readonly REPO_ROOT="/Users/whom/dvel/ghzhub/GR4-fork/gnuradio4-womm"
readonly LOCK_FILE="${REPO_ROOT}/vendor/MANIFEST.lock"
readonly REF="${1:-HEAD}"

[[ -f "${REPO_ROOT}/CLAUDE.md" ]] || { echo "FATAL: not the gnuradio4 root" >&2; exit 1; }

tmp="$(mktemp -d)"
trap 'rm -rf "${tmp}"' EXIT

fail=0
while read -r name ref sha tree_sha files bytes; do
  [[ "${name}" == \#* || -z "${name}" ]] && continue

  git -C "${REPO_ROOT}" archive "${REF}" "vendor/${name}" | tar -x -C "${tmp}"
  actual="$(cd "${tmp}/vendor/${name}" && find . -type f -print0 | sort -z \
            | xargs -0 shasum -a 256 | shasum -a 256 | cut -d' ' -f1)"
  n="$(find "${tmp}/vendor/${name}" -type f | wc -l | tr -d ' ')"

  if [[ "${actual}" == "${tree_sha}" && "${n}" == "${files}" ]]; then
    printf '  %-12s OK    %s  (%s files)\n' "${name}" "${actual:0:16}…" "${n}"
  else
    fail=1
    printf '  %-12s FAIL\n' "${name}"
    printf '               expected %s (%s files)\n' "${tree_sha}" "${files}"
    printf '               actual   %s (%s files)\n' "${actual}" "${n}"
  fi
  rm -rf "${tmp:?}/vendor"
done < "${LOCK_FILE}"

if (( fail )); then
  echo "VENDOR VERIFICATION FAILED" >&2
  exit 1
fi
echo "all vendored trees reproduce from ${REF}"
