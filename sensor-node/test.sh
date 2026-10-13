#!/bin/sh
# Build and run the sensor node's core tests on this computer (no board needed).
set -eu
cd "$(dirname "$0")"
out="$(mktemp -d)"
trap 'rm -rf "$out"' EXIT
# With the address and undefined-behaviour sanitizers: the parsers read untrusted input.
g++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=all -o "$out/core" tests/core_test.cpp lib/core/src/*.cpp
"$out/core"
