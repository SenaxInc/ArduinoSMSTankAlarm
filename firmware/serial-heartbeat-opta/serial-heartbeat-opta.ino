void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000) {
    delay(10);
  }

  Serial.println();
  Serial.println("Opta serial heartbeat test");
  Serial.println("boot ok");
}

void loop() {
  static unsigned long lastMillis = 0;
  unsigned long now = millis();

  if (now - lastMillis >= 1000) {
    lastMillis = now;
    Serial.print("heartbeat ms=");
    Serial.println(now);
  }
}