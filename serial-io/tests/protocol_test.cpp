// Tests for protocol.h, built and run on a computer: g++ -std=c++17 tests/protocol_test.cpp
#include <cstdio>
#include <string>

#include "../protocol.h"

using namespace akashic;

static int failures = 0;
#define CHECK(cond)                                                    \
  do {                                                                 \
    if (!(cond)) {                                                     \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);      \
      failures++;                                                      \
    }                                                                  \
  } while (0)

static void colours() {
  Rgb c;
  CHECK(parseColor("#FF8000", c) && c == (Rgb{255, 128, 0}));
  CHECK(parseColor("Red", c) && c == (Rgb{255, 0, 0}));
  CHECK(parseColor("off", c) && c == Rgb{});
  CHECK(!parseColor("#ff80", c));
  CHECK(!parseColor("#gg0000", c));
  CHECK(!parseColor("mauve", c));
  CHECK(roomColor("open", c) && c == (Rgb{0, 200, 0}));
  CHECK(roomColor("booked", c) && c == (Rgb{255, 150, 0}));
  CHECK(roomColor("occupied", c) && c == (Rgb{255, 0, 0}));
  CHECK(!roomColor("gone", c));
}

static void commands() {
  Command c = parseCommand("{\"state\":\"occupied\",\"word\":\"Busy\",\"until\":null}");
  CHECK(c.kind == Kind::Room && c.rgb == (Rgb{255, 0, 0}));
  CHECK(parseCommand("{\"state\":\"nope\"}").kind == Kind::None);
  CHECK(parseCommand("{\"word\":\"x\"}").kind == Kind::None);
  CHECK(parseCommand("{\"state\":\"open").kind == Kind::None);
  c = parseCommand(" rgb = #00ff00 \r");
  CHECK(c.kind == Kind::Rgb && c.rgb == (Rgb{0, 255, 0}));
  c = parseCommand("blink=amber");
  CHECK(c.kind == Kind::Blink && c.rgb == (Rgb{255, 150, 0}));
  CHECK(parseCommand("rgb=mauve").kind == Kind::None);
  c = parseCommand("brightness=200");
  CHECK(c.kind == Kind::Brightness && c.brightness == 200);
  CHECK(parseCommand("brightness=300").kind == Kind::None);
  CHECK(parseCommand("brightness=lots").kind == Kind::None);
  CHECK(parseCommand("brightness=").kind == Kind::None);
  c = parseCommand("relay=pulse");
  CHECK(c.kind == Kind::Output && c.name == "relay" && c.action == OutputAction::Pulse);
  CHECK(parseCommand("buzzer=on").action == OutputAction::On);
  CHECK(parseCommand("buzzer=off").kind == Kind::Output);
  CHECK(parseCommand("buzzer=loud").kind == Kind::None);
  CHECK(parseCommand("OCCUPIED").kind == Kind::None);
  CHECK(parseCommand("=on").kind == Kind::None);
  CHECK(parseCommand("   ").kind == Kind::None);
}

static void lines() {
  LineReader r(8);
  std::string got;
  for (char ch : std::string("rgb=red\r\n")) {
    if (r.add(ch)) got = r.line();
  }
  CHECK(got == "rgb=red");
  bool any = false;
  for (char ch : std::string("much-too-long\nok\n")) {
    if (r.add(ch)) {
      any = true;
      got = r.line();
    }
  }
  CHECK(any && got == "ok");
}

static void debouncing() {
  Debouncer d(30);
  d.reset(true, 0);
  CHECK(d.state());
  CHECK(!d.update(false, 10));  // starts to change
  CHECK(!d.update(true, 15));   // bounced back
  CHECK(!d.update(false, 20));
  CHECK(!d.update(false, 40));  // not held long enough
  CHECK(d.update(false, 50));   // held 30 ms: closed
  CHECK(!d.state());
  CHECK(!d.update(false, 90));  // no change
  CHECK(inputLine("door", true) == "door=open");
  CHECK(inputLine("door", false) == "door=closed");
}

static void strip() {
  Strip s;
  CHECK(s.apply(parseCommand("blink=red")));
  CHECK(s.at(100) == (Rgb{255, 0, 0}));
  CHECK(s.at(600) == Rgb{});
  CHECK(s.apply(parseCommand("{\"state\":\"open\"}")));
  CHECK(!s.blink && s.at(600) == (Rgb{0, 200, 0}));
  CHECK(s.apply(parseCommand("brightness=10")) && s.brightness == 10);
  CHECK(s.apply(parseCommand("rgb=off")) && s.at(0) == Rgb{});
  CHECK(!s.apply(parseCommand("relay=on")));
}

int main() {
  colours();
  commands();
  lines();
  debouncing();
  strip();
  if (failures) {
    std::printf("%d check(s) failed\n", failures);
    return 1;
  }
  std::printf("protocol: all checks passed\n");
  return 0;
}
