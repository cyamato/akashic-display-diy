#!/bin/sh
# Every build's tests, on this computer (no hardware needed). Needs uv, g++ and Node.js.
set -eu
cd "$(dirname "$0")"
mkdir -p coverage
(cd inky-pi && uv run --quiet --no-project --with pillow --with requests --with pytest --with pytest-cov \
  python -m pytest -q --cov --cov-report=xml:../coverage/inky-pi.xml)
(cd inky-frame && uv run --quiet --no-project --with pytest --with pytest-cov \
  python -m pytest -q tests --cov --cov-report=xml:../coverage/inky-frame.xml)
serial-io/test.sh
(cd stream-deck && npm ci --no-audit --no-fund --silent && npm test && npm run build --silent && npm run validate)
