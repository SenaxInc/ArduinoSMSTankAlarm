/**
 * sunsaver-rs485-coil-probe.ino
 *
 * Purpose:
 * - Add a focused Modbus coil-path diagnostic to the existing SunSaver bring-up suite.
 * - Probe FC01 (read coils) and control-coil style requests FC05/FC15.
 * - Treat any CRC-valid normal frame OR exception frame as proof of transport-level
 *   Modbus communication.
 *
 * Safety:
 * - FC05/FC15 writes are directed to an intentionally high/invalid coil address
 *   (0x7FFF) to avoid toggling real outputs.
 */

#include <ArduinoRS485.h>

struct TxStyle {
  uint8_t mode;
  const char *name;
};

struct QueryProfile {
  uint8_t kind;
  uint8_t fn;
  uint16_t addr;
  uint16_t arg;
  uint8_t dataByte;
  const char *name;
};

struct ParsedFrame {
  bool isException;
  uint8_t fn;
  uint8_t exCode;
  uint8_t byteCount;
  uint8_t firstDataByte;
  uint16_t echoAddr;
  uint16_t echoArg;
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

static const uint8_t KIND_READ_COILS = 1;
static const uint8_t KIND_WRITE_SINGLE = 2;
static const uint8_t KIND_WRITE_MULTI = 3;

static const TxStyle kTxStyles[] = {
  {0, "tx-contiguous"},
  {1, "tx-byte-paced"}
};

static const uint8_t kSlaveIds[] = {1, 2};

static const QueryProfile kQueries[] = {
  {KIND_READ_COILS, 0x01, 0x0000, 8, 0x00, "FC01 coils0000 qty8"},
  {KIND_READ_COILS, 0x01, 0x0010, 8, 0x00, "FC01 coils0010 qty8"},
  {KIND_READ_COILS, 0x01, 0x7FFF, 1, 0x00, "FC01 coils7FFF qty1"},
  {KIND_WRITE_SINGLE, 0x05, 0x7FFF, 0xFF00, 0x00, "FC05 coil7FFF ON"},
  {KIND_WRITE_MULTI, 0x0F, 0x7FFF, 1, 0x01, "FC15 coil7FFF qty1=1"}
};

static const uint32_t kBaud = 9600;
static const uint16_t kListenWindowMs = 1200;
static const uint16_t kStepIntervalMs = 1400;
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

static uint8_t buildRequest(const QueryProfile &q, uint8_t slave, uint8_t *frame) {
  if (q.kind == KIND_READ_COILS) {
    frame[0] = slave;
    frame[1] = q.fn;
    frame[2] = (uint8_t)(q.addr >> 8);
    frame[3] = (uint8_t)(q.addr & 0xFF);
    frame[4] = (uint8_t)(q.arg >> 8);
    frame[5] = (uint8_t)(q.arg & 0xFF);
    uint16_t crc = crc16Modbus(frame, 6);
    frame[6] = (uint8_t)(crc & 0xFF);
    frame[7] = (uint8_t)(crc >> 8);
    return 8;
  }

  if (q.kind == KIND_WRITE_SINGLE) {
    frame[0] = slave;
    frame[1] = q.fn;
    frame[2] = (uint8_t)(q.addr >> 8);
    frame[3] = (uint8_t)(q.addr & 0xFF);
    frame[4] = (uint8_t)(q.arg >> 8);
    frame[5] = (uint8_t)(q.arg & 0xFF);
    uint16_t crc = crc16Modbus(frame, 6);
    frame[6] = (uint8_t)(crc & 0xFF);
    frame[7] = (uint8_t)(crc >> 8);
    return 8;
  }

  frame[0] = slave;
  frame[1] = q.fn;
  frame[2] = (uint8_t)(q.addr >> 8);
  frame[3] = (uint8_t)(q.addr & 0xFF);
  frame[4] = (uint8_t)(q.arg >> 8);
  frame[5] = (uint8_t)(q.arg & 0xFF);
  frame[6] = 1;  // byte count for single-coil payload
  frame[7] = q.dataByte;
  uint16_t crc = crc16Modbus(frame, 8);
  frame[8] = (uint8_t)(crc & 0xFF);
  frame[9] = (uint8_t)(crc >> 8);
  return 10;
}

static void sendFrame(const TxStyle &style, const uint8_t *frame, uint8_t len) {
  if (style.mode == 0) {
    RS485.beginTransmission();
    RS485.write(frame, len);
    RS485.endTransmission();
    return;
  }

  RS485.beginTransmission();
  for (uint8_t i = 0; i < len; ++i) {
    RS485.write(frame[i]);
    delay(2);
  }
  RS485.endTransmission();
}

static bool tryParseFrameAt(const uint8_t *rx, uint16_t rxLen, uint16_t start,
                            uint8_t expectedSlave, uint8_t expectedFn,
                            ParsedFrame &out) {
  if (start + 5 > rxLen) return false;
  if (rx[start] != expectedSlave) return false;

  uint8_t fn = rx[start + 1];
  uint16_t frameLen = 0;
  bool isException = false;

  if (fn == (uint8_t)(expectedFn | 0x80)) {
    frameLen = 5;
    isException = true;
  } else if (fn == expectedFn) {
    if (expectedFn == 0x01) {
      uint8_t byteCount = rx[start + 2];
      frameLen = (uint16_t)(5 + byteCount);
    } else if (expectedFn == 0x05 || expectedFn == 0x0F) {
      frameLen = 8;
    } else {
      return false;
    }
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
  out.len = frameLen;
  out.byteCount = 0;
  out.firstDataByte = 0;
  out.echoAddr = 0;
  out.echoArg = 0;

  if (!isException) {
    if (expectedFn == 0x01) {
      out.byteCount = rx[start + 2];
      if (out.byteCount > 0) {
        out.firstDataByte = rx[start + 3];
      }
    } else {
      out.echoAddr = (uint16_t)(((uint16_t)rx[start + 2] << 8) | (uint16_t)rx[start + 3]);
      out.echoArg = (uint16_t)(((uint16_t)rx[start + 4] << 8) | (uint16_t)rx[start + 5]);
    }
  }

  return true;
}

static bool parseAnyValidFrame(const uint8_t *rx, uint16_t rxLen,
                               uint8_t expectedSlave, uint8_t expectedFn,
                               ParsedFrame &out) {
  if (rxLen < 5) return false;

  for (uint16_t i = 0; i + 5 <= rxLen; ++i) {
    if (tryParseFrameAt(rx, rxLen, i, expectedSlave, expectedFn, out)) {
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

static void runStep(uint32_t step) {
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

  uint8_t frame[16];
  uint8_t frameLen = buildRequest(q, slaveId, frame);

  flushRx(10);

  Serial.println();
  Serial.print(F("STEP ")); Serial.print(step + 1);
  Serial.print(F(" tuple="));
  Serial.print(tx.name);
  Serial.print(F(", slave=")); Serial.print(slaveId);
  Serial.print(F(", ")); Serial.println(q.name);
  Serial.print(F("  TX: "));
  printHexBuf(frame, frameLen);
  Serial.println();

  sendFrame(tx, frame, frameLen);

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
  bool ok = parseAnyValidFrame(rx, rxLen, slaveId, q.fn, parsed);
  if (ok) {
    if (parsed.isException) {
      ++gStats.validException;
      Serial.print(F("  VALID EXCEPTION fn=0x")); printHexByte(parsed.fn);
      Serial.print(F(" len=")); Serial.print(parsed.len);
      Serial.print(F(" code=0x")); printHexByte(parsed.exCode);
      Serial.println();
    } else {
      ++gStats.validResponse;
      Serial.print(F("  VALID RESPONSE fn=0x")); printHexByte(parsed.fn);
      Serial.print(F(" len=")); Serial.print(parsed.len);
      if (q.fn == 0x01) {
        Serial.print(F(" byteCount=")); Serial.print(parsed.byteCount);
        Serial.print(F(" firstData=0x")); printHexByte(parsed.firstDataByte);
      } else {
        Serial.print(F(" echoAddr=0x"));
        printHexByte((uint8_t)(parsed.echoAddr >> 8));
        printHexByte((uint8_t)(parsed.echoAddr & 0xFF));
        Serial.print(F(" echoArg=0x"));
        printHexByte((uint8_t)(parsed.echoArg >> 8));
        printHexByte((uint8_t)(parsed.echoArg & 0xFF));
      }
      Serial.println();
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
  Serial.println(F("SunSaver RS485 coil probe"));
  Serial.println(F("  Function probes: FC01 + FC05 + FC15"));
  Serial.println(F("  Write safety: FC05/FC15 target coil 0x7FFF"));
  Serial.println(F("  Serial fixed: 9600 8N2, delays 50/50"));
  Serial.println(F("================================================"));

  RS485.begin(kBaud, SERIAL_8N2);
  RS485.setDelays(50, 50);
}

void loop() {
  static uint32_t step = 0;
  static uint32_t lastStep = 0;
  static uint32_t lastHb = 0;
  static uint32_t cycle = 0;

  const uint8_t txCount = (uint8_t)(sizeof(kTxStyles) / sizeof(kTxStyles[0]));
  const uint8_t slaveCount = (uint8_t)(sizeof(kSlaveIds) / sizeof(kSlaveIds[0]));
  const uint8_t queryCount = (uint8_t)(sizeof(kQueries) / sizeof(kQueries[0]));
  const uint32_t totalPerCycle = (uint32_t)txCount * (uint32_t)slaveCount * (uint32_t)queryCount;

  uint32_t now = millis();

  if (now - lastHb >= 1000UL) {
    lastHb = now;
    Serial.print(F("hb ms=")); Serial.print(now);
    Serial.print(F(" step=")); Serial.println(step + 1);
  }

  if (now - lastStep < kStepIntervalMs) {
    return;
  }

  lastStep = now;
  runStep(step);
  ++step;

  if ((step % totalPerCycle) == 0) {
    ++cycle;
    Serial.println();
    Serial.println(F("========== COIL PROBE SUMMARY =========="));
    Serial.print(F("cycle=")); Serial.println(cycle);
    Serial.print(F("steps=")); Serial.println(gStats.steps);
    Serial.print(F("valid_response=")); Serial.println(gStats.validResponse);
    Serial.print(F("valid_exception=")); Serial.println(gStats.validException);
    Serial.print(F("silent=")); Serial.println(gStats.silent);
    Serial.print(F("one_byte_zero=")); Serial.println(gStats.oneByteZero);
    Serial.print(F("no_valid_frame=")); Serial.println(gStats.noValidFrame);
    Serial.println(F("========================================"));

    gStats.steps = 0;
    gStats.validResponse = 0;
    gStats.validException = 0;
    gStats.silent = 0;
    gStats.oneByteZero = 0;
    gStats.noValidFrame = 0;
  }
}
