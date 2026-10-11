// Just enough of the Arduino API (and Adafruit NeoPixel) to run serial-io.ino in a test.
#pragma once

#include <cstdint>
#include <map>
#include <string>

#define HIGH 1
#define LOW 0
#define INPUT 0
#define OUTPUT 1
#define INPUT_PULLUP 2
#define NEO_GRB 0
#define NEO_KHZ800 0

namespace fake {
inline uint32_t now = 0;
inline std::map<int, int> pins;
inline std::map<int, int> modes;
inline std::string in;   // what the display has sent, not yet read
inline std::string out;  // what the sketch has printed
}  // namespace fake

inline uint32_t millis() { return fake::now; }
inline void pinMode(int pin, int mode) { fake::modes[pin] = mode; }
inline int digitalRead(int pin) { return fake::pins[pin]; }
inline void digitalWrite(int pin, int level) { fake::pins[pin] = level; }

struct FakeSerial {
  void begin(uint32_t) {}
  int available() { return static_cast<int>(fake::in.size()); }
  int read() {
    const int c = static_cast<unsigned char>(fake::in[0]);
    fake::in.erase(0, 1);
    return c;
  }
  void println(const char* s) { fake::out += std::string(s) + "\r\n"; }
};
inline FakeSerial Serial;

class Adafruit_NeoPixel {
 public:
  Adafruit_NeoPixel(uint16_t, uint8_t, int) {}
  void begin() {}
  void setBrightness(uint8_t b) { brightness = b; }
  void setPixelColor(uint16_t, uint32_t c) { lastColor = c; }
  void show() { shows++; }
  static uint32_t Color(uint8_t r, uint8_t g, uint8_t b) { return (uint32_t(r) << 16) | (uint32_t(g) << 8) | b; }
  uint32_t lastColor = 0;
  uint8_t brightness = 0;
  int shows = 0;
};
