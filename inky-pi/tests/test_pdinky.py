import hashlib
import json
from pathlib import Path

import pytest
from PIL import Image, ImageDraw, ImageFont

from pdinky import config as cfg
from pdinky.agent import Agent, CODE_LIFETIME_SECONDS, rgb
from pdinky.client import DashClient, Offline, Revoked, new_token
from pdinky.render import code_image, content_key, fit, font, nearest, rooms_image, worst_state
from pdinky.state import State

SERVER = "https://dash.test"


class Res:
    def __init__(self, status=200, body=None, lines=(), raw=None):
        self.status_code = status
        self.body = body
        self.lines = lines
        self.raw = raw
        self.closed = False

    def json(self):
        if self.raw is not None:
            raise ValueError("not json")
        return self.body

    def iter_lines(self, decode_unicode=False):
        for line in self.lines:
            if isinstance(line, Exception):
                raise line
            yield line

    def close(self):
        self.closed = True


class Session:
    """Answers by (method, path) from a table of responses (or callables, or exceptions)."""

    def __init__(self, routes):
        self.routes = routes
        self.calls = []

    def request(self, method, url, **kwargs):
        path = url.replace(SERVER, "")
        self.calls.append((method, path, kwargs))
        answer = self.routes[(method, path)]
        if callable(answer) and not isinstance(answer, Res):
            answer = answer()
        if isinstance(answer, Exception):
            raise answer
        return answer


SUMMARY = {
    "time": "2026-10-09T03:00:00.000Z", "local_time": "16:00", "date": "Fri 9 Oct", "time_zone": "Pacific/Auckland", "name": "Level 3",
    "panels": [
        {"room": "room-a", "name": "Kauri", "state": "booked", "word": "Booked", "color": "#d97706", "detail": "Until 16:30", "until": "x", "next": None},
        {"room": "room-b", "name": None, "state": "open", "word": "Open", "color": "#16a34a", "detail": "Free for the rest of the day", "until": None, "next": None},
    ],
}


class FakeScreen:
    size = (400, 240)

    def __init__(self):
        self.shown = []

    def show(self, image):
        self.shown.append(image)


class FakeLights:
    def __init__(self):
        self.shown = []

    def show(self, colour):
        self.shown.append(colour)


class Clock:
    def __init__(self):
        self.t = 0.0

    def __call__(self):
        return self.t

    def sleep(self, s):
        self.t += s


# ---- settings ----
def test_settings_defaults_and_checks(tmp_path: Path):
    path = tmp_path / "inky.toml"
    path.write_text('server = "https://dash.test/"\nroom_key = "pds_abc"\nlocation = "Door"\n[lights]\nenabled = true\nbrightness = 5\ncount = 0\n')
    c = cfg.load(path)
    assert (c.server, c.mode, c.clock, c.room_key, c.location) == ("https://dash.test", "live", 24, "pds_abc", "Door")
    assert (c.lights.enabled, c.lights.brightness, c.lights.count) == (True, 1.0, 1)
    assert c.buttons.pins == (5, 6, 16, 24) and c.buttons.actions == ("busy", "free", "auto", "refresh")
    assert cfg.parse({"server": "http://localhost:8443", "busy_minutes": 9999, "refresh_seconds": 1}).busy_minutes == 1440
    for bad in [{}, {"server": "http://dash.test"}, {"server": SERVER, "mode": "sleepy"}, {"server": SERVER, "clock": 13},
                {"server": SERVER, "room_key": "secret"}, {"server": SERVER, "buttons": {"pins": [5], "actions": ["busy", "free"]}},
                {"server": SERVER, "buttons": {"pins": [5], "actions": ["dance"]}}]:
        with pytest.raises(cfg.ConfigError):
            cfg.parse(bad)
    with pytest.raises(cfg.ConfigError, match="No settings file"):
        cfg.load(tmp_path / "missing.toml")
    (tmp_path / "bad.toml").write_text("server = ")
    with pytest.raises(cfg.ConfigError, match="not valid TOML"):
        cfg.load(tmp_path / "bad.toml")


# ---- the server ----
def test_client_pairs_reads_and_sets_the_room():
    token = "tok-1"
    s = Session({
        ("POST", "/v1/pairings"): Res(201, {"code": "ABCD-1234", "expires_at": "x"}),
        ("POST", "/v1/entitlements"): Res(401, {}),
        ("GET", "/v1/display/summary"): Res(200, SUMMARY),
        ("PUT", "/v1/rooms/status"): Res(200, {"override": "busy"}),
    })
    c = DashClient(f"{SERVER}/", s)
    pairing = c.request_code("Door", "Level 2", token)
    assert (pairing.token, pairing.code) == (token, "ABCD-1234")
    assert s.calls[0][2]["json"] == {"token_sha256": hashlib.sha256(b"tok-1").hexdigest(), "label": "Door", "location": "Level 2"}
    assert "location" not in (c.request_code("Door", None).token and s.calls[-1][2]["json"])
    assert c.approved(token) is False
    assert c.summary(token, "Pacific/Auckland", 12) == SUMMARY
    assert s.calls[-1][2]["params"] == {"clock": "12", "tz": "Pacific/Auckland"}
    assert s.calls[-1][2]["headers"]["Authorization"] == "Bearer tok-1"
    c.summary(token, None, 24)
    assert s.calls[-1][2]["params"] == {"clock": "24"}
    assert c.set_room("pds_k", "busy", 30) == {"override": "busy"}
    assert s.calls[-1][2]["json"] == {"state": "busy", "minutes": 30}
    c.set_room("pds_k", "auto", 30)
    assert s.calls[-1][2]["json"] == {"state": "auto"}
    assert len(new_token()) > 30


def test_client_failures():
    c = DashClient(SERVER, Session({
        ("POST", "/v1/pairings"): Res(429, {"error_code": "too_many_pairings"}),
        ("POST", "/v1/entitlements"): Res(503, raw="oops"),
        ("GET", "/v1/display/summary"): Res(500, []),
        ("PUT", "/v1/rooms/status"): Res(401, {}),
    }))
    with pytest.raises(Offline, match="too_many_pairings"):
        c.request_code("x", None)
    with pytest.raises(Offline):
        c.approved("t")
    with pytest.raises(Offline):
        c.summary("t", None, 24)
    with pytest.raises(Offline):
        c.set_room("pds_k", "busy", 5)
    assert DashClient(SERVER, Session({("POST", "/v1/entitlements"): Res(200, {})})).approved("t") is True
    with pytest.raises(Revoked):
        DashClient(SERVER, Session({("GET", "/v1/display/summary"): Res(401, {})})).summary("t", None, 24)
    with pytest.raises(Offline):
        DashClient(SERVER, Session({("GET", "/v1/display/summary"): ConnectionError("down")})).summary("t", None, 24)
    # An answer that isn't JSON, or isn't an object, counts as empty.
    with pytest.raises(Offline):
        DashClient(SERVER, Session({("POST", "/v1/pairings"): Res(502, raw="<html>")})).request_code("x", None)
    with pytest.raises(Offline):
        DashClient(SERVER, Session({("POST", "/v1/pairings"): Res(201, ["x"])})).request_code("x", None)


def test_client_event_stream():
    heard = []
    res = Res(200, lines=["event: status", "data: {}", "", ": keep-alive", "event: status"])
    c = DashClient(SERVER, Session({("GET", "/v1/display/events"): res}))
    c.events("t", lambda: heard.append(1), lambda: True)
    assert heard == [1, 1] and res.closed
    assert c.session.calls[0][2]["timeout"] == (10, 90) and c.session.calls[0][2]["stream"] is True
    # Stopped part way.
    heard.clear()
    left = iter([True, False])
    DashClient(SERVER, Session({("GET", "/v1/display/events"): Res(200, lines=["event: status", "event: status"])})).events("t", lambda: heard.append(1), lambda: next(left))
    assert heard == [1]
    with pytest.raises(Revoked):
        DashClient(SERVER, Session({("GET", "/v1/display/events"): Res(401)})).events("t", lambda: None, lambda: True)
    with pytest.raises(Offline):
        DashClient(SERVER, Session({("GET", "/v1/display/events"): Res(502)})).events("t", lambda: None, lambda: True)
    with pytest.raises(Offline):
        DashClient(SERVER, Session({("GET", "/v1/display/events"): Res(200, lines=["event: status", OSError("reset")])})).events("t", lambda: None, lambda: True)


# ---- drawing ----
def test_colours_and_text():
    assert [nearest(c) for c in ("#16a34a", "#d97706", "#dc2626", "#2563eb", "#ffffff", "#facc15", "nope", None)] == ["green", "yellow", "red", "blue", "white", "yellow", "black", "black"]
    draw = ImageDraw.Draw(Image.new("RGB", (10, 10)))
    face = font(20)
    assert fit(draw, "Short", face, 500) == "Short"
    cut = fit(draw, "A very long room name that won't fit", face, 120)
    assert cut.endswith("…") and draw.textlength(cut, font=face) <= 120
    assert isinstance(font(12, False, ("/nonexistent.ttf",)), ImageFont.FreeTypeFont | ImageFont.ImageFont)
    assert rgb("#102030") == (16, 32, 48) and rgb("bad") == (0, 0, 0)


def test_screens_draw_and_change_only_with_content():
    img = rooms_image(SUMMARY, (800, 480))
    assert img.size == (800, 480)
    # The booked row is filled with its colour (yellow, nearest #d97706).
    assert img.getpixel((10, 80)) == (255, 230, 0)
    many = {**SUMMARY, "panels": [dict(SUMMARY["panels"][i % 2], name=f"R{i}") for i in range(10)]}
    assert rooms_image(many, (800, 480), ("/nonexistent.ttf",)).size == (800, 480)
    empty = rooms_image({"name": None, "date": None, "panels": []}, (400, 240))
    assert empty.size == (400, 240)
    assert code_image("ABCD-1234", SERVER, (800, 480)).size == (800, 480)
    # The clock doesn't count; a room's state does.
    later = json.loads(json.dumps(SUMMARY))
    later["local_time"] = "16:01"
    assert content_key(later) == content_key(SUMMARY)
    later["panels"][1]["state"] = "occupied"
    assert content_key(later) != content_key(SUMMARY)
    assert worst_state(later) == ("occupied", "#16a34a")
    assert worst_state({"panels": []}) is None


def test_state_survives_restarts(tmp_path: Path):
    s = State(tmp_path / "state")
    assert s.get("token") is None
    s.set("token", "abc")
    s.set("gone", None)
    assert State(tmp_path / "state").get("token") == "abc"
    assert oct((tmp_path / "state" / "inky-state.json").stat().st_mode)[-3:] == "600"
    s.set("token", None)
    assert State(tmp_path / "state").get("token") is None
    (tmp_path / "state" / "inky-state.json").write_text("{nope")
    assert State(tmp_path / "state").data == {}
    s.data["n"] = 5
    assert s.get("n") is None


# ---- the agent ----
def make(tmp_path, routes, **conf):
    clock = Clock()
    config = cfg.parse({"server": SERVER, "state_dir": str(tmp_path), **conf})
    session = Session(routes)
    agent = Agent(config, DashClient(SERVER, session), State(tmp_path), FakeScreen(), FakeLights(), sleep=clock.sleep, clock=clock)
    return agent, session, clock


def test_pairing_shows_a_code_and_keeps_the_token_once_added(tmp_path):
    answers = iter([Res(401), Offline("blip"), Res(401), Res(200)])
    agent, session, clock = make(tmp_path, {
        ("POST", "/v1/pairings"): Res(201, {"code": "ABCD-1234"}),
        ("POST", "/v1/entitlements"): lambda: next(answers),
    })
    token = agent.pair()
    assert token and agent.state.get("token") == token and agent.state.get("pending_token") is None
    assert len(agent.screen.shown) == 1
    # A long wait: a fresh code for the same token.
    agent2, session2, clock2 = make(tmp_path / "b", {
        ("POST", "/v1/pairings"): Res(201, {"code": "WXYZ-0000"}),
        ("POST", "/v1/entitlements"): Res(401),
    })
    assert agent2.pair(give_up_after=CODE_LIFETIME_SECONDS + 30) is None
    codes = [c for c in session2.calls if c[1] == "/v1/pairings"]
    assert len(codes) == 2 and codes[0][2]["json"]["token_sha256"] == codes[1][2]["json"]["token_sha256"]
    agent2.running = False
    assert agent2.pair() is None


def test_battery_draws_once_and_only_when_it_changed(tmp_path):
    agent, session, _ = make(tmp_path, {("GET", "/v1/display/summary"): Res(200, SUMMARY)}, mode="battery")
    agent.state.set("token", "tok")
    agent.run_once()
    agent.run_once()
    assert len(agent.screen.shown) == 1
    assert agent.lights.shown == [(217, 119, 6), (217, 119, 6)]
    # Removed: forgets its token (and pairs on its next wake); offline: nothing changes.
    agent.client.session.routes[("GET", "/v1/display/summary")] = Res(401)
    agent.run_once()
    assert agent.state.get("token") is None
    agent.state.set("token", "tok")
    agent.client.session.routes[("GET", "/v1/display/summary")] = ConnectionError("down")
    agent.run_once()
    assert agent.state.get("token") == "tok"
    # Never added, and nobody adds it in time: back to sleep.
    agent.state.set("token", None)
    agent.client.session.routes.update({("POST", "/v1/pairings"): Res(201, {"code": "C"}), ("POST", "/v1/entitlements"): Res(401)})
    agent.run_once()
    assert agent.state.get("token") is None


def test_buttons_set_the_room_or_refresh(tmp_path):
    agent, session, _ = make(tmp_path, {("PUT", "/v1/rooms/status"): Res(200, {})}, room_key="pds_room")
    agent.press("busy")
    assert session.calls[-1][2]["json"] == {"state": "busy", "minutes": 60} and agent.wake.is_set()
    agent.wake.clear()
    agent.press("refresh")
    assert agent.wake.is_set() and len(session.calls) == 1
    session.routes[("PUT", "/v1/rooms/status")] = Res(500)
    agent.press("free")
    keyless, s2, _ = make(tmp_path / "k", {})
    keyless.press("busy")
    assert s2.calls == [] and not keyless.wake.is_set()


def test_live_mode_listens_redraws_and_pairs_again_when_removed(tmp_path):
    summaries = iter([Res(200, SUMMARY), ConnectionError("blip"), Res(401), Res(200, SUMMARY)])
    agent, session, _ = make(tmp_path, {
        ("GET", "/v1/display/summary"): lambda: next(summaries),
        ("POST", "/v1/pairings"): Res(201, {"code": "NEW1-CODE"}),
        ("POST", "/v1/entitlements"): Res(200),
        ("GET", "/v1/display/events"): Res(200, lines=["event: status"]),
    }, refresh_seconds=15)
    agent.state.set("token", "tok")
    agent.wake.wait = lambda timeout: True  # no real waiting
    started = []
    agent.run_live(rounds=3, start=started.append)
    assert len(started) == 2  # the stream, and again after being added anew
    assert agent.lights.shown[1] is None  # cleared when removed
    assert agent.state.get("token") != "tok"
    # The listener: hears events, reconnects after a drop, stops when removed or told to.
    agent.running = True
    drops = iter([Offline("dropped"), Revoked()])

    def events(token, on_change, keep_going):
        raise next(drops)

    agent.client.events = events
    agent.listen("tok")
    agent.running = False
    agent.listen("tok")
    # Never added: nothing to listen to.
    stopped, s3, _ = make(tmp_path / "s", {})
    stopped.running = False
    stopped.run_live(start=started.append)
    assert len(started) == 2
