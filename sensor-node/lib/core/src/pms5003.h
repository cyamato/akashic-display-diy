// Reads the Plantower PMS5003 particle sensor's 32-byte frames from its serial line
// (9600 baud): 0x42 0x4D, a length of 28, 13 data words and a checksum. The particle
// figures used are the "atmospheric environment" ones (data words 4 to 6), in µg/m³.
#pragma once

#include <cstddef>
#include <cstdint>

namespace akashic {

struct Particles {
  uint16_t pm1 = 0;
  uint16_t pm2_5 = 0;
  uint16_t pm10 = 0;
};

class Pms5003 {
 public:
  /** Feed one byte; true when it completes a valid frame (then `latest()` is new). */
  bool feed(uint8_t byte);
  const Particles& latest() const { return latest_; }
  /** Frames dropped for a bad length or checksum. */
  uint32_t errors() const { return errors_; }

 private:
  uint8_t buf_[32] = {0};
  size_t len_ = 0;
  Particles latest_;
  uint32_t errors_ = 0;
};

}  // namespace akashic
