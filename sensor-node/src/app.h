// What the node's parts share. Sensing runs in the Arduino loop (it must never wait on the
// network, or a person walking through would be missed); the server connection runs in a
// task of its own, and the GraphQL endpoint in the HTTPS server's task.
#pragma once

#include <Arduino.h>

#include <mutex>

#include "ip_allow.h"
#include "node_state.h"

#if __has_include("node_config.h")
#include "node_config.h"
#else
#warning "No include/node_config.h: building with node_config.example.h (copy it and fill it in)"
#include "node_config.example.h"
#endif

#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "dev"
#endif

namespace node {

/** Guards everything below that more than one task touches. */
extern std::mutex lock;
/** What the node knows now. */
extern akashic::NodeState state;
/** People in and out not yet sent to the server. */
extern uint32_t pendingIn, pendingOut;
/** Who may call the GraphQL endpoint. */
extern akashic::IpAllow allow;

/** The node's own certificate, its key and its token, made on first start and kept. */
struct Identity {
  String keyPem;
  String certPem;
  String token;
};
extern Identity identity;

/** Load the identity from flash, or make and keep one. */
bool loadIdentity();
/** Start the HTTPS GraphQL endpoint. */
bool startGraphqlServer();
/** Set up the sensors; read them (call from the loop). */
void startSensing();
void sense();
/** Start the day's door counts again. */
void resetCounts();
/** Start the server connection's task. */
void startCloud();
/** Whether the network is up. */
bool online();
/** A JSON copy of the node's GraphQL answer to a query. */
std::string answer(const std::string& query);

}  // namespace node
