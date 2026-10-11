// Runs serial-io.ino on a computer against a fake Arduino: pins, millis(), Serial and
// the NeoPixel strip are stand-ins the test drives and reads.
// g++ -std=c++17 -I tests/fake tests/sketch_test.cpp
#include <cstdio>
#include <string>

#include "Arduino.h"
#include "../serial-io.ino"

static int failures = 0;
#define CHECK(cond)                                                    \
  do {                                                                 \
    if (!(cond)) {                                                     \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);      \
      failures++;                                                      \
    }                                                                  \
  } while (0)

static void step(uint32_t ms) {
  for (uint32_t i = 0; i < ms; i += 5) {
    fake::now += 5;
    loop();
  }
}

int main() {
  fake::pins[4] = HIGH;  // the door is open (pulled up)
  fake::pins[5] = LOW;   // the button is held down at boot
  setup();
  CHECK(fake::out == "Akashic serial-io ready\r\ndoor=open\r\nbutton=closed\r\n");
  CHECK(fake::modes[4] == INPUT_PULLUP && fake::modes[6] == OUTPUT);
  CHECK(fake::pins[6] == LOW && fake::pins[7] == LOW);
  fake::out.clear();

  // The door closes, bouncing on the way: one line once it settles.
  fake::pins[4] = LOW;
  step(10);
  fake::pins[4] = HIGH;
  step(5);
  fake::pins[4] = LOW;
  step(60);
  CHECK(fake::out == "door=closed\r\n");
  fake::out.clear();

  // A Room Status line turns the strip red; blink flashes it; brightness changes it.
  fake::in += "{\"state\":\"occupied\",\"word\":\"Busy\"}\n";
  step(5);
  CHECK(pixels.lastColor == Adafruit_NeoPixel::Color(255, 0, 0) && pixels.shows > 0);
  const int shows = pixels.shows;
  step(100);
  CHECK(pixels.shows == shows);  // nothing new: not redrawn
  fake::in += "blink=#00ff00\nbrightness=40\n";
  step(5);
  CHECK(pixels.lastColor == Adafruit_NeoPixel::Color(0, 255, 0) && pixels.brightness == 40);
  step(600);
  CHECK(pixels.lastColor == 0);

  // Outputs: on, off, and a pulse that ends by itself; unknown names are ignored.
  fake::in += "relay=on\r\n";
  step(5);
  CHECK(fake::pins[6] == HIGH);
  fake::in += "relay=off\nbuzzer=pulse\nlamp=on\nnonsense\n";
  step(5);
  CHECK(fake::pins[6] == LOW && fake::pins[7] == HIGH);
  step(1100);
  CHECK(fake::pins[7] == LOW);

  if (failures) {
    std::printf("%d check(s) failed\n", failures);
    return 1;
  }
  std::printf("sketch: all checks passed\n");
  return 0;
}
