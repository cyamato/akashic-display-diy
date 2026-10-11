# Akashic Display on a Pimoroni Inky Frame 7.3": the logic, kept free of hardware so it runs
# (and is tested) on ordinary Python as well as on the Inky Frame's MicroPython.
#
# Drawing is described as a list of steps, ("rect", x, y, w, h, colour) and
# ("text", text, x, y, scale, colour), which main.py carries out with PicoGraphics' bitmap
# font. `width_of(text, scale)` measures text the way the screen will.

import binascii
import hashlib
import os

# The colours every Inky Frame 7.3" shows (Spectra 6, and the older 7-colour panel).
PALETTE = {
    "black": (0, 0, 0),
    "white": (255, 255, 255),
    "red": (200, 0, 0),
    "green": (0, 150, 0),
    "blue": (0, 0, 200),
    "yellow": (255, 230, 0),
}
LIGHT = ("white", "yellow")
MAX_ROWS = 6
# Bitmap font: 8 pixels high per scale step.
LINE = 8


def new_token():
    """A display token made on the device (only its hash leaves it until it's added)."""
    return binascii.hexlify(os.urandom(24)).decode()


def token_hash(token):
    return binascii.hexlify(hashlib.sha256(token.encode()).digest()).decode()


def nearest(hex_colour):
    """The palette colour closest to an #rrggbb colour (black for anything unreadable)."""
    try:
        rgb = (int(hex_colour[1:3], 16), int(hex_colour[3:5], 16), int(hex_colour[5:7], 16))
    except (ValueError, TypeError):
        return "black"
    best, score = "black", None
    for name in PALETTE:
        c = PALETTE[name]
        d = (c[0] - rgb[0]) ** 2 + (c[1] - rgb[1]) ** 2 + (c[2] - rgb[2]) ** 2
        if score is None or d < score:
            best, score = name, d
    return best


def content_key(summary):
    """What's on the screen (not the clock): redraw only when this changes."""
    parts = [str(summary.get("name")), str(summary.get("date"))]
    for p in summary.get("panels") or []:
        parts.extend([str(p.get(k)) for k in ("room", "name", "state", "word", "color", "detail")])
    return token_hash("|".join(parts))[:16]


def button_lit(summary):
    """Which button's LED to light: A busy (said so), B free (said so), C following the calendar."""
    panels = summary.get("panels") or []
    if not panels:
        return None
    p = panels[0]
    if p.get("state") == "occupied":
        return "a"
    if p.get("state") == "open" and p.get("until"):
        return "b"
    return "c"


def fit(text, scale, width, width_of):
    """Text cut (with "...") to fit a width at a scale."""
    if width_of(text, scale) <= width:
        return text
    while text and width_of(text + "...", scale) > width:
        text = text[:-1]
    return text + "..."


def biggest(text, width, height, width_of, most=12):
    """The largest scale (at most `most`) at which text fits a box."""
    scale = most
    while scale > 1 and (width_of(text, scale) > width or LINE * scale > height):
        scale -= 1
    return scale


def rooms_layout(summary, width, height, width_of):
    """The rooms: a row each (two columns past three), busy and booked ones filled with their colour."""
    pad = width // 40
    head = height // 10
    hs = max(1, (head * 6 // 10) // LINE)
    steps = [("rect", 0, 0, width, height, "white")]
    steps.append(("text", fit(summary.get("name") or "", hs, width * 6 // 10, width_of), pad, (head - LINE * hs) // 2, hs, "black"))
    date = summary.get("date") or ""
    steps.append(("text", date, width - pad - width_of(date, hs), (head - LINE * hs) // 2, hs, "black"))
    steps.append(("rect", 0, head, width, 2, "black"))
    panels = (summary.get("panels") or [])[:MAX_ROWS]
    if not panels:
        msg = "No rooms on this display yet"
        steps.append(("text", msg, (width - width_of(msg, 3)) // 2, height // 2, 3, "black"))
        return steps
    cols = 2 if len(panels) > 3 else 1
    per_col = (len(panels) + cols - 1) // cols
    cw = width // cols
    rh = (height - head - 2) // per_col
    small = max(1, min(4, rh // 6 // LINE))
    for i, p in enumerate(panels):
        x = (i // per_col) * cw
        y = head + 2 + (i % per_col) * rh
        colour = nearest(p.get("color"))
        busy = p.get("state") != "open"
        word_ink, text_ink = colour, "black"
        if busy:
            steps.append(("rect", x + 2, y + 2, cw - 4, rh - 4, colour))
            word_ink = text_ink = "black" if colour in LIGHT else "white"
        inner = cw - 2 * pad
        top = y + pad // 2
        if p.get("name"):
            steps.append(("text", fit(p["name"], small, inner, width_of), x + pad, top, small, text_ink))
            top += LINE * small + pad // 2
        word = p.get("word") or ""
        room = rh - (top - y) - LINE * small - pad
        big = biggest(word, inner, room, width_of)
        steps.append(("text", word, x + pad, top, big, word_ink))
        steps.append(("text", fit(p.get("detail") or "", small, inner, width_of), x + pad, y + rh - pad // 2 - LINE * small, small, text_ink))
    return steps


def code_layout(code, server, width, height, width_of):
    """The pairing code, and where to add the display with it."""
    site = server.split("://", 1)[-1] + "/activate"
    steps = [("rect", 0, 0, width, height, "white"), ("text", "Akashic Display", width // 40, height // 20, 3, "black")]
    scale = biggest(code, width * 9 // 10, height // 4, width_of)
    steps.append(("text", code, (width - width_of(code, scale)) // 2, height * 35 // 100, scale, "blue"))
    for line, at in (("Add this display with this code at", 66), (site, 76)):
        steps.append(("text", line, (width - width_of(line, 3)) // 2, height * at // 100, 3, "black"))
    return steps


def pairing_body(token, label, location):
    body = {"token_sha256": token_hash(token), "label": label}
    if location:
        body["location"] = location
    return body
