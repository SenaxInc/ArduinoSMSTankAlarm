/**
 * sunsaver-rs485-flush-ab.ino
 *
 * Purpose:
 * - Compare two transmit modes on Opta RS-485 path:
 *   A) baseline beginTransmission/write/endTransmission
 *   B) same sequence plus explicit RS485.flush() before endTransmission
 * - Determine whether explicit flush changes response quality/counters.
 */

#include <ArduinoRS485.h>

struct TxStyle {
  uint8_t mode;
  const char *name;
};

struct QueryProfile {
  uint8_t kind;
  uint8_t fn;
  uint16_t a;
  uint16_t b;
  const char *name;
};

struct ParsedFrame {
  bool isException;
  uint8_t fn;
  uint8_t exCode;
  uint8_t byteCount;
  uint16_t value16;
  uint16_t len;
};

struct Stats {
  uint32_t steps;
  uint32_t validResponse;
  uint32_t validException;
  uint32_t silent;
  uint32_t oneByteZero;
  uint32_t noValidFrame;
};

static const uint8_t KIND_READ = 1;
static const uint8_t KIND_FC11 = 2;
static const uint8_t KIND_FC08 = 3;

static const TxStyle kTxStyles[] = {
  {0, "tx-contiguous"},
  {1, "tx-byte-paced"},
  {2, "tx-preamble"}
};

static const uint8_t kSlaveIds[] = {1, 2};

static const QueryProfile kQueries[] = {
  {KIND_READ, 0x04, 0x0008, 1, "FC04 reg0008"},
  {KIND_READ, 0x03, 0x0012, 1, "FC03 reg0012"},
  {KIND_FC11, 0x11, 0, 0, "FC11 report-slave-id"},
  {KIND_FC08, 0x08, 0x0000, 0xA55A, "FC08 loopback"}
};

static const uint32_t kBaud = 9600;
static const uint16_t kListenWindowMs = 1100;
static const uint16_t kStepIntervalMs = 1400;
static const uint16_t kMaxRxBytes = 120;

static Stats gBaseline = {0, 0, 0, 0, 0, 0};
static Stats gExtraFlush = {0, 0, 0, 0, 0, 0};

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

static uint8_t buildRequest(const QueryProfile &q, uint8_t slave, uint8_t *frame) {
  if (q.kind == KIND_READ) {
    frame[0] = slave;
    frame[1] = q.fn;
    frame[2] = (uint8_t)(q.a >> 8);
    frame[3] = (uint8_t)(q.a & 0xFF);
    frame[4] = (uint8_t)(q.b >> 8);
    frame[5] = (uint8_t)(q.b & 0xFF);
    uint16_t crc = crc16Modbus(frame, 6);
    frame[6] = (uint8_t)(crc & 0xFF);
    frame[7] = (uint8_t)(crc >> 8);
    return 8;
  }

  if (q.kind == KIND_FC11) {
    frame[0] = slave;
    frame[1] = q.fn;
    uint16_t crc = crc16Modbus(frame, 2);
    frame[2] = (uint8_t)(crc & 0xFF);
    frame[3] = (uint8_t)(crc >> 8);
    return 4;
  }

  frame[0] = slave;
  frame[1] = q.fn;
  frame[2] = (uint8_t)(q.a >> 8);
  frame[3] = (uint8_t)(q.a & 0xFF);
  frame[4] = (uint8_t)(q.b >> 8);
  frame[5] = (uint8_t)(q.b & 0xFF);
  uint16_t crc = crc16Modbus(frame, 6);
  frame[6] = (uint8_t)(crc & 0xFF);
  frame[7] = (uint8_t)(crc >> 8);
  return 8;
}

static void transmitFrame(const uint8_t *frame, uint8_t len, bool useExtraFlush) {
  RS485.beginTransmission();
  RS485.write(frame, len);
  if (useExtraFlush) {
    RS485.flush();
  }
  RS485.endTransmission();
}

static void transmitFrameBytePaced(const uint8_t *frame, uint8_t len, bool useExtraFlush) {
  RS485.beginTransmission();
  for (uint8_t i = 0; i < len; ++i) {
    RS485.write(frame[i]);
    delay(2);
  }
  if (useExtraFlush) {
    RS485.flush();
  }
  RS485.endTransmission();
}

static void sendFrame(const TxStyle &style, const uint8_t *frame, uint8_t len, bool useExtraFlush) {
  if (style.mode == 0) {
    transmitFrame(frame, len, useExtraFlush);
    return;
  }

  if (style.mode == 1) {
    transmitFrameBytePaced(frame, len, useExtraFlush);
    return;
  }

  uint8_t preamble[4] = {0xFF, 0xFF, 0xFF, 0xFF};
  transmitFrame(preamble, 4, useExtraFlush);
  delay(4);
  transmitFrame(frame, len, useExtraFlush);
}

static bool tryParseFrameAt(const uint8_t *rx, uint16_t rxLen, uint16_t start,
                            uint8_t expectedSlave, ParsedFrame &out) {
  if (start + 5 > rxLen) return false;
  if (rx[start] != expectedSlave) return false;

  uint8_t fn = rx[start + 1];
  uint16_t frameLen = 0;
  bool isException = false;
  uint8_t byteCount = 0;

  if (fn & 0x80) {
    frameLen = 5;
    isException = true;
  } else if (fn == 0x03 || fn == 0x04 || fn == 0x11) {
    byteCount = rx[start + 2];
    frameLen = (uint16_t)(5 + byteCount);
  } else if (fn == 0x08) {
    frameLen = 8;
  } else if (fn == 0x07) {
    frameLen = 5;
  } else {
    return false;
  }

  if (start + frameLen > rxLen) return false;

  uint16_t crcCalc = crc16Modbus(&rx[start], frameLen - 2);
  uint16_t crcWire = (uint16_t)rx[start + frameLen - 2] | ((uint16_t)rx[start + frameLen - 1] << 8);
  if (crcCalc != crcWire) return false;

  out.isException = isException;
  out.fn = fn;
  out.exCode = isException ? rx[start + 2] : 0;
  out.byteCount = byteCount;
  out.value16 = 0;
  out.len = frameLen;

  if (!isException) {
    if ((fn == 0x03 || fn == 0x04 || fn == 0x11) && byteCount >= 2) {
      out.value16 = (uint16_t)(((uint16_t)rx[start + 3] << 8) | (uint16_t)rx[start + 4]);
    } else if (fn == 0x08) {
      out.value16 = (uint16_t)(((uint16_t)rx[start + 4] << 8) | (uint16_t)rx[start + 5]);
    } else if (fn == 0x07) {
      out.value16 = (uint16_t)rx[start + 2];
    }
  }

  return true;
}

static bool parseAnyValidFrame(const uint8_t *rx, uint16_t rxLen, uint8_t expectedSlave,
                               ParsedFrame &out) {
  if (rxLen < 5) return false;

  for (uint16_t i = 0; i + 5 <= rxLen; ++i) {
    if (tryParseFrameAt(rx, rxLen, i, expectedSlave, out)) {
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
  RS485.noReceive();
}

static void runStep(uint32_t step, bool useExtraFlush, Stats &stats) {
  const uint8_t txCount = (uint8_t)(sizeof(kTxStyles) / sizeof(kTxStyles[0]));
  const uint8_t slaveCount = (uint8_t)(sizeof(kSlaveIds) / sizeof(kSlaveIds[0]));
  const uint8_t queryCount = (uint8_t)(sizeof(kQueries) / sizeof(kQueries[0]));
  uint32_t total = (uint32_t)txCount * (uint32_t)slaveCount * (uint32_t)queryCount;

  uint32_t idx = step % total;
  uint8_t txIdx = (uint8_t)(idx % txCount); idx /= txCount;
  uint8_t slaveIdx = (uint8_t)(idx % slaveCount); idx /= slaveCount;
  uint8_t queryIdx = (uint8_t)(idx % queryCount);

  const TxStyle &tx = kTxStyles[txIdx];
  uint8_t slaveId = kSlaveIds[slaveIdx];
  const QueryProfile &q = kQueries[queryIdx];

  uint8_t frame[12];
  uint8_t frameLen = buildRequest(q, slaveId, frame);

  flushRx(10);

  Serial.println();
  Serial.print(F("STEP ")); Serial.print(step + 1);
  Serial.print(F(" mode=")); Serial.print(useExtraFlush ? F("extra-flush") : F("baseline"));
  Serial.print(F(" tuple="));
  Serial.print(tx.name);
  Serial.print(F(", slave=")); Serial.print(slaveId);
  Serial.print(F(", ")); Serial.println(q.name);
  Serial.print(F("  TX: "));
  printHexBuf(frame, frameLen);
  Serial.println();

  sendFrame(tx, frame, frameLen, useExtraFlush);

  uint8_t rx[kMaxRxBytes];
  uint16_t rxLen = 0;
  RS485.receive();
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
  if (rxLen == 0) {
    Serial.print(F("<none>"));
  } else {
    printHexBuf(rx, rxLen);
  }
  Serial.print(F(" [")); Serial.print(rxLen); Serial.println(F(" bytes]"));

  ParsedFrame parsed;
  bool ok = parseAnyValidFrame(rx, rxLen, slaveId, parsed);
  if (ok) {
    if (parsed.isException) {
      ++stats.validException;
      Serial.print(F("  VALID EXCEPTION fn=0x")); printHexByte(parsed.fn);
      Serial.print(F(" len=")); Serial.print(parsed.len);
      Serial.print(F(" code=0x")); printHexByte(parsed.exCode);
      Serial.println();
    } else {
      ++stats.validResponse;
      Serial.print(F("  VALID RESPONSE fn=0x")); printHexByte(parsed.fn);
      Serial.print(F(" len=")); Serial.print(parsed.len);
      Serial.print(F(" value16=0x"));
      printHexByte((uint8_t)(parsed.value16 >> 8));
      printHexByte((uint8_t)(parsed.value16 & 0xFF));
      Serial.print(F(" (")); Serial.print(parsed.value16); Serial.println(F(")"));
    }
  } else {
    if (rxLen == 0) {
      ++stats.silent;
      Serial.println(F("  silent"));
    } else if (rxLen == 1 && rx[0] == 0x00) {
      ++stats.oneByteZero;
      Serial.println(F("  single 0x00 artifact"));
    } else {
      ++stats.noValidFrame;
      Serial.println(F("  bytes seen but no CRC-valid frame"));
    }
  }

  ++stats.steps;
}

static void printSummary(const char *title, const Stats &s) {
  Serial.println();
  Serial.println(F("========== FLUSH A/B SUMMARY =========="));
  Serial.print(F("phase=")); Serial.println(title);
  Serial.print(F("steps=")); Serial.println(s.steps);
  Serial.print(F("valid_response=")); Serial.println(s.validResponse);
  Serial.print(F("valid_exception=")); Serial.println(s.validException);
  Serial.print(F("silent=")); Serial.println(s.silent);
  Serial.print(F("one_byte_zero=")); Serial.println(s.oneByteZero);
  Serial.print(F("no_valid_frame=")); Serial.println(s.noValidFrame);
  Serial.println(F("======================================="));
}

void setup() {
  Serial.begin(115200);
  uint32_t t0 = millis();
  while (!Serial && millis() - t0 < 3000) delay(10);

  Serial.println();
  Serial.println(F("================================================"));
  Serial.println(F("SunSaver RS485 flush A/B probe"));
  Serial.println(F("  Phase A: baseline TX (no explicit RS485.flush)"));
  Serial.println(F("  Phase B: explicit RS485.flush before endTransmission"));
  Serial.println(F("  Serial fixed: 9600 8N2, delays 50/50"));
  Serial.println(F("================================================"));

  RS485.begin(kBaud, SERIAL_8N2);
  RS485.setDelays(50, 50);
}

void loop() {
  static uint8_t phase = 0;
  static uint32_t step = 0;
  static uint32_t lastStep = 0;
  static uint32_t lastHb = 0;
  static bool done = false;

  const uint8_t txCount = (uint8_t)(sizeof(kTxStyles) / sizeof(kTxStyles[0]));
  const uint8_t slaveCount = (uint8_t)(sizeof(kSlaveIds) / sizeof(kSlaveIds[0]));
  const uint8_t queryCount = (uint8_t)(sizeof(kQueries) / sizeof(kQueries[0]));
  const uint32_t totalPerPhase = (uint32_t)txCount * (uint32_t)slaveCount * (uint32_t)queryCount;

  uint32_t now = millis();

  if (now - lastHb >= 1000UL) {
    lastHb = now;
    Serial.print(F("hb ms=")); Serial.print(now);
    Serial.print(F(" phase=")); Serial.print(phase == 0 ? F("baseline") : F("extra-flush"));
    Serial.print(F(" step=")); Serial.println(step + 1);
  }

  if (done) {
    delay(250);
    return;
  }

  if (now - lastStep < kStepIntervalMs) {
    return;
  }

  lastStep = now;

  if (phase == 0) {
    runStep(step, false, gBaseline);
  } else {
    runStep(step, true, gExtraFlush);
  }

  ++step;

  if (step >= totalPerPhase) {
    if (phase == 0) {
      printSummary("baseline", gBaseline);
      phase = 1;
      step = 0;
      Serial.println();
      Serial.println(F("---- Switching to EXTRA-FLUSH phase ----"));
      return;
    }

    printSummary("extra-flush", gExtraFlush);
    Serial.println();
    Serial.println(F("==== A/B DELTA (extra-flush - baseline) ===="));
    Serial.print(F("delta_valid_response=")); Serial.println((int32_t)gExtraFlush.validResponse - (int32_t)gBaseline.validResponse);
    Serial.print(F("delta_valid_exception=")); Serial.println((int32_t)gExtraFlush.validException - (int32_t)gBaseline.validException);
    Serial.print(F("delta_silent=")); Serial.println((int32_t)gExtraFlush.silent - (int32_t)gBaseline.silent);
    Serial.print(F("delta_one_byte_zero=")); Serial.println((int32_t)gExtraFlush.oneByteZero - (int32_t)gBaseline.oneByteZero);
    Serial.print(F("delta_no_valid_frame=")); Serial.println((int32_t)gExtraFlush.noValidFrame - (int32_t)gBaseline.noValidFrame);
    Serial.println(F("==========================================="));
    Serial.println(F("Flush A/B complete. Reset board to rerun."));

    done = true;
  }
}
