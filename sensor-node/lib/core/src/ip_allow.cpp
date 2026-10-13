#include "ip_allow.h"

#include <cctype>

namespace akashic {

bool IpAllow::parse(const std::string& text, uint32_t& out) {
  uint32_t value = 0;
  int parts = 0;
  size_t i = 0;
  while (parts < 4) {
    if (i >= text.size() || !std::isdigit(static_cast<unsigned char>(text[i]))) return false;
    uint32_t part = 0;
    int digits = 0;
    while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i]))) {
      part = part * 10 + static_cast<uint32_t>(text[i++] - '0');
      if (++digits > 3 || part > 255) return false;
    }
    value = (value << 8) | part;
    if (++parts < 4) {
      if (i >= text.size() || text[i] != '.') return false;
      i++;
    }
  }
  if (i != text.size()) return false;
  out = value;
  return true;
}

bool IpAllow::isPrivate(uint32_t a) {
  return (a >> 24) == 10 || (a >> 20) == ((172u << 4) | 1) || (a >> 16) == ((192u << 8) | 168) || (a >> 24) == 127;
}

size_t IpAllow::set(const std::vector<std::string>& entries) {
  ranges_.clear();
  for (const std::string& e : entries) {
    const size_t slash = e.find('/');
    uint32_t base;
    if (!parse(e.substr(0, slash), base)) continue;
    int bits = 32;
    if (slash != std::string::npos) {
      const std::string b = e.substr(slash + 1);
      if (b.empty() || b.size() > 2 || !std::isdigit(static_cast<unsigned char>(b[0])) || (b.size() == 2 && !std::isdigit(static_cast<unsigned char>(b[1])))) continue;
      bits = std::stoi(b);
      if (bits > 32) continue;
    }
    const uint32_t mask = bits == 0 ? 0 : 0xFFFFFFFFu << (32 - bits);
    ranges_.push_back({base & mask, mask});
  }
  return ranges_.size();
}

bool IpAllow::allowed(uint32_t address) const {
  if (local_ && isPrivate(address)) return true;
  for (const Range& r : ranges_) {
    if ((address & r.mask) == r.base) return true;
  }
  return false;
}

bool IpAllow::allowed(const std::string& address) const {
  uint32_t a;
  return parse(address, a) && allowed(a);
}

}  // namespace akashic
