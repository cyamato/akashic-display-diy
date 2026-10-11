// Akashic Display serial I/O: a microcontroller plugged into a display by USB.
//
// - Inputs: dry contacts (a door contact, a button, a PIR's relay output, a thermostat's
//   alarm contact) between a pin and ground. Each change is sent to the display as
//   "door=open" or "door=closed", and the display's input rules act on it.
// - Outputs: named pins (a relay board, a buzzer, a door strike) switched by "relay=on",
//   "relay=off" or "relay=pulse".
// - An RGB LED strip (WS2812 / NeoPixel): a busy light or a status bar. It shows the room's
//   colour from a Room Status panel's JSON line, and "rgb=red", "blink=#ff0000" and
//   "brightness=120" from overlays and the panel's own text.
//
// Boards: ESP32, Raspberry Pi Pico (RP2040 or RP2350), or any Arduino-compatible board with
// the C++ standard library (not the classic AVR Uno). Library: Adafruit NeoPixel (Library
// Manager). Set the display's serial device in /admin to the same speed (BAUD below).
// The full guide is docs/serial_devices.md.
#include <Adafruit_NeoPixel.h>

#include "protocol.h"

// ---- your wiring: change these to match your board ----
struct InputPin { const char* name; uint8_t pin; };
struct OutputPin { const char* name; uint8_t pin; bool activeHigh; };

// Each contact goes between its pin and GND (the internal pull-up does the rest).
const InputPin INPUTS[] = {
  {"door", 4},
  {"button", 5},
};
// A relay module's IN pin, a buzzer through a transistor. activeHigh false for relay
// boards that switch on with LOW.
const OutputPin OUTPUTS[] = {
  {"relay", 6, true},
  {"buzzer", 7, true},
};
const uint8_t STRIP_PIN = 8;      // The strip's DIN, through a 330 ohm resistor.
const uint16_t STRIP_LEDS = 30;   // How many LEDs.
const uint32_t BAUD = 9600;       // The same as the display's serial device in /admin.
const uint32_t PULSE_MS = 1000;   // How long "pulse" holds an output on.
const uint32_t DEBOUNCE_MS = 30;  // How long a contact must settle.

// ---- no changes needed below ----
const size_t N_INPUTS = sizeof(INPUTS) / sizeof(INPUTS[0]);
const size_t N_OUTPUTS = sizeof(OUTPUTS) / sizeof(OUTPUTS[0]);

Adafruit_NeoPixel pixels(STRIP_LEDS, STRIP_PIN, NEO_GRB + NEO_KHZ800);
akashic::LineReader reader;
akashic::Strip strip;
akashic::Debouncer debouncers[N_INPUTS];
uint32_t pulseUntil[N_OUTPUTS];
akashic::Rgb shown;
uint8_t shownBrightness = 0;
bool stripDirty = true;

void setOutput(size_t i, bool on) {
  digitalWrite(OUTPUTS[i].pin, on == OUTPUTS[i].activeHigh ? HIGH : LOW);
}

void sendInput(size_t i) {
  Serial.println(akashic::inputLine(INPUTS[i].name, debouncers[i].state()).c_str());
}

void handle(const std::string& line) {
  const akashic::Command c = akashic::parseCommand(line);
  if (c.kind == akashic::Kind::Output) {
    for (size_t i = 0; i < N_OUTPUTS; i++) {
      if (c.name != OUTPUTS[i].name) continue;
      setOutput(i, c.action != akashic::OutputAction::Off);
      pulseUntil[i] = c.action == akashic::OutputAction::Pulse ? millis() + PULSE_MS : 0;
    }
    return;
  }
  if (strip.apply(c)) stripDirty = true;
}

void showStrip(uint32_t now) {
  const akashic::Rgb c = strip.at(now);
  if (!stripDirty && c == shown && strip.brightness == shownBrightness) return;
  pixels.setBrightness(strip.brightness);
  for (uint16_t i = 0; i < STRIP_LEDS; i++) pixels.setPixelColor(i, pixels.Color(c.r, c.g, c.b));
  pixels.show();
  shown = c;
  shownBrightness = strip.brightness;
  stripDirty = false;
}

void setup() {
  Serial.begin(BAUD);
  for (size_t i = 0; i < N_OUTPUTS; i++) {
    pinMode(OUTPUTS[i].pin, OUTPUT);
    setOutput(i, false);
    pulseUntil[i] = 0;
  }
  pixels.begin();
  showStrip(0);
  // Words with spaces aren't inputs: the display ignores this line.
  Serial.println("Akashic serial-io ready");
  for (size_t i = 0; i < N_INPUTS; i++) {
    pinMode(INPUTS[i].pin, INPUT_PULLUP);
    debouncers[i] = akashic::Debouncer(DEBOUNCE_MS);
    debouncers[i].reset(digitalRead(INPUTS[i].pin) == HIGH, millis());
    sendInput(i);
  }
}

void loop() {
  while (Serial.available() > 0) {
    if (reader.add(static_cast<char>(Serial.read()))) handle(reader.line());
  }
  const uint32_t now = millis();
  for (size_t i = 0; i < N_INPUTS; i++) {
    if (debouncers[i].update(digitalRead(INPUTS[i].pin) == HIGH, now)) sendInput(i);
  }
  for (size_t i = 0; i < N_OUTPUTS; i++) {
    if (pulseUntil[i] && static_cast<int32_t>(now - pulseUntil[i]) >= 0) {
      setOutput(i, false);
      pulseUntil[i] = 0;
    }
  }
  showStrip(now);
}
