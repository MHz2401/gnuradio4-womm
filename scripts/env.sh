# Activate the isolated womm toolchain prefix. Source this; do not execute it.
#
#   source scripts/env.sh
#
# This is the ONLY thing that activates the prefix. Nothing is written to
# ~/.zshrc, ~/.zprofile, /etc/paths or any login shell config, and no Homebrew
# formula is linked. Deactivate by starting a new shell.

WOMM_ROOT="/Users/whom/dvel/ghzhub/GR4-fork/gnuradio4-womm"
WOMM_PREFIX="${WOMM_ROOT}/.womm-prefix"

if [ ! -f "${WOMM_ROOT}/CLAUDE.md" ]; then
  echo "env.sh: ${WOMM_ROOT} is not the gnuradio4 root" >&2
  return 1 2>/dev/null || exit 1
fi

export WOMM_ROOT WOMM_PREFIX
export CMAKE_PREFIX_PATH="${WOMM_PREFIX}${CMAKE_PREFIX_PATH:+:${CMAKE_PREFIX_PATH}}"
export PKG_CONFIG_PATH="${WOMM_PREFIX}/lib/pkgconfig${PKG_CONFIG_PATH:+:${PKG_CONFIG_PATH}}"
export DYLD_FALLBACK_LIBRARY_PATH="${WOMM_PREFIX}/lib${DYLD_FALLBACK_LIBRARY_PATH:+:${DYLD_FALLBACK_LIBRARY_PATH}}"

# SoapySDR loads driver modules from here at runtime
export SOAPY_SDR_PLUGIN_PATH="${WOMM_PREFIX}/lib/SoapySDR/modules0.8-3"

# Only the prefix's own bin is prepended. No compiler, no cmake, no ninja is
# shadowed - those continue to resolve exactly as they did before activation,
# which is what the GATE 2 isolation proof checks.
case ":${PATH}:" in
  *":${WOMM_PREFIX}/bin:"*) ;;
  *) export PATH="${WOMM_PREFIX}/bin:${PATH}" ;;
esac

echo "womm prefix active: ${WOMM_PREFIX}"
