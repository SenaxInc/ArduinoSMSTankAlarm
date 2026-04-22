/**
 * sunsaver-rs485-boot-window.ino
 *
 * Purpose:
 * - Detect short-lived response windows right after reset/power-up.
 * - Poll rapidly for 60s using fixed known settings (9600 8N2, slave 1)
 *   and CRC-validate any returned Modbus frame.
 */

#include <ArduinoRS485.h>

struct QueryProfile {
  uint8_t fn;
  uint16_t addr;
  uint16_t count;
  const char *name;
};

static const uint8_t kSlaveId = 1;
static const uint32_t kBaud = 9600;
static const uint32_t kBootWindowMs = 60000;
static const uint16_t kListenWindowMs = 250;
static const uint16_t kInterStepMs = 180;
static const uint16_t kMaxRx = 96;

static const QueryProfile kQueries[] = {
  {0x04, 0x0008, 1, "FC04 reg0008"},
  {0x03, 0x0012, 1, "FC03 reg0012"},
  {0x03, 0x0025, 1, "FC03 reg0025"}
};

struct Stats {
  uint32_t steps;
  uint32_t validResponse;
  uint32_t validException;
  uint32_t oneByteZero;
  uint32_t otherBytesNoFrame;
  uint32_t silent;
};

static Stats gStats = {0, 0, 0, 0, 0, 0};

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

static void printHexBuf(const uint8_t *buf, uint16_t len) {
  for (uint16_t i = 0; i < len; ++i) {
    if (i) Serial.print(' ');
    printHexByte(buf[i]);
  }
}

static void buildRequest(uint8_t *frame, uint8_t slaveId, uint8_t fn,
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

static bool parseValidReply(const uint8_t *rx, uint16_t rxLen,
                            uint8_t expectedSlave, uint8_t expectedFn,
                            bool &isException, uint8_t &exCode,
                            uint16_t &reg, uint16_t &frameLen) {
  isException = false;
  exCode = 0;
  reg = 0;
  frameLen = 0;

  if (rxLen < 5) return false;

  for (uint16_t i = 0; i + 5 <= rxLen; ++i) {
    if (rx[i] != expectedSlave) continue;

    uint8_t fn = rx[i + 1];
    uint16_t len = 0;
    bool ex = false;

    if (fn == (uint8_t)(expectedFn | 0x80)) {
      len = 5;
      ex = true;
    } else if (fn == expectedFn) {
      uint8_t byteCount = rx[i + 2];
      len = (uint16_t)(5 + byteCount);
    } else {
      continue;
    }

    if (i + len > rxLen) continue;

    uint16_t crcCalc = crc16Modbus(&rx[i], len - 2);
    uint16_t crcWire = (uint16_t)rx[i + len - 2] | ((uint16_t)rx[i + len - 1] << 8);
    if (crcCalc != crcWire) continue;

    frameLen = len;
    isException = ex;
    if (ex) {
      exCode = rx[i + 2];
      return true;
    }

    if (rx[i + 2] >= 2) {
      reg = (uint16_t)(((uint16_t)rx[i + 3] << 8) | (uint16_t)rx[i + 4]);
    }
    return true;
  }

  return false;
}

void setup() {
  Serial.begin(115200);
  uint32_t t0 = millis();
  while (!Serial && millis() - t0 < 3000) delay(10);

  Serial.println();
  Serial.println(F("=============================================="));
  Serial.println(F("SunSaver RS-485 boot-window catcher"));
  Serial.print(F("boot window ms=")); Serial.println(kBootWindowMs);
  Serial.print(F("listen window ms=")); Serial.println(kListenWindowMs);
  Serial.println(F("=============================================="));

  RS485.begin(kBaud, SERIAL_8N2);
  RS485.setDelays(50, 50);
}

void loop() {
  static uint32_t bootStart = millis();
  static uint32_t step = 0;
  static uint32_t lastStepAt = 0;
  static uint32_t lastHeartbeat = 0;
  static bool finalPrinted = false;

  uint32_t now = millis();

  if (now - lastHeartbeat >= 1000UL) {
    lastHeartbeat = now;
    Serial.print(F("hb ms=")); Serial.print(now);
    Serial.print(F(" elapsed=")); Serial.println(now - bootStart);
  }

  if (now - bootStart >= kBootWindowMs) {
    if (!finalPrinted) {
      finalPrinted = true;
      Serial.println();
      Serial.println(F("=========== BOOT WINDOW SUMMARY ==========="));
      Serial.print(F("steps=")); Serial.println(gStats.steps);
      Serial.print(F("valid_response=")); Serial.println(gStats.validResponse);
      Serial.print(F("valid_exception=")); Serial.println(gStats.validException);
      Serial.print(F("one_byte_zero=")); Serial.println(gStats.oneByteZero);
      Serial.print(F("other_bytes_no_frame=")); Serial.println(gStats.otherBytesNoFrame);
      Serial.print(F("silent=")); Serial.println(gStats.silent);
      Serial.println(F("==========================================="));
      Serial.println(F("Boot window complete. Reboot to rerun."));
    }
    delay(200);
    return;
  }

  if (now - lastStepAt < kInterStepMs) {
    return;
  }
  lastStepAt = now;

  const QueryProfile &q = kQueries[step % (sizeof(kQueries) / sizeof(kQueries[0]))];
  uint8_t tx[8];
  buildRequest(tx, kSlaveId, q.fn, q.addr, q.count);

  Serial.print(F("STEP ")); Serial.print(step + 1);
  Serial.print(F(" ")); Serial.println(q.name);
  Serial.print(F("  TX: ")); printHexBuf(tx, 8); Serial.println();

  RS485.noReceive();
  RS485.beginTransmission();
  RS485.write(tx, 8);
  RS485.endTransmission();
  RS485.receive();

  uint8_t rx[kMaxRx];
  uint16_t rxLen = 0;
  uint32_t deadline = millis() + kListenWindowMs;
  while ((int32_t)(millis() - deadline) < 0) {
    while (RS485.available()) {
      int b = RS485.read();
      if (b >= 0 && rxLen < kMaxRx) {
        rx[rxLen++] = (uint8_t)b;
      }
    }
  }
  RS485.noReceive();

  Serial.print(F("  RX: "));
  if (rxLen == 0) {
    Serial.print(F("<none>"));
  } else {
    printHexBuf(rx, rxLen);
  }
  Serial.print(F(" [")); Serial.print(rxLen); Serial.println(F(" bytes]"));

  bool ex = false;
  uint8_t exCode = 0;
  uint16_t reg = 0;
  uint16_t frameLen = 0;
  bool ok = parseValidReply(rx, rxLen, kSlaveId, q.fn, ex, exCode, reg, frameLen);

  if (ok) {
    if (ex) {
      gStats.validException++;
      Serial.print(F("  VALID EXCEPTION len=")); Serial.print(frameLen);
      Serial.print(F(" code=0x")); printHexByte(exCode); Serial.println();
    } else {
      gStats.validResponse++;
      Serial.print(F("  VALID RESPONSE len=")); Serial.print(frameLen);
      Serial.print(F(" reg=0x")); printHexByte((uint8_t)(reg >> 8));
      printHexByte((uint8_t)(reg & 0xFF));
      Serial.print(F(" (")); Serial.print(reg); Serial.println(F(")"));
    }
  } else {
    if (rxLen == 0) {
      gStats.silent++;
      Serial.println(F("  silent"));
    } else if (rxLen == 1 && rx[0] == 0x00) {
      gStats.oneByteZero++;
      Serial.println(F("  one-byte 0x00 artifact"));
    } else {
      gStats.otherBytesNoFrame++;
      Serial.println(F("  bytes seen but no CRC-valid frame"));
    }
  }

  gStats.steps++;
  step++;
}
