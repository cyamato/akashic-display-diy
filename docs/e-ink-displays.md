# DIY e-ink displays

Two builds for a personal busy light or a room sign. Both are added with a code like any
Digital Signage Display, and both show what the display's summary says: its own room, or
its layout's Room Status panels, in their words and colours. Neither has been built on
real hardware yet. The software is tested off the device, but treat the first build as a
prototype.

| | Inky Impression + Pi Zero 2 W | Inky Frame 7.3" |
| --- | --- | --- |
| Best for | Plugged in at a door or desk, with an RGB light bar | Battery, hung anywhere |
| Changes appear | Within seconds (live event stream; the light bar at once, e-ink a little later) | At its next wake (default every 10 minutes, or a button) |
| Buttons | A busy, B available, C back to the calendar, D refresh | A busy, B available, C back to the calendar (the lit button shows which) |
| Software | `inky-pi` (Python, a systemd service) | `inky-frame` (MicroPython) |

## 1. Inky Impression 7.3" + Raspberry Pi Zero 2 W

**Parts**
- Pimoroni Inky Impression 7.3" (800 × 480, colour e-ink).
- Raspberry Pi Zero 2 W with a soldered header, a microSD card and a 5 V supply.
- A 3D-printed frame, e.g. [Inky Impression 7.3 frame case](https://www.printables.com/model/992883-inky-impression-73-frame-case).
- Optional light bar: a short WS2812 ("NeoPixel") strip along the top, about 8 LEDs.
- Optional battery: a Witty Pi L3V7 (scheduled wake-ups) and a LiPo, for example Adafruit's 6600 mAh. This combination is reported working on [Pimoroni's forum](https://forums.pimoroni.com/t/battery-power-for-pi-zero-2-w-7-3-e-ink-display/28491/3).

**Wiring the light bar**
- The Inky uses SPI0 and GPIO 17, 22 and 27, and its buttons are GPIO 5, 6, 16 and 24. GPIO 18 (PWM) is free.
- Connect the strip's data line to **GPIO 18** through a 330 Ω resistor, its 5 V to 5 V, and its ground to ground.
- Power a long strip separately from the Pi.
- Set `[lights] enabled = true` in the settings.

**Install**
1. Flash Raspberry Pi OS Lite (Bookworm or later) with Wi-Fi and SSH set in Raspberry Pi Imager.
2. Copy `inky-pi` to the Pi, then run `sudo ./install.sh live` (or `battery`). The installer:
   - turns on SPI with `dtoverlay=spi0-0cs,clockfreq=2000000` in `/boot/firmware/config.txt` (the slower clock the Pi Zero 2 W needs with the 7.3" Inky);
   - installs the agent in `/opt/pdinky`;
   - starts it at boot.
3. Edit `/etc/akashic-display/inky.toml`:
   - `server`;
   - optionally `room_key` (the room's status key from /admin → Rooms) so the buttons work;
   - the light bar settings.
4. Reboot. The Inky shows a **code**. Add it at `https://<server>/activate`, then give it a room or a layout in /admin.

**Battery mode**
- Set the wake interval in Witty Pi's software (every 10 minutes is a reasonable start).
- Each wake: boot, draw if anything changed, power off.
- The light bar and instant updates are for plugged-in use.
- Very rough estimate, not measured: about 20 Wh usable from 6600 mAh, and roughly a minute awake per wake, gives a week at 5-minute wakes or about three weeks at 15.

**Without hardware:** `python -m pdinky --config inky.toml --once` on any computer writes the screen to `/tmp/pdinky-screen.png`.

## 2. Pimoroni Inky Frame 7.3" (Pico 2 W)

**Parts**
- The Inky Frame 7.3".
- A LiPo battery, or 3 × AA in a holder, on its battery connector.

**Install**
1. Install Pimoroni's MicroPython for Inky Frame, if it isn't on there already (see Pimoroni's [Getting Started with Inky Frame](https://learn.pimoroni.com/article/getting-started-with-inky-frame)).
2. Copy `inky-frame/main.py` and `dash.py` to the Frame (Thonny, or `mpremote cp`).
3. Copy `secrets.py.example` to `secrets.py` and fill it in:
   - your Wi-Fi;
   - the server;
   - optionally `ROOM_KEY` for the buttons;
   - the wake interval in minutes.
4. It shows a code. Add it at `https://<server>/activate`.

**What each wake does**
1. Connect to Wi-Fi.
2. Act on the button that woke it, if any.
3. Fetch the summary.
4. Redraw only if something changed.
5. Light the button for the room's state.
6. Power off until the next wake.

On USB power the Frame can't power itself off, so it waits instead.

## 3. Light bar and buttons
- **Light bar colour:** the busiest room's colour, in each panel's own colours (for example green open, amber booked, red occupied). It's off when there's nothing to show.
- **Buttons:** they call `PUT /v1/rooms/status` with the room's status key, the same as building automation. Every display showing that room changes too.
