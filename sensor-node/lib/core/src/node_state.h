// What the node knows, as its GraphQL schema shows it and as it reports it to the server.
//
//   type Query     { node: Node!, occupancy: Occupancy!, air: Air! }
//   type Node      { name: String!, firmware: String!, uptime: Int!, network: String! }
//   type Occupancy { entered: Int!, left: Int!, present: Int!, doors: [Door!]! }
//   type Door      { name: String!, entered: Int!, left: Int! }
//   type Air       { co2: Float, temperature: Float, humidity: Float, pm1: Float,
//                    pm2_5: Float, pm10: Float, light: Float, age: Int }
//
// Counts are since the last daily reset. Temperature is in °C; `age` is seconds since the
// last reading. An unknown reading is null. Nothing here identifies a person: the depth
// maps the counts come from never leave the node.
#pragma once

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "graphql.h"

namespace akashic {

struct DoorCount {
  std::string name;
  uint32_t entered = 0;
  uint32_t left = 0;
};

struct NodeState {
  std::string name;
  std::string firmware;
  uint32_t uptimeSeconds = 0;
  /** "wifi" or "ethernet". */
  std::string network;
  std::vector<DoorCount> doors;
  /** NaN: unknown. */
  float co2 = NAN, temperature = NAN, humidity = NAN, pm1 = NAN, pm2_5 = NAN, pm10 = NAN, light = NAN;
  /** Seconds since the readings were taken (-1: never). */
  int32_t readingsAge = -1;

  uint32_t entered() const;
  uint32_t left() const;
  /** People inside by this node's doors (never below zero). */
  uint32_t present() const;
};

/** The GraphQL snapshot of a state (the `Query` object). */
gql::Value snapshot(const NodeState& s);

/** `PUT /v1/sites/readings` body: the known readings only ("" when none is known). */
std::string readingsBody(const NodeState& s);

/** `PUT /v1/sites/occupancy` body for people in and out since the last report. */
std::string occupancyBody(uint32_t in, uint32_t out);

/**
 * `PUT /v1/sensors/hello` body; `directUrl` empty: no direct address (tunnel only).
 */
std::string helloBody(const std::string& node, const std::string& firmware, const std::string& directUrl, const std::string& certPem, const std::string& token);

}  // namespace akashic
