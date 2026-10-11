# Akashic Display DIY

Build your own busy light, door sign or room sign for [Akashic Display](#what-it-works-with):
code and build guides for e-paper signs and for a small microcontroller that gives any
display inputs, outputs and an LED strip.

> [!WARNING]
> **Use at your own risk. There is no warranty of any kind.**
>
> These are hobby projects, provided "as is" under the [MIT licence](LICENSE). None of them
> has been built on real hardware yet: the software is tested on a computer only. Treat
> your first build as a prototype.
>
> You are responsible for your build: the parts you choose, the wiring, the power
> supplies and batteries, and where and how you mount it. Never switch mains voltage
> directly from a board's pins; use a properly rated, isolated relay module and a
> qualified electrician for anything on mains. Follow the maker's instructions for
> lithium batteries and chargers. A DIY sign or light is not a safety or alarm device:
> don't rely on it for emergencies.
>
> The authors accept no liability for any damage, injury or loss from using anything in
> this repository.

## The Builds

| Build | Folder | What it is |
| --- | --- | --- |
| E-paper door sign | [`inky-pi`](inky-pi) | A Pimoroni Inky Impression 7.3" on a Raspberry Pi Zero 2 W (Python). Changes within seconds; four buttons; an optional WS2812 light bar. |
| Battery e-paper sign | [`inky-frame`](inky-frame) | A Pimoroni Inky Frame 7.3" (MicroPython). Wakes every few minutes or on a button, on a LiPo or AA batteries. |
| Serial inputs, outputs and LED strip | [`serial-io`](serial-io) | An Arduino sketch for an ESP32, Raspberry Pi Pico or Arduino Nano ESP32, plugged into a display by USB: dry-contact inputs, a relay or buzzer, and an RGB strip. |
| Stream Deck keys | [`stream-deck`](stream-deck) | An Elgato Stream Deck plugin: keys for Busy, On the Phone, In a Meeting, Available and Auto that light up with your room's state. |

Guides:
- [DIY e-ink displays](docs/e-ink-displays.md): parts, wiring, install and battery mode for both e-paper builds.
- [Serial devices](docs/serial-devices.md): parts, wiring, the sketch, setting it up, and its line protocol.

## What It Works With

Each build connects to an Akashic Display server: the hosted service, a partner's, or
your own. You add it with a code like any other display and choose the room or layout it
shows. Use your server's address wherever the guides say `https://<server>` or
`https://dash.example.com`.

## Test Without Hardware

```sh
./test.sh
```

It runs the Python tests for both e-paper builds (needs [uv](https://docs.astral.sh/uv/)),
builds and runs the serial sketch's tests against a stand-in Arduino (needs `g++`), and
tests, builds and validates the Stream Deck plugin (needs Node.js 20.5 or later).

## Contributing

Issues and pull requests are welcome, especially reports from real builds. By
contributing, you agree that your contribution is licensed under the MIT licence.
