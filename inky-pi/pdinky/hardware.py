"""The Pi's hardware: the Inky Impression, the RGB light bar and the buttons.

Each import happens only on the Pi; without the hardware (or its libraries) the agent
still runs, drawing nowhere, so it can be tried on any computer.
"""

from __future__ import annotations

import logging
import subprocess
from typing import Callable

from PIL import Image

from .config import Buttons as ButtonConfig
from .config import Lights as LightConfig

log = logging.getLogger("pdinky")


class Screen:
    """The Inky Impression (found by its EEPROM), or a file when there isn't one."""

    def __init__(self, fallback_path: str = "/tmp/pdinky-screen.png"):  # pragma: no cover - needs an Inky
        self.fallback_path = fallback_path
        try:
            from inky.auto import auto

            self.inky = auto()
            self.size = self.inky.resolution
        except Exception as err:
            log.warning("no Inky display found (%s); writing %s instead", err, fallback_path)
            self.inky = None
            self.size = (800, 480)

    def show(self, image: Image.Image) -> None:  # pragma: no cover - needs an Inky
        if self.inky is None:
            image.save(self.fallback_path)
            return
        self.inky.set_image(image, saturation=0.6)
        self.inky.show()


class LightBar:
    """A WS2812 strip on one GPIO pin (GPIO 18: PWM, free next to the Inky)."""

    def __init__(self, config: LightConfig):  # pragma: no cover - needs the strip
        self.strip = None
        if not config.enabled:
            return
        try:
            from rpi_ws281x import PixelStrip

            self.strip = PixelStrip(config.count, config.pin, brightness=round(config.brightness * 255))
            self.strip.begin()
        except Exception as err:
            log.warning("light bar unavailable (%s)", err)

    def show(self, rgb: tuple[int, int, int] | None) -> None:  # pragma: no cover - needs the strip
        if self.strip is None:
            return
        from rpi_ws281x import Color

        colour = Color(*rgb) if rgb else Color(0, 0, 0)
        for i in range(self.strip.numPixels()):
            self.strip.setPixelColor(i, colour)
        self.strip.show()


def watch_buttons(config: ButtonConfig, on_press: Callable[[str], None]) -> list:  # pragma: no cover - needs GPIO
    """Call on_press with a button's action when it's pressed (keep the returned objects alive)."""
    if not config.enabled:
        return []
    try:
        from gpiozero import Button
    except Exception as err:
        log.warning("buttons unavailable (%s)", err)
        return []
    buttons = []
    for pin, action in zip(config.pins, config.actions):
        button = Button(pin, pull_up=True, bounce_time=0.1)
        button.when_pressed = lambda a=action: on_press(a)
        buttons.append(button)
    return buttons


def power_off() -> None:  # pragma: no cover - turns the Pi off
    """Shut down (a battery build: Witty Pi wakes it on its schedule)."""
    subprocess.run(["sudo", "shutdown", "-h", "now"], check=False)
