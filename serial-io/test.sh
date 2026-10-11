#!/bin/sh
# Build and run the serial I/O tests on this computer (no board needed).
set -eu
cd "$(dirname "$0")"
out="$(mktemp -d)"
trap 'rm -rf "$out"' EXIT
g++ -std=c++17 -Wall -Wextra -Werror -o "$out/protocol" tests/protocol_test.cpp
"$out/protocol"
g++ -std=c++17 -Wall -Wextra -Werror -I tests/fake -x c++ -o "$out/sketch" tests/sketch_test.cpp
"$out/sketch"
