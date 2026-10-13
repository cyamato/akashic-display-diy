# Everything Presence One (EP1) for Akashic Display

> [!WARNING]
> **Use at your own risk; there is no warranty of any kind.** It hasn't been tried on a real EP1 yet; the configuration validates with ESPHome.

The EP1 (Everything Smart Technology) runs ESPHome. It combines mmWave radar and a PIR
sensor into one **Occupancy** sensor, and measures temperature, humidity and light; the
optional module adds CO2. Our add-on, the ESPHome package
[`esphome/everything-presence-one.yaml`](../esphome/everything-presence-one.yaml), sends:

- **The room's status.** While the EP1 sees someone, the room is **busy** for 15 minutes,
  renewed every 5 minutes. When the room empties it goes back to its calendar. If the EP1
  goes offline, the busy status lapses on its own. Room signs and busy lights change at
  once.
- **The site's readings** every minute.

**Set it up:**
1. In /admin:
   - **Rooms** → the room → **New status key**. Copy it.
   - **Sites** → the site → **New Sensor Key**. Copy it, or use the site's existing key
     (every sensor at a site shares one).
2. In the ESPHome dashboard, **adopt** the EP1 (take over its configuration).
3. Add the two keys to ESPHome's `secrets.yaml`:
   ```yaml
   akashic_room_key: "…"
   akashic_sensor_key: "pdo_…"
   ```
4. Add the add-on to the EP1's configuration:
   ```yaml
   substitutions:
     akashic_server: "https://dash.example.com"   # your Akashic Display server
     akashic_room_key: !secret akashic_room_key
     akashic_sensor_key: !secret akashic_sensor_key
     # With the CO2 module:
     # akashic_extra_readings: 'add("co2", id(co2).state);'
   packages:
     akashic: github://cyamato/akashic-display-diy/esphome/everything-presence-one.yaml@main
   ```
5. **Install** (flash it). Watch the EP1's logs: you should see a `PUT` to
   `/v1/rooms/status` when someone walks in.

**Options:**
- Leave `akashic_room_key` empty to send only readings.
- Leave `akashic_sensor_key` empty to send only presence.
- `akashic_busy_minutes` (default 15) and `akashic_readings_interval` (default `60s`) change
  the timings.

**How it works.** The add-on reads the EP1's own sensors by the ids its firmware gives them
(`occupancy`, `temperature_sensor`, `humidity_sensor`, `illuminance_sensor`, and `co2`).
The EP1's temperature and humidity offsets still apply. It uses the EP1's own HTTPS client,
so it needs only outgoing HTTPS. Use at your own risk: it is MIT licensed, with no
warranty, and hasn't been tried on a real EP1 yet. The configuration validates with ESPHome
2026.9.

Verkada air quality sensors need no hardware work: connect them in /admin → Sites (see the Akashic Display user guide).
