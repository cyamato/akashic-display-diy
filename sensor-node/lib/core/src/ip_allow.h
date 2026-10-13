// Who may call the node's GraphQL endpoint: our servers' addresses (the list the server
// sends at hello, `egress_ips`: addresses or CIDR ranges) and, optionally, the shop's own
// network (private IPv4 ranges). A second check behind the firewall's NAT rule. IPv4 only.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace akashic {

class IpAllow {
 public:
  /** Replace the allowed list; entries that don't parse are skipped. @return How many were kept. */
  size_t set(const std::vector<std::string>& entries);
  /** Let private addresses (10/8, 172.16/12, 192.168/16, 127/8) in too. */
  void allowLocal(bool on) { local_ = on; }
  bool allowed(uint32_t address) const;
  bool allowed(const std::string& address) const;

  /** Parse "a.b.c.d" (host order). */
  static bool parse(const std::string& text, uint32_t& out);
  static bool isPrivate(uint32_t address);

 private:
  struct Range {
    uint32_t base;
    uint32_t mask;
  };
  std::vector<Range> ranges_;
  bool local_ = false;
};

}  // namespace akashic
