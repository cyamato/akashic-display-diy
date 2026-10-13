// The node's GraphQL endpoint: POST /graphql over HTTPS with its own certificate. A caller
// needs the node's token, and an allowed address: the server's (sent at hello) or, with
// ALLOW_LAN, the shop's own network. Answers come from a snapshot, so a slow caller never
// holds up counting.
// Arduino first: its headers and lwIP's must come in this order.
#include "app.h"

#include <esp_https_server.h>
#include <lwip/sockets.h>

#include "graphql.h"

namespace node {

namespace {

httpd_handle_t server = nullptr;

/** The caller's IPv4 address (0 when it can't be read). */
uint32_t peer(httpd_req_t* req) {
  sockaddr_storage addr;
  socklen_t len = sizeof addr;
  if (getpeername(httpd_req_to_sockfd(req), reinterpret_cast<sockaddr*>(&addr), &len) != 0) return 0;
  if (addr.ss_family == AF_INET) return ntohl(reinterpret_cast<sockaddr_in*>(&addr)->sin_addr.s_addr);
#if LWIP_IPV6
  if (addr.ss_family == AF_INET6) {
    const auto* a6 = reinterpret_cast<sockaddr_in6*>(&addr);
    const uint8_t* b = reinterpret_cast<const uint8_t*>(&a6->sin6_addr);
    // An IPv4 address mapped into IPv6 (::ffff:a.b.c.d).
    static const uint8_t kMapped[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0xFF};
    if (memcmp(b, kMapped, 12) == 0) return (uint32_t(b[12]) << 24) | (uint32_t(b[13]) << 16) | (uint32_t(b[14]) << 8) | b[15];
  }
#endif
  return 0;
}

esp_err_t reply(httpd_req_t* req, const char* status, const std::string& body) {
  httpd_resp_set_status(req, status);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  return httpd_resp_send(req, body.c_str(), body.size());
}

bool tokenOk(httpd_req_t* req) {
  char header[160] = {0};
  if (httpd_req_get_hdr_value_str(req, "Authorization", header, sizeof header) != ESP_OK) return false;
  const String expected = String("Bearer ") + identity.token;
  // Compare every character, so the time taken says nothing about how much matched.
  const size_t n = strlen(header);
  if (n != expected.length()) return false;
  uint8_t diff = 0;
  for (size_t i = 0; i < n; i++) diff |= static_cast<uint8_t>(header[i] ^ expected[i]);
  return diff == 0;
}

esp_err_t graphqlPost(httpd_req_t* req) {
  bool allowed;
  {
    std::lock_guard<std::mutex> hold(lock);
    allowed = allow.allowed(peer(req));
  }
  if (!allowed) return reply(req, "403 Forbidden", "{\"errors\":[{\"message\":\"Not from an allowed address\"}]}");
  if (!tokenOk(req)) return reply(req, "401 Unauthorized", "{\"errors\":[{\"message\":\"Send the node's token: Authorization: Bearer <token>\"}]}");
  if (req->content_len == 0 || req->content_len > akashic::gql::kMaxQuery * 2) {
    return reply(req, "400 Bad Request", "{\"errors\":[{\"message\":\"Send a GraphQL request as JSON: {\\\"query\\\": \\\"…\\\"}\"}]}");
  }
  std::string body(req->content_len, '\0');
  size_t got = 0;
  while (got < body.size()) {
    const int n = httpd_req_recv(req, &body[got], body.size() - got);
    if (n == HTTPD_SOCK_ERR_TIMEOUT) continue;
    if (n <= 0) return ESP_FAIL;
    got += static_cast<size_t>(n);
  }
  std::string query;
  if (!akashic::gql::queryFromBody(body, query)) {
    return reply(req, "400 Bad Request", "{\"errors\":[{\"message\":\"Send a GraphQL request as JSON: {\\\"query\\\": \\\"…\\\"}\"}]}");
  }
  return reply(req, "200 OK", answer(query));
}

}  // namespace

bool startGraphqlServer() {
  httpd_ssl_config_t conf = HTTPD_SSL_CONFIG_DEFAULT();
  // ESP-IDF 4.4 calls the server's own certificate `cacert`.
  conf.cacert_pem = reinterpret_cast<const uint8_t*>(identity.certPem.c_str());
  conf.cacert_len = identity.certPem.length() + 1;
  conf.prvtkey_pem = reinterpret_cast<const uint8_t*>(identity.keyPem.c_str());
  conf.prvtkey_len = identity.keyPem.length() + 1;
  conf.port_secure = GRAPHQL_PORT;
  // Few callers (the server, and perhaps one on the shop's network): keep memory free.
  conf.httpd.max_open_sockets = 3;
  conf.httpd.lru_purge_enable = true;
  if (httpd_ssl_start(&server, &conf) != ESP_OK) return false;
  httpd_uri_t post = {};
  post.uri = "/graphql";
  post.method = HTTP_POST;
  post.handler = graphqlPost;
  httpd_register_uri_handler(server, &post);
  Serial.printf("GraphQL endpoint: https://<this node>:%d/graphql\n", GRAPHQL_PORT);
  return true;
}

}  // namespace node
