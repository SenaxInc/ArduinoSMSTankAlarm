/**
 * sunsaver-rs485-passive-sniff.ino
 *
 * Pure passive RX. No TX. Decouples Opta TX path from any observed bytes.
 * Sweeps baud + framing config so we catch traffic regardless of bus settings.
 * Each config listens for kListenWindowMs, prints any bytes, then rotates.
 * Summary every full sweep.
 */

#include <ArduinoRS485.h>

struct CfgProfile {
  uint32_t baud;
  uint16_t cfg;
  const char *name;
};

static const CfgProfile kCfgs[] = {
  { 9600, SERIAL_8N1, "9600 8N1"},
  { 9600, SERIAL_8N2, "9600 8N2"},
  { 9600, SERIAL_8E1, "9600 8E1"},
  { 9600, SERIAL_8O1, "9600 8O1"},
  { 4800, SERIAL_8N1, "4800 8N1"},
  {19200, SERIAL_8N1, "19200 8N1"},
  {38400, SERIAL_8N1, "38400 8N1"},
  { 2400, SERIAL_8N1, "2400 8N1"}
};

static const uint16_t kListenWindowMs = 5000;
static const uint16_t kMaxRxBytes = 240;

struct Stats {
  uint32_t cfgsTried;
  uint32_t cfgsWithBytes;
  uint32_t totalBytes;
};

static Stats gStats = {0, 0, 0};

static void printHexByte(uint8_t b) {
  if (b < 0x10) Serial.print('0');
  Serial.print(b, HEX);
}

static void printHexBuf(const uint8_t *buf, uint16_t len) {
  for (uint16_t i = 0; i < len; ++i) {
    if (i) Serial.print(' ');
    printHexByte(buf[i]);
  }
}

static void runCfg(const CfgProfile &c) {
  RS485.end();
  delay(5);
  RS485.begin(c.baud, c.cfg);
  RS485.receive();

  // Drain any startup noise quickly
  uint32_t drainEnd = millis() + 25;
  while ((int32_t)(millis() - drainEnd) < 0) {
    while (RS485.available()) RS485.read();
  }

  uint8_t rx[kMaxRxBytes];
  uint16_t rxLen = 0;

  uint32_t deadline = millis() + kListenWindowMs;
  while ((int32_t)(millis() - deadline) < 0) {
    while (RS485.available()) {
      int b = RS485.read();
      if (b >= 0 && rxLen < kMaxRxBytes) {
        rx[rxLen++] = (uint8_t)b;
      }
    }
  }
  RS485.noReceive();

  Serial.println();
  Serial.print(F("CFG ")); Serial.print(c.name);
  Serial.print(F(" listen=")); Serial.print(kListenWindowMs); Serial.print(F("ms"));
  Serial.print(F(" bytes=")); Serial.println(rxLen);
  if (rxLen > 0) {
    Serial.print(F("  RX: ")); printHexBuf(rx, rxLen); Serial.println();
    ++gStats.cfgsWithBytes;
    gStats.totalBytes += rxLen;
  }
  ++gStats.cfgsTried;
}

void setup() {
  Serial.begin(115200);
  uint32_t t0 = millis();
  while (!Serial && millis() - t0 < 3000) delay(10);

  Serial.println();
  Serial.println(F("================================================"));
  Serial.println(F("SunSaver RS485 PASSIVE SNIFF (no TX)"));
  Serial.println(F("  Sweeps baud + framing, listens 5s each"));
  Serial.println(F("================================================"));
}

void loop() {
  static uint8_t idx = 0;
  static uint32_t cycle = 0;
  const uint8_t n = (uint8_t)(sizeof(kCfgs) / sizeof(kCfgs[0]));

  runCfg(kCfgs[idx]);
  ++idx;
  if (idx >= n) {
    ++cycle;
    Serial.println();
    Serial.println(F("====== PASSIVE SNIFF SUMMARY ======"));
    Serial.print(F("cycle=")); Serial.println(cycle);
    Serial.print(F("cfgs_tried=")); Serial.println(gStats.cfgsTried);
    Serial.print(F("cfgs_with_bytes=")); Serial.println(gStats.cfgsWithBytes);
    Serial.print(F("total_bytes=")); Serial.println(gStats.totalBytes);
    Serial.println(F("==================================="));
    idx = 0;
    gStats.cfgsTried = 0;
    gStats.cfgsWithBytes = 0;
    gStats.totalBytes = 0;
  }
}
