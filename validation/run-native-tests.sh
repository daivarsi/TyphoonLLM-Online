#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
task_tmp=$(mktemp -d)
trap 'rm -rf -- "$task_tmp"' EXIT
for name in evidence_pack reference_history source_echo source_history; do
  cc -std=c11 -Wall -Wextra "validation/${name}_test.c" -o "$task_tmp/$name"
  "$task_tmp/$name"
done
cc -std=c11 -D_POSIX_C_SOURCE=200809L -I validation/include -I vita/src validation/reply_completion_test.c vita/src/session.c -lm -o "$task_tmp/session"
"$task_tmp/session"
