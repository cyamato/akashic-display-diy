#include "node_state.h"

#include <cstdio>

namespace akashic {

uint32_t NodeState::entered() const {
  uint32_t n = 0;
  for (const auto& d : doors) n += d.entered;
  return n;
}

uint32_t NodeState::left() const {
  uint32_t n = 0;
  for (const auto& d : doors) n += d.left;
  return n;
}

uint32_t NodeState::present() const { return entered() > left() ? entered() - left() : 0; }

namespace {

gql::Value maybe(float v) { return std::isfinite(v) ? gql::Value::num(v) : gql::Value::null(); }

void field(std::string& out, const char* name, float v) {
  if (!std::isfinite(v)) return;
  char buf[48];
  std::snprintf(buf, sizeof buf, "%s\"%s\":%.2f", out.size() > 1 ? "," : "", name, static_cast<double>(v));
  out += buf;
}

}  // namespace

gql::Value snapshot(const NodeState& s) {
  using gql::Value;
  Value doors = Value::list();
  for (const auto& d : s.doors) {
    doors.add(Value::object("Door").set("name", Value::str(d.name)).set("entered", Value::num(d.entered)).set("left", Value::num(d.left)));
  }
  return Value::object("Query")
      .set("node", Value::object("Node")
                       .set("name", Value::str(s.name))
                       .set("firmware", Value::str(s.firmware))
                       .set("uptime", Value::num(s.uptimeSeconds))
                       .set("network", Value::str(s.network)))
      .set("occupancy", Value::object("Occupancy")
                            .set("entered", Value::num(s.entered()))
                            .set("left", Value::num(s.left()))
                            .set("present", Value::num(s.present()))
                            .set("doors", doors))
      .set("air", Value::object("Air")
                      .set("co2", maybe(s.co2))
                      .set("temperature", maybe(s.temperature))
                      .set("humidity", maybe(s.humidity))
                      .set("pm1", maybe(s.pm1))
                      .set("pm2_5", maybe(s.pm2_5))
                      .set("pm10", maybe(s.pm10))
                      .set("light", maybe(s.light))
                      .set("age", s.readingsAge < 0 ? Value::null() : Value::num(s.readingsAge)));
}

std::string readingsBody(const NodeState& s) {
  std::string out = "{";
  field(out, "co2", s.co2);
  field(out, "temperature", s.temperature);
  field(out, "humidity", s.humidity);
  field(out, "pm1", s.pm1);
  field(out, "pm2_5", s.pm2_5);
  field(out, "pm10", s.pm10);
  field(out, "light", s.light);
  return out.size() == 1 ? "" : out + "}";
}

std::string occupancyBody(uint32_t in, uint32_t out) {
  char buf[64];
  std::snprintf(buf, sizeof buf, "{\"in\":%lu,\"out\":%lu}", static_cast<unsigned long>(in), static_cast<unsigned long>(out));
  return buf;
}

std::string helloBody(const std::string& node, const std::string& firmware, const std::string& directUrl, const std::string& certPem, const std::string& token) {
  std::string out = "{\"node\":" + gql::quote(node) + ",\"firmware\":" + gql::quote(firmware);
  if (!directUrl.empty()) {
    out += ",\"direct\":{\"url\":" + gql::quote(directUrl) + ",\"cert_pem\":" + gql::quote(certPem) + ",\"token\":" + gql::quote(token) + "}";
  }
  return out + "}";
}

}  // namespace akashic
