/**
 * sunsaver-rs485-full-id-scan.ino
 *
 * Purpose:
 * - Exhaustively scan Modbus slave IDs 1..247 on current Opta + MRC-1 + SunSaver path.
 * - Probe both FC03 and FC04 for each ID with CRC-qualified frame parsing.
 * - Provide a definitive answer to "is the wrong slave address the issue?"
 */

#include <ArduinoRS485.h>

struct ParsedFrame {
  bool isException;
  uint8_t fn;
  uint8_t exCode;
  uint8_t byteCount;
  uint16_t value16;
  uint16_t frameLen;
};

struct Hit {
  uint8_t slaveId;
  uint8_t requestFn;
  bool isException;
  uint8_t responseFn;
  uint8_t exCode;
  uint16_t value16;
  uint16_t frameLen;
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
static const uint16_t kListenWindowMs = 120;
static const uint16_t kStepIntervalMs = 10;
static const uint16_t kMaxRxBytes = 96;

static const uint8_t kFunctionCodes[] = {0x03, 0x04};
static const uint16_t kAddressForFn[] = {0x0012, 0x0008};
static const uint16_t kRegisterCount = 1;

static const uint8_t kMinSlaveId = 1;
static const uint8_t kMaxSlaveId = 247;
static const uint8_t kFnCount = (uint8_t)(sizeof(kFunctionCodes) / sizeof(kFunctionCodes[0]));
static const uint16_t kTotalSteps = (uint16_t)(kFnCount * (uint16_t)(kMaxSlaveId - kMinSlaveId + 1));

static Stats gStats = {0, 0, 0, 0, 0, 0};
static Hit gHits[16];
static uint8_t gHitCount = 0;

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

static void buildReadRequest(uint8_t *frame, uint8_t slaveId, uint8_t fn,
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

static bool parseValidReply(const uint8_t *rx, uint16_t rxLen,
                            uint8_t expectedSlave, uint8_t expectedFn,
                            ParsedFrame &out) {
  if (rxLen < 5) return false;

  for (uint16_t i = 0; i + 5 <= rxLen; ++i) {
    if (rx[i] != expectedSlave) continue;

    uint8_t fn = rx[i + 1];
    bool isException = false;
    uint16_t frameLen = 0;

    if (fn == (uint8_t)(expectedFn | 0x80)) {
      isException = true;
      frameLen = 5;
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

    out.isException = isException;
    out.fn = fn;
    out.exCode = isException ? rx[i + 2] : 0;
    out.frameLen = frameLen;
    out.byteCount = isException ? 0 : rx[i + 2];
    out.value16 = 0;

    if (!isException && out.byteCount >= 2) {
      out.value16 = (uint16_t)(((uint16_t)rx[i + 3] << 8) | (uint16_t)rx[i + 4]);
    }

    return true;
  }

  return false;
}

static void maybeStoreHit(uint8_t slaveId, uint8_t requestFn, const ParsedFrame &parsed) {
  if (gHitCount >= (uint8_t)(sizeof(gHits) / sizeof(gHits[0]))) return;

  Hit &h = gHits[gHitCount++];
  h.slaveId = slaveId;
  h.requestFn = requestFn;
  h.isException = parsed.isException;
  h.responseFn = parsed.fn;
  h.exCode = parsed.exCode;
  h.value16 = parsed.value16;
  h.frameLen = parsed.frameLen;
}

static void runStep(uint16_t step) {
  uint8_t slaveId = (uint8_t)(kMinSlaveId + (step / kFnCount));
  uint8_t fnIdx = (uint8_t)(step % kFnCount);
  uint8_t fn = kFunctionCodes[fnIdx];
  uint16_t addr = kAddressForFn[fnIdx];

  if (fnIdx == 0 && ((slaveId == 1) || (slaveId % 25 == 0) || (slaveId == kMaxSlaveId))) {
    Serial.print(F("progress slave="));
    Serial.print(slaveId);
    Serial.print(F("/"));
    Serial.print(kMaxSlaveId);
    Serial.print(F(" step="));
    Serial.print(step + 1);
    Serial.print(F("/"));
    Serial.println(kTotalSteps);
  }

  uint8_t tx[8];
  buildReadRequest(tx, slaveId, fn, addr, kRegisterCount);

  flushRx(2);

  RS485.beginTransmission();
  RS485.write(tx, 8);
  RS485.endTransmission();

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

  ParsedFrame parsed;
  bool ok = parseValidReply(rx, rxLen, slaveId, fn, parsed);
  if (ok) {
    if (parsed.isException) {
      ++gStats.validException;
      Serial.print(F("HIT EX slave="));
      Serial.print(slaveId);
      Serial.print(F(" reqFn=0x"));
      printHexByte(fn);
      Serial.print(F(" rspFn=0x"));
      printHexByte(parsed.fn);
      Serial.print(F(" ex=0x"));
      printHexByte(parsed.exCode);
      Serial.print(F(" len="));
      Serial.println(parsed.frameLen);
    } else {
      ++gStats.validResponse;
      Serial.print(F("HIT OK slave="));
      Serial.print(slaveId);
      Serial.print(F(" fn=0x"));
      printHexByte(parsed.fn);
      Serial.print(F(" len="));
      Serial.print(parsed.frameLen);
      Serial.print(F(" value=0x"));
      printHexByte((uint8_t)(parsed.value16 >> 8));
      printHexByte((uint8_t)(parsed.value16 & 0xFF));
      Serial.print(F(" ("));
      Serial.print(parsed.value16);
      Serial.println(F(")"));
    }
    maybeStoreHit(slaveId, fn, parsed);
  } else {
    if (rxLen == 0) {
      ++gStats.silent;
    } else if (rxLen == 1 && rx[0] == 0x00) {
      ++gStats.oneByteZero;
    } else {
      ++gStats.noValidFrame;
    }
  }

  ++gStats.steps;
}

static void printSummary() {
  Serial.println();
  Serial.println(F("========== FULL ID SCAN SUMMARY =========="));
  Serial.print(F("ids_scanned=")); Serial.println((uint16_t)(kMaxSlaveId - kMinSlaveId + 1));
  Serial.print(F("steps=")); Serial.println(gStats.steps);
  Serial.print(F("valid_response=")); Serial.println(gStats.validResponse);
  Serial.print(F("valid_exception=")); Serial.println(gStats.validException);
  Serial.print(F("silent=")); Serial.println(gStats.silent);
  Serial.print(F("one_byte_zero=")); Serial.println(gStats.oneByteZero);
  Serial.print(F("no_valid_frame=")); Serial.println(gStats.noValidFrame);
  Serial.print(F("hits_recorded=")); Serial.println(gHitCount);

  if (gHitCount == 0) {
    Serial.println(F("hits: <none>"));
  } else {
    for (uint8_t i = 0; i < gHitCount; ++i) {
      const Hit &h = gHits[i];
      Serial.print(F("hit[")); Serial.print(i); Serial.print(F("] slave=")); Serial.print(h.slaveId);
      Serial.print(F(" reqFn=0x")); printHexByte(h.requestFn);
      Serial.print(F(" rspFn=0x")); printHexByte(h.responseFn);
      Serial.print(F(" type=")); Serial.print(h.isException ? F("EX") : F("OK"));
      if (h.isException) {
        Serial.print(F(" ex=0x")); printHexByte(h.exCode);
      } else {
        Serial.print(F(" value=0x"));
        printHexByte((uint8_t)(h.value16 >> 8));
        printHexByte((uint8_t)(h.value16 & 0xFF));
      }
      Serial.print(F(" len=")); Serial.println(h.frameLen);
    }
  }

  Serial.println(F("=========================================="));
  Serial.println(F("Scan complete. Reset board to rerun."));
}

void setup() {
  Serial.begin(115200);
  uint32_t t0 = millis();
  while (!Serial && millis() - t0 < 3000) delay(10);

  Serial.println();
  Serial.println(F("================================================"));
  Serial.println(F("SunSaver RS485 full slave-ID scan"));
  Serial.println(F("  Sweep: slave IDs 1..247"));
  Serial.println(F("  Functions per ID: FC03 reg0012, FC04 reg0008"));
  Serial.print(F("  Listen window ms: ")); Serial.println(kListenWindowMs);
  Serial.println(F("  Serial fixed: 9600 8N2, delays 50/50"));
  Serial.println(F("================================================"));

  RS485.begin(kBaud, SERIAL_8N2);
  RS485.setDelays(50, 50);
}

void loop() {
  static uint16_t step = 0;
  static uint32_t lastStep = 0;
  static uint32_t lastHb = 0;
  static bool done = false;

  uint32_t now = millis();

  if (now - lastHb >= 1000UL) {
    lastHb = now;
    Serial.print(F("hb ms=")); Serial.print(now);
    Serial.print(F(" step=")); Serial.print(step + 1);
    Serial.print(F("/")); Serial.println(kTotalSteps);
  }

  if (done) {
    delay(250);
    return;
  }

  if (now - lastStep < kStepIntervalMs) {
    return;
  }

  lastStep = now;
  runStep(step);
  ++step;

  if (step >= kTotalSteps) {
    printSummary();
    done = true;
  }
}
