// The sensors: one or two VL53L5CX time-of-flight sensors over the doorways (15 frames a
// second), and the air and light sensors (every few seconds). All on one I2C bus, except the
// PMS5003 on a serial line. Depth maps are used here and never stored or sent.
// The settings first: they say which sensors are fitted.
#include "app.h"

#include <SparkFun_VL53L5CX_Library.h>
#include <Wire.h>

#if HAS_SCD41
#include <SensirionI2CScd4x.h>
#endif
#if HAS_SHT40
#include <SensirionI2CSht4x.h>
#endif
#if HAS_TSL2591
#include <Adafruit_TSL2591.h>
#endif

#include "door_counter.h"
#include "pms5003.h"

namespace node {

namespace {

struct Door {
  const char* name;
  bool transpose;
  SparkFun_VL53L5CX sensor;
  akashic::DoorCounter counter;
  bool ok = false;
};

Door doorA{DOOR_A_NAME, DOOR_A_TRANSPOSE != 0};
Door doorB{DOOR_B_NAME, DOOR_B_TRANSPOSE != 0};
const bool kTwoDoors = DOOR_B_NAME[0] != '\0';
VL53L5CX_ResultsData frame;

#if HAS_SCD41
SensirionI2CScd4x scd41;
#endif
#if HAS_SHT40
SensirionI2CSht4x sht40;
#endif
#if HAS_TSL2591
Adafruit_TSL2591 tsl(2591);
bool tslOk = false;
#endif
#if HAS_PMS5003
akashic::Pms5003 pms;
/** Whether a frame has arrived since the last climate reading. */
bool pmsFresh = false;
#endif

uint32_t lastClimate = 0;
uint32_t readingsAt = 0;
bool anyReading = false;

akashic::DoorConfig doorConfig(bool flip) {
  akashic::DoorConfig c;
  c.flip = flip;
  c.minHeightMm = MIN_HEIGHT_MM;
  return c;
}

/** Start one sensor at an address (the second is moved off the default first). */
bool startTof(Door& d, uint8_t address) {
  if (!d.sensor.begin(0x29, Wire)) return false;
  if (address != 0x29 && !d.sensor.setAddress(address)) return false;
  d.sensor.setResolution(8 * 8);
  d.sensor.setRangingFrequency(15);
  return d.sensor.startRanging();
}

void readDoor(Door& d, size_t index) {
  if (!d.ok || !d.sensor.isDataReady() || !d.sensor.getRangingData(&frame)) return;
  uint16_t distance[64];
  bool valid[64];
  for (int z = 0; z < 64; z++) {
    // Transposed: the sensor is turned a quarter, so its columns run across the doorway.
    const int src = d.transpose ? (z % 8) * 8 + z / 8 : z;
    const int16_t mm = frame.distance_mm[src];
    const uint8_t status = frame.target_status[src];
    distance[z] = mm > 0 ? static_cast<uint16_t>(mm) : 0;
    valid[z] = (status == 5 || status == 9) && mm > 0;
  }
  const akashic::DoorEvent e = d.counter.update(distance, valid);
  if (!e.in && !e.out) return;
  std::lock_guard<std::mutex> hold(lock);
  state.doors[index].entered += e.in;
  state.doors[index].left += e.out;
  pendingIn += e.in;
  pendingOut += e.out;
}

/** Air and light: every 5 seconds (the SCD41 measures every 5). */
void readClimate() {
  float co2 = NAN, temperature = NAN, humidity = NAN, light = NAN;
#if HAS_SCD41
  bool ready = false;
  if (!scd41.getDataReadyFlag(ready) && ready) {
    uint16_t ppm = 0;
    float t, h;
    // Its own temperature runs warm (it heats itself): the SHT40's are used instead.
    if (!scd41.readMeasurement(ppm, t, h) && ppm) co2 = ppm;
  }
#endif
#if HAS_SHT40
  float t, h;
  if (!sht40.measureHighPrecision(t, h)) {
    temperature = t;
    humidity = h;
  }
#endif
#if HAS_TSL2591
  if (tslOk) {
    const uint32_t lum = tsl.getFullLuminosity();
    const float lux = tsl.calculateLux(lum & 0xFFFF, lum >> 16);
    if (lux >= 0) light = lux;
  }
#endif
  std::lock_guard<std::mutex> hold(lock);
  auto keep = [](float& into, float v) {
    if (std::isfinite(v)) into = v;
  };
  keep(state.co2, co2);
  keep(state.temperature, temperature);
  keep(state.humidity, humidity);
  keep(state.light, light);
  bool particles = false;
#if HAS_PMS5003
  if (pmsFresh) {
    pmsFresh = false;
    particles = true;
    state.pm1 = pms.latest().pm1;
    state.pm2_5 = pms.latest().pm2_5;
    state.pm10 = pms.latest().pm10;
  }
#endif
  if (particles || std::isfinite(co2) || std::isfinite(temperature) || std::isfinite(light)) {
    readingsAt = millis();
    anyReading = true;
  }
  state.readingsAge = anyReading ? static_cast<int32_t>((millis() - readingsAt) / 1000) : -1;
}

}  // namespace

void startSensing() {
  Wire.begin(PIN_SDA, PIN_SCL);
  Wire.setClock(400000);
  state.doors.push_back({DOOR_A_NAME, 0, 0});
  if (kTwoDoors) state.doors.push_back({DOOR_B_NAME, 0, 0});
  doorA.counter = akashic::DoorCounter(doorConfig(DOOR_A_FLIP));
  doorB.counter = akashic::DoorCounter(doorConfig(DOOR_B_FLIP));
  // Two sensors share the default address: hold B in reset, move A, then start B.
  pinMode(PIN_TOF_A_LPN, OUTPUT);
  pinMode(PIN_TOF_B_LPN, OUTPUT);
  digitalWrite(PIN_TOF_A_LPN, HIGH);
  digitalWrite(PIN_TOF_B_LPN, kTwoDoors ? LOW : HIGH);
  delay(10);
  Serial.println("Starting the time-of-flight sensor(s); loading their firmware takes a few seconds...");
  doorA.ok = startTof(doorA, kTwoDoors ? 0x30 : 0x29);
  if (kTwoDoors) {
    digitalWrite(PIN_TOF_B_LPN, HIGH);
    delay(10);
    doorB.ok = startTof(doorB, 0x29);
  }
  Serial.printf("Door %s: %s\n", DOOR_A_NAME, doorA.ok ? "ready (learning the floor: keep the doorway clear)" : "sensor not found");
  if (kTwoDoors) Serial.printf("Door %s: %s\n", DOOR_B_NAME, doorB.ok ? "ready" : "sensor not found");
#if HAS_SCD41
  scd41.begin(Wire);
  scd41.stopPeriodicMeasurement();
  delay(500);
  if (scd41.startPeriodicMeasurement()) Serial.println("SCD41 not found");
#endif
#if HAS_SHT40
  sht40.begin(Wire);
#endif
#if HAS_TSL2591
  tslOk = tsl.begin(&Wire);
  if (tslOk) {
    tsl.setGain(TSL2591_GAIN_MED);
    // Short, so reading it barely pauses the door counting.
    tsl.setTiming(TSL2591_INTEGRATIONTIME_100MS);
  } else {
    Serial.println("TSL2591 not found");
  }
#endif
#if HAS_PMS5003
  Serial2.begin(9600, SERIAL_8N1, PIN_PMS_RX, PIN_PMS_TX);
#endif
}

void sense() {
  readDoor(doorA, 0);
  if (kTwoDoors) readDoor(doorB, 1);
#if HAS_PMS5003
  while (Serial2.available()) {
    if (pms.feed(static_cast<uint8_t>(Serial2.read()))) pmsFresh = true;
  }
#endif
  if (millis() - lastClimate >= 5000) {
    lastClimate = millis();
    readClimate();
  }
}

/** Start the day again: the doors' counts (the server's are reset by the cloud task). */
void resetCounts() {
  std::lock_guard<std::mutex> hold(lock);
  for (auto& d : state.doors) d.entered = d.left = 0;
}

}  // namespace node
