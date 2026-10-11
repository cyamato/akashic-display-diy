# Akashic Display on a Pimoroni Inky Frame 7.3" (Pico 2 W), MicroPython.
#
# Each wake (on battery, from the RTC every MINUTES, or a button): connect to Wi-Fi, act on
# the button that woke it (A busy, B available, C back to the calendar: needs ROOM_KEY),
# be added with a code if it isn't yet, fetch the display's summary, redraw only if it
# changed, light the button for the room's state, and sleep. On USB power the Inky Frame
# can't power itself off, so it waits instead. Settings are in secrets.py.

import json
import time

import inky_frame
import network
import urequests
from picographics import PicoGraphics, DISPLAY_INKY_FRAME_7 as DISPLAY

import dash
import secrets

STATE = "/dash_state.json"
BUTTONS = {"a": inky_frame.button_a, "b": inky_frame.button_b, "c": inky_frame.button_c, "d": inky_frame.button_d, "e": inky_frame.button_e}
ACTIONS = {"a": "busy", "b": "free", "c": "auto"}

graphics = PicoGraphics(DISPLAY)
WIDTH, HEIGHT = graphics.get_bounds()
graphics.set_font("bitmap8")


def load():
    try:
        with open(STATE) as f:
            return json.load(f)
    except (OSError, ValueError):
        return {}


def save(state):
    with open(STATE, "w") as f:
        json.dump(state, f)


def pen(name):
    # The panel's own index for the colour where the firmware names it, else the nearest.
    value = getattr(inky_frame, name.upper(), None)
    return value if value is not None else graphics.create_pen(*dash.PALETTE[name])


def width_of(text, scale):
    return graphics.measure_text(text, scale)


def draw(steps):
    for step in steps:
        if step[0] == "rect":
            graphics.set_pen(pen(step[5]))
            graphics.rectangle(step[1], step[2], step[3], step[4])
        else:
            graphics.set_pen(pen(step[5]))
            graphics.text(step[1], step[2], step[3], -1, step[4])
    graphics.update()


def connect():
    wlan = network.WLAN(network.STA_IF)
    wlan.active(True)
    if not wlan.isconnected():
        inky_frame.led_wifi.on()
        wlan.connect(secrets.WIFI_SSID, secrets.WIFI_PASSWORD)
        for _ in range(60):
            if wlan.isconnected():
                break
            time.sleep(0.5)
        inky_frame.led_wifi.off()
    return wlan.isconnected()


def call(method, path, token=None, body=None):
    headers = {"Accept": "application/json"}
    if token:
        headers["Authorization"] = "Bearer " + token
    url = secrets.SERVER.rstrip("/") + path
    res = urequests.request(method, url, headers=headers, json=body)
    try:
        data = res.json() if res.status_code not in (204,) else {}
    except ValueError:
        data = {}
    status = res.status_code
    res.close()
    return status, data


def pair(state):
    """Show a code until someone adds the display (up to 15 minutes); its token, or None."""
    token = state.get("pending") or dash.new_token()
    state["pending"] = token
    save(state)
    asked = None
    started = time.time()
    while time.time() - started < 15 * 60:
        if asked is None or time.time() - asked > 9 * 60:
            status, data = call("POST", "/v1/pairings", body=dash.pairing_body(token, secrets.LABEL, getattr(secrets, "LOCATION", None)))
            if status == 201:
                draw(dash.code_layout(data["code"], secrets.SERVER, WIDTH, HEIGHT, width_of))
                state.pop("shown", None)
                asked = time.time()
        else:
            status, _ = call("POST", "/v1/entitlements", token=token)
            if status == 200:
                state["token"] = token
                state.pop("pending", None)
                save(state)
                return token
        time.sleep(15)
    return None


def query():
    q = "?clock=" + str(getattr(secrets, "CLOCK", 24))
    tz = getattr(secrets, "TIME_ZONE", "")
    return q + ("&tz=" + tz if tz else "")


def run():
    state = load()
    inky_frame.led_busy.on()
    if not connect():
        return
    # The button that woke it says what to do first.
    room_key = getattr(secrets, "ROOM_KEY", "")
    for name, action in ACTIONS.items():
        if room_key and BUTTONS[name].read():
            body = {"state": action}
            if action != "auto":
                body["minutes"] = getattr(secrets, "BUSY_MINUTES", 60)
            call("PUT", "/v1/rooms/status", token=room_key, body=body)
            break
    token = state.get("token") or pair(state)
    if not token:
        return
    status, summary = call("GET", "/v1/display/summary" + query(), token=token)
    if status == 401:
        state.pop("token", None)
        save(state)
        return
    if status != 200:
        return
    key = dash.content_key(summary)
    if key != state.get("shown"):
        draw(dash.rooms_layout(summary, WIDTH, HEIGHT, width_of))
        state["shown"] = key
        save(state)
    lit = dash.button_lit(summary)
    for name, button in BUTTONS.items():
        if name == lit:
            button.led_on()
        else:
            button.led_off()


while True:
    try:
        run()
    except Exception as err:  # keep going: next wake tries again
        print("error:", err)
    inky_frame.led_busy.off()
    minutes = getattr(secrets, "MINUTES", 10)
    # On battery this powers off until the RTC (or a button) wakes it; on USB it returns.
    inky_frame.sleep_for(minutes)
    time.sleep(minutes * 60)
