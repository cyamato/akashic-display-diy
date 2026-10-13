// The node's settings. Copy this file to node_config.h (which git ignores) and fill it in.
#pragma once

// ---- Network ----
// Wi-Fi (the ESP32-S3 build). The PoE build uses Ethernet and ignores these.
#define WIFI_SSID ""
#define WIFI_PASSWORD ""

// ---- Your Akashic Display server ----
// Its address, with no path: the hosted service's, a partner's, or your own.
#define SERVER_URL "https://dash.example.com"
// The certificate authority that signed the server's certificate, in PEM. For a server
// with a Let's Encrypt certificate this is "ISRG Root X1" (letsencrypt.org/certificates).
#define SERVER_ROOT_CA \
  "-----BEGIN CERTIFICATE-----\n" \
  "...paste the root certificate here, one line per string...\n" \
  "-----END CERTIFICATE-----\n"
// The site's sensor key, from /admin → Sites → New Sensor Key (pdo_…).
#define SENSOR_KEY "pdo_"
// This node's name within its site: lower case letters, digits and dashes.
#define NODE_NAME "door-a"

// ---- Its GraphQL endpoint ----
// The port it listens on (HTTPS, with a certificate it makes itself on first start).
#define GRAPHQL_PORT 8443
// How the internet reaches it: your public address and the port your firewall forwards to
// this node (a NAT rule allowing only the server's addresses, shown in /admin → Sites).
// Leave empty to use the tunnel only.
#define DIRECT_URL ""
// Let devices on your own network (private addresses) call it too (with its token).
#define ALLOW_LAN 1

// ---- Time and the daily reset ----
// Your time zone as a POSIX TZ string (this one is New Zealand), and an NTP server.
#define TZ_POSIX "NZST-12NZDT,M9.5.0,M4.1.0/3"
#define NTP_SERVER "pool.ntp.org"
// When to start the day's counts again (local time, HH:MM), after closing.
#define RESET_AT "04:00"
// Also set the whole site's count to zero then (one node per site should).
#define RESET_SITE 1

// ---- Doors: one VL53L5CX per doorway, looking straight down ----
// Door A's sensor is always fitted. Its name, and which way is in: rows 0 to 7 of its grid
// run across the doorway, row 0 outside. FLIP if row 0 is inside; TRANSPOSE if the sensor
// is turned a quarter so its columns run across the doorway instead.
#define DOOR_A_NAME "front"
#define DOOR_A_FLIP 0
#define DOOR_A_TRANSPOSE 0
// A second sensor for a second doorway ("" for none).
#define DOOR_B_NAME ""
#define DOOR_B_FLIP 0
#define DOOR_B_TRANSPOSE 0
// Height above the floor (mm) that counts as a person.
#define MIN_HEIGHT_MM 700

// ---- Air and light sensors fitted (1) or not (0) ----
#define HAS_SCD41 1
#define HAS_SHT40 1
#define HAS_TSL2591 1
#define HAS_PMS5003 1
