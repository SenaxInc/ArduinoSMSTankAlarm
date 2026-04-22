/**
 * sunsaver-tx-stress.ino
 *
 * Continuous EIA-485 TX stress test for the Arduino Opta.
 *
 * Purpose: prove whether the Opta is electrically driving the RS-485 bus at
 * all. Sends 0x55 0xAA byte pairs continuously at 9600 baud, with a brief
 * silent gap every second so the Opta can also print a Serial heartbeat.
 *
 * Expected MRC-1 LED behavior:
 *  - If the LED goes from "occasional amber flicker" to a fast/continuous
 *    red+green shimmer the moment this sketch starts: Opta TX IS reaching
 *    the bus -> receive-side issue.
 *  - If the LED looks identical to before (slow idle flicker): Opta is NOT
 *    driving the bus -> wrong terminal, broken transceiver, or Opta variant
 *    without onboard RS-485.
 */

#include <ArduinoRS485.h>

static const uint32_t BAUD_RATE = 9600;

void setup() {
  Serial.begin(115200);
  uint32_t s = millis();
  while (!Serial && millis() - s < 3000) delay(10);

  Serial.println();
  Serial.println(F("================================================"));
  Serial.println(F("Opta EIA-485 TX stress test (continuous traffic)"));
  Serial.print  (F("  Baud: ")); Serial.println(BAUD_RATE);
  Serial.println(F("  Pattern: 0x55 0xAA (continuous 8N1)"));
  Serial.println(F("  Watch the MRC-1 LED -- it should now flicker"));
  Serial.println(F("  red+green VERY rapidly. If it doesn't, the Opta"));
  Serial.println(F("  is not driving the bus."));
  Serial.println(F("================================================"));

  RS485.begin(BAUD_RATE);
  RS485.setDelays(0, 0);
}

void loop() {
  static unsigned long lastHb = 0;
  static uint32_t bytesSent = 0;
  unsigned long now = millis();

  // Burst ~200 byte-pairs back-to-back, then yield
  RS485.beginTransmission();
  for (uint16_t i = 0; i < 200; ++i) {
    RS485.write((uint8_t)0x55);
    RS485.write((uint8_t)0xAA);
  }
  RS485.endTransmission();
  bytesSent += 400;

  if (now - lastHb >= 1000UL) {
    lastHb = now;
    Serial.print(F("hb ms="));
    Serial.print(now);
    Serial.print(F(" tx_bytes="));
    Serial.println(bytesSent);
  }
}
