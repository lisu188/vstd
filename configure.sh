#!/usr/bin/env bash
set -Eeuo pipefail

# Codex Web setup script for lisu188/vstd.
#
# What it does:
#   1. Installs the system packages needed by the repo on Codex's Ubuntu-based image.
#   2. Applies two compatibility fixes required by the current upstream sources:
#        - vfunctional.h uses std::unordered_map without including <unordered_map>
#        - CMakeLists.txt enforces C++23 so the project uses the newest standard supported by the Codex toolchain
#   3. Runs a smoke build so the environment cache already contains a verified build tree.
#
# Optional knobs for debugging/local testing:
#   VSTD_SKIP_INSTALL=1  -> do not run apt-get
#   VSTD_SKIP_BUILD=1    -> do not run cmake build

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
    cmake \
    ninja-build \
    pkg-config \
    python3-dev \
    libsdl2-dev \
    libboost-dev \
    libboost-system-dev \
    libboost-filesystem-dev \
    libboost-python-dev
}

patch_repo() {
  log "Applying compatibility patches for modern Codex toolchains"
  python3 <<'PY'
from pathlib import Path
import re

root = Path('.')

vf = root / 'vfunctional.h'
text = vf.read_text()
if '#include <unordered_map>' not in text:
    if '#pragma once' in text:
        text = text.replace('#pragma once', '#pragma once\n#include <unordered_map>', 1)
    else:
        text = '#include <unordered_map>\n' + text
    vf.write_text(text)

cm = root / 'CMakeLists.txt'
text = cm.read_text()
orig = text

text = re.sub(
    r'cmake_minimum_required\(VERSION\s+[0-9.]+\)',
    'cmake_minimum_required(VERSION 3.16)',
    text,
    count=1,
    flags=re.IGNORECASE,
)
text = re.sub(r'project\(\s*vstd\s*\)', 'project(vstd LANGUAGES CXX)', text, count=1)

policy_block = 'if(POLICY CMP0167)\n  cmake_policy(SET CMP0167 NEW)\nendif()'
if 'CMP0167' not in text:
    text = text.replace('project(vstd LANGUAGES CXX)', f'project(vstd LANGUAGES CXX)\n\n{policy_block}', 1)

std_block = 'set(CMAKE_CXX_STANDARD 23)\nset(CMAKE_CXX_STANDARD_REQUIRED ON)\nset(CMAKE_CXX_EXTENSIONS OFF)'
if 'CMAKE_CXX_STANDARD' not in text:
    text = re.sub(
        r'set\(CMAKE_CXX_FLAGS\s+"\$\{CMAKE_CXX_FLAGS\}\s*-std=c\+\+14"\)',
        std_block,
        text,
        count=1,
    )
    if 'CMAKE_CXX_STANDARD' not in text:
        text = text.replace(policy_block, policy_block + '\n\n' + std_block, 1)

text = re.sub(
    r'find_package\(Boost\s+1\.58\s+COMPONENTS\s+system\s+REQUIRED\)',
    'find_package(Boost 1.58 REQUIRED COMPONENTS system filesystem)',
    text,
    count=1,
    flags=re.IGNORECASE,
)

if 'find_package(Threads REQUIRED)' not in text:
    anchor = 'find_package(Boost 1.58 REQUIRED COMPONENTS system filesystem)'
    if anchor in text:
        text = text.replace(anchor, anchor + '\nfind_package(Threads REQUIRED)', 1)

text = re.sub(
    r'target_link_libraries\(\s*vstd\s+\$\{Boost_LIBRARIES\}\s*\)',
    'target_link_libraries(vstd PRIVATE Boost::system Boost::filesystem Threads::Threads)',
    text,
    count=1,
    flags=re.IGNORECASE,
)

if text != orig:
    cm.write_text(text)
PY
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

  cmake -S . -B build "${generator[@]}"
  cmake --build build --parallel "$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)"
}

main() {
  if [ ! -f CMakeLists.txt ] || [ ! -f vfunctional.h ]; then
    echo "Run this script from the vstd repository root." >&2
    exit 1
  fi

  install_deps
  patch_repo
  smoke_build
  log "vstd is ready for Codex Web"
}

main "$@"
