#!/bin/sh
# Every build's tests, on this computer (no hardware needed). Needs uv and g++.
set -eu
cd "$(dirname "$0")"
mkdir -p coverage
(cd inky-pi && uv run --quiet --no-project --with pillow --with requests --with pytest --with pytest-cov \
  python -m pytest -q --cov --cov-report=xml:../coverage/inky-pi.xml)
(cd inky-frame && uv run --quiet --no-project --with pytest --with pytest-cov \
  python -m pytest -q tests --cov --cov-report=xml:../coverage/inky-frame.xml)
serial-io/test.sh
