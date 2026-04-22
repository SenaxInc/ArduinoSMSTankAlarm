/**
 * sunsaver-rs485-mrc1-probe.ino
 *
 * Probe MRC-1's own diagnostic Modbus interface on candidate slave IDs with
 * a wider FC sweep, plus broadcast (slave 0). Forum-pattern TX bracket and
 * 1.2x char-time post-delay. Anything CRC-valid (response OR exception) is
 * a breakthrough proving the bus + path are alive.
 */

#include <ArduinoRS485.h>

static const uint32_t kBaud = 9600;
static const uint16_t kCfg = SERIAL_8N1;
static const uint32_t kPostUs = 1300; // ~1.2 x 10-bit char time at 9600

static const uint8_t kSlaveIds[] = {0, 1, 2, 16, 17, 100, 247};

struct Q {
  uint8_t fn;
  uint16_t addr;
  uint16_t count;
  const char *name;
};

static const Q kQueries[] = {
  {0x01, 0x0000, 1, "FC01 coil0"},
  {0x02, 0x0000, 1, "FC02 disc0"},
  {0x03, 0x0000, 1, "FC03 hr0"},
  {0x04, 0x0000, 1, "FC04 ir0"},
  {0x07, 0x0000, 0, "FC07 stat"}
};

static const uint16_t kListenWindowMs = 1200;
static const uint16_t kStepIntervalMs = 1500;
static const uint16_t kMaxRxBytes = 120;

struct Stats {
  uint32_t steps;
  uint32_t validResponse;
  uint32_t validException;
  uint32_t silent;
  uint32_t oneByteZero;
  uint32_t noValidFrame;
  uint32_t hits;
};

static Stats gStats = {0, 0, 0, 0, 0, 0, 0};

static uint16_t crc16Modbus(const uint8_t *data, size_t len) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < len; ++i) {
    crc ^= (uint16_t)data[i];
    for (uint8_t b = 0; b < 8; ++b) {
      if (crc & 0x0001) { crc >>= 1; crc ^= 0xA001; } else { crc >>= 1; }
    }
  }
  return crc;
}

static void printHexByte(uint8_t b) {
  if (b < 0x10) Serial.print('0');
  Serial.print(b, HEX);
}
static void printHexBuf(const uint8_t *buf, uint16_t len) {
  for (uint16_t i = 0; i < len; ++i) { if (i) Serial.print(' '); printHexByte(buf[i]); }
}

static uint8_t buildFrame(uint8_t *frame, uint8_t slave, const Q &q) {
  if (q.fn == 0x07) {
    frame[0] = slave; frame[1] = q.fn;
    uint16_t crc = crc16Modbus(frame, 2);
    frame[2] = (uint8_t)(crc & 0xFF); frame[3] = (uint8_t)(crc >> 8);
    return 4;
  }
  frame[0] = slave; frame[1] = q.fn;
  frame[2] = (uint8_t)(q.addr >> 8); frame[3] = (uint8_t)(q.addr & 0xFF);
  frame[4] = (uint8_t)(q.count >> 8); frame[5] = (uint8_t)(q.count & 0xFF);
  uint16_t crc = crc16Modbus(frame, 6);
  frame[6] = (uint8_t)(crc & 0xFF); frame[7] = (uint8_t)(crc >> 8);
  return 8;
}

static void sendForumPattern(const uint8_t *frame, uint8_t len) {
  RS485.noReceive();
  RS485.beginTransmission();
  RS485.write(frame, len);
  RS485.flush();
  delay(1);
  RS485.endTransmission();
  RS485.receive();
}

static bool parseAny(const uint8_t *rx, uint16_t rxLen, uint8_t expectedSlave,
                     uint8_t expectedFn, bool &isException, uint8_t &exCode,
                     uint16_t &frameLen) {
  if (rxLen < 4) return false;
  for (uint16_t i = 0; i + 4 <= rxLen; ++i) {
    if (rx[i] != expectedSlave) continue;
    uint8_t fn = rx[i + 1];
    uint16_t len = 0;
    bool ex = false;
    if (fn == (uint8_t)(expectedFn | 0x80)) { len = 5; ex = true; }
    else if (fn == expectedFn) {
      if (expectedFn == 0x07) len = 5;
      else { if (i + 3 > rxLen) continue; len = (uint16_t)(5 + rx[i + 2]); }
    } else continue;
    if (i + len > rxLen) continue;
    uint16_t cc = crc16Modbus(&rx[i], len - 2);
    uint16_t cw = (uint16_t)rx[i + len - 2] | ((uint16_t)rx[i + len - 1] << 8);
    if (cc != cw) continue;
    isException = ex; exCode = ex ? rx[i + 2] : 0; frameLen = len;
    return true;
  }
  return false;
}

static void flushRx(uint16_t ms) {
  RS485.receive();
  uint32_t e = millis() + ms;
  while ((int32_t)(millis() - e) < 0) { while (RS485.available()) RS485.read(); }
}

static void runStep(uint32_t step) {
  const uint8_t sC = (uint8_t)(sizeof(kSlaveIds) / sizeof(kSlaveIds[0]));
  const uint8_t qC = (uint8_t)(sizeof(kQueries) / sizeof(kQueries[0]));
  uint32_t total = (uint32_t)sC * qC;
  uint32_t idx = step % total;
  uint8_t qi = (uint8_t)(idx % qC); idx /= qC;
  uint8_t si = (uint8_t)(idx % sC);

  uint8_t slave = kSlaveIds[si];
  const Q &q = kQueries[qi];

  flushRx(10);
  RS485.noReceive();

  uint8_t tx[8];
  uint8_t txLen = buildFrame(tx, slave, q);

  Serial.println();
  Serial.print(F("STEP ")); Serial.print(step + 1);
  Serial.print(F(" slave=")); Serial.print(slave);
  Serial.print(F(", ")); Serial.println(q.name);
  Serial.print(F("  TX: ")); printHexBuf(tx, txLen); Serial.println();

  if (slave == 0) {
    // Broadcast: spec says no reply, but useful to observe
    sendForumPattern(tx, txLen);
  } else {
    sendForumPattern(tx, txLen);
  }

  uint8_t rx[kMaxRxBytes];
  uint16_t rxLen = 0;
  uint32_t dl = millis() + kListenWindowMs;
  while ((int32_t)(millis() - dl) < 0) {
    while (RS485.available()) {
      int v = RS485.read();
      if (v >= 0 && rxLen < kMaxRxBytes) rx[rxLen++] = (uint8_t)v;
    }
  }
  RS485.noReceive();

  Serial.print(F("  RX: "));
  if (rxLen == 0) Serial.print(F("<none>")); else printHexBuf(rx, rxLen);
  Serial.print(F(" [")); Serial.print(rxLen); Serial.println(F(" bytes]"));

  bool ex = false; uint8_t exCode = 0; uint16_t flen = 0;
  bool ok = (slave != 0) && parseAny(rx, rxLen, slave, q.fn, ex, exCode, flen);
  if (ok) {
    ++gStats.hits;
    if (ex) { ++gStats.validException; Serial.print(F("  *** HIT *** VALID EXCEPTION code=0x")); printHexByte(exCode); Serial.println(); }
    else    { ++gStats.validResponse;  Serial.println(F("  *** HIT *** VALID RESPONSE")); }
  } else {
    if (rxLen == 0) { ++gStats.silent; Serial.println(F("  silent")); }
    else if (rxLen == 1 && rx[0] == 0x00) { ++gStats.oneByteZero; Serial.println(F("  single 0x00 artifact")); }
    else { ++gStats.noValidFrame; Serial.println(F("  bytes seen but no CRC-valid frame")); }
  }
  ++gStats.steps;
}

void setup() {
  Serial.begin(115200);
  uint32_t t0 = millis();
  while (!Serial && millis() - t0 < 3000) delay(10);
  Serial.println();
  Serial.println(F("==============================================="));
  Serial.println(F("MRC-1 OWN-ADDRESS PROBE (forum bracket)"));
  Serial.println(F("  IDs: 0,1,2,16,17,100,247  FCs: 01,02,03,04,07"));
  Serial.println(F("==============================================="));
  RS485.begin(kBaud, kCfg);
  RS485.setDelays(0, (int)kPostUs);
}

void loop() {
  static uint32_t step = 0;
  static uint32_t lastStep = 0;
  static uint32_t cycle = 0;
  const uint32_t total = 7UL * 5UL;
  uint32_t now = millis();
  if (now - lastStep < kStepIntervalMs) return;
  lastStep = now;
  runStep(step);
  ++step;
  if ((step % total) == 0) {
    ++cycle;
    Serial.println();
    Serial.println(F("====== MRC-1 PROBE SUMMARY ======"));
    Serial.print(F("cycle=")); Serial.println(cycle);
    Serial.print(F("steps=")); Serial.println(gStats.steps);
    Serial.print(F("hits=")); Serial.println(gStats.hits);
    Serial.print(F("valid_response=")); Serial.println(gStats.validResponse);
    Serial.print(F("valid_exception=")); Serial.println(gStats.validException);
    Serial.print(F("silent=")); Serial.println(gStats.silent);
    Serial.print(F("one_byte_zero=")); Serial.println(gStats.oneByteZero);
    Serial.print(F("no_valid_frame=")); Serial.println(gStats.noValidFrame);
    Serial.println(F("================================="));
    gStats = {0, 0, 0, 0, 0, 0, 0};
  }
}
