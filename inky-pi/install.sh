#!/usr/bin/env bash
# Install the Akashic Display agent on a Raspberry Pi (Zero 2 W or better) with an Inky
# Impression, on Raspberry Pi OS (Bookworm or later). Run from this directory:
#   sudo ./install.sh live      # plugged in: always on, light bar, instant changes
#   sudo ./install.sh battery   # Witty Pi + battery: draw at each wake, then power off
set -euo pipefail

MODE="${1:-live}"
if [[ "$MODE" != "live" && "$MODE" != "battery" ]]; then
  echo "usage: sudo $0 live|battery" >&2
  exit 2
fi
if [[ $EUID -ne 0 ]]; then
  echo "run with sudo" >&2
  exit 1
fi

CONFIG=/boot/firmware/config.txt
echo "==> Enabling SPI for the Inky, at the slower clock the Pi Zero 2 W needs"
raspi-config nonint do_spi 0
# The Inky drives its own chip select; 2 MHz keeps the Zero 2 W's SPI reliable over the
# header (see the Pimoroni forum's Inky Impression 7.3" threads).
if ! grep -q '^dtoverlay=spi0-0cs' "$CONFIG"; then
  echo 'dtoverlay=spi0-0cs,clockfreq=2000000' >> "$CONFIG"
fi

echo "==> Installing packages"
apt-get update
apt-get install -y --no-install-recommends python3-venv python3-dev fonts-dejavu-core

echo "==> Installing the agent in /opt/pdinky"
python3 -m venv /opt/pdinky
/opt/pdinky/bin/pip install --upgrade pip
/opt/pdinky/bin/pip install ".[pi]"

install -d -m 0755 /etc/akashic-display
if [[ ! -f /etc/akashic-display/inky.toml ]]; then
  sed "s/^mode = .*/mode = \"$MODE\"/" inky.toml.example > /etc/akashic-display/inky.toml
  echo "==> Edit /etc/akashic-display/inky.toml (server, room_key, light bar), then reboot"
fi
install -d -m 0700 /var/lib/akashic-display

echo "==> Starting it at boot ($MODE)"
# Root: the LED strip (rpi_ws281x) needs direct access to the PWM hardware.
install -m 0644 "pdinky-$MODE.service" /etc/systemd/system/pdinky.service
systemctl daemon-reload
systemctl enable pdinky.service
if [[ "$MODE" == "battery" ]]; then
  echo "==> Set the wake schedule in the Witty Pi software (e.g. every 10 minutes)."
fi
echo "Done. Reboot to start (SPI settings take effect after a reboot)."
