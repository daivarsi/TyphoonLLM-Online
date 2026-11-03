#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
: "${VITASDK:?Set VITASDK to the existing VitaSDK path}"
export PATH="$VITASDK/bin:$PATH"
cmake -S vita -B build-vita -DCMAKE_BUILD_TYPE=Release
cmake --build build-vita -j4
