#!/usr/bin/env bash
# Build+run one or more examples in the gcc:13 container (colours OFF, width 126).
# Usage (from repo root):
#   MSYS_NO_PATHCONV=1 docker run --rm -v "/c/Users/Viktor/Desktop/vitikevich/magic_eye":/src:ro -w /src \
#     gcc:13 bash tools/eyerun.sh 02_aggregates_and_padding 05_single_inheritance
# With colours on (raw ANSI): pass EYE_COLOR=1 in the environment.
set -e
: "${EYE_WIDTH:=126}"
: "${EYE_COLOR:=0}"
export EYE_WIDTH EYE_COLOR
rc=0                          # копим статус: провал сборки → ненулевой выход
for ex in "$@"; do
  if g++ -std=c++20 -Wall -Wextra -Iinclude "examples/$ex.cpp" -o /tmp/e 2>/tmp/err; then
    echo "===== $ex ====="
    /tmp/e
  else
    echo "##### BUILD FAIL: $ex"; cat /tmp/err; rc=1
  fi
done
exit $rc                       # чтобы smoke/CI видели провал (а не ложный успех)
