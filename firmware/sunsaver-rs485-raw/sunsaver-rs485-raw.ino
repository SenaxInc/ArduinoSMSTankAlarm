/**
 * sunsaver-rs485-raw.ino
 *
 * Raw RS-485 byte-level diagnostic for the SunSaver MPPT bring-up.
 *
 * Bypasses ArduinoModbus entirely. Manually crafts a Modbus RTU
 * "Read Holding Registers" query (function 0x03) for slave 1, register 0x0012
 * (battery voltage), and then dumps EVERY raw byte received from the bus
 * for ~800 ms. This isolates electrical/protocol issues from library issues.
 *
 * If we see bytes returned -> the SunSaver is alive and the wires work;
 * issue is framing/parity/timing in the Modbus library.
 * If we see ZERO bytes -> the request is not reaching the SunSaver, or
 * the SunSaver is not responding (slave ID, baud, wiring, or transceiver
 * direction all suspect).
 */

#include <ArduinoRS485.h>

static const uint32_t BAUD_RATE = 9600;
static const uint8_t  SLAVE_ID  = 1;
static const uint16_t REG_ADDR  = 0x0012;  // Battery voltage
static const uint16_t REG_COUNT = 1;
static const uint32_t POLL_INTERVAL_MS = 3000;
static const uint32_t LISTEN_WINDOW_MS = 800;

static uint16_t crc16Modbus(const uint8_t *data, size_t len) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < len; ++i) {
    crc ^= (uint16_t)data[i];
    for (uint8_t b = 0; b < 8; ++b) {
      if (crc & 0x0001) {
        crc >>= 1;
        crc ^= 0xA001;
      } else {
        crc >>= 1;
      }
    }
  }
  return crc;
}

static void printHexByte(uint8_t b) {
  if (b < 0x10) Serial.print('0');
  Serial.print(b, HEX);
}

static void sendQueryAndDump(const char *label, uint8_t slaveId, uint8_t fn,
                             uint16_t addr, uint16_t count) {
  uint8_t frame[8];
  frame[0] = slaveId;
  frame[1] = fn;
  frame[2] = (uint8_t)(addr >> 8);
  frame[3] = (uint8_t)(addr & 0xFF);
  frame[4] = (uint8_t)(count >> 8);
  frame[5] = (uint8_t)(count & 0xFF);
  uint16_t crc = crc16Modbus(frame, 6);
  // Modbus CRC is little-endian on the wire
  frame[6] = (uint8_t)(crc & 0xFF);
  frame[7] = (uint8_t)(crc >> 8);

  Serial.print(F("\n>>> ")); Serial.print(label);
  Serial.print(F(" TX ["));
  for (uint8_t i = 0; i < 8; ++i) {
    if (i) Serial.print(' ');
    printHexByte(frame[i]);
  }
  Serial.println(F("]"));

  // Transmit
  RS485.beginTransmission();
  RS485.write(frame, 8);
  RS485.endTransmission();

  // Listen for any bytes
  Serial.print(F("<<< RX ("));
  Serial.print(LISTEN_WINDOW_MS);
  Serial.print(F("ms): "));
  RS485.receive();
  uint32_t deadline = millis() + LISTEN_WINDOW_MS;
  uint16_t rxCount = 0;
  while (millis() < deadline) {
    if (RS485.available()) {
      int b = RS485.read();
      if (b >= 0) {
        if (rxCount > 0) Serial.print(' ');
        printHexByte((uint8_t)b);
        ++rxCount;
      }
    }
  }
  RS485.noReceive();
  Serial.print(F("  [")); Serial.print(rxCount); Serial.println(F(" bytes]"));
}

void setup() {
  Serial.begin(115200);
  uint32_t s = millis();
  while (!Serial && millis() - s < 3000) delay(10);

  Serial.println();
  Serial.println(F("==============================================="));
  Serial.println(F("SunSaver MPPT raw RS-485 byte diagnostic v2"));
  Serial.print  (F("  Baud: ")); Serial.println(BAUD_RATE);
  Serial.println(F("  Sweep: slave IDs 1, 2; FC 0x03 and 0x04"));
  Serial.println(F("  Sweep: parity 8N2 then 8N1"));
  Serial.println(F("==============================================="));
}

static const uint8_t  kSlaveIds[]   = { 1, 2 };
static const uint8_t  kSlaveCount   = sizeof(kSlaveIds) / sizeof(kSlaveIds[0]);
static const uint8_t  kFnCodes[]    = { 0x03, 0x04 };  // Read Holding / Read Input
static const uint8_t  kFnCount      = sizeof(kFnCodes) / sizeof(kFnCodes[0]);
static const uint16_t kSerialCfgs[] = { SERIAL_8N2, SERIAL_8N1 };
static const char *   kSerialNames[]= { "8N2",       "8N1"       };
static const uint8_t  kCfgCount     = sizeof(kSerialCfgs) / sizeof(kSerialCfgs[0]);

void loop() {
  static uint32_t cycle = 0;
  static unsigned long lastCycle = 0;
  static unsigned long lastHb = 0;
  unsigned long now = millis();

  if (now - lastHb >= 1000UL) {
    lastHb = now;
    Serial.print(F("hb ms=")); Serial.println(now);
  }

  if (now - lastCycle < POLL_INTERVAL_MS) return;
  lastCycle = now;
  ++cycle;

  // Pick the parity for this cycle (rotate every cycle)
  uint8_t cfgIdx = (cycle - 1) % kCfgCount;
  uint16_t cfg = kSerialCfgs[cfgIdx];
  const char *cfgName = kSerialNames[cfgIdx];

  Serial.println();
  Serial.println(F("------------------------------------------------"));
  Serial.print(F("Cycle ")); Serial.print(cycle);
  Serial.print(F(" using ")); Serial.println(cfgName);
  Serial.println(F("------------------------------------------------"));

  // Re-init RS-485 with the new framing
  RS485.end();
  delay(20);
  RS485.begin(BAUD_RATE, cfg);
  RS485.setDelays(50, 50);
  delay(20);

  for (uint8_t i = 0; i < kSlaveCount; ++i) {
    for (uint8_t f = 0; f < kFnCount; ++f) {
      char label[32];
      snprintf(label, sizeof(label), "%s slv=%u fc=0x%02X",
               cfgName, (unsigned)kSlaveIds[i], (unsigned)kFnCodes[f]);
      sendQueryAndDump(label, kSlaveIds[i], kFnCodes[f], REG_ADDR, REG_COUNT);
      delay(150);
    }
  }
}
