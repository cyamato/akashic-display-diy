// Tests for the sensor node's core library, built and run on a computer (see ../test.sh).
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "../lib/core/src/door_counter.h"
#include "../lib/core/src/graphql.h"
#include "../lib/core/src/ip_allow.h"
#include "../lib/core/src/node_state.h"
#include "../lib/core/src/pms5003.h"
#include "../lib/core/src/sse.h"

using namespace akashic;

static int failures = 0;
#define CHECK(cond)                                                    \
  do {                                                                 \
    if (!(cond)) {                                                     \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);      \
      failures++;                                                      \
    }                                                                  \
  } while (0)
#define CHECK_EQ(a, b)                                                                          \
  do {                                                                                          \
    const std::string x_ = (a), y_ = (b);                                                       \
    if (x_ != y_) {                                                                             \
      std::printf("FAIL %s:%d:\n  got  %s\n  want %s\n", __FILE__, __LINE__, x_.c_str(), y_.c_str()); \
      failures++;                                                                               \
    }                                                                                           \
  } while (0)

// ---- Door counter ----

/** A frame: the floor 2400 mm away, and a person (1700 mm tall) over 2x2 zones at `row`. */
struct Frame {
  uint16_t d[64];
  bool v[64];
  explicit Frame(float row = -1, int col = 3, uint16_t floor = 2400) {
    for (int z = 0; z < 64; z++) {
      d[z] = floor;
      v[z] = true;
    }
    if (row < 0) return;
    const int r = static_cast<int>(row);
    for (int dr = 0; dr < 2; dr++) {
      for (int dc = 0; dc < 2; dc++) {
        const int rr = r + dr, cc = col + dc;
        if (rr < 8 && cc < 8) d[rr * 8 + cc] = 700;
      }
    }
  }
};

static DoorCounter learnt(const DoorConfig& cfg = DoorConfig()) {
  DoorCounter c(cfg);
  Frame empty;
  for (int i = 0; i < cfg.learnFrames; i++) c.update(empty.d, empty.v);
  return c;
}

/** Walk through rows (one frame each), then leave; the sum of what was counted. */
static DoorEvent walk(DoorCounter& c, const std::vector<int>& rows, int col = 3) {
  DoorEvent total;
  for (int r : rows) {
    Frame f(static_cast<float>(r), col);
    const DoorEvent e = c.update(f.d, f.v);
    total.in = static_cast<int8_t>(total.in + e.in);
    total.out = static_cast<int8_t>(total.out + e.out);
  }
  Frame empty;
  for (int i = 0; i < 3; i++) {
    const DoorEvent e = c.update(empty.d, empty.v);
    total.in = static_cast<int8_t>(total.in + e.in);
    total.out = static_cast<int8_t>(total.out + e.out);
  }
  return total;
}

static void doorCounter() {
  // Nothing counts while the floor is being learnt.
  DoorCounter fresh;
  Frame person(2);
  CHECK(!fresh.ready());
  CHECK(fresh.update(person.d, person.v).in == 0);

  DoorCounter c = learnt();
  CHECK(c.ready());
  CHECK(c.floorMm(0) == 2400);
  // Outside to inside is in; back again is out.
  DoorEvent e = walk(c, {0, 1, 2, 3, 4, 5, 6});
  CHECK(e.in == 1 && e.out == 0);
  e = walk(c, {6, 5, 4, 3, 2, 1, 0});
  CHECK(e.in == 0 && e.out == 1);
  // Someone who turns back, or doesn't cross far enough, isn't counted.
  e = walk(c, {0, 1, 2, 3, 2, 1, 0});
  CHECK(e.in == 0 && e.out == 0);
  e = walk(c, {2, 3, 4});
  CHECK(e.in == 0 && e.out == 0);
  // One missed frame doesn't split a walk in two.
  {
    Frame a(1), b(5), empty;
    CHECK(c.update(a.d, a.v).in == 0);
    CHECK(c.update(empty.d, empty.v).in == 0);
    CHECK(c.present());
    CHECK(c.update(b.d, b.v).in == 0);
    DoorEvent total;
    for (int i = 0; i < 2; i++) total.in = static_cast<int8_t>(total.in + c.update(empty.d, empty.v).in);
    CHECK(total.in == 1);
  }
  // Flipped: row 0 is inside.
  DoorConfig flipped;
  flipped.flip = true;
  DoorCounter f = learnt(flipped);
  e = walk(f, {0, 1, 2, 3, 4, 5, 6});
  CHECK(e.in == 0 && e.out == 1);
  // Only this doorway's columns count.
  DoorConfig left;
  left.lastColumn = 3;
  DoorCounter l = learnt(left);
  e = walk(l, {0, 2, 4, 6}, 5);
  CHECK(e.in == 0 && e.out == 0);
  e = walk(l, {0, 2, 4, 6}, 1);
  CHECK(e.in == 1);
  // One noisy zone, or zones with no valid target, are not a person.
  {
    DoorCounter n = learnt();
    Frame noisy;
    noisy.d[10] = 500;
    for (int i = 0; i < 5; i++) CHECK(n.update(noisy.d, noisy.v).in == 0);
    CHECK(!n.present());
    Frame invalid(3);
    for (int z = 0; z < 64; z++) invalid.v[z] = false;
    CHECK(n.update(invalid.d, invalid.v).in == 0);
    CHECK(!n.present());
  }
  // The floor follows slow changes while nobody is there.
  {
    DoorCounter s = learnt();
    Frame mat(-1, 3, 2300);
    for (int i = 0; i < 400; i++) s.update(mat.d, mat.v);
    CHECK(s.floorMm(0) < 2310 && s.floorMm(0) >= 2299);
  }
}

// ---- GraphQL ----

static gql::Value root() {
  NodeState s;
  s.name = "door-a";
  s.firmware = "0.1.0";
  s.uptimeSeconds = 125;
  s.network = "ethernet";
  s.doors = {{"front", 12, 5}, {"side", 3, 4}};
  s.co2 = 612;
  s.temperature = 22.25f;
  s.readingsAge = 4;
  return snapshot(s);
}

static void graphql() {
  const gql::Value r = root();
  CHECK_EQ(gql::execute("{ air { co2 temperature humidity } }", r), R"({"data":{"air":{"co2":612,"temperature":22.25,"humidity":null}}})");
  CHECK_EQ(gql::execute("query Door($x: Int = 1) { occupancy { present, doors { name entered } } }", r),
           R"({"data":{"occupancy":{"present":6,"doors":[{"name":"front","entered":12},{"name":"side","entered":3}]}}})");
  CHECK_EQ(gql::execute("# comment\n{ n: node { name, kind: __typename } __typename }", r), R"({"data":{"n":{"name":"door-a","kind":"Node"},"__typename":"Query"}})");
  CHECK_EQ(gql::execute("{ node { uptime network } air { age } }", r), R"({"data":{"node":{"uptime":125,"network":"ethernet"},"air":{"age":4}}})");
  // Errors, as GraphQL servers word them.
  CHECK_EQ(gql::execute("{ air { co3 } }", r), R"({"errors":[{"message":"Cannot query field \"co3\" on type \"Air\"."}]})");
  CHECK_EQ(gql::execute("{ air }", r), R"({"errors":[{"message":"Field \"air\" of type \"Air\" must have a selection of subfields."}]})");
  CHECK_EQ(gql::execute("{ node { name { x } } }", r), R"({"errors":[{"message":"Field \"name\" must not have a selection since it has no subfields."}]})");
  CHECK_EQ(gql::execute("mutation { reset }", r), R"({"errors":[{"message":"Only queries are supported"}]})");
  CHECK_EQ(gql::execute("{ ...F }", r), R"({"errors":[{"message":"Fragments are not supported"}]})");
  CHECK_EQ(gql::execute("fragment F on Air { co2 }", r), R"({"errors":[{"message":"Fragments are not supported"}]})");
  CHECK_EQ(gql::execute("{ air(site: 1) { co2 } }", r), R"({"errors":[{"message":"Field \"air\" takes no arguments"}]})");
  CHECK_EQ(gql::execute("{ air @skip(if: true) { co2 } }", r), R"({"errors":[{"message":"Directives are not supported"}]})");
  CHECK_EQ(gql::execute("{ }", r), R"({"errors":[{"message":"Syntax Error: Expected Name, found \"}\""}]})");
  CHECK_EQ(gql::execute("{ air { co2 }", r), R"({"errors":[{"message":"Syntax Error: Expected \"}\""}]})");
  CHECK_EQ(gql::execute("{ air { co2 } } { node { name } }", r), R"({"errors":[{"message":"Syntax Error: Unexpected text after the query"}]})");
  CHECK_EQ(gql::execute("{ a { b { c { d { e { f { g { h { i } } } } } } } } }", r), R"({"errors":[{"message":"The query is nested too deeply"}]})");
  CHECK_EQ(gql::execute(std::string(gql::kMaxQuery + 1, ' '), r), R"({"errors":[{"message":"The query is too long"}]})");
  CHECK_EQ(gql::execute("subscription { air { co2 } }", r), R"({"errors":[{"message":"Only queries are supported"}]})");
  CHECK_EQ(gql::execute("{ __typename { x } }", r), R"({"errors":[{"message":"Field \"__typename\" must not have a selection since type \"String\" has no subfields."}]})");
  // Text is escaped.
  CHECK_EQ(gql::quote("a\"b\\c\n\x01"), R"("a\"b\\c\n\u0001")");
}

static void bodies() {
  std::string q;
  CHECK(gql::queryFromBody(R"({"query":"{ air { co2 } }","variables":{"a":[1,{"b":null}]}})", q) && q == "{ air { co2 } }");
  CHECK(gql::queryFromBody(R"( { "operationName": null, "query" : "{ node { name } }\n\u0041" } )", q) && q == "{ node { name } }\nA");
  CHECK(!gql::queryFromBody(R"({"query":5})", q));
  CHECK(!gql::queryFromBody(R"({"q":"x"})", q));
  CHECK(!gql::queryFromBody("{}", q));
  CHECK(!gql::queryFromBody("not json", q));
  CHECK(!gql::queryFromBody(R"({"query":"x")", q));
  CHECK(!gql::queryFromBody(R"({"query":"\u00zz"})", q));

  gql::Value v;
  CHECK(gql::parseJson(R"({"tunnel":true,"egress_ips":["198.51.100.7","203.0.113.0/24"],"report_seconds":60,"x":null,"y":-1.5e2})", v));
  CHECK(v.field("tunnel")->flag);
  CHECK(v.field("egress_ips")->items.size() == 2 && v.field("egress_ips")->items[1].text == "203.0.113.0/24");
  CHECK(v.field("report_seconds")->number == 60);
  CHECK(v.field("x")->kind == gql::Value::Kind::Null);
  CHECK(v.field("y")->number == -150);
  CHECK(!gql::parseJson("{\"a\":1} x", v));
  CHECK(!gql::parseJson("{\"a\":nope}", v));
  CHECK(!gql::parseJson("[1,", v));
}

// ---- PMS5003 ----

static std::vector<uint8_t> pmsFrame(uint16_t pm1, uint16_t pm25, uint16_t pm10, bool goodSum = true) {
  std::vector<uint8_t> f = {0x42, 0x4D, 0x00, 0x1C};
  for (int w = 1; w <= 13; w++) {
    const uint16_t v = w == 4 ? pm1 : w == 5 ? pm25 : w == 6 ? pm10 : static_cast<uint16_t>(w * 100);
    f.push_back(static_cast<uint8_t>(v >> 8));
    f.push_back(static_cast<uint8_t>(v & 0xFF));
  }
  uint16_t sum = 0;
  for (uint8_t b : f) sum = static_cast<uint16_t>(sum + b);
  if (!goodSum) sum++;
  f.push_back(static_cast<uint8_t>(sum >> 8));
  f.push_back(static_cast<uint8_t>(sum & 0xFF));
  return f;
}

static void pms() {
  Pms5003 p;
  int frames = 0;
  // Noise first, a repeated start byte, then a frame.
  for (uint8_t b : std::vector<uint8_t>{0x00, 0x42, 0x42}) frames += p.feed(b);
  std::vector<uint8_t> f = pmsFrame(3, 7, 9);
  for (size_t i = 1; i < f.size(); i++) frames += p.feed(f[i]);
  CHECK(frames == 1);
  CHECK(p.latest().pm1 == 3 && p.latest().pm2_5 == 7 && p.latest().pm10 == 9);
  // A bad checksum is dropped; the next frame reads.
  for (uint8_t b : pmsFrame(50, 60, 70, false)) frames += p.feed(b);
  CHECK(frames == 1 && p.errors() == 1 && p.latest().pm2_5 == 7);
  // A wrong length is dropped at once.
  for (uint8_t b : std::vector<uint8_t>{0x42, 0x4D, 0x00, 0x14}) frames += p.feed(b);
  CHECK(p.errors() == 2);
  for (uint8_t b : pmsFrame(11, 12, 13)) frames += p.feed(b);
  CHECK(frames == 2 && p.latest().pm10 == 13);
}

// ---- Server-Sent Events ----

static void sse() {
  SseReader r;
  std::vector<SseEvent> got;
  const std::string stream = ": keep-alive\n\nevent: ask\r\nid: abc123\ndata: {\"query\":\"{ air { co2 } }\"}\n\ndata: one\ndata: two\n\nretry: 5\n\n";
  for (char c : stream) {
    if (r.feed(c)) got.push_back(r.event());
  }
  CHECK(got.size() == 2);
  CHECK(got[0].event == "ask" && got[0].id == "abc123" && got[0].data == "{\"query\":\"{ air { co2 } }\"}");
  CHECK(got[1].event.empty() && got[1].data == "one\ntwo");
  // A line too long to keep spoils only its own event.
  std::string big = "event: ask\ndata: " + std::string(SseReader::kMaxLine + 10, 'x') + "\n\ndata: ok\n\n";
  got.clear();
  for (char c : big) {
    if (r.feed(c)) got.push_back(r.event());
  }
  CHECK(got.size() == 1 && got[0].data == "ok" && got[0].event.empty());
}

static void chunksAndUrls() {
  ChunkedReader r;
  std::string body;
  const std::string wire = "5\r\nhello\r\nA;ext=1\r\n, world!!!\r\n0\r\n\r\n";
  for (char c : wire) {
    char out;
    if (r.feed(c, out)) body += out;
  }
  CHECK(body == "hello, world!!!" && r.finished() && !r.broken());
  ChunkedReader bad;
  char out;
  for (char c : std::string("zz\r\n")) bad.feed(c, out);
  CHECK(bad.broken() && bad.finished());

  UrlParts u;
  CHECK(splitUrl("https://tunnels.example.com/v1/sensors/tunnel", u) && u.host == "tunnels.example.com" && u.port == 443 && u.path == "/v1/sensors/tunnel");
  CHECK(splitUrl("https://203.0.113.9:8443?x=1#frag", u) && u.host == "203.0.113.9" && u.port == 8443 && u.path == "/?x=1");
  CHECK(splitUrl("https://h.test", u) && u.path == "/");
  for (const char* bad : {"http://h.test/", "https://", "https://user@h.test/", "https://h.test:0/", "https://h.test:99999/", "https://h.test:8a/", "ftp://x"}) CHECK(!splitUrl(bad, u));
}

// ---- Allowed addresses ----

static void ipAllow() {
  uint32_t a;
  CHECK(IpAllow::parse("198.51.100.7", a) && a == ((198u << 24) | (51u << 16) | (100u << 8) | 7u));
  for (const char* bad : {"", "1.2.3", "1.2.3.4.5", "256.1.1.1", "1.2.3.x", "1..2.3", "01234.1.1.1", " 1.2.3.4"}) CHECK(!IpAllow::parse(bad, a));
  IpAllow allow;
  CHECK(allow.set({"198.51.100.7", "203.0.113.0/24", "nope", "1.2.3.4/33", "1.2.3.4/", "2001:db8::1"}) == 2);
  CHECK(allow.allowed("198.51.100.7"));
  CHECK(!allow.allowed("198.51.100.8"));
  CHECK(allow.allowed("203.0.113.200"));
  CHECK(!allow.allowed("203.0.114.1"));
  CHECK(!allow.allowed("192.168.1.20"));
  allow.allowLocal(true);
  CHECK(allow.allowed("192.168.1.20") && allow.allowed("10.0.0.5") && allow.allowed("172.31.255.1") && allow.allowed("127.0.0.1"));
  CHECK(!allow.allowed("172.32.0.1") && !allow.allowed("8.8.8.8"));
  CHECK(allow.set({"0.0.0.0/0"}) == 1 && allow.allowed("8.8.8.8"));
}

// ---- What the node reports ----

static void reports() {
  NodeState s;
  CHECK(readingsBody(s).empty());
  s.co2 = 612;
  s.temperature = 22.256f;
  s.pm2_5 = 8;
  CHECK_EQ(readingsBody(s), R"({"co2":612.00,"temperature":22.26,"pm2_5":8.00})");
  CHECK_EQ(occupancyBody(3, 1), R"({"in":3,"out":1})");
  CHECK_EQ(helloBody("door-a", "0.1.0", "", "", ""), R"({"node":"door-a","firmware":"0.1.0"})");
  CHECK_EQ(helloBody("door-a", "0.1.0", "https://203.0.113.9:8443/graphql", "-----BEGIN CERTIFICATE-----\nAB\n", "tok"),
           R"({"node":"door-a","firmware":"0.1.0","direct":{"url":"https://203.0.113.9:8443/graphql","cert_pem":"-----BEGIN CERTIFICATE-----\nAB\n","token":"tok"}})");
  // More out than in (a missed entry) is never fewer than nobody.
  s.doors = {{"front", 1, 3}};
  CHECK(s.present() == 0);
}

int main() {
  doorCounter();
  graphql();
  bodies();
  pms();
  sse();
  chunksAndUrls();
  ipAllow();
  reports();
  if (failures) {
    std::printf("%d check(s) failed\n", failures);
    return 1;
  }
  std::printf("sensor-node core: all checks passed\n");
  return 0;
}
