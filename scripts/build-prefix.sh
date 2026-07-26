#!/usr/bin/env bash
# Build every vendored dependency into the isolated prefix.
#
# Nothing is written outside ${PREFIX} and ${BUILD_ROOT}. No Homebrew formula is
# installed, linked, or upgraded. Knobs are constants below; no argument parsing.
set -euo pipefail

readonly REPO_ROOT="/Users/whom/dvel/ghzhub/GR4-fork/gnuradio4-womm"
readonly PREFIX="${REPO_ROOT}/.womm-prefix"
readonly BUILD_ROOT="${REPO_ROOT}/.womm-prefix/_build"
readonly SRC_ROOT="${REPO_ROOT}/.womm-prefix/_src"
readonly VENDOR="${REPO_ROOT}/vendor"

# Apple clang 21 is tried first: it installs nothing and leaves no footprint.
# Fallback if the reflection static_asserts or -Werror fail is Homebrew llvm@20,
# which is what upstream macOS CI uses and is keg-only so shadows nothing.
readonly CC_BIN="/usr/bin/clang"
readonly CXX_BIN="/usr/bin/clang++"

# 192 GiB and 24 cores. CLAUDE.md:433 mandates -j6, calibrated for a 16-32 GiB
# laptop; overridden per DRIFT.md D-1. These are small C/C++ deps, not the
# template-heavy gr4 TUs, so the stated OOM failure mode does not apply.
readonly JOBS=16

readonly UHD_HINT="/opt/homebrew"   # UHD 4.10.0.0, read-only; never modified

[[ -f "${REPO_ROOT}/CLAUDE.md" ]] || { echo "FATAL: not the gnuradio4 root" >&2; exit 1; }
mkdir -p "${PREFIX}" "${BUILD_ROOT}" "${SRC_ROOT}"

# Stage a pristine vendored tree into _src and apply our patch series, so
# vendor/ stays byte-exact for scripts/verify-vendor.sh. Distro model:
# untouched upstream source + an explicit, reviewable patch series.
stage_patched() {        # name
  local name="$1" src="${SRC_ROOT}/$1"
  rm -rf "${src}"; mkdir -p "${SRC_ROOT}"; cp -R "${VENDOR}/${name}" "${src}"
  local p
  for p in "${REPO_ROOT}"/patches/womm/"${name}"-*.patch; do
    [[ -e "${p}" ]] || continue
    patch -s -p1 -d "${src}" < "${p}" || { echo "PATCH FAILED: ${p}" >&2; exit 1; }
    echo "  applied $(basename "${p}")" >&2   # stdout is the path, keep it clean
  done
  printf '%s' "${src}"
}

cmake_build() {          # name  source_dir  extra_cmake_args...
  local name="$1" src="$2"; shift 2
  printf '\n========== %s ==========\n' "${name}"
  cmake -S "${src}" -B "${BUILD_ROOT}/${name}" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="${PREFIX}" \
    -DCMAKE_PREFIX_PATH="${PREFIX};${UHD_HINT}" \
    -DCMAKE_C_COMPILER="${CC_BIN}" \
    -DCMAKE_CXX_COMPILER="${CXX_BIN}" \
    -DCMAKE_INSTALL_RPATH="${PREFIX}/lib" \
    -DCMAKE_BUILD_WITH_INSTALL_RPATH=ON \
    "$@" > "${BUILD_ROOT}/${name}.configure.log" 2>&1 \
    || { echo "CONFIGURE FAILED: ${name}"; tail -25 "${BUILD_ROOT}/${name}.configure.log"; exit 1; }
  cmake --build "${BUILD_ROOT}/${name}" -j "${JOBS}" --target install \
    > "${BUILD_ROOT}/${name}.build.log" 2>&1 \
    || { echo "BUILD FAILED: ${name}"; tail -25 "${BUILD_ROOT}/${name}.build.log"; exit 1; }
  echo "  installed -> ${PREFIX}"
}

# --- header-only: plain copies, no build system involved --------------------
printf '\n========== header-only deps ==========\n'
mkdir -p "${PREFIX}/include/boost" "${PREFIX}/include/vir"
cp "${VENDOR}/boost-ut/include/boost/ut.hpp" "${PREFIX}/include/boost/"
cp -R "${VENDOR}/vir-simd/vir/." "${PREFIX}/include/vir/"
echo "  boost/ut.hpp, vir/ -> ${PREFIX}/include"

# --- cpp-httplib: use its own CMake so find_package(httplib CONFIG) works ---
cmake_build cpp-httplib "${VENDOR}/cpp-httplib" \
  -DHTTPLIB_REQUIRE_OPENSSL=OFF -DHTTPLIB_REQUIRE_ZLIB=OFF \
  -DHTTPLIB_REQUIRE_BROTLI=OFF -DHTTPLIB_TEST=OFF -DHTTPLIB_COMPILE=OFF

# --- libsoundio: our rules, CoreAudio backend only -------------------------
cmake_build libsoundio "${REPO_ROOT}/vendor-build/libsoundio"

# --- cpr: wraps the SDK's libcurl ------------------------------------------
cmake_build cpr "${VENDOR}/cpr" \
  -DCPR_USE_SYSTEM_CURL=ON -DCPR_BUILD_TESTS=OFF -DCPR_ENABLE_SSL=ON \
  -DBUILD_SHARED_LIBS=ON

# --- SoapySDR: core only; no python/swig bindings, we do not use them -------
cmake_build SoapySDR "${VENDOR}/SoapySDR" \
  -DENABLE_PYTHON=OFF -DENABLE_PYTHON3=OFF -DENABLE_TESTS=OFF -DENABLE_DOCS=OFF

# --- SoapyUHD: the only route to the B210s ---------------------------------
# Needs two patches against UHD 4.10 / Boost 1.90; see patches/womm/ and DRIFT.md
printf '\n========== SoapyUHD (patched) ==========\n'
cmake_build SoapyUHD "$(stage_patched SoapyUHD)" -DUHD_DIR="${UHD_HINT}"

printf '\n========== installed ==========\n'
find "${PREFIX}" -maxdepth 2 -not -path '*_build*' -type d | sed "s|${PREFIX}|  \$PREFIX|"
