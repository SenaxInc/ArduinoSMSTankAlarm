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

// Default values for new configuration parameters
#ifndef TANK_NUMBER
#define TANK_NUMBER 1
#endif
#ifndef SITE_LOCATION_NAME
#define SITE_LOCATION_NAME "Tank Site"
#endif
#ifndef INCHES_PER_UNIT
#define INCHES_PER_UNIT 1.0
#endif
#ifndef TANK_HEIGHT_INCHES
#define TANK_HEIGHT_INCHES 120
#endif
#ifndef HIGH_ALARM_INCHES
#define HIGH_ALARM_INCHES 100
#endif
#ifndef LOW_ALARM_INCHES
#define LOW_ALARM_INCHES 12
#endif
#ifndef DIGITAL_HIGH_ALARM
#define DIGITAL_HIGH_ALARM true
#endif
#ifndef DIGITAL_LOW_ALARM
#define DIGITAL_LOW_ALARM false
#endif
#ifndef LARGE_DECREASE_THRESHOLD_INCHES
#define LARGE_DECREASE_THRESHOLD_INCHES 24
#endif
#ifndef LARGE_DECREASE_WAIT_HOURS
#define LARGE_DECREASE_WAIT_HOURS 2
#endif
#ifndef SD_CONFIG_FILE
#define SD_CONFIG_FILE "tank_config.txt"
#endif
#ifndef SD_HOURLY_LOG_FILE
#define SD_HOURLY_LOG_FILE "hourly_log.txt"
#endif
#ifndef SD_DAILY_LOG_FILE
#define SD_DAILY_LOG_FILE "daily_log.txt"
#endif
#ifndef SD_ALARM_LOG_FILE
#define SD_ALARM_LOG_FILE "alarm_log.txt"
#endif
#ifndef SD_DECREASE_LOG_FILE
#define SD_DECREASE_LOG_FILE "decrease_log.txt"
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

// Tank level measurements in inches
float currentLevelInches = 0.0;
float previousLevelInches = 0.0;
float levelChange24Hours = 0.0;

// Large decrease detection
float decreaseStartLevel = 0.0;
unsigned long decreaseStartTime = 0;
bool decreaseDetected = false;

// Data logging
#ifndef LOG_FILE_NAME
#define LOG_FILE_NAME "tanklog.txt"
#endif
String logFileName = LOG_FILE_NAME;

// SD Card configuration variables (loaded from SD card config file)
String siteLocationName = SITE_LOCATION_NAME;
int tankNumber = TANK_NUMBER;
float inchesPerUnit = INCHES_PER_UNIT;
float tankHeightInches = TANK_HEIGHT_INCHES;
float highAlarmInches = HIGH_ALARM_INCHES;
float lowAlarmInches = LOW_ALARM_INCHES;
bool digitalHighAlarm = DIGITAL_HIGH_ALARM;
bool digitalLowAlarm = DIGITAL_LOW_ALARM;
float largeDecreaseThreshold = LARGE_DECREASE_THRESHOLD_INCHES;
int largeDecreaseWaitHours = LARGE_DECREASE_WAIT_HOURS;

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
    
    // Load configuration from SD card
    loadSDCardConfiguration();
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
  
  // Get initial level in inches
  currentLevelInches = getTankLevelInches();
  previousLevelInches = currentLevelInches;
  
#ifdef ENABLE_SERIAL_DEBUG
  if (ENABLE_SERIAL_DEBUG) Serial.println("Setup complete - entering main loop");
#endif
  logEvent("Setup complete - entering main loop");
}

void loop() {
  // Read tank level
  currentLevelState = readTankLevel();
  
  // Get current level in inches
  float newLevelInches = getTankLevelInches();
  
  // Update level measurements
  if (newLevelInches >= 0) {  // Valid reading
    currentLevelInches = newLevelInches;
  }
  
  // Check for alarm condition (tank level HIGH indicates alarm)
  if (currentLevelState == HIGH && !alarmSent) {
    handleAlarmCondition();
  }
  
  // Reset alarm flag if level returns to normal
  if (currentLevelState == LOW && alarmSent) {
    alarmSent = false;
    logEvent("Tank level returned to normal");
    logAlarmEvent("normal");
  }
  
  // Check for large decrease in level
  checkLargeDecrease();
  
  // Check if it's time for hourly log entry
  logHourlyData();
  
  // Check if it's time for daily report
  if (time_tick_report >= report_hours) {
    sendDailyReport();
    logDailyData();
    time_tick_report = 0;  // Reset daily counter
    
    // Update 24-hour change tracking
    levelChange24Hours = currentLevelInches - previousLevelInches;
    previousLevelInches = currentLevelInches;
  }
  
  // Log current status
  String statusMsg = "Tank level: " + String(currentLevelInches, 1) + " inches";
  statusMsg += " (" + String(currentLevelState == HIGH ? "ALARM" : "Normal") + ")";
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
  
  if (level != level2) {
    // If readings don't match, take a third reading
#ifdef SENSOR_DEBOUNCE_MS
    delay(SENSOR_DEBOUNCE_MS);
#else
    delay(100);
#endif
    level = digitalRead(TANK_LEVEL_PIN);
  }
  
  // Check if alarm condition is met based on configuration
  bool alarmTriggered = false;
  if (digitalHighAlarm && level == HIGH) {
    alarmTriggered = true;
  }
  if (digitalLowAlarm && level == LOW) {
    alarmTriggered = true;
  }
  
  return alarmTriggered ? HIGH : LOW;
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
  
  // Convert voltage to inches
  float levelInches = convertToInches(avgVoltage);
  
#ifdef ENABLE_SERIAL_DEBUG
  if (ENABLE_SERIAL_DEBUG) {
    Serial.print("Voltage: ");
    Serial.print(avgVoltage);
    Serial.print("V, Tank Level: ");
    Serial.print(levelInches);
    Serial.println(" inches");
  }
#endif
  
  // Log current level to SD card occasionally
  static unsigned long lastLogTime = 0;
  if (millis() - lastLogTime > 300000) { // Log every 5 minutes
    String logMsg = "Analog sensor - Voltage: " + String(avgVoltage) + "V, Level: " + String(levelInches) + " inches";
    logEvent(logMsg);
    lastLogTime = millis();
  }
  
  // Return HIGH if tank level exceeds high alarm threshold or goes below low alarm threshold
  return (levelInches >= highAlarmInches || levelInches <= lowAlarmInches) ? HIGH : LOW;
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
  
  // Convert current to inches
  float levelInches = convertToInches(current);
  
#ifdef ENABLE_SERIAL_DEBUG
  if (ENABLE_SERIAL_DEBUG) {
    Serial.print("Current: ");
    Serial.print(current);
    Serial.print("mA, Tank Level: ");
    Serial.print(levelInches);
    Serial.println(" inches");
  }
#endif
  
  // Log current level to SD card occasionally
  static unsigned long lastLogTime = 0;
  if (millis() - lastLogTime > 300000) { // Log every 5 minutes
    String logMsg = "Current loop sensor - Current: " + String(current) + "mA, Level: " + String(levelInches) + " inches";
    logEvent(logMsg);
    lastLogTime = millis();
  }
  
  // Return HIGH if tank level exceeds high alarm threshold or goes below low alarm threshold
  return (levelInches >= highAlarmInches || levelInches <= lowAlarmInches) ? HIGH : LOW;
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

void handleAlarmCondition() {
#ifdef ENABLE_SERIAL_DEBUG
  if (ENABLE_SERIAL_DEBUG) Serial.println("ALARM: Tank level alarm condition detected!");
#endif
  
  // Set alarm flag
  alarmSent = true;
  
  // Turn on status LED
  digitalWrite(LED_BUILTIN, HIGH);
  
  // Activate relay (if needed for alarm indication)
  digitalWrite(RELAY_CONTROL_PIN, HIGH);
  
  // Determine alarm type
  String alarmType = "change";
  if (currentLevelInches >= highAlarmInches) {
    alarmType = "high";
  } else if (currentLevelInches <= lowAlarmInches) {
    alarmType = "low";
  }
  
  // Log alarm event
  String alarmMsg = "ALARM: Tank level " + alarmType + " - " + String(currentLevelInches, 1) + " inches";
  logEvent(alarmMsg);
  logAlarmEvent(alarmType);
  
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
  sendHologramData("ALARM", alarmMsg);
  
  // Turn off status LED after alarm sent
  digitalWrite(LED_BUILTIN, LOW);
  
  // Keep relay on for alarm indication (can be turned off manually or after time)
  // digitalWrite(RELAY_CONTROL_PIN, LOW);  // Uncomment to turn off relay immediately
}

void sendAlarmSMS() {
  String feetInchesFormat = formatInchesToFeetInches(currentLevelInches);
  String message = "TANK ALARM " + siteLocationName + " Tank #" + String(tankNumber) + 
                  ": Level " + feetInchesFormat + " at " + getCurrentTimestamp() + 
                  ". Immediate attention required.";
  
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
  
  String feetInchesFormat = formatInchesToFeetInches(currentLevelInches);
  String changeFeetInchesFormat = formatInchesToFeetInches(abs(levelChange24Hours));
  String changePrefix = (levelChange24Hours >= 0) ? "+" : "";
  
  String message = "Daily Tank Report " + siteLocationName + " - " + getCurrentTimestamp();
  message += "\nTank #" + String(tankNumber) + " Level: " + feetInchesFormat;
  message += "\n24hr Change: " + changePrefix + changeFeetInchesFormat;
  message += "\nStatus: " + String(currentLevelState == HIGH ? "ALARM" : "Normal");
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
  String feetInchesFormat = formatInchesToFeetInches(currentLevelInches);
  String message = "Tank Alarm System Started " + siteLocationName + " - " + getCurrentTimestamp();
  message += "\nTank #" + String(tankNumber) + " Initial Level: " + feetInchesFormat;
  message += "\nStatus: " + String(currentLevelState == HIGH ? "ALARM" : "Normal");
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

// Load configuration from SD card
void loadSDCardConfiguration() {
  if (!SD.begin(SD_CARD_CS_PIN)) {
    logEvent("Failed to initialize SD card for configuration loading");
    return;
  }
  
  File configFile = SD.open(SD_CONFIG_FILE);
  if (!configFile) {
#ifdef ENABLE_SERIAL_DEBUG
    if (ENABLE_SERIAL_DEBUG) Serial.println("Config file not found, using defaults");
#endif
    logEvent("Config file not found, using defaults from config.h");
    return;
  }
  
#ifdef ENABLE_SERIAL_DEBUG
  if (ENABLE_SERIAL_DEBUG) Serial.println("Loading configuration from SD card...");
#endif
  
  while (configFile.available()) {
    String line = configFile.readStringUntil('\n');
    line.trim();
    
    // Skip comments and empty lines
    if (line.startsWith("#") || line.length() == 0) {
      continue;
    }
    
    // Parse key=value pairs
    int equalPos = line.indexOf('=');
    if (equalPos > 0) {
      String key = line.substring(0, equalPos);
      String value = line.substring(equalPos + 1);
      key.trim();
      value.trim();
      
      // Update configuration variables
      if (key == "SITE_NAME") {
        siteLocationName = value;
      } else if (key == "TANK_NUMBER") {
        tankNumber = value.toInt();
      } else if (key == "TANK_HEIGHT_INCHES") {
        tankHeightInches = value.toFloat();
      } else if (key == "INCHES_PER_UNIT") {
        inchesPerUnit = value.toFloat();
      } else if (key == "HIGH_ALARM_INCHES") {
        highAlarmInches = value.toFloat();
      } else if (key == "LOW_ALARM_INCHES") {
        lowAlarmInches = value.toFloat();
      } else if (key == "DIGITAL_HIGH_ALARM") {
        digitalHighAlarm = (value == "true");
      } else if (key == "DIGITAL_LOW_ALARM") {
        digitalLowAlarm = (value == "true");
      } else if (key == "LARGE_DECREASE_THRESHOLD_INCHES") {
        largeDecreaseThreshold = value.toFloat();
      } else if (key == "LARGE_DECREASE_WAIT_HOURS") {
        largeDecreaseWaitHours = value.toInt();
      }
    }
  }
  
  configFile.close();
  
  String configMsg = "Configuration loaded - Site: " + siteLocationName + 
                    ", Tank: " + String(tankNumber) + 
                    ", Height: " + String(tankHeightInches) + "in";
  logEvent(configMsg);
  
#ifdef ENABLE_SERIAL_DEBUG
  if (ENABLE_SERIAL_DEBUG) Serial.println(configMsg);
#endif
}

// Convert sensor reading to inches
float convertToInches(float sensorValue) {
#if SENSOR_TYPE == DIGITAL_FLOAT
  // For digital sensors, return full tank height if HIGH, 0 if LOW
  return (sensorValue == HIGH) ? tankHeightInches : 0.0;
  
#elif SENSOR_TYPE == ANALOG_VOLTAGE
  // Calculate percentage first, then convert to inches
  float voltage = sensorValue;
  float tankPercent = ((voltage - TANK_EMPTY_VOLTAGE) / (TANK_FULL_VOLTAGE - TANK_EMPTY_VOLTAGE)) * 100.0;
  tankPercent = constrain(tankPercent, 0.0, 100.0);
  return (tankPercent / 100.0) * tankHeightInches;
  
#elif SENSOR_TYPE == CURRENT_LOOP
  // Calculate percentage first, then convert to inches
  float current = sensorValue;
  float tankPercent = ((current - TANK_EMPTY_CURRENT) / (TANK_FULL_CURRENT - TANK_EMPTY_CURRENT)) * 100.0;
  tankPercent = constrain(tankPercent, 0.0, 100.0);
  return (tankPercent / 100.0) * tankHeightInches;
  
#else
  return 0.0;
#endif
}

// Get current tank level in inches
float getTankLevelInches() {
#if SENSOR_TYPE == DIGITAL_FLOAT
  return convertToInches(currentLevelState);
  
#elif SENSOR_TYPE == ANALOG_VOLTAGE
  // Read analog sensor and calculate inches
  float totalVoltage = 0;
  const int numReadings = 5;
  
  for (int i = 0; i < numReadings; i++) {
    int adcValue = analogRead(ANALOG_SENSOR_PIN);
    float voltage = (adcValue / 4095.0) * 3.3;
    totalVoltage += voltage;
    delay(5);
  }
  
  float avgVoltage = totalVoltage / numReadings;
  return convertToInches(avgVoltage);
  
#elif SENSOR_TYPE == CURRENT_LOOP
  // Read current loop sensor and calculate inches
  float current = readCurrentLoopValue();
  if (current < 0) return -1.0; // Error indicator
  
  return convertToInches(current);
  
#else
  return -1.0; // Error
#endif
}

// Convert inches to feet and inches format
String formatInchesToFeetInches(float totalInches) {
  int feet = (int)(totalInches / 12);
  float inches = totalInches - (feet * 12);
  
  String result = String(feet) + "FT," + String(inches, 1) + "IN";
  return result;
}

// Create timestamp in YYYYMMDD format
String getDateTimestamp() {
  String timestamp = String(rtc.getYear() + 2000);
  if (rtc.getMonth() < 10) timestamp += "0";
  timestamp += String(rtc.getMonth());
  if (rtc.getDay() < 10) timestamp += "0";
  timestamp += String(rtc.getDay());
  if (rtc.getHours() < 10) timestamp += "0";
  timestamp += String(rtc.getHours()) + ":";
  if (rtc.getMinutes() < 10) timestamp += "0";
  timestamp += String(rtc.getMinutes());
  
  return timestamp;
}

// Log hourly data in required format
// YYYYMMDD00:00,H,(Tank Number),(Number of Feet)FT,(Number of Inches)IN,+(Number of feet added in last 24hrs)FT,(Number of inches added in last 24hrs)IN,
void logHourlyData() {
  if (!SD.begin(SD_CARD_CS_PIN)) return;
  
  String timestamp = getDateTimestamp();
  String feetInchesFormat = formatInchesToFeetInches(currentLevelInches);
  String changeFeetInchesFormat = formatInchesToFeetInches(abs(levelChange24Hours));
  String changePrefix = (levelChange24Hours >= 0) ? "+" : "-";
  
  String logEntry = timestamp + ",H," + String(tankNumber) + "," + feetInchesFormat + "," + 
                   changePrefix + changeFeetInchesFormat + ",";
  
  File hourlyFile = SD.open(SD_HOURLY_LOG_FILE, FILE_WRITE);
  if (hourlyFile) {
    hourlyFile.println(logEntry);
    hourlyFile.close();
    
#ifdef ENABLE_SERIAL_DEBUG
    if (ENABLE_SERIAL_DEBUG) Serial.println("Hourly log: " + logEntry);
#endif
  }
}

// Log daily data in required format
// YYYYMMDD00:00,D,(site location name),(Tank Number),(Number of Feet)FT,(Number of Inches)IN,+(Number of feet added in last 24hrs)FT,(Number of inches added in last 24hrs)IN,
void logDailyData() {
  if (!SD.begin(SD_CARD_CS_PIN)) return;
  
  String timestamp = getDateTimestamp();
  String feetInchesFormat = formatInchesToFeetInches(currentLevelInches);
  String changeFeetInchesFormat = formatInchesToFeetInches(abs(levelChange24Hours));
  String changePrefix = (levelChange24Hours >= 0) ? "+" : "-";
  
  String logEntry = timestamp + ",D," + siteLocationName + "," + String(tankNumber) + "," + 
                   feetInchesFormat + "," + changePrefix + changeFeetInchesFormat + ",";
  
  File dailyFile = SD.open(SD_DAILY_LOG_FILE, FILE_WRITE);
  if (dailyFile) {
    dailyFile.println(logEntry);
    dailyFile.close();
    
#ifdef ENABLE_SERIAL_DEBUG
    if (ENABLE_SERIAL_DEBUG) Serial.println("Daily log: " + logEntry);
#endif
  }
}

// Log alarm events in required format
// YYYYMMDD00:00,A,(site location name),(Tank Number),(alarm state, high or low or change)
void logAlarmEvent(String alarmState) {
  if (!SD.begin(SD_CARD_CS_PIN)) return;
  
  String timestamp = getDateTimestamp();
  String logEntry = timestamp + ",A," + siteLocationName + "," + String(tankNumber) + "," + alarmState;
  
  File alarmFile = SD.open(SD_ALARM_LOG_FILE, FILE_WRITE);
  if (alarmFile) {
    alarmFile.println(logEntry);
    alarmFile.close();
    
#ifdef ENABLE_SERIAL_DEBUG
    if (ENABLE_SERIAL_DEBUG) Serial.println("Alarm log: " + logEntry);
#endif
  }
}

// Check for large decreases in tank level
void checkLargeDecrease() {
  float levelDifference = previousLevelInches - currentLevelInches;
  
  if (levelDifference >= largeDecreaseThreshold && !decreaseDetected) {
    // Large decrease detected for first time
    decreaseDetected = true;
    decreaseStartLevel = previousLevelInches;
    decreaseStartTime = millis();
    
    String msg = "Large decrease detected: " + String(levelDifference, 1) + " inches";
    logEvent(msg);
    
#ifdef ENABLE_SERIAL_DEBUG
    if (ENABLE_SERIAL_DEBUG) Serial.println(msg);
#endif
  }
  
  // Check if we should log the large decrease (after waiting period)
  if (decreaseDetected && (millis() - decreaseStartTime) >= (largeDecreaseWaitHours * 60 * 60 * 1000)) {
    float totalDecrease = decreaseStartLevel - currentLevelInches;
    
    if (totalDecrease >= largeDecreaseThreshold) {
      logLargeDecrease(totalDecrease);
    }
    
    // Reset decrease detection
    decreaseDetected = false;
  }
}

// Log large decrease in required format
// YYYYMMDD00:00,S,(Tank Number),(total Number of Feet decreased)FT,(total Number of Inches decreased)IN
void logLargeDecrease(float totalDecrease) {
  if (!SD.begin(SD_CARD_CS_PIN)) return;
  
  String timestamp = getDateTimestamp();
  String decreaseFeetInchesFormat = formatInchesToFeetInches(totalDecrease);
  
  String logEntry = timestamp + ",S," + String(tankNumber) + "," + decreaseFeetInchesFormat;
  
  File decreaseFile = SD.open(SD_DECREASE_LOG_FILE, FILE_WRITE);
  if (decreaseFile) {
    decreaseFile.println(logEntry);
    decreaseFile.close();
    
#ifdef ENABLE_SERIAL_DEBUG
    if (ENABLE_SERIAL_DEBUG) Serial.println("Large decrease log: " + logEntry);
#endif
  }
  
  // Log the event in main log as well
  String eventMsg = "Large decrease logged: " + String(totalDecrease, 1) + " inches over " + 
                   String(largeDecreaseWaitHours) + " hours";
  logEvent(eventMsg);
}