// The connection to the Akashic Display server, in a task of its own:
// - hello at start and every `hello_seconds`: the node's name and, with DIRECT_URL, its
//   direct address, certificate and token. The server says whether it reached the node
//   directly and, if not, to open the tunnel; and which addresses may call the node.
// - people in and out within a few seconds of walking through; readings every minute;
// - the daily reset;
// - the tunnel, only when the server can't reach the node directly: an event stream the
//   server's questions arrive on, each answered with a POST.
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <time.h>

#include "app.h"
#include "graphql.h"
#include "sse.h"

namespace node {

namespace {

/** What the server said at hello. */
struct Plan {
  bool tunnel = true;
  std::string tunnelUrl;
  uint32_t reportSeconds = 60;
  uint32_t helloSeconds = 900;
} plan;

const String kAuth = String("Bearer ") + SENSOR_KEY;

/** Call the server; the HTTP status (negative: no answer). */
int call(const char* method, const String& url, const std::string& body, std::string* out = nullptr) {
  WiFiClientSecure tls;
  tls.setCACert(SERVER_ROOT_CA);
  HTTPClient http;
  http.setTimeout(10000);
  if (!http.begin(tls, url)) return -1;
  http.addHeader("Authorization", kAuth);
  http.addHeader("Content-Type", "application/json");
  const int status = http.sendRequest(method, reinterpret_cast<uint8_t*>(const_cast<char*>(body.data())), body.size());
  if (out && status > 0) *out = http.getString().c_str();
  http.end();
  return status;
}

uint32_t number(const akashic::gql::Value& v, const char* name, uint32_t fallback) {
  const auto* f = v.field(name);
  return f && f->kind == akashic::gql::Value::Kind::Number && f->number >= 10 ? static_cast<uint32_t>(f->number) : fallback;
}

bool hello() {
  const bool direct = DIRECT_URL[0] != '\0' && !identity.certPem.isEmpty();
  const std::string body = akashic::helloBody(NODE_NAME, FIRMWARE_VERSION, direct ? DIRECT_URL : "", identity.certPem.c_str(), identity.token.c_str());
  std::string out;
  const int status = call("PUT", String(SERVER_URL) + "/v1/sensors/hello", body, &out);
  akashic::gql::Value reply;
  if (status != 200 || !akashic::gql::parseJson(out, reply)) {
    Serial.printf("Hello failed (HTTP %d): %s\n", status, out.c_str());
    return false;
  }
  std::vector<std::string> egress;
  if (const auto* list = reply.field("egress_ips")) {
    for (const auto& item : list->items) egress.push_back(item.text);
  }
  {
    std::lock_guard<std::mutex> hold(lock);
    allow.set(egress);
  }
  const auto* tunnel = reply.field("tunnel");
  const auto* url = reply.field("tunnel_url");
  plan.tunnel = !tunnel || tunnel->flag;
  if (url && !url->text.empty()) plan.tunnelUrl = url->text;
  plan.reportSeconds = number(reply, "report_seconds", 60);
  plan.helloSeconds = number(reply, "hello_seconds", 900);
  const auto* endpoint = reply.field("endpoint");
  Serial.printf("Hello: %s; reached %s\n", endpoint ? endpoint->text.c_str() : "?", plan.tunnel ? "through the tunnel" : "directly");
  return true;
}

void sendCounts() {
  uint32_t in, out;
  {
    std::lock_guard<std::mutex> hold(lock);
    in = pendingIn;
    out = pendingOut;
    pendingIn = pendingOut = 0;
  }
  if (!in && !out) return;
  if (call("PUT", String(SERVER_URL) + "/v1/sites/occupancy", akashic::occupancyBody(in, out)) != 200) {
    // Keep them for the next try.
    std::lock_guard<std::mutex> hold(lock);
    pendingIn += in;
    pendingOut += out;
  }
}

void sendReadings() {
  std::string body;
  {
    std::lock_guard<std::mutex> hold(lock);
    body = akashic::readingsBody(state);
  }
  if (!body.empty()) call("PUT", String(SERVER_URL) + "/v1/sites/readings", body);
}

/** At RESET_AT local time, once a day. */
void dailyReset() {
  static int doneOnDay = -1;
  struct tm now;
  if (!getLocalTime(&now, 0) || now.tm_year < 120) return;
  int h = 0, m = 0;
  if (sscanf(RESET_AT, "%d:%d", &h, &m) != 2 || now.tm_hour != h || now.tm_min != m || now.tm_yday == doneOnDay) return;
  doneOnDay = now.tm_yday;
  resetCounts();
  if (RESET_SITE) call("PUT", String(SERVER_URL) + "/v1/sites/occupancy", "{\"count\":0}");
  Serial.println("Counts reset for the day");
}

/** The tunnel's event stream, read a little at a time. */
class Tunnel {
 public:
  bool isOpen() { return open_ && tls_.connected(); }

  void close() {
    if (open_) Serial.println("Tunnel closed");
    tls_.stop();
    open_ = false;
  }

  bool connect() {
    akashic::UrlParts u;
    if (!akashic::splitUrl(plan.tunnelUrl, u)) return false;
    tls_.stop();
    tls_.setCACert(SERVER_ROOT_CA);
    if (!tls_.connect(u.host.c_str(), u.port)) return false;
    const std::string path = u.path + (u.path.find('?') == std::string::npos ? "?" : "&") + "node=" + NODE_NAME;
    tls_.printf("GET %s HTTP/1.1\r\nHost: %s\r\nAuthorization: %s\r\nAccept: text/event-stream\r\nCache-Control: no-cache\r\n\r\n",
                path.c_str(), u.host.c_str(), kAuth.c_str());
    // The status line and headers.
    String line = readLine();
    if (line.indexOf(" 200") < 0) {
      Serial.printf("Tunnel refused: %s\n", line.c_str());
      tls_.stop();
      return false;
    }
    chunked_ = false;
    while ((line = readLine()).length() > 0) {
      line.toLowerCase();
      if (line.startsWith("transfer-encoding:") && line.indexOf("chunked") >= 0) chunked_ = true;
    }
    chunk_ = akashic::ChunkedReader();
    sse_ = akashic::SseReader();
    heard_ = millis();
    open_ = true;
    Serial.println("Tunnel open");
    return true;
  }

  /** Read what has arrived; answer any questions. */
  void service() {
    while (tls_.available()) {
      const char c = static_cast<char>(tls_.read());
      heard_ = millis();
      if (!chunked_) {
        take(c);
        continue;
      }
      char data;
      if (chunk_.feed(c, data)) take(data);
      if (chunk_.finished()) {
        close();
        return;
      }
    }
    // The server comments every 25 seconds; silence for much longer means it's gone.
    if (millis() - heard_ > 90000) close();
  }

 private:
  WiFiClientSecure tls_;
  akashic::ChunkedReader chunk_;
  akashic::SseReader sse_;
  bool chunked_ = false;
  bool open_ = false;
  uint32_t heard_ = 0;

  String readLine() {
    String line;
    const uint32_t start = millis();
    while (millis() - start < 10000) {
      if (!tls_.available()) {
        delay(5);
        continue;
      }
      const char c = static_cast<char>(tls_.read());
      if (c == '\n') return line;
      if (c != '\r') line += c;
    }
    return line;
  }

  void take(char c) {
    if (!sse_.feed(c) || sse_.event().event != "ask" || sse_.event().id.empty()) return;
    const akashic::SseEvent ev = sse_.event();
    std::string query;
    const std::string result = akashic::gql::queryFromBody(ev.data, query) ? answer(query) : "{\"errors\":[{\"message\":\"Send a GraphQL request as JSON\"}]}";
    call("POST", String(plan.tunnelUrl.c_str()) + "/" + ev.id.c_str(), result);
  }
};

Tunnel tunnel;

void task(void*) {
  uint32_t lastHello = 0, lastReadings = 0, lastCounts = 0, nextTunnelTry = 0, tunnelWait = 5000;
  bool greeted = false;
  for (;;) {
    if (!online()) {
      tunnel.close();
      delay(1000);
      continue;
    }
    const uint32_t now = millis();
    // Until the first hello works, try again every 30 seconds.
    if (lastHello == 0 || now - lastHello >= (greeted ? plan.helloSeconds : 30) * 1000UL) {
      lastHello = now ? now : 1;
      greeted = hello() || greeted;
    }
    if (now - lastCounts >= 2000) {
      lastCounts = now;
      sendCounts();
    }
    if (now - lastReadings >= plan.reportSeconds * 1000UL) {
      lastReadings = now;
      sendReadings();
    }
    dailyReset();
    if (plan.tunnel && !plan.tunnelUrl.empty()) {
      if (tunnel.isOpen()) {
        tunnel.service();
      } else if (millis() >= nextTunnelTry) {
        // Back off while the server can't be reached: 5 seconds, doubling to 2 minutes.
        if (tunnel.connect()) {
          tunnelWait = 5000;
        } else {
          nextTunnelTry = millis() + tunnelWait;
          tunnelWait = tunnelWait >= 120000 ? 120000 : tunnelWait * 2;
        }
      }
    } else if (tunnel.isOpen()) {
      tunnel.close();
    }
    delay(20);
  }
}

}  // namespace

void startCloud() { xTaskCreatePinnedToCore(task, "cloud", 16384, nullptr, 1, nullptr, 0); }

}  // namespace node
