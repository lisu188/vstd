#!/usr/bin/env bash
set -Eeuo pipefail

# Codex Web setup script for lisu188/vstd.
#
# What it does:
#   1. Installs the system packages needed by the repo on Codex's Ubuntu-based image.
#   2. Runs a C++26 smoke build without modifying checked-in source files.
#
# Optional knobs for debugging/local testing:
#   VSTD_SKIP_INSTALL=1  -> do not run apt-get
#   VSTD_SKIP_BUILD=1    -> do not run cmake build
#   CXX                 -> select a C++26 toolchain (defaults to GCC 15)
#   VSTD_BUILD_DIR      -> select a fresh build tree when changing toolchains

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT"

log() {
  printf '\n==> %s\n' "$*"
}

have() {
  command -v "$1" >/dev/null 2>&1
}

run_as_root() {
  if [ "$(id -u)" -eq 0 ]; then
    "$@"
  elif have sudo; then
    sudo "$@"
  else
    "$@"
  fi
}

install_deps() {
  if [ "${VSTD_SKIP_INSTALL:-0}" = "1" ]; then
    log "Skipping apt install because VSTD_SKIP_INSTALL=1"
    return
  fi

  if ! have apt-get; then
    echo "This script expects the Ubuntu/Debian Codex environment (apt-get not found)." >&2
    exit 1
  fi

  log "Installing build and library dependencies"
  run_as_root apt-get update
  run_as_root env DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
    build-essential \
    gcc-15 \
    g++-15 \
    ninja-build \
    pkg-config \
    python3-dev \
    libsdl2-dev \
    libboost-dev \
    libboost-system-dev \
    libboost-filesystem-dev \
    libboost-python-dev

  if ! have cmake; then
    echo "Install CMake 4.0.2 or newer (CI uses 4.3.1) before configuring vstd." >&2
    exit 1
  fi
}

smoke_build() {
  if [ "${VSTD_SKIP_BUILD:-0}" = "1" ]; then
    log "Skipping build because VSTD_SKIP_BUILD=1"
    return
  fi

  log "Running a smoke build"
  local generator=()
  if have ninja; then
    generator=(-G Ninja)
  fi

  local build_dir="${VSTD_BUILD_DIR:-build-cxx26}"
  cmake -S . -B "$build_dir" "${generator[@]}" -DCMAKE_CXX_COMPILER="${CXX:-g++-15}"
  cmake --build "$build_dir" --parallel "$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)"
}

main() {
  if [ ! -f CMakeLists.txt ] || [ ! -f vfunctional.h ]; then
    echo "Run this script from the vstd repository root." >&2
    exit 1
  fi

  install_deps
  smoke_build
  log "vstd is ready for Codex Web"
}

main "$@"
