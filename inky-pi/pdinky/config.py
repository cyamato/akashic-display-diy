"""The agent's settings, from a TOML file (default /etc/akashic-display/inky.toml)."""

from __future__ import annotations

import tomllib
from dataclasses import dataclass, field
from pathlib import Path

DEFAULT_PATH = Path("/etc/akashic-display/inky.toml")
MODES = ("live", "battery")
BUTTON_ACTIONS = ("busy", "free", "auto", "refresh")


class ConfigError(ValueError):
    """The settings file is missing something or has a bad value."""


@dataclass(frozen=True)
class Lights:
    """An addressable RGB strip (WS2812) along the top, on one GPIO pin."""

    enabled: bool = False
    pin: int = 18
    count: int = 8
    brightness: float = 0.3


@dataclass(frozen=True)
class Buttons:
    """The Inky Impression's buttons A-D (GPIO 5, 6, 16, 24 on the 7.3") and what each does."""

    enabled: bool = True
    pins: tuple[int, ...] = (5, 6, 16, 24)
    actions: tuple[str, ...] = ("busy", "free", "auto", "refresh")


@dataclass(frozen=True)
class Config:
    """Everything the agent needs."""

    server: str
    mode: str = "live"
    time_zone: str | None = None
    clock: int = 24
    label: str = "Inky Impression"
    location: str | None = None
    # The room's status key (pds_...), so the buttons can say busy or free; empty: buttons only refresh.
    room_key: str = ""
    busy_minutes: int = 60
    # Seconds between checks while awake (calendar bookings start by the clock, not by an event).
    refresh_seconds: int = 60
    # Battery: power off after drawing (Witty Pi wakes it on its schedule).
    shutdown: bool = True
    state_dir: Path = Path("/var/lib/akashic-display")
    lights: Lights = field(default_factory=Lights)
    buttons: Buttons = field(default_factory=Buttons)


def load(path: Path = DEFAULT_PATH) -> Config:
    """Read and check the settings."""
    try:
        raw = tomllib.loads(path.read_text())
    except FileNotFoundError as err:
        raise ConfigError(f"No settings file at {path}") from err
    except tomllib.TOMLDecodeError as err:
        raise ConfigError(f"{path} is not valid TOML: {err}") from err
    return parse(raw)


def parse(raw: dict) -> Config:
    """Settings from a parsed TOML document."""
    server = str(raw.get("server", "")).rstrip("/")
    if not server.startswith("https://") and not server.startswith("http://localhost"):
        raise ConfigError("server must be the dashboard's https:// address")
    mode = raw.get("mode", "live")
    if mode not in MODES:
        raise ConfigError(f"mode must be one of {', '.join(MODES)}")
    clock = raw.get("clock", 24)
    if clock not in (12, 24):
        raise ConfigError("clock must be 12 or 24")
    room_key = str(raw.get("room_key", ""))
    if room_key and not room_key.startswith("pds_"):
        raise ConfigError("room_key must be a room's status key (pds_...) from /admin -> Rooms")
    lights_raw = raw.get("lights", {})
    lights = Lights(
        enabled=bool(lights_raw.get("enabled", False)),
        pin=int(lights_raw.get("pin", 18)),
        count=max(1, int(lights_raw.get("count", 8))),
        brightness=min(1.0, max(0.0, float(lights_raw.get("brightness", 0.3)))),
    )
    buttons_raw = raw.get("buttons", {})
    pins = tuple(int(p) for p in buttons_raw.get("pins", (5, 6, 16, 24)))
    actions = tuple(str(a) for a in buttons_raw.get("actions", ("busy", "free", "auto", "refresh")))
    if len(pins) != len(actions) or any(a not in BUTTON_ACTIONS for a in actions):
        raise ConfigError(f"buttons.actions must match buttons.pins, each one of {', '.join(BUTTON_ACTIONS)}")
    return Config(
        server=server,
        mode=mode,
        time_zone=raw.get("time_zone") or None,
        clock=clock,
        label=str(raw.get("label", "Inky Impression"))[:60],
        location=(str(raw["location"])[:100] if raw.get("location") else None),
        room_key=room_key,
        busy_minutes=min(1440, max(1, int(raw.get("busy_minutes", 60)))),
        refresh_seconds=max(15, int(raw.get("refresh_seconds", 60))),
        shutdown=bool(raw.get("shutdown", True)),
        state_dir=Path(raw.get("state_dir", "/var/lib/akashic-display")),
        lights=lights,
        buttons=Buttons(enabled=bool(buttons_raw.get("enabled", True)), pins=pins, actions=actions),
    )
