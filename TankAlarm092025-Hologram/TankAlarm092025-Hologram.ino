/*
  Tank Alarm 092025 - MKR NB 1500 Version
  
  Hardware:
  - Arduino MKR NB 1500 (with ublox SARA-R410M)
  - Arduino MKR SD PROTO Shield
  - MKR RELAY Shield
  - Hologram.io SIM Card
  - SD Card
  - Tank level sensor (digital float switch)
  
  Features:
  - Tank level monitoring with digital sensor
  - SMS alerts via Hologram.io for alarm states
  - Daily email reports of levels and changes
  - SD card data logging
  - Relay control capability
  - Low power sleep modes
  
  Created: September 2025
  Using GitHub Copilot for code generation
*/

// Core Arduino MKR libraries
#include <MKRNB.h>        // MKR NB 1500 cellular connectivity
#include <SD.h>           // SD card functionality
#include <ArduinoLowPower.h>  // Low power sleep modes
#include <RTCZero.h>      // Real-time clock
#include <Wire.h>         // I2C communication for current loop sensors

// Include configuration file (copy config_template.h to config.h and customize)
#include "config_template.h"  // Change to "config.h" after setup

// Sensor type definitions
#define DIGITAL_FLOAT 0
#define ANALOG_VOLTAGE 1  
#define CURRENT_LOOP 2

// Initialize cellular components
NB nbAccess;
NBSMS sms;
NBClient client;

// Initialize RTC for timing
RTCZero rtc;

// Pin definitions (can be overridden in config.h)
#ifndef TANK_LEVEL_PIN
#define TANK_LEVEL_PIN 7
#endif
#ifndef RELAY_CONTROL_PIN
#define RELAY_CONTROL_PIN 5
#endif
#ifndef SD_CARD_CS_PIN
#define SD_CARD_CS_PIN 4
#endif

// Hologram.io configuration
const char HOLOGRAM_APN[] = "hologram";
const char HOLOGRAM_URL[] = "cloudsocket.hologram.io";
const int HOLOGRAM_PORT = 9999;

// Use configuration values or defaults
#ifndef HOLOGRAM_DEVICE_KEY
#define HOLOGRAM_DEVICE_KEY "your_device_key_here"
#endif

#ifndef ALARM_PHONE_PRIMARY
#define ALARM_PHONE_PRIMARY "+12223334444"
#endif
#ifndef ALARM_PHONE_SECONDARY
#define ALARM_PHONE_SECONDARY "+15556667777"
#endif
#ifndef DAILY_REPORT_PHONE
#define DAILY_REPORT_PHONE "+18889990000"
#endif

// Timing variables
volatile int time_tick_hours = 1;
volatile int time_tick_report = 1;

#ifndef SLEEP_INTERVAL_HOURS
#define SLEEP_INTERVAL_HOURS 1
#endif
#ifndef DAILY_REPORT_HOURS
#define DAILY_REPORT_HOURS 24
#endif

const int sleep_hours = SLEEP_INTERVAL_HOURS;
const int report_hours = DAILY_REPORT_HOURS;

// Tank level state
int currentLevelState = LOW;
int previousLevelState = LOW;
bool alarmSent = false;

// Data logging
#ifndef LOG_FILE_NAME
#define LOG_FILE_NAME "tanklog.txt"
#endif
String logFileName = LOG_FILE_NAME;

void setup() {
  // Initialize serial communication for debugging
#ifdef ENABLE_SERIAL_DEBUG
  if (ENABLE_SERIAL_DEBUG) {
    Serial.begin(9600);
    // Don't wait for serial in production
    #ifdef DEBUG_WAIT_SERIAL
    while (!Serial && millis() < 5000) {
      ; // Wait up to 5 seconds for serial connection
    }
    #endif
  }
#endif
  
  // Initialize pins based on sensor type
#if SENSOR_TYPE == DIGITAL_FLOAT
  pinMode(TANK_LEVEL_PIN, INPUT_PULLUP);  // Digital float switch with pullup
#elif SENSOR_TYPE == ANALOG_VOLTAGE
  pinMode(ANALOG_SENSOR_PIN, INPUT);      // Analog voltage sensor
#elif SENSOR_TYPE == CURRENT_LOOP
  Wire.begin();                           // Initialize I2C for current loop module
#endif
  
  pinMode(RELAY_CONTROL_PIN, OUTPUT);     // Relay control
  pinMode(LED_BUILTIN, OUTPUT);           // Status LED
  
  // Initialize SD card
  if (!SD.begin(SD_CARD_CS_PIN)) {
#ifdef ENABLE_SERIAL_DEBUG
    if (ENABLE_SERIAL_DEBUG) Serial.println("SD card initialization failed!");
#endif
    // Continue without SD logging if card fails
  } else {
#ifdef ENABLE_SERIAL_DEBUG
    if (ENABLE_SERIAL_DEBUG) Serial.println("SD card initialized successfully");
#endif
    logEvent("System startup - SD card initialized");
  }
  
  // Initialize RTC
  rtc.begin();
  
  // Connect to cellular network
  if (connectToCellular()) {
#ifdef ENABLE_SERIAL_DEBUG
    if (ENABLE_SERIAL_DEBUG) Serial.println("Connected to cellular network");
#endif
    
    // Send startup notification
    sendStartupNotification();
    
    // Log startup event
    logEvent("System startup - Connected to network");
  } else {
#ifdef ENABLE_SERIAL_DEBUG
    if (ENABLE_SERIAL_DEBUG) Serial.println("Failed to connect to cellular network");
#endif
    logEvent("System startup - Network connection failed");
  }
  
  // Read initial tank level
  currentLevelState = readTankLevel();
  previousLevelState = currentLevelState;
  
#ifdef ENABLE_SERIAL_DEBUG
  if (ENABLE_SERIAL_DEBUG) Serial.println("Setup complete - entering main loop");
#endif
  logEvent("Setup complete - entering main loop");
}

void loop() {
  // Read tank level
  currentLevelState = readTankLevel();
  
  // Check for alarm condition (tank level HIGH indicates alarm)
  if (currentLevelState == HIGH && !alarmSent) {
    handleAlarmCondition();
  }
  
  // Reset alarm flag if level returns to normal
  if (currentLevelState == LOW && alarmSent) {
    alarmSent = false;
    logEvent("Tank level returned to normal");
  }
  
  // Check if it's time for daily report
  if (time_tick_report >= report_hours) {
    sendDailyReport();
    time_tick_report = 0;  // Reset daily counter
  }
  
  // Log current status
  String statusMsg = "Tank level: " + String(currentLevelState == HIGH ? "HIGH" : "LOW");
  statusMsg += ", Hours: " + String(time_tick_hours);
  statusMsg += ", Report hours: " + String(time_tick_report);
  
#ifdef ENABLE_SERIAL_DEBUG
  if (ENABLE_SERIAL_DEBUG) Serial.println("Status: " + statusMsg);
#endif
  
  // Only log to SD card every few cycles to reduce wear
  if (time_tick_hours % 6 == 0 || currentLevelState != previousLevelState) {
    logEvent(statusMsg);
  }
  
  // Update previous state
  previousLevelState = currentLevelState;
  
  // Increment hourly counter
  time_tick_hours++;
  time_tick_report++;
  
  // Sleep for configured interval (convert hours to milliseconds)
#ifdef ENABLE_SERIAL_DEBUG
  if (ENABLE_SERIAL_DEBUG) {
    Serial.print("Entering sleep mode for ");
    Serial.print(sleep_hours);
    Serial.println(" hour(s)");
  }
#endif
  
#ifdef ENABLE_LOW_POWER_MODE
  if (ENABLE_LOW_POWER_MODE) {
    LowPower.sleep(sleep_hours * 60 * 60 * 1000);  // Convert hours to milliseconds
  } else {
    delay(sleep_hours * 60 * 60 * 1000);  // Fallback to delay if low power disabled
  }
#else
  delay(sleep_hours * 60 * 60 * 1000);  // Default delay
#endif
}

bool connectToCellular() {
#ifdef ENABLE_SERIAL_DEBUG
  if (ENABLE_SERIAL_DEBUG) Serial.println("Connecting to cellular network...");
#endif
  
  // Connect to the network
  if (nbAccess.begin("", HOLOGRAM_APN) != NB_READY) {
#ifdef ENABLE_SERIAL_DEBUG
    if (ENABLE_SERIAL_DEBUG) Serial.println("Failed to connect to cellular network");
#endif
    return false;
  }
  
  return true;
}

int readTankLevel() {
#if SENSOR_TYPE == DIGITAL_FLOAT
  return readDigitalFloatSensor();
#elif SENSOR_TYPE == ANALOG_VOLTAGE
  return readAnalogVoltageSensor();
#elif SENSOR_TYPE == CURRENT_LOOP
  return readCurrentLoopSensor();
#else
  #error "Invalid SENSOR_TYPE defined. Use DIGITAL_FLOAT, ANALOG_VOLTAGE, or CURRENT_LOOP"
#endif
}

// Read digital float switch sensor
int readDigitalFloatSensor() {
  // Read digital tank level sensor (float switch)
  // HIGH = tank full/alarm condition, LOW = tank normal
  int level = digitalRead(TANK_LEVEL_PIN);
  
  // Add debouncing
#ifdef SENSOR_DEBOUNCE_MS
  delay(SENSOR_DEBOUNCE_MS);
#else
  delay(100);
#endif
  
  int level2 = digitalRead(TANK_LEVEL_PIN);
  
  if (level == level2) {
    return level;
  } else {
    // If readings don't match, take a third reading
#ifdef SENSOR_DEBOUNCE_MS
    delay(SENSOR_DEBOUNCE_MS);
#else
    delay(100);
#endif
    return digitalRead(TANK_LEVEL_PIN);
  }
}

// Read analog voltage sensor (Dwyer 626 series)
int readAnalogVoltageSensor() {
  // Read analog sensor multiple times for stability
  float totalVoltage = 0;
  const int numReadings = 10;
  
  for (int i = 0; i < numReadings; i++) {
    int adcValue = analogRead(ANALOG_SENSOR_PIN);
    // Convert ADC value to voltage (MKR NB 1500 has 3.3V reference, 12-bit ADC)
    float voltage = (adcValue / 4095.0) * 3.3;
    totalVoltage += voltage;
    delay(10);
  }
  
  float avgVoltage = totalVoltage / numReadings;
  
  // Calculate tank level percentage
  float tankPercent = ((avgVoltage - TANK_EMPTY_VOLTAGE) / (TANK_FULL_VOLTAGE - TANK_EMPTY_VOLTAGE)) * 100.0;
  
  // Constrain to valid range
  tankPercent = constrain(tankPercent, 0.0, 100.0);
  
#ifdef ENABLE_SERIAL_DEBUG
  if (ENABLE_SERIAL_DEBUG) {
    Serial.print("Voltage: ");
    Serial.print(avgVoltage);
    Serial.print("V, Tank Level: ");
    Serial.print(tankPercent);
    Serial.println("%");
  }
#endif
  
  // Log current level to SD card occasionally
  static unsigned long lastLogTime = 0;
  if (millis() - lastLogTime > 300000) { // Log every 5 minutes
    String logMsg = "Analog sensor - Voltage: " + String(avgVoltage) + "V, Level: " + String(tankPercent) + "%";
    logEvent(logMsg);
    lastLogTime = millis();
  }
  
  // Return HIGH if tank level exceeds alarm threshold
  return (tankPercent >= ALARM_THRESHOLD_PERCENT) ? HIGH : LOW;
}

// Read 4-20mA current loop sensor via I2C
int readCurrentLoopSensor() {
  float current = readCurrentLoopValue();
  
  if (current < 0) {
    // Error reading sensor
#ifdef ENABLE_SERIAL_DEBUG
    if (ENABLE_SERIAL_DEBUG) Serial.println("Error reading current loop sensor");
#endif
    logEvent("Error reading current loop sensor");
    return LOW; // Default to normal state on error
  }
  
  // Calculate tank level percentage
  float tankPercent = ((current - TANK_EMPTY_CURRENT) / (TANK_FULL_CURRENT - TANK_EMPTY_CURRENT)) * 100.0;
  
  // Constrain to valid range
  tankPercent = constrain(tankPercent, 0.0, 100.0);
  
#ifdef ENABLE_SERIAL_DEBUG
  if (ENABLE_SERIAL_DEBUG) {
    Serial.print("Current: ");
    Serial.print(current);
    Serial.print("mA, Tank Level: ");
    Serial.print(tankPercent);
    Serial.println("%");
  }
#endif
  
  // Log current level to SD card occasionally
  static unsigned long lastLogTime = 0;
  if (millis() - lastLogTime > 300000) { // Log every 5 minutes
    String logMsg = "Current loop sensor - Current: " + String(current) + "mA, Level: " + String(tankPercent) + "%";
    logEvent(logMsg);
    lastLogTime = millis();
  }
  
  // Return HIGH if tank level exceeds alarm threshold
  return (tankPercent >= ALARM_THRESHOLD_CURRENT_PERCENT) ? HIGH : LOW;
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

// Get current tank level as percentage for reporting
float getTankLevelPercent() {
#if SENSOR_TYPE == DIGITAL_FLOAT
  // For digital sensors, return 0% or 100% based on state
  return (currentLevelState == HIGH) ? 100.0 : 0.0;
  
#elif SENSOR_TYPE == ANALOG_VOLTAGE
  // Read analog sensor and calculate percentage
  float totalVoltage = 0;
  const int numReadings = 5; // Fewer readings for reporting
  
  for (int i = 0; i < numReadings; i++) {
    int adcValue = analogRead(ANALOG_SENSOR_PIN);
    float voltage = (adcValue / 4095.0) * 3.3;
    totalVoltage += voltage;
    delay(5);
  }
  
  float avgVoltage = totalVoltage / numReadings;
  float tankPercent = ((avgVoltage - TANK_EMPTY_VOLTAGE) / (TANK_FULL_VOLTAGE - TANK_EMPTY_VOLTAGE)) * 100.0;
  return constrain(tankPercent, 0.0, 100.0);
  
#elif SENSOR_TYPE == CURRENT_LOOP
  // Read current loop sensor and calculate percentage
  float current = readCurrentLoopValue();
  if (current < 0) return -1.0; // Error indicator
  
  float tankPercent = ((current - TANK_EMPTY_CURRENT) / (TANK_FULL_CURRENT - TANK_EMPTY_CURRENT)) * 100.0;
  return constrain(tankPercent, 0.0, 100.0);
  
#else
  return -1.0; // Error
#endif
}

void handleAlarmCondition() {
#ifdef ENABLE_SERIAL_DEBUG
  if (ENABLE_SERIAL_DEBUG) Serial.println("ALARM: Tank level HIGH detected!");
#endif
  
  // Set alarm flag
  alarmSent = true;
  
  // Turn on status LED
  digitalWrite(LED_BUILTIN, HIGH);
  
  // Activate relay (if needed for alarm indication)
  digitalWrite(RELAY_CONTROL_PIN, HIGH);
  
  // Log alarm event
  logEvent("ALARM: Tank level HIGH detected");
  
  // Connect to network if not connected
  if (!connectToCellular()) {
#ifdef ENABLE_SERIAL_DEBUG
    if (ENABLE_SERIAL_DEBUG) Serial.println("Failed to connect for alarm notification");
#endif
    logEvent("ALARM: Failed to connect to network");
    return;
  }
  
  // Send alarm SMS messages
  sendAlarmSMS();
  
  // Send alarm data to Hologram.io
  sendHologramData("ALARM", "Tank level HIGH - immediate attention required");
  
  // Turn off status LED after alarm sent
  digitalWrite(LED_BUILTIN, LOW);
  
  // Keep relay on for alarm indication (can be turned off manually or after time)
  // digitalWrite(RELAY_CONTROL_PIN, LOW);  // Uncomment to turn off relay immediately
}

void sendAlarmSMS() {
  String message = "TANK ALARM: High level detected at " + getCurrentTimestamp();
  message += ". Immediate attention required.";
  
  // Send to primary contact
  if (sms.beginSMS(ALARM_PHONE_PRIMARY)) {
    sms.print(message);
    sms.endSMS();
#ifdef ENABLE_SERIAL_DEBUG
    if (ENABLE_SERIAL_DEBUG) Serial.println("Alarm SMS sent to primary contact");
#endif
    logEvent("Alarm SMS sent to primary contact");
  }
  
  delay(5000);  // Wait between messages
  
  // Send to secondary contact
  if (sms.beginSMS(ALARM_PHONE_SECONDARY)) {
    sms.print(message);
    sms.endSMS();
#ifdef ENABLE_SERIAL_DEBUG
    if (ENABLE_SERIAL_DEBUG) Serial.println("Alarm SMS sent to secondary contact");
#endif
    logEvent("Alarm SMS sent to secondary contact");
  }
}

void sendDailyReport() {
#ifdef ENABLE_SERIAL_DEBUG
  if (ENABLE_SERIAL_DEBUG) Serial.println("Sending daily report...");
#endif
  
  // Connect to network if not connected
  if (!connectToCellular()) {
#ifdef ENABLE_SERIAL_DEBUG
    if (ENABLE_SERIAL_DEBUG) Serial.println("Failed to connect for daily report");
#endif
    logEvent("Daily report: Failed to connect to network");
    return;
  }
  
  String message = "Daily Tank Report - " + getCurrentTimestamp();
  
  // Get detailed tank level information
  float tankPercent = getTankLevelPercent();
  String levelInfo;
  
#if SENSOR_TYPE == DIGITAL_FLOAT
  levelInfo = String(currentLevelState == HIGH ? "HIGH (Alarm)" : "LOW (Normal)");
#elif SENSOR_TYPE == ANALOG_VOLTAGE
  if (tankPercent >= 0) {
    levelInfo = String(tankPercent, 1) + "% (" + String(currentLevelState == HIGH ? "ALARM" : "Normal") + ")";
  } else {
    levelInfo = "Sensor Error";
  }
#elif SENSOR_TYPE == CURRENT_LOOP
  if (tankPercent >= 0) {
    levelInfo = String(tankPercent, 1) + "% (" + String(currentLevelState == HIGH ? "ALARM" : "Normal") + ")";
  } else {
    levelInfo = "Sensor Error";
  }
#endif

  message += "\nTank Level: " + levelInfo;
  message += "\nSystem Status: Normal";
  message += "\nNext report in 24 hours";
  
  // Send daily SMS
  if (sms.beginSMS(DAILY_REPORT_PHONE)) {
    sms.print(message);
    sms.endSMS();
#ifdef ENABLE_SERIAL_DEBUG
    if (ENABLE_SERIAL_DEBUG) Serial.println("Daily report SMS sent");
#endif
    logEvent("Daily report SMS sent");
  }
  
  // Send daily data to Hologram.io
  sendHologramData("DAILY", message);
  
  // Log daily report event
  logEvent("Daily report completed");
}

void sendStartupNotification() {
  String message = "Tank Alarm System Started - " + getCurrentTimestamp();
  
  // Get detailed tank level information for startup
  float tankPercent = getTankLevelPercent();
  String levelInfo;
  
#if SENSOR_TYPE == DIGITAL_FLOAT
  levelInfo = String(currentLevelState == HIGH ? "HIGH (Alarm)" : "LOW (Normal)");
#elif SENSOR_TYPE == ANALOG_VOLTAGE
  if (tankPercent >= 0) {
    levelInfo = String(tankPercent, 1) + "% (" + String(currentLevelState == HIGH ? "ALARM" : "Normal") + ")";
  } else {
    levelInfo = "Sensor Error";
  }
#elif SENSOR_TYPE == CURRENT_LOOP
  if (tankPercent >= 0) {
    levelInfo = String(tankPercent, 1) + "% (" + String(currentLevelState == HIGH ? "ALARM" : "Normal") + ")";
  } else {
    levelInfo = "Sensor Error";
  }
#endif

  message += "\nInitial Level: " + levelInfo;
  message += "\nSystem ready for monitoring";
  message += "\nSystem ready for monitoring";
  
  // Send startup SMS to daily report number
  if (sms.beginSMS(DAILY_REPORT_PHONE)) {
    sms.print(message);
    sms.endSMS();
#ifdef ENABLE_SERIAL_DEBUG
    if (ENABLE_SERIAL_DEBUG) Serial.println("Startup notification sent");
#endif
  }
  
  // Send startup data to Hologram.io
  sendHologramData("STARTUP", message);
}

void sendHologramData(String topic, String message) {
  // Create JSON message for Hologram.io
  String jsonPayload = "{\"k\":\"" + String(HOLOGRAM_DEVICE_KEY) + "\",";
  jsonPayload += "\"d\":\"" + message + "\",";
  jsonPayload += "\"t\":[\"" + topic + "\"]}";
  
  // Connect to Hologram.io server
  if (client.connect(HOLOGRAM_URL, HOLOGRAM_PORT)) {
#ifdef ENABLE_SERIAL_DEBUG
    if (ENABLE_SERIAL_DEBUG) Serial.println("Connected to Hologram.io");
#endif
    
    // Send the data
    client.print(jsonPayload);
    client.stop();
    
#ifdef ENABLE_SERIAL_DEBUG
    if (ENABLE_SERIAL_DEBUG) Serial.println("Data sent to Hologram.io: " + topic);
#endif
    logEvent("Data sent to Hologram.io: " + topic);
  } else {
#ifdef ENABLE_SERIAL_DEBUG
    if (ENABLE_SERIAL_DEBUG) Serial.println("Failed to connect to Hologram.io");
#endif
    logEvent("Failed to connect to Hologram.io");
  }
}

void logEvent(String event) {
  // Log events to SD card with timestamp
  if (SD.begin(SD_CARD_CS_PIN)) {
    File logFile = SD.open(logFileName, FILE_WRITE);
    if (logFile) {
      String logEntry = getCurrentTimestamp() + " - " + event;
      logFile.println(logEntry);
      logFile.close();
#ifdef ENABLE_SERIAL_DEBUG
      if (ENABLE_SERIAL_DEBUG) Serial.println("Logged: " + logEntry);
#endif
    }
  }
}

String getCurrentTimestamp() {
  // Create timestamp string
  // Note: This is a simple timestamp - for production use, consider NTP sync
  String timestamp = String(rtc.getYear()) + "-";
  if (rtc.getMonth() < 10) timestamp += "0";
  timestamp += String(rtc.getMonth()) + "-";
  if (rtc.getDay() < 10) timestamp += "0";
  timestamp += String(rtc.getDay()) + " ";
  if (rtc.getHours() < 10) timestamp += "0";
  timestamp += String(rtc.getHours()) + ":";
  if (rtc.getMinutes() < 10) timestamp += "0";
  timestamp += String(rtc.getMinutes()) + ":";
  if (rtc.getSeconds() < 10) timestamp += "0";
  timestamp += String(rtc.getSeconds());
  
  return timestamp;
}