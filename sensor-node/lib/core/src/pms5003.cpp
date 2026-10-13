#include "pms5003.h"

namespace akashic {

namespace {
uint16_t word(const uint8_t* b, size_t at) { return static_cast<uint16_t>((b[at] << 8) | b[at + 1]); }
}  // namespace

bool Pms5003::feed(uint8_t byte) {
  // Wait for the two start bytes.
  if (len_ == 0 && byte != 0x42) return false;
  if (len_ == 1 && byte != 0x4D) {
    len_ = byte == 0x42 ? 1 : 0;
    return false;
  }
  buf_[len_++] = byte;
  if (len_ < 32) {
    // The frame length (bytes 2-3) must be 28.
    if (len_ == 4 && word(buf_, 2) != 28) {
      errors_++;
      len_ = 0;
    }
    return false;
  }
  len_ = 0;
  uint16_t sum = 0;
  for (size_t i = 0; i < 30; i++) sum = static_cast<uint16_t>(sum + buf_[i]);
  if (sum != word(buf_, 30)) {
    errors_++;
    return false;
  }
  // Data word n starts at byte 4 + 2(n - 1); words 4-6 are atmospheric PM1.0, PM2.5, PM10.
  latest_.pm1 = word(buf_, 10);
  latest_.pm2_5 = word(buf_, 12);
  latest_.pm10 = word(buf_, 14);
  return true;
}

}  // namespace akashic
