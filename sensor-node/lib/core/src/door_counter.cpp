#include "door_counter.h"

namespace akashic {

DoorEvent DoorCounter::update(const uint16_t distance[64], const bool valid[64]) {
  DoorEvent ev;
  // Learn the floor first: the average of the first frames (nobody should stand under it).
  if (!ready()) {
    for (int z = 0; z < 64; z++) {
      if (valid[z]) floor_[z] += (distance[z] - floor_[z]) / static_cast<float>(learnt_ + 1);
    }
    learnt_++;
    return ev;
  }
  // Zones well above the floor, in this doorway's columns.
  int zones = 0;
  float rowSum = 0, weight = 0;
  bool occupied[64] = {false};
  for (int z = 0; z < 64; z++) {
    const int row = z / 8, col = z % 8;
    if (col < cfg_.firstColumn || col > cfg_.lastColumn || !valid[z] || floor_[z] <= 0) continue;
    const float height = floor_[z] - distance[z];
    if (height < cfg_.minHeightMm) continue;
    occupied[z] = true;
    zones++;
    rowSum += row * height;
    weight += height;
  }
  const bool someone = zones >= cfg_.minZones;
  if (someone) {
    // Where they are along the walk-through, 0 (outside) to 7 (inside).
    float row = rowSum / weight;
    if (cfg_.flip) row = 7.0f - row;
    if (!tracking_) {
      tracking_ = true;
      startRow_ = row;
    }
    lastRow_ = row;
    empty_ = 0;
  } else {
    // Follow the floor slowly (a mat, a moved display stand), only while nobody is there.
    if (!tracking_) {
      for (int z = 0; z < 64; z++) {
        if (valid[z] && !occupied[z]) floor_[z] += (distance[z] - floor_[z]) / 64.0f;
      }
    }
    if (tracking_ && ++empty_ >= cfg_.gapFrames) {
      tracking_ = false;
      const float travel = lastRow_ - startRow_;
      if (startRow_ < 3.5f && lastRow_ > 3.5f && travel >= cfg_.minTravelRows) ev.in = 1;
      if (startRow_ > 3.5f && lastRow_ < 3.5f && -travel >= cfg_.minTravelRows) ev.out = 1;
    }
  }
  return ev;
}

}  // namespace akashic
