"""What the agent keeps between runs (a battery build boots for every update): its token, and what's on the screen."""

from __future__ import annotations

import json
import os
from pathlib import Path


class State:
    """A small JSON file in the state directory (readable only by the agent's user)."""

    def __init__(self, directory: Path):
        self.path = directory / "inky-state.json"
        try:
            self.data = json.loads(self.path.read_text())
        except (FileNotFoundError, ValueError):
            self.data = {}

    def get(self, key: str) -> str | None:
        value = self.data.get(key)
        return value if isinstance(value, str) else None

    def set(self, key: str, value: str | None) -> None:
        if value is None:
            self.data.pop(key, None)
        else:
            self.data[key] = value
        self.path.parent.mkdir(parents=True, exist_ok=True)
        tmp = self.path.with_suffix(".tmp")
        tmp.write_text(json.dumps(self.data))
        os.chmod(tmp, 0o600)
        tmp.replace(self.path)
