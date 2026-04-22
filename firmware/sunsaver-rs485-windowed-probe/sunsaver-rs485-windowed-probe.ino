/**
 * sunsaver-rs485-windowed-probe.ino
 *
 * Purpose:
 * - Keep existing hardware unchanged (Opta + MRC-1 + SunSaver).
 * - Differentiate TX->RX turnaround artifact bytes from real replies.
 * - Open RX at controlled offsets (0ms vs 8ms) and validate CRC frames.
 *
 * Why this test matters:
 * Previous probes repeatedly observed a single 0x00 byte. If that byte is
 * generated locally during transceiver direction switching, delaying RX open
 * should remove it. If a real SunSaver response exists later in time, this
 * sketch should capture and validate it.
 */

#include <ArduinoRS485.h>

struct SerialProfile {
  uint32_t baud;
  uint16_t config;
  const char *name;
};

struct DelayProfile {
  int preUs;
  int postUs;
  const char *name;
};

struct QueryProfile {
  uint8_t fn;
  uint16_t addr;
  uint16_t count;
  const char *name;
};

struct RxWindowProfile {
  uint16_t openDelayMs;
  const char *name;
};

static const SerialProfile kSerialProfiles[] = {
  { 9600, SERIAL_8N2, "9600 8N2" },
  { 9600, SERIAL_8N1, "9600 8N1" }
};

static const DelayProfile kDelayProfiles[] = {
  { 0, 0, "dly0/0" },
  { 50, 50, "dly50/50" }
};

static const uint8_t kSlaveIds[] = { 1, 2 };

static const QueryProfile kQueryProfiles[] = {
  { 0x04, 0x0008, 1, "FC04 reg0008" },
  { 0x03, 0x0012, 1, "FC03 reg0012" },
  { 0x03, 0x0025, 1, "FC03 reg0025" }
};

static const RxWindowProfile kRxWindows[] = {
  { 0, "rx@0ms" },
  { 8, "rx@8ms" }
};

static const uint16_t kListenWindowMs = 1600;
static const uint16_t kStepIntervalMs = 1400;
static const uint16_t kMaxRxBytes = 120;

struct Stats {
  uint32_t steps;
  uint32_t validResponse;
  uint32_t validException;
  uint32_t silent;
  uint32_t oneByteZero;
  uint32_t nonFrameBytes;
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

static bool parseValidReply(const uint8_t *rx, uint16_t rxLen, uint8_t expectedSlave,
                            uint8_t expectedFn, uint16_t &outReg,
                            bool &outIsException, uint8_t &outExCode,
                            uint16_t &outFrameLen) {
  outReg = 0;
  outIsException = false;
  outExCode = 0;
  outFrameLen = 0;

  if (rxLen < 5) return false;

  for (uint16_t i = 0; i + 5 <= rxLen; ++i) {
    uint8_t slave = rx[i + 0];
    uint8_t fn = rx[i + 1];

    if (slave != expectedSlave) continue;

    bool isException = false;
    uint16_t frameLen = 0;

    if (fn == (uint8_t)(expectedFn | 0x80)) {
      frameLen = 5;
      isException = true;
    } else if (fn == expectedFn) {
      uint8_t byteCount = rx[i + 2];
      frameLen = (uint16_t)(5 + byteCount);
    } else {
      continue;
    }

    if (i + frameLen > rxLen) continue;

    uint16_t crcCalc = crc16Modbus(&rx[i], frameLen - 2);
    uint16_t crcWire = (uint16_t)rx[i + frameLen - 2] | ((uint16_t)rx[i + frameLen - 1] << 8);
    if (crcCalc != crcWire) continue;

    outFrameLen = frameLen;
    outIsException = isException;

    if (isException) {
      outExCode = rx[i + 2];
      return true;
    }

    if (rx[i + 2] >= 2) {
      outReg = (uint16_t)((uint16_t)rx[i + 3] << 8) | (uint16_t)rx[i + 4];
    }

    return true;
  }

  return false;
}

static void flushRxForMs(uint16_t ms) {
  RS485.receive();
  uint32_t endAt = millis() + ms;
  while ((int32_t)(millis() - endAt) < 0) {
    while (RS485.available()) {
      RS485.read();
    }
  }
  RS485.noReceive();
}

void setup() {
  Serial.begin(115200);
  uint32_t t0 = millis();
  while (!Serial && millis() - t0 < 3000) delay(10);

  Serial.println();
  Serial.println(F("================================================"));
  Serial.println(F("SunSaver RS-485 windowed probe"));
  Serial.println(F("  Distinguish local artifact vs real Modbus reply"));
  Serial.print(F("  RX windows: "));
  Serial.print(kRxWindows[0].name);
  Serial.print(F(", "));
  Serial.println(kRxWindows[1].name);
  Serial.print(F("  Listen window ms: "));
  Serial.println(kListenWindowMs);
  Serial.println(F("================================================"));
}

static void runStep(uint32_t step) {
  const uint8_t serialCount = (uint8_t)(sizeof(kSerialProfiles) / sizeof(kSerialProfiles[0]));
  const uint8_t delayCount = (uint8_t)(sizeof(kDelayProfiles) / sizeof(kDelayProfiles[0]));
  const uint8_t slaveCount = (uint8_t)(sizeof(kSlaveIds) / sizeof(kSlaveIds[0]));
  const uint8_t queryCount = (uint8_t)(sizeof(kQueryProfiles) / sizeof(kQueryProfiles[0]));
  const uint8_t rxWinCount = (uint8_t)(sizeof(kRxWindows) / sizeof(kRxWindows[0]));

  uint32_t total = (uint32_t)serialCount * (uint32_t)delayCount * (uint32_t)slaveCount *
                   (uint32_t)queryCount * (uint32_t)rxWinCount;
  uint32_t idx = step % total;

  uint8_t serialIdx = (uint8_t)(idx % serialCount); idx /= serialCount;
  uint8_t delayIdx = (uint8_t)(idx % delayCount); idx /= delayCount;
  uint8_t slaveIdx = (uint8_t)(idx % slaveCount); idx /= slaveCount;
  uint8_t queryIdx = (uint8_t)(idx % queryCount); idx /= queryCount;
  uint8_t rxIdx = (uint8_t)(idx % rxWinCount);

  const SerialProfile &sp = kSerialProfiles[serialIdx];
  const DelayProfile &dp = kDelayProfiles[delayIdx];
  const uint8_t slaveId = kSlaveIds[slaveIdx];
  const QueryProfile &qp = kQueryProfiles[queryIdx];
  const RxWindowProfile &rw = kRxWindows[rxIdx];

  RS485.begin(sp.baud, sp.config);
  RS485.setDelays(dp.preUs, dp.postUs);
  flushRxForMs(10);

  uint8_t tx[8];
  buildRequest(tx, slaveId, qp.fn, qp.addr, qp.count);

  Serial.println();
  Serial.print(F("STEP ")); Serial.print(step + 1);
  Serial.print(F(" tuple="));
  Serial.print(sp.name); Serial.print(F(", "));
  Serial.print(dp.name); Serial.print(F(", slave="));
  Serial.print(slaveId); Serial.print(F(", "));
  Serial.print(qp.name); Serial.print(F(", "));
  Serial.println(rw.name);

  Serial.print(F("  TX: "));
  printHexBuf(tx, 8);
  Serial.println();

  RS485.beginTransmission();
  RS485.write(tx, 8);
  RS485.endTransmission();

  if (rw.openDelayMs > 0) {
    delay(rw.openDelayMs);
  }

  uint8_t rx[kMaxRxBytes];
  uint16_t rxLen = 0;

  RS485.receive();
  const uint32_t deadline = millis() + kListenWindowMs;
  while ((int32_t)(millis() - deadline) < 0) {
    while (RS485.available()) {
      int b = RS485.read();
      if (b >= 0 && rxLen < kMaxRxBytes) {
        rx[rxLen++] = (uint8_t)b;
      }
    }
  }
  RS485.noReceive();

  Serial.print(F("  RX("));
  Serial.print(kListenWindowMs);
  Serial.print(F("ms): "));
  if (rxLen == 0) {
    Serial.print(F("<none>"));
  } else {
    printHexBuf(rx, rxLen);
  }
  Serial.print(F("  [")); Serial.print(rxLen); Serial.println(F(" bytes]"));

  bool isException = false;
  uint16_t reg = 0;
  uint8_t exCode = 0;
  uint16_t frameLen = 0;
  bool ok = parseValidReply(rx, rxLen, slaveId, qp.fn, reg, isException, exCode, frameLen);

  if (ok) {
    if (isException) {
      ++gStats.validException;
      Serial.print(F("  VALID MODBUS EXCEPTION len="));
      Serial.print(frameLen);
      Serial.print(F(" exCode=0x"));
      printHexByte(exCode);
      Serial.println();
    } else {
      ++gStats.validResponse;
      Serial.print(F("  VALID MODBUS RESPONSE len="));
      Serial.print(frameLen);
      Serial.print(F(" regValue=0x"));
      printHexByte((uint8_t)(reg >> 8));
      printHexByte((uint8_t)(reg & 0xFF));
      Serial.print(F(" ("));
      Serial.print(reg);
      Serial.println(F(")"));
    }
  } else {
    if (rxLen == 0) {
      ++gStats.silent;
      Serial.println(F("  No bytes captured"));
    } else if (rxLen == 1 && rx[0] == 0x00) {
      ++gStats.oneByteZero;
      Serial.println(F("  Single 0x00 byte (likely local artifact)"));
    } else {
      ++gStats.nonFrameBytes;
      Serial.println(F("  Bytes captured, but no CRC-valid frame"));
    }
  }

  ++gStats.steps;
}

void loop() {
  static uint32_t step = 0;
  static uint32_t lastStep = 0;
  static uint32_t cycle = 0;
  static uint32_t lastHb = 0;

  const uint8_t serialCount = (uint8_t)(sizeof(kSerialProfiles) / sizeof(kSerialProfiles[0]));
  const uint8_t delayCount = (uint8_t)(sizeof(kDelayProfiles) / sizeof(kDelayProfiles[0]));
  const uint8_t slaveCount = (uint8_t)(sizeof(kSlaveIds) / sizeof(kSlaveIds[0]));
  const uint8_t queryCount = (uint8_t)(sizeof(kQueryProfiles) / sizeof(kQueryProfiles[0]));
  const uint8_t rxWinCount = (uint8_t)(sizeof(kRxWindows) / sizeof(kRxWindows[0]));
  const uint32_t totalPerCycle = (uint32_t)serialCount * (uint32_t)delayCount * (uint32_t)slaveCount *
                                 (uint32_t)queryCount * (uint32_t)rxWinCount;

  uint32_t now = millis();

  if (now - lastHb >= 1000UL) {
    lastHb = now;
    Serial.print(F("hb ms="));
    Serial.print(now);
    Serial.print(F(" step="));
    Serial.println(step + 1);
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
    Serial.println(F("============= WINDOWED CYCLE SUMMARY ============="));
    Serial.print(F("cycle=")); Serial.println(cycle);
    Serial.print(F("steps=")); Serial.println(gStats.steps);
    Serial.print(F("valid_response=")); Serial.println(gStats.validResponse);
    Serial.print(F("valid_exception=")); Serial.println(gStats.validException);
    Serial.print(F("silent=")); Serial.println(gStats.silent);
    Serial.print(F("one_byte_zero=")); Serial.println(gStats.oneByteZero);
    Serial.print(F("non_frame_bytes=")); Serial.println(gStats.nonFrameBytes);
    Serial.println(F("=================================================="));

    gStats.steps = 0;
    gStats.validResponse = 0;
    gStats.validException = 0;
    gStats.silent = 0;
    gStats.oneByteZero = 0;
    gStats.nonFrameBytes = 0;
  }
}
