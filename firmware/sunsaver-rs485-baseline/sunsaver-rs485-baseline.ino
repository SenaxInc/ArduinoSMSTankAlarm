/**
 * sunsaver-rs485-baseline.ino
 *
 * Purpose:
 * - Compare RX behavior when Opta does NOT transmit versus when it does.
 * - Determine whether recurring 0x00 bytes are ambient bus noise or
 *   TX/RX turnaround artifacts tied to local transmission.
 *
 * Test Phases:
 * 1) Baseline phase (no TX): 25s receive-only monitoring.
 * 2) Active phase (with TX): 35s periodic Modbus query + receive monitoring.
 */

#include <ArduinoRS485.h>

static const uint32_t BAUD_RATE = 9600;
static const uint16_t LISTEN_POLL_MS = 20;

static const uint8_t SLAVE_ID = 1;
static const uint8_t FUNCTION_CODE = 0x04;
static const uint16_t REG_ADDR = 0x0008;
static const uint16_t REG_COUNT = 1;

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

static void buildQuery(uint8_t *frame) {
  frame[0] = SLAVE_ID;
  frame[1] = FUNCTION_CODE;
  frame[2] = (uint8_t)(REG_ADDR >> 8);
  frame[3] = (uint8_t)(REG_ADDR & 0xFF);
  frame[4] = (uint8_t)(REG_COUNT >> 8);
  frame[5] = (uint8_t)(REG_COUNT & 0xFF);
  uint16_t crc = crc16Modbus(frame, 6);
  frame[6] = (uint8_t)(crc & 0xFF);
  frame[7] = (uint8_t)(crc >> 8);
}

static void printHexByte(uint8_t b) {
  if (b < 0x10) Serial.print('0');
  Serial.print(b, HEX);
}

struct PhaseStats {
  uint32_t totalBytes;
  uint32_t zeroBytes;
  uint32_t nonZeroBytes;
  uint32_t bursts;
};

void setup() {
  Serial.begin(115200);
  uint32_t t0 = millis();
  while (!Serial && millis() - t0 < 3000) delay(10);

  Serial.println();
  Serial.println(F("=============================================="));
  Serial.println(F("SunSaver RS-485 baseline/no-TX comparison"));
  Serial.println(F("  Phase A: receive-only (no TX)"));
  Serial.println(F("  Phase B: periodic query + receive"));
  Serial.println(F("=============================================="));

  RS485.begin(BAUD_RATE, SERIAL_8N2);
  RS485.setDelays(50, 50);
}

void loop() {
  static uint8_t phase = 0;
  static uint32_t phaseStart = 0;
  static uint32_t lastHeartbeat = 0;
  static uint32_t lastTx = 0;
  static uint32_t cycle = 0;
  static PhaseStats baseline;
  static PhaseStats active;
  static bool phaseInit = false;

  if (!phaseInit) {
    baseline.totalBytes = 0;
    baseline.zeroBytes = 0;
    baseline.nonZeroBytes = 0;
    baseline.bursts = 0;

    active.totalBytes = 0;
    active.zeroBytes = 0;
    active.nonZeroBytes = 0;
    active.bursts = 0;
    phaseStart = millis();
    lastHeartbeat = 0;
    lastTx = 0;
    phase = 0;
    phaseInit = true;
    ++cycle;
    Serial.println();
    Serial.print(F("===== CYCLE "));
    Serial.print(cycle);
    Serial.println(F(" START ====="));
    Serial.println(F("PHASE A START (no TX)"));
    RS485.receive();
  }

  uint32_t now = millis();

  if (now - lastHeartbeat >= 1000UL) {
    lastHeartbeat = now;
    Serial.print(F("hb ms="));
    Serial.print(now);
    Serial.print(F(" phase="));
    Serial.println(phase == 0 ? F("A(no-tx)") : F("B(with-tx)"));
  }

  if (phase == 0) {
    uint32_t before = baseline.totalBytes;
    while (RS485.available()) {
      int b = RS485.read();
      if (b >= 0) {
        baseline.totalBytes++;
        if ((uint8_t)b == 0x00) baseline.zeroBytes++;
        else baseline.nonZeroBytes++;
      }
    }
    if (baseline.totalBytes > before) {
      baseline.bursts++;
    }

    if (now - phaseStart >= 25000UL) {
      RS485.noReceive();
      Serial.println(F("PHASE A END"));
      Serial.print(F("  totalBytes=")); Serial.println(baseline.totalBytes);
      Serial.print(F("  zeroBytes=")); Serial.println(baseline.zeroBytes);
      Serial.print(F("  nonZeroBytes=")); Serial.println(baseline.nonZeroBytes);
      Serial.print(F("  bursts=")); Serial.println(baseline.bursts);

      phase = 1;
      phaseStart = now;
      lastTx = 0;
      Serial.println(F("PHASE B START (with TX)"));
      RS485.receive();
    }
  } else {
    if (now - lastTx >= 2000UL) {
      lastTx = now;
      uint8_t frame[8];
      buildQuery(frame);

      Serial.print(F("TX @ms="));
      Serial.print(now);
      Serial.print(F(" ["));
      for (uint8_t i = 0; i < 8; ++i) {
        if (i) Serial.print(' ');
        printHexByte(frame[i]);
      }
      Serial.println(F("]"));

      RS485.noReceive();
      RS485.beginTransmission();
      RS485.write(frame, 8);
      RS485.endTransmission();
      RS485.receive();
    }

    uint32_t before = active.totalBytes;
    while (RS485.available()) {
      int b = RS485.read();
      if (b >= 0) {
        active.totalBytes++;
        if ((uint8_t)b == 0x00) active.zeroBytes++;
        else active.nonZeroBytes++;
      }
    }
    if (active.totalBytes > before) {
      active.bursts++;
    }

    if (now - phaseStart >= 35000UL) {
      RS485.noReceive();
      Serial.println(F("PHASE B END"));
      Serial.print(F("  totalBytes=")); Serial.println(active.totalBytes);
      Serial.print(F("  zeroBytes=")); Serial.println(active.zeroBytes);
      Serial.print(F("  nonZeroBytes=")); Serial.println(active.nonZeroBytes);
      Serial.print(F("  bursts=")); Serial.println(active.bursts);

      Serial.println(F("----- COMPARISON -----"));
      Serial.print(F("A(no-tx).total=")); Serial.print(baseline.totalBytes);
      Serial.print(F(" B(with-tx).total=")); Serial.println(active.totalBytes);
      Serial.print(F("A(no-tx).zero=")); Serial.print(baseline.zeroBytes);
      Serial.print(F(" B(with-tx).zero=")); Serial.println(active.zeroBytes);
      Serial.println(F("======================"));

      phaseInit = false;
    }
  }

  delay(LISTEN_POLL_MS);
}
