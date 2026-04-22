/**
 * sunsaver-rs485-baud-parity-sweep.ino
 *
 * Forum-pattern TX bracket plus baud/parity sweep.
 * Tests {4800, 9600, 19200, 38400} x {8N1, 8N2, 8E1, 8O1} against slaves {1,2}
 * with FC04 reg0008. One cycle = 4 baud x 4 parity x 2 slaves = 32 steps.
 * Post-delay scaled to the actual selected baud (1.2x of 11-bit char time).
 */

#include <ArduinoRS485.h>

struct BaudProfile {
  uint32_t baud;
  const char *name;
};

struct ParityProfile {
  uint16_t cfg;
  uint8_t bits;        // bits per char including start/parity/stop
  const char *name;
};

static const BaudProfile kBauds[] = {
  { 4800,  "4800"},
  { 9600,  "9600"},
  {19200,  "19200"},
  {38400,  "38400"}
};

static const ParityProfile kParities[] = {
  {SERIAL_8N1, 10, "8N1"},
  {SERIAL_8N2, 11, "8N2"},
  {SERIAL_8E1, 11, "8E1"},
  {SERIAL_8O1, 11, "8O1"}
};

static const uint8_t kSlaveIds[] = {1, 2};

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
};

static Stats gStats = {0, 0, 0, 0, 0, 0};

static uint16_t crc16Modbus(const uint8_t *data, size_t len) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < len; ++i) {
    crc ^= (uint16_t)data[i];
    for (uint8_t b = 0; b < 8; ++b) {
      if (crc & 0x0001) { crc >>= 1; crc ^= 0xA001; }
      else { crc >>= 1; }
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

static void buildRead(uint8_t *frame, uint8_t slave, uint8_t fn,
                      uint16_t addr, uint16_t count) {
  frame[0] = slave; frame[1] = fn;
  frame[2] = (uint8_t)(addr >> 8); frame[3] = (uint8_t)(addr & 0xFF);
  frame[4] = (uint8_t)(count >> 8); frame[5] = (uint8_t)(count & 0xFF);
  uint16_t crc = crc16Modbus(frame, 6);
  frame[6] = (uint8_t)(crc & 0xFF); frame[7] = (uint8_t)(crc >> 8);
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
  if (rxLen < 5) return false;
  for (uint16_t i = 0; i + 5 <= rxLen; ++i) {
    if (rx[i] != expectedSlave) continue;
    uint8_t fn = rx[i + 1];
    uint16_t len = 0;
    bool ex = false;
    if (fn == (uint8_t)(expectedFn | 0x80)) { len = 5; ex = true; }
    else if (fn == expectedFn) { len = (uint16_t)(5 + rx[i + 2]); }
    else continue;
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
  const uint8_t bC = (uint8_t)(sizeof(kBauds) / sizeof(kBauds[0]));
  const uint8_t pC = (uint8_t)(sizeof(kParities) / sizeof(kParities[0]));
  const uint8_t sC = (uint8_t)(sizeof(kSlaveIds) / sizeof(kSlaveIds[0]));
  uint32_t total = (uint32_t)bC * pC * sC;
  uint32_t idx = step % total;
  uint8_t bi = (uint8_t)(idx % bC); idx /= bC;
  uint8_t pi = (uint8_t)(idx % pC); idx /= pC;
  uint8_t si = (uint8_t)(idx % sC);

  const BaudProfile &b = kBauds[bi];
  const ParityProfile &p = kParities[pi];
  uint8_t slave = kSlaveIds[si];

  uint32_t charUs = ((uint32_t)p.bits * 1000000UL) / b.baud;
  uint32_t postUs = (charUs * 12UL) / 10UL;  // 1.2 char time

  RS485.end();
  delay(5);
  RS485.begin(b.baud, p.cfg);
  RS485.setDelays(0, (int)postUs);
  flushRx(10);
  RS485.noReceive();

  uint8_t tx[8];
  buildRead(tx, slave, 0x04, 0x0008, 1);

  Serial.println();
  Serial.print(F("STEP ")); Serial.print(step + 1);
  Serial.print(F(" baud=")); Serial.print(b.name);
  Serial.print(F(", parity=")); Serial.print(p.name);
  Serial.print(F(", post=")); Serial.print(postUs); Serial.print(F("us"));
  Serial.print(F(", slave=")); Serial.println(slave);
  Serial.print(F("  TX: ")); printHexBuf(tx, 8); Serial.println();

  sendForumPattern(tx, 8);

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
  bool ok = parseAny(rx, rxLen, slave, 0x04, ex, exCode, flen);
  if (ok) {
    if (ex) { ++gStats.validException; Serial.print(F("  VALID EXCEPTION code=0x")); printHexByte(exCode); Serial.println(); }
    else    { ++gStats.validResponse;  Serial.println(F("  VALID RESPONSE")); }
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
  Serial.println(F("SunSaver RS485 BAUD+PARITY SWEEP (forum bracket)"));
  Serial.println(F("==============================================="));
  RS485.begin(9600, SERIAL_8N1);
  RS485.setDelays(0, 1300);
}

void loop() {
  static uint32_t step = 0;
  static uint32_t lastStep = 0;
  static uint32_t cycle = 0;
  const uint32_t total = 4UL * 4UL * 2UL;
  uint32_t now = millis();
  if (now - lastStep < kStepIntervalMs) return;
  lastStep = now;
  runStep(step);
  ++step;
  if ((step % total) == 0) {
    ++cycle;
    Serial.println();
    Serial.println(F("====== BAUD+PARITY SWEEP SUMMARY ======"));
    Serial.print(F("cycle=")); Serial.println(cycle);
    Serial.print(F("steps=")); Serial.println(gStats.steps);
    Serial.print(F("valid_response=")); Serial.println(gStats.validResponse);
    Serial.print(F("valid_exception=")); Serial.println(gStats.validException);
    Serial.print(F("silent=")); Serial.println(gStats.silent);
    Serial.print(F("one_byte_zero=")); Serial.println(gStats.oneByteZero);
    Serial.print(F("no_valid_frame=")); Serial.println(gStats.noValidFrame);
    Serial.println(F("======================================="));
    gStats = {0, 0, 0, 0, 0, 0};
  }
}
