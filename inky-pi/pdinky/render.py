"""Drawing the screen for a colour e-ink panel (Inky Impression: black, white, red, green, blue, yellow)."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

# The colours every Inky Impression can show (Spectra 6, and the older 7-colour panel).
PALETTE = {
    "black": (0, 0, 0),
    "white": (255, 255, 255),
    "red": (200, 0, 0),
    "green": (0, 150, 0),
    "blue": (0, 0, 200),
    "yellow": (255, 230, 0),
}
# Light colours get black text; the rest white.
LIGHT = {"white", "yellow"}
MAX_ROWS = 8

FONT_PATHS = (
    "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
)


def nearest(hex_colour: str) -> str:
    """The palette colour closest to an #rrggbb colour (black for anything unreadable)."""
    try:
        r, g, b = (int(hex_colour[i : i + 2], 16) for i in (1, 3, 5))
    except (ValueError, TypeError):
        return "black"
    return min(PALETTE, key=lambda n: sum((a - c) ** 2 for a, c in zip(PALETTE[n], (r, g, b))))


def font(size: int, bold: bool = True, paths: tuple[str, ...] = FONT_PATHS) -> ImageFont.ImageFont:
    """A TrueType font at a size (DejaVu on Raspberry Pi OS), else Pillow's own."""
    for path in paths if bold else paths[1:]:
        if Path(path).exists():
            return ImageFont.truetype(path, size)
    return ImageFont.load_default(size)


def fit(draw: ImageDraw.ImageDraw, text: str, face: ImageFont.ImageFont, width: int) -> str:
    """Text cut (with an ellipsis) to fit a width."""
    if draw.textlength(text, font=face) <= width:
        return text
    while text and draw.textlength(f"{text}…", font=face) > width:
        text = text[:-1]
    return f"{text}…"


def content_key(summary: dict) -> str:
    """What's on the screen (not the clock): redraw only when this changes."""
    shown = {"name": summary.get("name"), "date": summary.get("date"), "panels": summary.get("panels", [])}
    return hashlib.sha256(json.dumps(shown, sort_keys=True).encode()).hexdigest()[:16]


def worst_state(summary: dict) -> tuple[str, str] | None:
    """The busiest room's state and colour (occupied over booked over open), for the light bar."""
    order = {"occupied": 2, "booked": 1, "open": 0}
    panels = summary.get("panels") or []
    if not panels:
        return None
    p = max(panels, key=lambda x: order.get(x.get("state"), 0))
    return p.get("state", "open"), p.get("color", "#000000")


def rooms_image(summary: dict, size: tuple[int, int], paths: tuple[str, ...] = FONT_PATHS) -> Image.Image:
    """The rooms: a row each (two columns past four), busy and booked ones filled with their colour."""
    width, height = size
    img = Image.new("RGB", size, PALETTE["white"])
    draw = ImageDraw.Draw(img)
    pad = round(width * 0.025)
    head = round(height * 0.1)
    title_face = font(round(head * 0.45), True, paths)
    draw.text((pad, head // 2), fit(draw, summary.get("name") or "", title_face, round(width * 0.6)), font=title_face, fill=PALETTE["black"], anchor="lm")
    draw.text((width - pad, head // 2), summary.get("date") or "", font=font(round(head * 0.35), False, paths), fill=PALETTE["black"], anchor="rm")
    draw.rectangle((0, head, width, head + 1), fill=PALETTE["black"])
    panels = (summary.get("panels") or [])[:MAX_ROWS]
    if not panels:
        draw.text((width // 2, height // 2), "No rooms on this display yet", font=font(round(height * 0.06), False, paths), fill=PALETTE["black"], anchor="mm")
        return img
    cols = 2 if len(panels) > 4 else 1
    per_col = -(-len(panels) // cols)
    cw, rh = width // cols, (height - head - 2) // per_col
    for i, p in enumerate(panels):
        x, y = (i // per_col) * cw, head + 2 + (i % per_col) * rh
        colour = nearest(p.get("color", "#000000"))
        busy = p.get("state") != "open"
        ink = PALETTE[colour]
        if busy:
            draw.rectangle((x + 2, y + 2, x + cw - 3, y + rh - 3), fill=ink)
            ink = PALETTE["black"] if colour in LIGHT else PALETTE["white"]
        inner = cw - 2 * pad
        big, small = round(min(rh * 0.38, cw * 0.12)), round(min(rh * 0.16, cw * 0.04))
        text_ink = ink if busy else PALETTE["black"]
        if p.get("name"):
            draw.text((x + pad, y + rh * 0.08), fit(draw, p["name"], font(small, True, paths), inner), font=font(small, True, paths), fill=text_ink)
        draw.text((x + pad, y + rh * 0.08 + small * 1.3), fit(draw, p.get("word", ""), font(big, True, paths), inner), font=font(big, True, paths), fill=ink)
        draw.text((x + pad, y + rh * 0.92), fit(draw, p.get("detail", ""), font(small, False, paths), inner), font=font(small, False, paths), fill=text_ink, anchor="ld")
    return img


def code_image(code: str, server: str, size: tuple[int, int], paths: tuple[str, ...] = FONT_PATHS) -> Image.Image:
    """The pairing code, and where to add the display with it."""
    width, height = size
    img = Image.new("RGB", size, PALETTE["white"])
    draw = ImageDraw.Draw(img)
    draw.text((round(width * 0.025), round(height * 0.05)), "Akashic Display", font=font(round(height * 0.05), True, paths), fill=PALETTE["black"])
    draw.text((width // 2, round(height * 0.45)), code, font=font(round(height * 0.16), True, paths), fill=PALETTE["blue"], anchor="mm")
    small = font(round(height * 0.05), False, paths)
    draw.text((width // 2, round(height * 0.66)), "Add this display with this code at", font=small, fill=PALETTE["black"], anchor="mm")
    draw.text((width // 2, round(height * 0.74)), f"{server.split('://', 1)[-1]}/activate", font=small, fill=PALETTE["black"], anchor="mm")
    return img
