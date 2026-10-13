# Sensor Node: People Counting and Air Quality for Shops and Cafés

A ceiling-mounted node for a shop or café. It counts people in and out through one or two
doorways, and measures CO2, temperature, humidity, particles (PM1, PM2.5, PM10) and light.
It sends these to your Akashic Display server:

- **Counts** within a few seconds of someone walking through.
- **Readings** every minute.
- **Its own GraphQL endpoint**, which the server reaches directly or through a tunnel.

What you can do with that in Akashic Display:
- media panels that play what fits, such as `site.people > 20` or `site.co2 > 1000`;
- KPI charts of the shop's air or today's visitors;
- media feeds from any field the node answers.

> [!WARNING]
> **Use at your own risk; there is no warranty of any kind.** It hasn't been built on real
> hardware yet. The counting, GraphQL and parsing code is tested on a computer, and the
> firmware compiles for both boards, but treat your first build as a prototype. It is not a
> safety device: don't use it for fire-safety occupancy limits or anything life-critical.

The code is in [`sensor-node`](../sensor-node). It is Arduino C++, built with
[PlatformIO](https://platformio.org).

## 1. What It Does, and Doesn't

- **Counts only.** Each time-of-flight sensor sees an 8×8 grid of distances. The node uses
  it to tell a person from the floor and which way they walked, then throws it away. Depth
  maps never leave the node, not even through GraphQL. There are no cameras, and nothing
  identifies anyone.
- **Readings describe the room**, never a person.
- **Two ways to reach its GraphQL endpoint.** The server needs to call the node for KPI
  panels and feeds.
  1. **Direct (recommended).** Forward one port on the shop's firewall to the node, allowing
     only the server's addresses. The node makes its own certificate and token on first
     start and sends both to the server over its authenticated connection. The server then
     trusts only that certificate, so nothing else can answer for the node.
  2. **Tunnel, the fallback.** When the server can't reach the node directly, the node keeps
     a connection open to the server and answers questions over it. That needs no firewall
     change, but it holds a connection open all day.

  The node says hello when it starts and every 15 minutes. Each time, the server tries the
  direct address and tells the node whether to use the tunnel.

## 2. Parts

| Part | What for | Notes |
| --- | --- | --- |
| **Board**, one of: | | |
| Olimex ESP32-POE-ISO | PoE Ethernet: power and network on one cable | Recommended for shops: no Wi-Fi drop-outs. Needs a PoE switch or injector. The ISO version is isolated from the Ethernet supply. |
| ESP32-S3-DevKitC-1 | Wi-Fi | Needs USB-C power at the ceiling. |
| **Sensors** (I2C, e.g. on SparkFun Qwiic or Adafruit STEMMA QT boards) | | |
| VL53L5CX time-of-flight breakout, one per doorway (up to two) | Counting | 8×8 zones, up to 4 m, 63° diagonal field of view, 15 frames a second at 8×8 (ST). |
| Sensirion SCD41 | CO2 | A true photoacoustic CO2 sensor. Avoid cheap "eCO2" (MOX) sensors, which estimate CO2 and are unreliable. |
| Sensirion SHT40 | Temperature and humidity | Goes in the case's separate vented chamber (§5). |
| ams TSL2591 | Light (lux) | Under the case's light hole. |
| Plantower PMS5003 | Particles PM1, PM2.5, PM10 | Serial (UART), 5 V supply, 3.3 V logic. It has its own fan, so mount it with its intake and outlet on the case's slots. |
| **Case** | | Print [`sensor-node/enclosure/sensor-node.scad`](../sensor-node/enclosure/sensor-node.scad) in **PETG or ASA**, not PLA (§5). M3 heat-set inserts and screws. |
| Cables | | Qwiic/STEMMA QT cables to daisy-chain the I2C boards; jumper wires for the PMS5003 and the sensors' LPn pins. |

**Left out on purpose: oxygen.** Indoor air stays very close to 20.9% oxygen, even in a
crowded room. CO2 is the useful sign of poor ventilation. Electrochemical O2 sensors also
wear out; check the maker's stated life if you add one.

**mmWave radar instead of time-of-flight?** Radar such as the HLK-LD2410 detects that
someone is present, even through plastic, but it can't tell which way they walked through a
door. So it can't count. The node uses time-of-flight for counting.

## 3. Mounting

- **Over each doorway, looking straight down,** 30 cm or so inside the door.
- **Coverage.** At a 2.4 m (8 ft) ceiling, one VL53L5CX sees about 2 m of floor. At 0.7 m
  above the floor, the height that counts as a person, it sees about 1.4 m. One sensor
  covers one doorway up to about 1.4 m wide. Two doorways side by side need two sensors.
  - Up to two sensors connect to one node; a third doorway needs another node.
  - Several nodes at one site are fine: their counts add up on the server.
- **Orientation.** Rows 0 to 7 of the sensor's grid should run from outside to inside.
  - If they run the other way, set `DOOR_A_FLIP 1`.
  - If you turned the sensor a quarter turn, set `DOOR_A_TRANSPOSE 1`.
  - Watch the serial log while you walk through: the counts should go up the right way.
- **Learning the floor.** At start the node takes about two seconds to learn the floor. Keep
  the doorway clear when you power it on.
- **Limits.** Like any overhead counter, it has a few:
  - two people walking side by side under one sensor may count as one;
  - two people crossing opposite ways at the same moment may cancel out;
  - someone who stands under the sensor and turns back isn't counted.

  The node starts the day's counts again at `RESET_AT` (after closing), which also clears
  any drift.

## 4. Wiring

All the I2C boards share one bus (SDA, SCL, 3.3 V, GND). Daisy-chain them with Qwiic or
STEMMA QT cables.

| Signal | ESP32-S3-DevKitC-1 | Olimex ESP32-POE-ISO (UEXT connector and GPIO header) |
| --- | --- | --- |
| I2C SDA | GPIO 8 | GPIO 13 (UEXT) |
| I2C SCL | GPIO 9 | GPIO 16 (UEXT) |
| PMS5003 TX → board RX | GPIO 18 | GPIO 36 (UEXT RX; input only) |
| PMS5003 RX ← board TX | GPIO 17 | GPIO 4 (UEXT TX) |
| VL53L5CX door A: LPn | GPIO 4 | GPIO 32 |
| VL53L5CX door B: LPn | GPIO 5 | GPIO 33 |
| PMS5003 power | 5 V | 5 V |

- **The LPn pins.** Both VL53L5CX sensors start at the same I2C address. The node holds
  door B's sensor in reset (LPn low), moves door A's sensor to another address, then starts
  B. With one sensor, LPn A can stay unconnected (pulled up on the SparkFun board).
- **The SET pin.** Leave the PMS5003's SET pin unconnected (high: running).
- **Check the pins against your board's schematic before powering on.** The Olimex pin
  numbers here are from its UEXT connector; GPIO 36 is input-only, so it suits RX.

## 5. The Case

`sensor-node/enclosure/sensor-node.scad` is a parametric OpenSCAD model in two parts: the
shell, and a lid that screws to the ceiling. Open it in OpenSCAD, check each board's size
against yours (the values marked "measure"), and export each part:

```sh
openscad -D 'part="shell"' -o shell.stl sensor-node.scad
openscad -D 'part="lid"'   -o lid.stl   sensor-node.scad
```

It's designed around three problems:
- **Heat.** The board and the sensors warm the case. A temperature sensor in the same box
  as an ESP32 or a PoE module reads high.
  - The SHT40 has a **chimney** of its own: a compartment behind a double wall with an air
    gap, open to the room below and at the side.
  - The board's compartment vents at the ceiling end, so its heat leaves away from the
    sensors.
- **The laser windows.** The VL53L5CX sends and receives infrared light. Don't cover it
  with clear acrylic or glass, which blocks or reflects the light. The case has an open
  window over each sensor; mount each sensor flush behind its window.
- **Particle airflow.** The PMS5003's fan sets the airflow. Its intake and outlet line up
  with separate slots, so it doesn't breathe its own exhaust.

**Print in PETG or ASA.** The node runs warm day and night, close to a ceiling, and a PLA
case can sag over months. The model is a starting point; no case has been printed yet.

## 6. Set Up the Server Side

1. In `/admin` → **Sites**, add the site (or use an existing one) and press **New Sensor
   Key**. Copy the `pdo_…` key; it isn't shown again. Every node at the site uses the same
   key, with a different `NODE_NAME`.
2. Under Sites you'll also see **the addresses our servers call from**. Your firewall rule
   should allow only those (§8).

## 7. Build and Flash

1. Install [PlatformIO](https://docs.platformio.org/en/latest/core/installation/index.html)
   (`pip install platformio`).
2. Copy `sensor-node/include/node_config.example.h` to `sensor-node/include/node_config.h` and fill it in:
   - your Wi-Fi network and password (Wi-Fi build only);
   - the server: `SERVER_URL`, `SERVER_ROOT_CA` (the certificate authority that signed
     your server's certificate; for Let's Encrypt, "ISRG Root X1") and `SENSOR_KEY`;
   - this node: `NODE_NAME` (lower case, like `door-a`) and `DIRECT_URL` (§8, or empty
     for the tunnel only);
   - your time zone (`TZ_POSIX`) and `RESET_AT`;
   - the doors and which sensors are fitted.
3. Build and flash:
   ```sh
   cd sensor-node
   pio run -e esp32-poe-iso -t upload   # or -e esp32-s3
   pio device monitor
   ```
   On first start the node makes its certificate and token, which takes a few seconds. The
   serial log then shows:
   - its GraphQL token (for calling it from your own network);
   - whether each sensor was found;
   - the server's reply to hello: its endpoint (`sensor://<site>/<node>`), and whether it is
     reached directly or through the tunnel.

`node_config.h` holds your sensor key and Wi-Fi password. Git ignores it; don't share it.

## 8. Reach It Directly (Firewall NAT Rule)

The server reaches the node directly when your firewall forwards a port to it:

1. **Give the node a fixed address** on your network (a DHCP reservation in your router).
2. **Add a port-forward (NAT) rule:**
   - from the internet, on a port of your choice (for example 8443);
   - to the node's address, port `GRAPHQL_PORT` (8443 by default);
   - **from our servers' addresses only.** They're shown in /admin → Sites. Any other source
     address must be refused.
3. **Set `DIRECT_URL`** to how the internet reaches it, such as
   `https://203.0.113.9:8443/graphql`: your public address (or a DNS name for it) and the
   forwarded port.
4. **Flash again and watch the log.** The hello should say "reached directly". If it says
   "through the tunnel", the server couldn't reach the node; check the rule and the
   address.

The node checks callers too. At hello the server sends the same address list, and the node
refuses calls from anywhere else. With `ALLOW_LAN 1`, devices on your own network are
allowed too, but they still need the token.

**No firewall access?** Leave `DIRECT_URL` empty. The node uses the tunnel, which needs only
outgoing HTTPS.

## 9. Its GraphQL Endpoint

`POST https://<node>:8443/graphql`, with `Authorization: Bearer <token>` and a JSON body
`{"query": "…"}`. From your own network:

```sh
curl -k https://192.168.1.50:8443/graphql \
  -H "Authorization: Bearer <token from the serial log>" \
  -H "Content-Type: application/json" \
  -d '{"query": "{ occupancy { present doors { name entered left } } air { co2 temperature pm2_5 } }"}'
```

(`-k` because the node's certificate is its own. The server trusts it by its fingerprint.)

```graphql
type Query     { node: Node!, occupancy: Occupancy!, air: Air! }
type Node      { name: String!, firmware: String!, uptime: Int!, network: String! }
type Occupancy { entered: Int!, left: Int!, present: Int!, doors: [Door!]! }
type Door      { name: String!, entered: Int!, left: Int! }
type Air       { co2: Float, temperature: Float, humidity: Float, pm1: Float,
                 pm2_5: Float, pm10: Float, light: Float, age: Int }
```

- **Counts** are since the day's reset.
- **Readings:**
  - temperature is in °C;
  - CO2 is in ppm;
  - particles are in µg/m³;
  - light is in lux;
  - `age` is seconds since the last reading;
  - an unknown reading is null.
- **What it accepts:** queries only, with field selections, aliases and `__typename`. No
  fragments, arguments or mutations.

**In Akashic Display**, name the node `sensor://<site>/<node>` as a KPI panel's or a media
feed's endpoint; /admin offers it in the endpoint field's list. For example:
- a KPI panel showing `{ air { co2 } }` with the path `data.air.co2`;
- a media feed reading `data.occupancy.present` as `feed.visitors`.

The readings it sends are also condition sources without any feed: `site.co2`,
`site.temperature`, `site.humidity`, `site.pm2_5`, `site.light`, and so on.

## 10. Test Without Hardware

```sh
sensor-node/test.sh
```

It builds and runs the core library's tests on your computer: door counting from simulated
depth maps, the GraphQL executor, the PMS5003 frame reader, the event-stream and chunked
readers, and the address allowlist. It needs `g++` with the address and undefined-behaviour
sanitizers. `pio run` compiles the firmware for both boards.
