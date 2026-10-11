"""Talking to the dashboard: pairing, the display's summary, its event stream, and the room's status."""

from __future__ import annotations

import hashlib
import json
import secrets
from dataclasses import dataclass
from typing import Callable, Iterator, Protocol


class Offline(Exception):
    """The server can't be reached (or answered with an error)."""


class Revoked(Exception):
    """The server no longer knows this display's token: it must be added again."""


class Response(Protocol):  # pragma: no cover - a shape, not code
    status_code: int

    def json(self) -> object: ...

    def iter_lines(self, decode_unicode: bool = ...) -> Iterator[str]: ...

    def close(self) -> None: ...


class Session(Protocol):  # pragma: no cover - a shape, not code
    def request(self, method: str, url: str, **kwargs: object) -> Response: ...


@dataclass
class Pairing:
    """A code to show until someone adds the display at /activate."""

    token: str
    code: str


def new_token() -> str:
    """A display token made on the device (only its hash ever leaves it until it's added)."""
    return secrets.token_urlsafe(32)


class DashClient:
    """The dashboard's API, for one display."""

    def __init__(self, server: str, session: Session, timeout: float = 15.0):
        self.server = server.rstrip("/")
        self.session = session
        self.timeout = timeout

    def _call(self, method: str, path: str, token: str | None = None, **kwargs: object) -> Response:
        headers = {"Accept": "application/json", **({"Authorization": f"Bearer {token}"} if token else {})}
        timeout = kwargs.pop("timeout", self.timeout)
        try:
            return self.session.request(method, f"{self.server}{path}", headers=headers, timeout=timeout, **kwargs)
        except Exception as err:  # network errors of any HTTP library
            raise Offline(str(err)) from err

    def request_code(self, label: str, location: str | None, token: str | None = None) -> Pairing:
        """Ask for a pairing code for a (new) token."""
        token = token or new_token()
        body = {"token_sha256": hashlib.sha256(token.encode()).hexdigest(), "label": label}
        if location:
            body["location"] = location
        res = self._call("POST", "/v1/pairings", json=body)
        data = _json(res)
        if res.status_code != 201 or not isinstance(data.get("code"), str):
            raise Offline(f"pairing refused: {data.get('error_code', res.status_code)}")
        return Pairing(token=token, code=data["code"])

    def approved(self, token: str) -> bool:
        """Whether someone has added the display yet."""
        res = self._call("POST", "/v1/entitlements", token=token)
        if res.status_code == 200:
            return True
        if res.status_code == 401:
            return False
        raise Offline(f"entitlements: HTTP {res.status_code}")

    def summary(self, token: str, time_zone: str | None, clock: int) -> dict:
        """What the display shows now (its rooms, in its words and colours)."""
        params = {"clock": str(clock), **({"tz": time_zone} if time_zone else {})}
        res = self._call("GET", "/v1/display/summary", token=token, params=params)
        if res.status_code == 401:
            raise Revoked()
        if res.status_code != 200:
            raise Offline(f"summary: HTTP {res.status_code}")
        return _json(res)

    def events(self, token: str, on_change: Callable[[], None], keep_going: Callable[[], bool]) -> None:
        """
        Call on_change for each status event (a room or the display changed) until the
        stream ends or keep_going() says stop. Raises Revoked or Offline.
        """
        # The server sends a keep-alive every 25 seconds: wait longer than that between lines.
        res = self._call("GET", "/v1/display/events", token=token, stream=True, timeout=(10, 90))
        if res.status_code == 401:
            raise Revoked()
        if res.status_code != 200:
            raise Offline(f"events: HTTP {res.status_code}")
        try:
            for line in res.iter_lines(decode_unicode=True):
                if not keep_going():
                    return
                if line == "event: status":
                    on_change()
        except Exception as err:
            raise Offline(str(err)) from err
        finally:
            res.close()

    def set_room(self, room_key: str, state: str, minutes: int) -> dict:
        """Say the room is busy or free (for some minutes), or back to its calendar ('auto')."""
        body = {"state": state, **({"minutes": minutes} if state != "auto" else {})}
        res = self._call("PUT", "/v1/rooms/status", token=room_key, json=body)
        if res.status_code != 200:
            raise Offline(f"room status: HTTP {res.status_code}")
        return _json(res)


def _json(res: Response) -> dict:
    try:
        data = res.json()
    except (ValueError, json.JSONDecodeError):
        return {}
    return data if isinstance(data, dict) else {}
