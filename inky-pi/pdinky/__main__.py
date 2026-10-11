"""Run the agent: python -m pdinky [--config /etc/akashic-display/inky.toml] [--once]."""

from __future__ import annotations

import argparse
import logging
import sys
from pathlib import Path

from .config import DEFAULT_PATH, ConfigError, load


def main(argv: list[str] | None = None) -> int:  # pragma: no cover - wires up the hardware
    parser = argparse.ArgumentParser(prog="pdinky", description="Akashic Display on an Inky Impression")
    parser.add_argument("--config", type=Path, default=DEFAULT_PATH)
    parser.add_argument("--once", action="store_true", help="draw once and exit (battery mode does this anyway)")
    args = parser.parse_args(argv)
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")
    try:
        config = load(args.config)
    except ConfigError as err:
        print(err, file=sys.stderr)
        return 2

    import requests

    from . import hardware
    from .agent import Agent
    from .client import DashClient
    from .state import State

    agent = Agent(config, DashClient(config.server, requests.Session()), State(config.state_dir), hardware.Screen(), hardware.LightBar(config.lights))
    if config.mode == "battery" or args.once:
        agent.run_once()
        if config.mode == "battery" and config.shutdown and not args.once:
            hardware.power_off()
        return 0
    buttons = hardware.watch_buttons(config.buttons, agent.press)  # noqa: F841 - kept alive while running
    agent.run_live()
    return 0


if __name__ == "__main__":  # pragma: no cover
    sys.exit(main())
