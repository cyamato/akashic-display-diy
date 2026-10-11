// Akashic Display serial I/O: the line protocol between a display and its microcontroller.
//
// Plain C++17 with no Arduino headers, so it is tested on a computer (tests/protocol_test.cpp)
// and used as-is by serial-io.ino.
//
// The display sends lines (each ends with the display's line ending, \n by default):
//   {"state":"occupied",...}   a Room Status panel's JSON line: the strip shows the room's colour
//   rgb=#ff0000 | rgb=red      the strip in one colour (off turns it off)
//   blink=#ff0000 | blink=red  the strip blinks in one colour
//   brightness=0..255          how bright the strip is
//   relay=on|off|pulse         a named output on, off, or on for a moment (a door strike)
// The device sends a line for each input that changes (and each at start-up):
//   door=open | door=closed    a dry contact between the input's pin and ground
#pragma once

#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>

namespace akashic {

// ---- colours ----

struct Rgb {
  uint8_t r = 0, g = 0, b = 0;
  bool operator==(const Rgb& o) const { return r == o.r && g == o.g && b == o.b; }
};

// A colour by name, or #rrggbb. Returns false for anything else.
inline bool parseColor(const std::string& text, Rgb& out) {
  struct Named { const char* name; Rgb rgb; };
  static const Named kNamed[] = {
    {"off", {0, 0, 0}}, {"black", {0, 0, 0}}, {"red", {255, 0, 0}}, {"green", {0, 200, 0}}, {"blue", {0, 60, 255}},
    {"amber", {255, 150, 0}}, {"yellow", {255, 220, 0}}, {"white", {255, 255, 255}}, {"purple", {150, 0, 255}}, {"cyan", {0, 200, 200}},
  };
  std::string t;
  for (char c : text) t += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  for (const auto& n : kNamed) {
    if (t == n.name) {
      out = n.rgb;
      return true;
    }
  }
  if (t.size() != 7 || t[0] != '#') return false;
  for (size_t i = 1; i < 7; i++) {
    if (!std::isxdigit(static_cast<unsigned char>(t[i]))) return false;
  }
  const long v = std::strtol(t.c_str() + 1, nullptr, 16);
  out = {static_cast<uint8_t>((v >> 16) & 0xff), static_cast<uint8_t>((v >> 8) & 0xff), static_cast<uint8_t>(v & 0xff)};
  return true;
}

// The colour for a Room Status state: open green, booked amber, occupied red.
inline bool roomColor(const std::string& state, Rgb& out) {
  if (state == "open") return parseColor("green", out);
  if (state == "booked") return parseColor("amber", out);
  if (state == "occupied") return parseColor("red", out);
  return false;
}

// ---- commands from the display ----

enum class Kind { None, Rgb, Blink, Brightness, Output, Room };
enum class OutputAction { Off, On, Pulse };

struct Command {
  Kind kind = Kind::None;
  Rgb rgb;                      // Rgb, Blink, Room
  uint8_t brightness = 0;       // Brightness
  std::string name;             // Output
  OutputAction action = OutputAction::Off;  // Output
};

// The value of "key":"…" in a JSON line (no escapes needed for the fields read here).
inline bool jsonString(const std::string& line, const char* key, std::string& out) {
  const std::string k = std::string("\"") + key + "\":\"";
  const size_t at = line.find(k);
  if (at == std::string::npos) return false;
  const size_t from = at + k.size();
  const size_t end = line.find('"', from);
  if (end == std::string::npos) return false;
  out = line.substr(from, end - from);
  return true;
}

inline std::string trim(const std::string& s) {
  size_t a = 0, b = s.size();
  while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) a++;
  while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) b--;
  return s.substr(a, b - a);
}

// Read one line from the display. Anything it doesn't understand is Kind::None.
inline Command parseCommand(const std::string& raw) {
  Command c;
  const std::string line = trim(raw);
  if (line.empty()) return c;
  if (line[0] == '{') {
    std::string state;
    if (jsonString(line, "state", state) && roomColor(state, c.rgb)) c.kind = Kind::Room;
    return c;
  }
  const size_t eq = line.find('=');
  if (eq == std::string::npos || eq == 0) return c;
  const std::string key = trim(line.substr(0, eq));
  const std::string value = trim(line.substr(eq + 1));
  if (key == "rgb" || key == "blink") {
    if (parseColor(value, c.rgb)) c.kind = key == "rgb" ? Kind::Rgb : Kind::Blink;
    return c;
  }
  if (key == "brightness") {
    char* end = nullptr;
    const long v = std::strtol(value.c_str(), &end, 10);
    if (!value.empty() && end && *end == '\0' && v >= 0 && v <= 255) {
      c.kind = Kind::Brightness;
      c.brightness = static_cast<uint8_t>(v);
    }
    return c;
  }
  if (value == "on" || value == "off" || value == "pulse") {
    c.kind = Kind::Output;
    c.name = key;
    c.action = value == "on" ? OutputAction::On : value == "pulse" ? OutputAction::Pulse : OutputAction::Off;
  }
  return c;
}

// Collects bytes into lines (\n ends a line; \r is dropped; an over-long line is thrown away).
class LineReader {
 public:
  explicit LineReader(size_t max = 160) : max_(max) {}
  // Add a byte; true when a line is complete (read it with line()).
  bool add(char ch) {
    if (ch == '\r') return false;
    if (ch == '\n') {
      done_ = overflow_ ? std::string() : buf_;
      buf_.clear();
      const bool ok = !overflow_;
      overflow_ = false;
      return ok;
    }
    if (buf_.size() >= max_) {
      overflow_ = true;
      return false;
    }
    buf_ += ch;
    return false;
  }
  const std::string& line() const { return done_; }

 private:
  size_t max_;
  std::string buf_, done_;
  bool overflow_ = false;
};

// ---- inputs to the display ----

// A dry contact, debounced: a change counts once it has held for `ms`.
class Debouncer {
 public:
  explicit Debouncer(uint32_t ms = 30) : ms_(ms) {}
  // Feed a reading at a time (milliseconds); true when the settled state changed.
  bool update(bool level, uint32_t now) {
    if (level != last_) {
      last_ = level;
      since_ = now;
      return false;
    }
    if (level != stable_ && now - since_ >= ms_) {
      stable_ = level;
      return true;
    }
    return false;
  }
  // Start from a reading (at boot), so the first state is reported as it is.
  void reset(bool level, uint32_t now) {
    last_ = stable_ = level;
    since_ = now;
  }
  bool state() const { return stable_; }

 private:
  uint32_t ms_;
  bool last_ = false, stable_ = false;
  uint32_t since_ = 0;
};

// The line for an input: with the pin pulled up, a closed contact reads LOW.
inline std::string inputLine(const char* name, bool pinHigh) {
  return std::string(name) + (pinHigh ? "=open" : "=closed");
}

// ---- the strip ----

// What the strip shows now: a colour, blinking or not (500 ms on, 500 ms off).
struct Strip {
  Rgb color;
  bool blink = false;
  uint8_t brightness = 80;
  Rgb at(uint32_t now) const {
    if (blink && (now / 500) % 2 == 1) return Rgb{};
    return color;
  }
  // Apply a command (the rest are ignored); true when the strip changed.
  bool apply(const Command& c) {
    switch (c.kind) {
      case Kind::Rgb:
      case Kind::Room:
        color = c.rgb;
        blink = false;
        return true;
      case Kind::Blink:
        color = c.rgb;
        blink = true;
        return true;
      case Kind::Brightness:
        brightness = c.brightness;
        return true;
      default:
        return false;
    }
  }
};

}  // namespace akashic
