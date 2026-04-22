/**
 * sunsaver-rs485-patient.ino
 *
 * Long-window patient probe of the SunSaver MPPT.
 *
 * For each cycle:
 *   - Sends the Modbus RTU query for register 0x0008 (filtered battery V),
 *     function 0x04 (Read Input Registers), slave 1, repeated 3 times back
 *     to back with 200ms gaps.
 *   - Listens for 5 full seconds, dumping every byte received with timestamp.
 *
 * This tries to catch slow responses, post-warmup responses, or any byte at
 * all coming back from the SunSaver.
 */

#include <ArduinoRS485.h>

static const uint32_t BAUD_RATE       = 9600;
static const uint8_t  SLAVE_ID        = 1;
static const uint8_t  FUNCTION_CODE   = 0x04;  // Read Input Registers
static const uint16_t REG_ADDR        = 0x0008;  // Filtered battery voltage
static const uint16_t REG_COUNT       = 1;
static const uint32_t POLL_INTERVAL_MS = 8000;
static const uint32_t LISTEN_WINDOW_MS = 5000;

static uint16_t crc16Modbus(const uint8_t *data, size_t len) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < len; ++i) {
    crc ^= (uint16_t)data[i];
    for (uint8_t b = 0; b < 8; ++b) {
      if (crc & 0x0001) { crc >>= 1; crc ^= 0xA001; }
      else              { crc >>= 1; }
    }
  }
  return crc;
}

static void printHexByte(uint8_t b) {
  if (b < 0x10) Serial.print('0');
  Serial.print(b, HEX);
}

static void buildFrame(uint8_t *frame, uint8_t slaveId, uint8_t fn,
                       uint16_t addr, uint16_t count) {
  frame[0] = slaveId;
  frame[1] = fn;
  frame[2] = (uint8_t)(addr >> 8);
  frame[3] = (uint8_t)(addr & 0xFF);
  frame[4] = (uint8_t)(count >> 8);
  frame[5] = (uint8_t)(count & 0xFF);
  uint16_t crc = crc16Modbus(frame, 6);
  frame[6] = (uint8_t)(crc & 0xFF);
  frame[7] = (uint8_t)(crc >> 8);
}

static void txQuery() {
  uint8_t frame[8];
  buildFrame(frame, SLAVE_ID, FUNCTION_CODE, REG_ADDR, REG_COUNT);

  Serial.print(F("  TX ["));
  for (uint8_t i = 0; i < 8; ++i) {
    if (i) Serial.print(' ');
    printHexByte(frame[i]);
  }
  Serial.print(F("] @ ms="));
  Serial.println(millis());

  RS485.beginTransmission();
  RS485.write(frame, 8);
  RS485.endTransmission();
}

void setup() {
  Serial.begin(115200);
  uint32_t s = millis();
  while (!Serial && millis() - s < 3000) delay(10);

  Serial.println();
  Serial.println(F("==============================================="));
  Serial.println(F("SunSaver MPPT patient probe (long listen)"));
  Serial.println(F("  9600 8N2, slave=1, fc=0x04, reg=0x0008"));
  Serial.println(F("  3 queries per cycle, 5s listen window"));
  Serial.println(F("==============================================="));

  RS485.begin(BAUD_RATE, SERIAL_8N2);
  // Use library defaults for delays -- no setDelays() override
}

void loop() {
  static unsigned long lastCycle = 0;
  static unsigned long lastHb = 0;
  static uint32_t cycle = 0;
  static uint32_t totalRxBytes = 0;
  unsigned long now = millis();

  if (now - lastHb >= 1000UL) {
    lastHb = now;
    Serial.print(F("hb ms=")); Serial.print(now);
    Serial.print(F(" total_rx=")); Serial.println(totalRxBytes);
  }

  if (now - lastCycle < POLL_INTERVAL_MS) return;
  lastCycle = now;
  ++cycle;

  Serial.println();
  Serial.println(F("------------------------------------------------"));
  Serial.print(F("Cycle ")); Serial.print(cycle);
  Serial.print(F(" @ ms=")); Serial.println(now);
  Serial.println(F("------------------------------------------------"));

  // Send 3 queries back-to-back with 200ms gaps
  txQuery();
  delay(200);
  txQuery();
  delay(200);
  txQuery();

  // Now listen for 5 full seconds
  Serial.print(F("  RX (5000ms): "));
  RS485.receive();
  unsigned long deadline = millis() + LISTEN_WINDOW_MS;
  uint16_t rxCount = 0;
  while (millis() < deadline) {
    if (RS485.available()) {
      int b = RS485.read();
      if (b >= 0) {
        if (rxCount > 0) Serial.print(' ');
        printHexByte((uint8_t)b);
        ++rxCount;
        ++totalRxBytes;
      }
    }
  }
  RS485.noReceive();
  Serial.print(F("  ["));
  Serial.print(rxCount);
  Serial.println(F(" bytes]"));
}
