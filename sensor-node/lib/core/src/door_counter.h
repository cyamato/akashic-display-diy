// Counts people through a doorway from an overhead 8x8 time-of-flight depth map (VL53L5CX).
//
// The sensor looks straight down. It learns each zone's distance to the floor, so anything
// standing well above the floor is a person. The rows run across the doorway: row 0 is
// outside, row 7 inside (or the other way round with `flip`). A person who appears on one
// side and leaves the view on the other is counted in or out. Plain C++, no Arduino: it is
// tested on a computer.
//
// Limits (an overhead counter's usual ones): two people side by side under one sensor may
// count as one, and two crossing opposite ways at the same moment may cancel out.
#pragma once

#include <cstdint>

namespace akashic {

/** One doorway's view. */
struct DoorConfig {
  /** Columns of the 8x8 grid that look at this doorway (inclusive). */
  uint8_t firstColumn = 0;
  uint8_t lastColumn = 7;
  /** Row 0 is inside instead of outside. */
  bool flip = false;
  /** Height above the floor, mm, that counts as a person (a child's shoulders, roughly). */
  uint16_t minHeightMm = 700;
  /** Zones that must be occupied to count as someone there. */
  uint8_t minZones = 2;
  /** Rows a person must travel between appearing and leaving. */
  uint8_t minTravelRows = 3;
  /** Empty frames before someone counts as gone (absorbs a missed frame). */
  uint8_t gapFrames = 2;
  /** Frames averaged to learn the floor at start. */
  uint8_t learnFrames = 30;
};

/** What a frame changed. */
struct DoorEvent {
  int8_t in = 0;
  int8_t out = 0;
};

class DoorCounter {
 public:
  DoorCounter() = default;
  explicit DoorCounter(const DoorConfig& config) : cfg_(config) {}

  /**
   * One frame: 64 distances (mm), and whether each zone's target is valid (VL53L5CX target
   * status 5 or 9). An invalid zone counts as floor.
   */
  DoorEvent update(const uint16_t distance[64], const bool valid[64]);

  /** Whether the floor has been learnt yet (nothing is counted before). */
  bool ready() const { return learnt_ >= cfg_.learnFrames; }
  /** Whether someone is in view now. */
  bool present() const { return tracking_; }
  /** The learnt floor distance of a zone, mm. */
  uint16_t floorMm(uint8_t zone) const { return static_cast<uint16_t>(floor_[zone]); }
  const DoorConfig& config() const { return cfg_; }

 private:
  DoorConfig cfg_;
  float floor_[64] = {0};
  uint16_t learnt_ = 0;
  bool tracking_ = false;
  float startRow_ = 0;
  float lastRow_ = 0;
  uint8_t empty_ = 0;
};

}  // namespace akashic
