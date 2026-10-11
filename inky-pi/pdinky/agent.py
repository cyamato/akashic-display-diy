"""The agent: pair, show the display's rooms, light the bar, act on the buttons.

Two modes:
- live (plugged in): stays on, hears changes over the display's event stream (seconds),
  checks again every refresh_seconds (calendar bookings start by the clock), and redraws
  only when what's on the screen changes. The light bar changes at once.
- battery (Witty Pi): wakes, draws if anything changed, and powers off again.
"""

from __future__ import annotations

import logging
import threading
import time
from typing import Callable, Protocol

from PIL import Image

from .client import DashClient, Offline, Revoked
from .config import Config
from .render import code_image, content_key, rooms_image, worst_state
from .state import State

log = logging.getLogger("pdinky")

# Seconds between checks while waiting to be added, and before asking for a fresh code.
PAIR_POLL_SECONDS = 10
CODE_LIFETIME_SECONDS = 9 * 60
# Battery builds wait this long to be added before going back to sleep.
BATTERY_PAIRING_SECONDS = 15 * 60


class Screen(Protocol):  # pragma: no cover - a shape, not code
    size: tuple[int, int]

    def show(self, image: Image.Image) -> None: ...


class Lights(Protocol):  # pragma: no cover - a shape, not code
    def show(self, rgb: tuple[int, int, int] | None) -> None: ...


def rgb(hex_colour: str) -> tuple[int, int, int]:
    """#rrggbb as a tuple (black when unreadable)."""
    try:
        return tuple(int(hex_colour[i : i + 2], 16) for i in (1, 3, 5))  # type: ignore[return-value]
    except (ValueError, TypeError):
        return (0, 0, 0)


class Agent:
    def __init__(
        self,
        config: Config,
        client: DashClient,
        state: State,
        screen: Screen,
        lights: Lights,
        sleep: Callable[[float], None] = time.sleep,
        clock: Callable[[], float] = time.monotonic,
    ):
        self.config = config
        self.client = client
        self.state = state
        self.screen = screen
        self.lights = lights
        self.sleep = sleep
        self.clock = clock
        self.wake = threading.Event()
        self.running = True

    # ---- being added ----
    def pair(self, give_up_after: float | None = None) -> str | None:
        """Show a code until someone adds the display; its token, or None if it gave up."""
        started = self.clock()
        token = self.state.get("pending_token")
        asked = None
        while self.running:
            now = self.clock()
            if give_up_after is not None and now - started > give_up_after:
                return None
            try:
                if asked is None or now - asked > CODE_LIFETIME_SECONDS:
                    pairing = self.client.request_code(self.config.label, self.config.location, token)
                    token = pairing.token
                    self.state.set("pending_token", token)
                    self.screen.show(code_image(pairing.code, self.config.server, self.screen.size))
                    self.state.set("shown", None)
                    asked = now
                    log.info("showing code %s", pairing.code)
                elif self.client.approved(token):
                    self.state.set("token", token)
                    self.state.set("pending_token", None)
                    log.info("added")
                    return token
            except Offline as err:
                log.warning("can't reach the server: %s", err)
            self.sleep(PAIR_POLL_SECONDS)
        return None

    # ---- showing ----
    def refresh(self, token: str) -> None:
        """Draw the rooms if anything changed; light the bar either way."""
        summary = self.client.summary(token, self.config.time_zone, self.config.clock)
        key = content_key(summary)
        if key != self.state.get("shown"):
            self.screen.show(rooms_image(summary, self.screen.size))
            self.state.set("shown", key)
        worst = worst_state(summary)
        self.lights.show(rgb(worst[1]) if worst else None)

    def press(self, action: str) -> None:
        """A button: busy, free or auto for the room (with its status key), or just refresh."""
        if action != "refresh":
            if not self.config.room_key:
                log.warning("button %s: set room_key in the settings to change the room", action)
                return
            try:
                self.client.set_room(self.config.room_key, action, self.config.busy_minutes)
            except Offline as err:
                log.warning("couldn't set the room: %s", err)
        self.wake.set()

    # ---- modes ----
    def run_once(self) -> None:
        """Battery: draw what's current (being added first if need be), then power off."""
        token = self.state.get("token") or self.pair(BATTERY_PAIRING_SECONDS)
        if token:
            try:
                self.refresh(token)
            except Revoked:
                self.state.set("token", None)
                log.warning("this display was removed; it will show a new code on its next wake")
            except Offline as err:
                log.warning("can't reach the server: %s", err)

    def listen(self, token: str) -> None:
        """Hear the display's changes until stopped (reconnecting after drops)."""
        while self.running:
            try:
                self.client.events(token, self.wake.set, lambda: self.running)
            except Revoked:
                return
            except Offline as err:
                log.info("event stream dropped: %s", err)
            self.sleep(10)

    def run_live(self, rounds: int | None = None, start: Callable[[Callable[[], None]], None] | None = None) -> None:
        """Plugged in: stay on and keep the screen and the bar current (rounds: for tests)."""
        start = start or (lambda fn: threading.Thread(target=fn, daemon=True).start())
        token = self.state.get("token") or self.pair()
        if token:
            start(lambda: self.listen(token))
        while self.running and token and (rounds is None or rounds > 0):
            try:
                self.refresh(token)
            except Revoked:
                self.state.set("token", None)
                self.lights.show(None)
                token = self.pair()
                if token:
                    start(lambda t=token: self.listen(t))
                continue
            except Offline as err:
                log.warning("can't reach the server: %s", err)
            self.wake.wait(self.config.refresh_seconds)
            self.wake.clear()
            if rounds is not None:
                rounds -= 1
