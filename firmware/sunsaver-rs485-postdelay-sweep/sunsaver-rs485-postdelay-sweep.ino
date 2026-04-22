/**
 * sunsaver-rs485-postdelay-sweep.ino
 *
 * Purpose:
 * - Reproduce the exact Opta TX-side mitigation pattern from the Arduino forum
 *   thread "Opta RS485 bug: last byte of any frame is modified" (post #16/#18):
 *     RS485.noReceive();
 *     RS485.beginTransmission();
 *     RS485.write(frame, len);
 *     RS485.flush();
 *     delay(1);
 *     RS485.endTransmission();
 *     RS485.receive();
 * - Sweep post-delay in multiples of 1 character time at 9600 baud (8N1 and 8N2).
 * - Compare against our prior flush A/B and 1x char-time probe.
 *
 * Why this differs from prior probes:
 * - Prior probes used RS485.setDelays(0, postDelayUs) only.
 * - This probe ALSO performs explicit noReceive/receive bracketing and adds a
 *   delay(1) between flush() and endTransmission() exactly like the forum sketch.
 */

#include <ArduinoRS485.h>

struct PostDelayProfile {
  uint32_t postUs;
  const char *name;
};

struct SerialProfile {
  uint16_t cfg;
  uint8_t bitsPerChar;
  const char *name;
};

struct QueryProfile {
  uint8_t fn;
  uint16_t a;
  uint16_t b;
  const char *name;
};

struct Stats {
  uint32_t steps;
  uint32_t validResponse;
  uint32_t validException;
  uint32_t silent;
  uint32_t oneByteZero;
  uint32_t noValidFrame;
};

static const uint32_t kBaud = 9600;

// 1 char time at 9600 8N1 = ~1042 us, at 9600 8N2 = ~1146 us.
// Sweep 1x, 2x, 4x, 8x of an 11-bit (8N2) char time so values cover both configs.
static const PostDelayProfile kPostDelays[] = {
  {1146,  "1x-charT"},
  {2300,  "2x-charT"},
  {4600,  "4x-charT"},
  {9200,  "8x-charT"}
};

static const SerialProfile kSerialProfiles[] = {
  {SERIAL_8N1, 10, "8N1"},
  {SERIAL_8N2, 11, "8N2"}
};

static const uint8_t kSlaveIds[] = {1, 2};

static const QueryProfile kQueries[] = {
  {0x04, 0x0008, 1, "FC04 reg0008"},
  {0x03, 0x0012, 1, "FC03 reg0012"}
};

static const uint16_t kListenWindowMs = 1200;
static const uint16_t kStepIntervalMs = 1500;
static const uint16_t kMaxRxBytes = 120;

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

static void buildRead(uint8_t *frame, uint8_t slave, uint8_t fn,
                      uint16_t addr, uint16_t count) {
  frame[0] = slave;
  frame[1] = fn;
  frame[2] = (uint8_t)(addr >> 8);
  frame[3] = (uint8_t)(addr & 0xFF);
  frame[4] = (uint8_t)(count >> 8);
  frame[5] = (uint8_t)(count & 0xFF);
  uint16_t crc = crc16Modbus(frame, 6);
  frame[6] = (uint8_t)(crc & 0xFF);
  frame[7] = (uint8_t)(crc >> 8);
}

// Forum-pattern transmit: explicit noReceive + flush + delay(1) + endTransmission + receive.
static void sendForumPattern(const uint8_t *frame, uint8_t len) {
  RS485.noReceive();
  RS485.beginTransmission();
  RS485.write(frame, len);
  RS485.flush();
  delay(1);
  RS485.endTransmission();
  RS485.receive();
}

static bool tryParseFrameAt(const uint8_t *rx, uint16_t rxLen, uint16_t start,
                            uint8_t expectedSlave, uint8_t expectedFn,
                            bool &isException, uint8_t &exCode,
                            uint16_t &value16, uint16_t &frameLen) {
  if (start + 5 > rxLen) return false;
  if (rx[start] != expectedSlave) return false;

  uint8_t fn = rx[start + 1];
  uint16_t len = 0;
  bool ex = false;
  uint8_t byteCount = 0;

  if (fn == (uint8_t)(expectedFn | 0x80)) {
    len = 5;
    ex = true;
  } else if (fn == expectedFn) {
    byteCount = rx[start + 2];
    len = (uint16_t)(5 + byteCount);
  } else {
    return false;
  }

  if (start + len > rxLen) return false;

  uint16_t crcCalc = crc16Modbus(&rx[start], len - 2);
  uint16_t crcWire = (uint16_t)rx[start + len - 2] | ((uint16_t)rx[start + len - 1] << 8);
  if (crcCalc != crcWire) return false;

  isException = ex;
  exCode = ex ? rx[start + 2] : 0;
  frameLen = len;
  value16 = 0;
  if (!ex && byteCount >= 2) {
    value16 = (uint16_t)(((uint16_t)rx[start + 3] << 8) | (uint16_t)rx[start + 4]);
  }
  return true;
}

static bool parseAny(const uint8_t *rx, uint16_t rxLen, uint8_t expectedSlave,
                     uint8_t expectedFn, bool &isException, uint8_t &exCode,
                     uint16_t &value16, uint16_t &frameLen) {
  if (rxLen < 5) return false;
  for (uint16_t i = 0; i + 5 <= rxLen; ++i) {
    if (tryParseFrameAt(rx, rxLen, i, expectedSlave, expectedFn,
                        isException, exCode, value16, frameLen)) {
      return true;
    }
  }
  return false;
}

static void flushRx(uint16_t ms) {
  RS485.receive();
  uint32_t endAt = millis() + ms;
  while ((int32_t)(millis() - endAt) < 0) {
    while (RS485.available()) {
      RS485.read();
    }
  }
}

static void runStep(uint32_t step) {
  const uint8_t pdCount = (uint8_t)(sizeof(kPostDelays) / sizeof(kPostDelays[0]));
  const uint8_t serCount = (uint8_t)(sizeof(kSerialProfiles) / sizeof(kSerialProfiles[0]));
  const uint8_t slaveCount = (uint8_t)(sizeof(kSlaveIds) / sizeof(kSlaveIds[0]));
  const uint8_t qCount = (uint8_t)(sizeof(kQueries) / sizeof(kQueries[0]));
  uint32_t total = (uint32_t)pdCount * (uint32_t)serCount * (uint32_t)slaveCount * (uint32_t)qCount;

  uint32_t idx = step % total;
  uint8_t pdIdx = (uint8_t)(idx % pdCount); idx /= pdCount;
  uint8_t serIdx = (uint8_t)(idx % serCount); idx /= serCount;
  uint8_t slaveIdx = (uint8_t)(idx % slaveCount); idx /= slaveCount;
  uint8_t qIdx = (uint8_t)(idx % qCount);

  const PostDelayProfile &pd = kPostDelays[pdIdx];
  const SerialProfile &sp = kSerialProfiles[serIdx];
  uint8_t slave = kSlaveIds[slaveIdx];
  const QueryProfile &q = kQueries[qIdx];

  RS485.end();
  delay(5);
  RS485.begin(kBaud, sp.cfg);
  RS485.setDelays(0, (int)pd.postUs);

  flushRx(10);
  RS485.noReceive();

  uint8_t tx[8];
  buildRead(tx, slave, q.fn, q.a, q.b);

  Serial.println();
  Serial.print(F("STEP ")); Serial.print(step + 1);
  Serial.print(F(" tuple=")); Serial.print(sp.name);
  Serial.print(F(", post=")); Serial.print(pd.name);
  Serial.print(F("(")); Serial.print(pd.postUs); Serial.print(F("us)"));
  Serial.print(F(", slave=")); Serial.print(slave);
  Serial.print(F(", ")); Serial.println(q.name);
  Serial.print(F("  TX: ")); printHexBuf(tx, 8); Serial.println();

  sendForumPattern(tx, 8);

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

  Serial.print(F("  RX(")); Serial.print(kListenWindowMs); Serial.print(F("ms): "));
  if (rxLen == 0) Serial.print(F("<none>")); else printHexBuf(rx, rxLen);
  Serial.print(F(" [")); Serial.print(rxLen); Serial.println(F(" bytes]"));

  bool ex = false;
  uint8_t exCode = 0;
  uint16_t v16 = 0;
  uint16_t flen = 0;
  bool ok = parseAny(rx, rxLen, slave, q.fn, ex, exCode, v16, flen);

  if (ok) {
    if (ex) {
      ++gStats.validException;
      Serial.print(F("  VALID EXCEPTION len=")); Serial.print(flen);
      Serial.print(F(" code=0x")); printHexByte(exCode); Serial.println();
    } else {
      ++gStats.validResponse;
      Serial.print(F("  VALID RESPONSE len=")); Serial.print(flen);
      Serial.print(F(" value16=0x"));
      printHexByte((uint8_t)(v16 >> 8));
      printHexByte((uint8_t)(v16 & 0xFF));
      Serial.print(F(" (")); Serial.print(v16); Serial.println(F(")"));
    }
  } else {
    if (rxLen == 0) {
      ++gStats.silent;
      Serial.println(F("  silent"));
    } else if (rxLen == 1 && rx[0] == 0x00) {
      ++gStats.oneByteZero;
      Serial.println(F("  single 0x00 artifact"));
    } else {
      ++gStats.noValidFrame;
      Serial.println(F("  bytes seen but no CRC-valid frame"));
    }
  }

  ++gStats.steps;
}

void setup() {
  Serial.begin(115200);
  uint32_t t0 = millis();
  while (!Serial && millis() - t0 < 3000) delay(10);

  Serial.println();
  Serial.println(F("================================================"));
  Serial.println(F("SunSaver RS485 post-delay sweep (forum pattern)"));
  Serial.println(F("  TX bracket: noReceive + flush + delay(1) + endTransmission + receive"));
  Serial.println(F("  Sweep: postDelay 1x/2x/4x/8x charT, 8N1 and 8N2"));
  Serial.println(F("================================================"));

  RS485.begin(kBaud, SERIAL_8N2);
  RS485.setDelays(0, 1146);
}

void loop() {
  static uint32_t step = 0;
  static uint32_t lastStep = 0;
  static uint32_t lastHb = 0;
  static uint32_t cycle = 0;

  const uint8_t pdCount = (uint8_t)(sizeof(kPostDelays) / sizeof(kPostDelays[0]));
  const uint8_t serCount = (uint8_t)(sizeof(kSerialProfiles) / sizeof(kSerialProfiles[0]));
  const uint8_t slaveCount = (uint8_t)(sizeof(kSlaveIds) / sizeof(kSlaveIds[0]));
  const uint8_t qCount = (uint8_t)(sizeof(kQueries) / sizeof(kQueries[0]));
  const uint32_t totalPerCycle = (uint32_t)pdCount * (uint32_t)serCount * (uint32_t)slaveCount * (uint32_t)qCount;

  uint32_t now = millis();

  if (now - lastHb >= 1000UL) {
    lastHb = now;
    Serial.print(F("hb ms=")); Serial.print(now);
    Serial.print(F(" step=")); Serial.println(step + 1);
  }

  if (now - lastStep < kStepIntervalMs) return;
  lastStep = now;

  runStep(step);
  ++step;

  if ((step % totalPerCycle) == 0) {
    ++cycle;
    Serial.println();
    Serial.println(F("====== POSTDELAY SWEEP SUMMARY ======"));
    Serial.print(F("cycle=")); Serial.println(cycle);
    Serial.print(F("steps=")); Serial.println(gStats.steps);
    Serial.print(F("valid_response=")); Serial.println(gStats.validResponse);
    Serial.print(F("valid_exception=")); Serial.println(gStats.validException);
    Serial.print(F("silent=")); Serial.println(gStats.silent);
    Serial.print(F("one_byte_zero=")); Serial.println(gStats.oneByteZero);
    Serial.print(F("no_valid_frame=")); Serial.println(gStats.noValidFrame);
    Serial.println(F("====================================="));

    gStats.steps = 0;
    gStats.validResponse = 0;
    gStats.validException = 0;
    gStats.silent = 0;
    gStats.oneByteZero = 0;
    gStats.noValidFrame = 0;
  }
}
