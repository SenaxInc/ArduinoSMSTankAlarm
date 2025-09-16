/*
  Tank Alarm 092025 - Hardware Test Sketch
  
  This is a simple test sketch to verify hardware functionality
  before deploying the full tank alarm system.
  
  Tests:
  - MKR NB 1500 cellular connectivity
  - SD card functionality
  - Tank level sensor reading
  - Relay control
  - SMS sending capability
  
  Upload this sketch first to verify all hardware is working properly.
*/

#include <MKRNB.h>
#include <SD.h>
#include <Wire.h>

// Include configuration (create config.h from template for testing)
// For testing, you can define sensor type here or use config.h
#ifndef SENSOR_TYPE
#define SENSOR_TYPE 0  // 0=DIGITAL_FLOAT, 1=ANALOG_VOLTAGE, 2=CURRENT_LOOP
#endif

// Sensor type definitions
#define DIGITAL_FLOAT 0
#define ANALOG_VOLTAGE 1  
#define CURRENT_LOOP 2

// Initialize cellular components
NB nbAccess;
NBSMS sms;

// Pin definitions
const int TANK_LEVEL_PIN = 7;
const int ANALOG_SENSOR_PIN = A1;
const int RELAY_PIN = 5;
const int SD_CS_PIN = 4;
const int LED_PIN = LED_BUILTIN;

// I2C Configuration for current loop testing
const int I2C_CURRENT_LOOP_ADDRESS = 0x48;
const int CURRENT_LOOP_CHANNEL = 0;

// Test configuration
const char APN[] = "hologram";
String TEST_PHONE = "12223334444";  // Replace with your phone number for testing

void setup() {
  Serial.begin(9600);
  while (!Serial) {
    ; // Wait for serial port to connect
  }
  
  Serial.println("=== Tank Alarm 092025 Hardware Test ===");
  Serial.println("Starting hardware tests...\n");
  
  // Initialize pins
  pinMode(TANK_LEVEL_PIN, INPUT_PULLUP);
  pinMode(ANALOG_SENSOR_PIN, INPUT);
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  
  // Initialize I2C if using current loop sensor
  if (SENSOR_TYPE == CURRENT_LOOP) {
    Wire.begin();
  }
  
  // Test 1: LED Test
  Serial.println("Test 1: LED Test");
  testLED();
  
  // Test 2: Tank Level Sensor Test
  Serial.println("Test 2: Tank Level Sensor Test");
  testTankSensor();
  
  // Test 3: Relay Test
  Serial.println("Test 3: Relay Test");
  testRelay();
  
  // Test 4: SD Card Test
  Serial.println("Test 4: SD Card Test");
  testSDCard();
  
  // Test 5: Cellular Connection Test
  Serial.println("Test 5: Cellular Connection Test");
  testCellular();
  
  // Test 6: SMS Test (optional - uncomment if you want to test SMS)
  // Serial.println("Test 6: SMS Test");
  // testSMS();
  
  Serial.println("\n=== All Tests Complete ===");
  Serial.println("Review test results above.");
  Serial.println("If any tests failed, check hardware connections and configuration.");
}

void loop() {
  // Continuous sensor monitoring for testing
#if SENSOR_TYPE == DIGITAL_FLOAT
  int sensorState = digitalRead(TANK_LEVEL_PIN);
  Serial.print("Digital sensor state: ");
  Serial.println(sensorState == HIGH ? "HIGH (ALARM)" : "LOW (NORMAL)");
  
#elif SENSOR_TYPE == ANALOG_VOLTAGE
  int adcValue = analogRead(ANALOG_SENSOR_PIN);
  float voltage = (adcValue / 4095.0) * 3.3;
  Serial.print("Analog sensor - ADC: ");
  Serial.print(adcValue);
  Serial.print(", Voltage: ");
  Serial.print(voltage, 3);
  Serial.println("V");
  
#elif SENSOR_TYPE == CURRENT_LOOP
  float current = readCurrentLoopValue();
  Serial.print("Current loop sensor: ");
  if (current >= 0) {
    Serial.print(current, 2);
    Serial.println(" mA");
  } else {
    Serial.println("ERROR - Check I2C connection");
  }
#endif
  
  // Blink LED to show system is running
  digitalWrite(LED_PIN, HIGH);
  delay(100);
  digitalWrite(LED_PIN, LOW);
  
  delay(2000);  // Check every 2 seconds
}

void testLED() {
  Serial.println("  Blinking LED 5 times...");
  for (int i = 0; i < 5; i++) {
    digitalWrite(LED_PIN, HIGH);
    delay(200);
    digitalWrite(LED_PIN, LOW);
    delay(200);
  }
  Serial.println("  LED test complete.\n");
}

void testTankSensor() {
  Serial.println("  Testing tank level sensor...");
  
#if SENSOR_TYPE == DIGITAL_FLOAT
  Serial.println("  Digital Float Switch Test:");
  Serial.println("  (Try triggering your float switch during this test)");
  
  for (int i = 0; i < 10; i++) {
    int level = digitalRead(TANK_LEVEL_PIN);
    Serial.print("  Reading ");
    Serial.print(i + 1);
    Serial.print(": ");
    Serial.println(level == HIGH ? "HIGH" : "LOW");
    delay(500);
  }
  
#elif SENSOR_TYPE == ANALOG_VOLTAGE
  Serial.println("  Analog Voltage Sensor Test (0.5-4.5V):");
  Serial.println("  (Check voltage readings from your pressure sensor)");
  
  for (int i = 0; i < 10; i++) {
    int adcValue = analogRead(ANALOG_SENSOR_PIN);
    float voltage = (adcValue / 4095.0) * 3.3;
    Serial.print("  Reading ");
    Serial.print(i + 1);
    Serial.print(": ADC=");
    Serial.print(adcValue);
    Serial.print(", Voltage=");
    Serial.print(voltage, 3);
    Serial.println("V");
    delay(500);
  }
  
#elif SENSOR_TYPE == CURRENT_LOOP
  Serial.println("  Current Loop Sensor Test (4-20mA):");
  Serial.println("  (Check current readings from your I2C module)");
  
  for (int i = 0; i < 10; i++) {
    float current = readCurrentLoopValue();
    Serial.print("  Reading ");
    Serial.print(i + 1);
    Serial.print(": ");
    if (current >= 0) {
      Serial.print(current, 2);
      Serial.println(" mA");
    } else {
      Serial.println("ERROR - Check I2C connection");
    }
    delay(500);
  }
  
#else
  Serial.println("  Unknown sensor type configured!");
#endif

  Serial.println("  Tank sensor test complete.\n");
}

void testRelay() {
  Serial.println("  Testing relay control...");
  Serial.println("  (Listen for relay clicking)");
  
  for (int i = 0; i < 3; i++) {
    Serial.print("  Activating relay... ");
    digitalWrite(RELAY_PIN, HIGH);
    delay(1000);
    Serial.print("Deactivating relay... ");
    digitalWrite(RELAY_PIN, LOW);
    delay(1000);
    Serial.println("Done");
  }
  Serial.println("  Relay test complete.\n");
}

void testSDCard() {
  Serial.println("  Initializing SD card...");
  
  if (!SD.begin(SD_CS_PIN)) {
    Serial.println("  ERROR: SD card initialization failed!");
    Serial.println("  Check SD card insertion and formatting.\n");
    return;
  }
  
  Serial.println("  SD card initialized successfully.");
  
  // Test writing to SD card
  File testFile = SD.open("test.txt", FILE_WRITE);
  if (testFile) {
    Serial.println("  Writing test data to SD card...");
    testFile.println("Tank Alarm 092025 Test");
    testFile.println("Test timestamp: " + String(millis()));
    testFile.close();
    Serial.println("  Test file written successfully.");
  } else {
    Serial.println("  ERROR: Could not create test file on SD card.");
  }
  
  // Test reading from SD card
  testFile = SD.open("test.txt");
  if (testFile) {
    Serial.println("  Reading test file from SD card:");
    while (testFile.available()) {
      Serial.write(testFile.read());
    }
    testFile.close();
  } else {
    Serial.println("  ERROR: Could not read test file from SD card.");
  }
  
  Serial.println("  SD card test complete.\n");
}

void testCellular() {
  Serial.println("  Connecting to cellular network...");
  Serial.println("  (This may take up to 60 seconds)");
  
  bool connected = false;
  if (nbAccess.begin("", APN) == NB_READY) {
    connected = true;
    Serial.println("  Successfully connected to cellular network!");
    
    // Get signal strength
    Serial.print("  Signal strength: ");
    Serial.println(nbAccess.getSignalStrength());
    
    // Get network operator
    Serial.print("  Network operator: ");
    String carrier = nbAccess.getCurrentCarrier();
    Serial.println(carrier.length() > 0 ? carrier : "Unknown");
    
  } else {
    Serial.println("  ERROR: Failed to connect to cellular network!");
    Serial.println("  Check SIM card installation and signal strength.");
  }
  
  Serial.println("  Cellular test complete.\n");
}

void testSMS() {
  Serial.println("  Testing SMS functionality...");
  Serial.println("  WARNING: This will send a test SMS to " + TEST_PHONE);
  Serial.println("  Make sure this is your phone number!");
  
  // Uncomment the following lines to actually send test SMS
  /*
  if (sms.beginSMS(TEST_PHONE)) {
    sms.print("Tank Alarm 092025 Test SMS - ");
    sms.print(millis());
    if (sms.endSMS()) {
      Serial.println("  Test SMS sent successfully!");
    } else {
      Serial.println("  ERROR: Failed to send test SMS.");
    }
  } else {
    Serial.println("  ERROR: Could not begin SMS.");
  }
  */
  
  Serial.println("  SMS test skipped (uncomment code to enable).");
  Serial.println("  SMS test complete.\n");
}

// Read current value from NCD.io 4-channel current loop I2C module
float readCurrentLoopValue() {
  // Request 2 bytes from the current loop module
  Wire.beginTransmission(I2C_CURRENT_LOOP_ADDRESS);
  Wire.write(CURRENT_LOOP_CHANNEL); // Select channel
  if (Wire.endTransmission() != 0) {
    return -1; // Communication error
  }
  
  Wire.requestFrom(I2C_CURRENT_LOOP_ADDRESS, 2);
  
  if (Wire.available() >= 2) {
    // Read 16-bit value (big endian)
    uint16_t rawValue = (Wire.read() << 8) | Wire.read();
    
    // Convert to current (mA)
    // NCD.io module typically provides 16-bit resolution for 4-20mA range
    float current = 4.0 + ((rawValue / 65535.0) * 16.0); // 4mA + (0-16mA range)
    
    return current;
  }
  
  return -1; // Error reading
}