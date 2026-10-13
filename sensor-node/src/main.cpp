// Akashic Display sensor node: counts people through up to two doorways, measures CO2,
// temperature, humidity, particles and light, reports them to the server, and answers
// GraphQL (directly over HTTPS, or through the server's tunnel).
#include "app.h"

#if NET_ETHERNET
#include <ETH.h>
#else
#include <WiFi.h>
#endif

namespace node {

std::mutex lock;
akashic::NodeState state;
uint32_t pendingIn = 0, pendingOut = 0;
akashic::IpAllow allow;
Identity identity;

bool online() {
#if NET_ETHERNET
  return ETH.linkUp() && static_cast<uint32_t>(ETH.localIP()) != 0;
#else
  return WiFi.status() == WL_CONNECTED;
#endif
}

std::string answer(const std::string& query) {
  akashic::gql::Value root;
  {
    std::lock_guard<std::mutex> hold(lock);
    state.uptimeSeconds = millis() / 1000;
    root = akashic::snapshot(state);
  }
  return akashic::gql::execute(query, root);
}

}  // namespace node

using namespace node;

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.printf("\nAkashic Display sensor node %s (%s)\n", FIRMWARE_VERSION, NODE_NAME);
  state.name = NODE_NAME;
  state.firmware = FIRMWARE_VERSION;
#if NET_ETHERNET
  state.network = "ethernet";
  ETH.begin();
#else
  state.network = "wifi";
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
#endif
  configTzTime(TZ_POSIX, NTP_SERVER);
  allow.allowLocal(ALLOW_LAN);
  if (!loadIdentity()) Serial.println("Could not make the node's certificate; its GraphQL endpoint is off");
  startSensing();
  for (int i = 0; i < 300 && !online(); i++) delay(100);
  if (!identity.certPem.isEmpty() && !startGraphqlServer()) Serial.println("Could not start the GraphQL endpoint");
  startCloud();
}

void loop() {
  sense();
  delay(1);
}
