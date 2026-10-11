# Serial Devices: Inputs, Outputs and LED Strips

A display can reach the room it sits in. A small microcontroller plugged into the display by
USB can:
- read **dry contacts**, such as a door contact, a button, a motion sensor's relay output or a
  thermostat's alarm contact;
- switch **outputs**, such as a relay, a buzzer or a door strike;
- light an **RGB LED strip**, such as a busy light over a door or a status bar under a screen.

The display passes inputs to the server, where **input rules** turn them into actions. The
server and the display's panels send the device what to show and switch.

> Nothing here has been built on real hardware yet. The sketch and its protocol are tested on
> a computer against a stand-in Arduino (`serial-io/test.sh`). Treat the first build as a
> prototype.

It works in Chrome, Edge and ChromeOS (Web Serial). Other browsers show the display
without its serial device.

## 1. What You Need

| Part | For example |
| --- | --- |
| A board with USB serial | ESP32 DevKit, Raspberry Pi Pico (RP2040 or RP2350), Arduino Nano ESP32. Not the classic AVR Uno, which has no C++ standard library. |
| Dry contacts | A magnetic door contact, a push button, a PIR sensor with a relay output |
| Outputs | A relay module (opto-isolated), or a buzzer through a transistor |
| An RGB strip | WS2812B (NeoPixel), 5 V, with a 330 Ω resistor on DIN and a 1000 µF capacitor across the supply |
| Power | The display's USB for the board. A strip of more than about 10 LEDs at full white needs its own 5 V supply, with its ground joined to the board's. |

## 2. Wiring

- **Dry contacts:** wire each one between its input pin and **GND**. The sketch turns on the pin's
  internal pull-up, so an open contact reads `open` and a closed one reads `closed`. Nothing
  else is needed.
- **Relays and buzzers:** connect the module's IN to its output pin. Some relay boards switch on
  with LOW; set `activeHigh` to `false` for those. Never switch mains directly from a pin.
- **The strip:** connect its DIN to `STRIP_PIN` through the 330 Ω resistor. Run its 5 V and GND from
  a supply big enough for it. On 3.3 V boards (ESP32, Pico), most strips work from a short
  wire. Add a 74AHCT125 level shifter if the colours flicker.

## 3. The Sketch

`serial-io/serial-io.ino`, with `protocol.h` beside it:
1. Install **Adafruit NeoPixel** from the Arduino IDE's Library Manager.
2. Set your pins at the top under *your wiring*:
   - `INPUTS`: a name and pin for each contact;
   - `OUTPUTS`: a name, pin and `activeHigh` for each output;
   - `STRIP_PIN` and `STRIP_LEDS`;
   - `BAUD`.
3. Upload it to the board.

To run its tests on a computer: `serial-io/test.sh` (needs `g++`).

## 4. Set It Up in /admin

1. **The display's serial device.** In Organisations → Digital Signage Displays, open the
   display's settings (⚙). Under **Serial Device**, choose the speed, which must match `BAUD`.
   The line ending is `\n` unless your firmware wants something else.
2. **Choose the port once, on the display.** Tap **Connect Serial Device** at the bottom right and
   choose the board. After that, the display reconnects by itself, including after the board is
   unplugged and plugged back in. For screens nobody can touch, a Chrome enterprise policy
   (`SerialAllowUsbDevicesForUrls`) can allow the device in advance. The display's row in
   /admin shows how the device is doing:
   - *connected*;
   - *not connected* (choose its port, or plug it in);
   - *didn't open*;
   - *can't be used* (the browser has no Web Serial).
3. **What the device hears**:
   - **Room Status panels** with *Send each change to the display's serial device* send a
     JSON line for each change, like `{"state":"occupied",...}`. The sketch turns the strip
     green when the room is open, amber when booked and red when occupied. With *My own text*,
     you choose the line for each state instead, for example `blink=red` for occupied.
   - **Overlays** each have a line to send when they show and another when they hide, for
     example `rgb=#ff00ff` and `rgb=off`, or `buzzer=pulse`.
4. **What the device says**: each input, as **input rules** under Organisations → Input Rules.

## 5. The Protocol

One line each way per message. The display adds its line ending; the board ends its lines
with `\r\n` (`Serial.println`).

| From the display | Meaning |
| --- | --- |
| `{"state":"open"…}` | A Room Status panel's JSON line: the strip shows green, amber (booked) or red (occupied) |
| `rgb=#ff8800`, `rgb=red`, `rgb=off` | The whole strip in one colour. Names: red, green, blue, amber, yellow, white, purple, cyan, off |
| `blink=red` | The strip blinks (half a second on, half off) |
| `brightness=120` | Strip brightness, 0 to 255 (80 at start-up) |
| `relay=on`, `relay=off`, `relay=pulse` | A named output on, off, or on for `PULSE_MS` (a door strike) |

| From the device | Meaning |
| --- | --- |
| `door=open`, `door=closed` | An input changed (and each input once at start-up) |
| `button` | A name alone is an input with no value (a press) |
| `{"input":"temp","value":21.5}` | A JSON line works too, for sensors that send numbers |

Input names are 1 to 40 letters, digits, dots, dashes or underscores. Values are at most 40
characters. Any other line, such as `Akashic serial-io ready`, is ignored, so the
board can print its own messages. A display sends at most 30 inputs a minute; debounce noisy
contacts (the sketch does, 30 ms).

## 6. Examples

**A door left open.**
1. Make an overlay *Door open* (a band along the top, warning colours, "Please close the
   back door"). Its serial text when shown is `blink=amber`, and when hidden `rgb=off`.
2. Add two input rules for the input `door`:
   - value `open`: **Show An Overlay** *Door open*, **On This Display**;
   - value `closed`: **Hide An Overlay** *Door open*, **On This Display**.

**A busy light at a home office door.** A strip over the door and a Room Status panel for your
office with *Send each change* on. The strip turns red when you're in a meeting or say you're
busy. Add a button (`button`) with a rule that does **Set A Room Busy Or Free** → *This
Display's Room*, Busy for 30 minutes.

**An emergency.** An *Evacuate* overlay (whole screen, emergency colours, the evacuation map as
its image). Its serial text when shown is `blink=red`, plus `buzzer=on` on a second display.
Show it:
- from /admin;
- from a site;
- from your fire panel through the integration key
  (`POST /v1/integrations/overlays/<id>/show`);
- from a trigger link on a phone's home screen.

**An instant sale.** A *Flash sale* overlay (a band along the bottom, sale colours, a product
video). A trigger link shows it for 60 minutes, and a Zapier zap calls the link when your
online store starts a sale.

## 7. Zapier, IFTTT and Your Own Systems

- **Into Akashic Display:**
  - A **trigger link** (Organisations → Trigger Links) is a secret address that runs its
    actions when anything POSTs to it. In Zapier use *Webhooks by Zapier* → *POST*; in IFTTT
    use the *Webhooks* service's *Make a web request* action. Anyone with the address can run it,
    so make a new one if it leaks.
  - Systems that can send a header use the organisation's **integration key** instead:
    - `POST /v1/integrations/overlays/<id>/show` with `{"minutes": 30}`, and `/hide`;
    - alerts and building status;
    - rooms' status keys for occupancy sensors (see the User Guide).
- **Out of Akashic Display:**
  - A **webhook** (Organisations → Webhooks) is told about display inputs, overlays shown and
    hidden, and alerts raised and cleared.
  - In Zapier use *Webhooks by Zapier* → *Catch Hook* and paste its address. For IFTTT, choose
    the IFTTT format and use the *Webhooks* service's
    `https://maker.ifttt.com/trigger/<event>/with/key/<your key>` address. IFTTT gets
    `value1` (the event), `value2` (a sentence) and `value3` (the details as JSON).
  - Every delivery carries `Akashic-Signature: t=<unix seconds>,v1=<hex>`, the HMAC-SHA256 of
    `<t>.<body>` with the webhook's secret (shown once when it's made). Check it, and reject
    old `t` values.
  - Deliveries go once, without retries. **Send A Test** shows the last answer.
