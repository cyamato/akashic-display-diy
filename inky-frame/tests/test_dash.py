import hashlib
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

import dash  # noqa: E402

# The bitmap8 font: about 6 pixels wide per character per scale step.
def width_of(text, scale):
    return len(text) * 6 * scale


SUMMARY = {
    "name": "Level 3", "date": "Fri 9 Oct",
    "panels": [
        {"room": "room-a", "name": "Kauri", "state": "booked", "word": "Booked", "color": "#d97706", "detail": "Until 16:30", "until": "x"},
        {"room": "room-b", "name": None, "state": "open", "word": "Open", "color": "#16a34a", "detail": "Free for the rest of the day", "until": None},
    ],
}


def texts(steps):
    return [s[1] for s in steps if s[0] == "text"]


def test_tokens_and_pairing():
    t = dash.new_token()
    assert len(t) == 48 and t != dash.new_token()
    assert dash.token_hash("abc") == hashlib.sha256(b"abc").hexdigest()
    assert dash.pairing_body("abc", "Door", "L2") == {"token_sha256": hashlib.sha256(b"abc").hexdigest(), "label": "Door", "location": "L2"}
    assert "location" not in dash.pairing_body("abc", "Door", None)


def test_colours_and_buttons():
    assert [dash.nearest(c) for c in ("#16a34a", "#d97706", "#dc2626", "#2563eb", "#ffffff", "nope", None)] == ["green", "yellow", "red", "blue", "white", "black", "black"]
    assert dash.button_lit({"panels": [{"state": "occupied"}]}) == "a"
    assert dash.button_lit({"panels": [{"state": "open", "until": "x"}]}) == "b"
    assert dash.button_lit(SUMMARY) == "c"
    assert dash.button_lit({"panels": []}) is None


def test_redraw_only_when_the_content_changes():
    later = {**SUMMARY, "local_time": "16:01"}
    assert dash.content_key(later) == dash.content_key(SUMMARY)
    busy = {**SUMMARY, "panels": [dict(SUMMARY["panels"][0], state="occupied")] + SUMMARY["panels"][1:]}
    assert dash.content_key(busy) != dash.content_key(SUMMARY)
    assert len(dash.content_key({})) == 16


def test_fitting_text():
    assert dash.fit("Short", 2, 500, width_of) == "Short"
    cut = dash.fit("A very long room name", 2, 100, width_of)
    assert cut.endswith("...") and width_of(cut, 2) <= 100
    assert dash.biggest("Busy", 800, 400, width_of) == 12
    assert dash.biggest("Booked", 100, 400, width_of) == 2
    assert dash.biggest("x" * 500, 10, 10, width_of) == 1


def test_rooms_layout():
    steps = dash.rooms_layout(SUMMARY, 800, 480, width_of)
    assert steps[0] == ("rect", 0, 0, 800, 480, "white")
    assert texts(steps) == ["Level 3", "Fri 9 Oct", "Kauri", "Booked", "Until 16:30", "Open", "Free for the rest of the day"]
    # The booked row is filled with its colour, its text in black (yellow is light).
    assert ("rect", 2, 52, 796, 211, "yellow") in steps
    word = next(s for s in steps if s[0] == "text" and s[1] == "Booked")
    assert word[5] == "black"
    # The open row: its word in its colour.
    assert next(s for s in steps if s[0] == "text" and s[1] == "Open")[5] == "green"
    red = {**SUMMARY, "panels": [dict(SUMMARY["panels"][0], state="occupied", color="#dc2626")]}
    assert next(s for s in dash.rooms_layout(red, 800, 480, width_of) if s[0] == "text" and s[1] == "Booked")[5] == "white"
    many = {**SUMMARY, "panels": [dict(SUMMARY["panels"][i % 2], name="R%d" % i) for i in range(9)]}
    assert len([t for t in texts(dash.rooms_layout(many, 800, 480, width_of)) if t.startswith("R")]) == dash.MAX_ROWS
    assert texts(dash.rooms_layout({}, 800, 480, width_of))[-1] == "No rooms on this display yet"


def test_code_layout():
    steps = dash.code_layout("ABCD-1234", "https://dash.test", 800, 480, width_of)
    assert texts(steps) == ["Akashic Display", "ABCD-1234", "Add this display with this code at", "dash.test/activate"]
    assert next(s for s in steps if s[0] == "text" and s[1] == "ABCD-1234")[5] == "blue"
