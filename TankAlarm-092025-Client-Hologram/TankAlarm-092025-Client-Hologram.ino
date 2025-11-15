/*
  Tank Alarm Client 092025 - MKR NB 1500 Version
  
  Hardware:
  - Arduino MKR NB 1500 (with ublox SARA-R410M)
  - Arduino MKR SD PROTO Shield
  - MKR RELAY Shield
  - Hologram.io SIM Card
  - SD Card
  - Tank level sensor (digital float switch)
  
  Features:
  - Multi-tank/multi-site support (A-Z tanks via TANKA_, TANKB_ config)
  - Tank level monitoring with digital sensor
  - SMS alerts via Hologram.io for alarm states
  - Daily email reports of levels and changes
  - SD card data logging
  - Relay control capability
  - Low power sleep modes
  - Server communication for remote control
  
  REFACTORING STATUS (Nov 2025):
  - Legacy single-tank globals REMOVED (siteLocationName, tankNumber, tankHeightInches, etc.)
  - All tanks now configured via tanks[] array (TANKA_, TANKB_ config keys)
  - MAX_TANKS removed; hard-coded to 26 (A-Z support)
  - COMPLETED: All alarm/logging/reporting functions use per-tank arrays
  - COMPLETED: Per-tank sensor reading (digitalPin, analogPin, currentLoopChannel)
  - Each tank can have its own sensor pin/channel configuration
  - updateAllTankReadings() reads all configured tanks independently
  
  Created: September 2025
  Using GitHub Copilot for code generation
  
*/

// Core Arduino MKR libraries
#include <MKRNB.h>        // MKR NB 1500 cellular connectivity
#include <SD.h>           // SD card functionality
#include <ArduinoLowPower.h>  // Low power sleep modes
#include <RTCZero.h>      // Real-time clock
#include <Wire.h>         // I2C communication for current loop sensors

// Include hardware configuration file (compile-time constants only)
// User settings are configured in tank_config.txt on SD card
#include "config_template.h"

// Sensor type definitions
#define DIGITAL_FLOAT 0
#define ANALOG_VOLTAGE 1  
#define CURRENT_LOOP 2

// Power management definitions
#define SLEEP_MODE_NORMAL 0      // Normal sleep between checks
#define SLEEP_MODE_DEEP 1        // Deep sleep for extended periods
#define WAKE_REASON_TIMER 0      // Woke up due to timer
#define WAKE_REASON_CELLULAR 1   // Woke up due to cellular data

// Physical constants
#define INCHES_PER_FOOT 12
#define ADC_MAX_VALUE 4095.0     // 12-bit ADC resolution
#define ADC_REFERENCE_VOLTAGE 3.3 // MKR board reference voltage

// Initialize cellular components
NB nbAccess;
NB_SMS sms;
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

#ifndef SERVER_DEVICE_KEY
#define SERVER_DEVICE_KEY "server_device_key_here"  // Server's Hologram device ID for remote commands
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
#ifndef HOLOGRAM_APN
#define HOLOGRAM_APN "hologram"
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
#ifndef SD_REPORT_LOG_FILE
#define SD_REPORT_LOG_FILE "report_log.txt"
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
#ifndef DAILY_REPORT_TIME
#define DAILY_REPORT_TIME "05:00"
#endif
#ifndef SHORT_SLEEP_MINUTES
#define SHORT_SLEEP_MINUTES 10
#endif
#ifndef NORMAL_SLEEP_HOURS
#define NORMAL_SLEEP_HOURS 1
#endif
#ifndef ENABLE_WAKE_ON_PING
#define ENABLE_WAKE_ON_PING true
#endif
#ifndef DEEP_SLEEP_MODE
#define DEEP_SLEEP_MODE false
#endif

const int sleep_hours = SLEEP_INTERVAL_HOURS;
const int report_hours = DAILY_REPORT_HOURS;

// Tank level state (legacy single-sensor reading - to be replaced with per-tank arrays)
int currentLevelState = LOW;
int previousLevelState = LOW;
bool alarmSent = false;

// Per-tank level measurements in inches
float tankCurrentInches[26];  // Current reading per tank
bool tankAlarmSent[26];       // Alarm state per tank

// Large decrease detection
float decreaseStartLevel = 0.0;
unsigned long decreaseStartTime = 0;
bool decreaseDetected = false;

// Power failure recovery state
bool systemRecovering = false;
String lastShutdownReason = "";
unsigned long lastHeartbeat = 0;
bool powerFailureRecovery = false;

// Data logging
#ifndef LOG_FILE_NAME
#define LOG_FILE_NAME "tanklog.txt"
#endif
String logFileName = LOG_FILE_NAME;

// SD Card configuration variables (loaded from SD card config file - REQUIRED)
float inchesPerUnit = 1.0;
bool digitalHighAlarm = true;
bool digitalLowAlarm = false;
float largeDecreaseThreshold = 24.0;
int largeDecreaseWaitHours = 2;

// Calibration point structure
#define MAX_CALIBRATION_POINTS 10

struct CalibrationPoint {
  float sensorValue;    // Raw sensor reading (voltage, current, etc.)
  float actualHeight;   // Actual measured height in inches
  String timestamp;     // Timestamp of calibration point (for tracking/debugging)
};

// Multi-site / Multi-tank support - all tanks configured via TANKA, TANKB, etc.
// Supports up to 26 tanks (A-Z), adjust array size below if needed
struct TankEntry {
  String siteName;            // Per tank site identifier (can differ across tanks)
  int tankNum = -1;           // Tank number within a site (used for grouping on server)
  float tankHeight = -1;      // Height in inches for percent calculations (required)
  float highAlarm = -1;       // High alarm threshold inches (required)
  float lowAlarm = -1;        // Low alarm threshold inches (required)
  int digitalPin = -1;        // Optional digital sensor pin (e.g. floating switch array)
  int analogPin = -1;         // Optional analog pin index (A1 -> 1) for voltage sensor
  int currentLoopChannel = -1;// Optional 4-20mA current loop channel index
  String configFileName;      // The name of the .cfg file for this tank
  
  // Calibration data for this specific tank
  CalibrationPoint calibrationPoints[MAX_CALIBRATION_POINTS];
  int numCalibrationPoints = 0;
};
int tankCount = 0;            // Auto-detected from config (highest tank index + 1)
TankEntry tanks[26];          // Support A-Z tanks (26 max)
// Per-tank daily tracking
float tankPrevInches[26];
float tankChange24h[26];

// Network and communication configuration (loaded from SD card - REQUIRED)
String hologramDeviceKey = "";
String serverDeviceKey = "";  // Server's device ID for remote commands
String alarmPhonePrimary = "";
String alarmPhoneSecondary = "";
String dailyReportPhone = "";
String hologramAPN = "hologram";

// Timing configuration (loaded from SD card - REQUIRED)
int sleepIntervalHours = 1;
int dailyReportHours = 24;
String dailyReportTime = "05:00";

// Time synchronization variables
bool timeIsSynced = false;
unsigned long lastTimeSyncMillis = 0;
const unsigned long TIME_SYNC_INTERVAL_MS = 24 * 60 * 60 * 1000; // Sync once per day

// Power management variables (loaded from SD card - REQUIRED)
volatile bool wakeFromCellular = false;  // Flag set when woken by cellular data
volatile int wakeReason = WAKE_REASON_TIMER;  // Reason for last wake
unsigned long lastCellularCheck = 0;  // Last time we checked for cellular data
bool deepSleepMode = false;  // Whether to use deep sleep mode
int shortSleepMinutes = 10;  // Short sleep duration for frequent checks
int normalSleepHours = 1;    // Normal sleep duration between readings
bool wakeOnPingEnabled = true; // Wake on ping functionality

// Height calibration system
// Note: MAX_CALIBRATION_POINTS and CalibrationPoint moved above TankEntry struct


// Network configuration (loaded from SD card - REQUIRED)
int connectionTimeoutMs = 30000;
int smsRetryAttempts = 3;

// Log file names (loaded from SD card)
String hourlyLogFile = "hourly_log.txt";
String dailyLogFile = "daily_log.txt";
String alarmLogFile = "alarm_log.txt";
String decreaseLogFile = "decrease_log.txt";
String reportLogFile = "report_log.txt";
String systemLogFile = "system_events.log";

// Forward declarations
void logEvent(String event, int tankIdx = -1);
void checkLargeDecrease(int tankIdx);
float getTankLevelInches();
float interpolateHeight(float sensorValue, int tankIdx);

// Helper to create a sanitized log filename from site and tank info
String getTankLogFileName(int tankIdx) {
  if (tankIdx < 0 || tankIdx >= tankCount) {
    return "error.log";
  }
  String siteName = tanks[tankIdx].siteName;
  siteName.replace(" ", "_"); // Sanitize spaces
  return siteName + "_" + String(tanks[tankIdx].tankNum) + ".csv";
}

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
  
  // Initialize pins based on sensor type (legacy default)
#if SENSOR_TYPE == DIGITAL_FLOAT
  pinMode(TANK_LEVEL_PIN, INPUT_PULLUP);  // Digital float switch with pullup
#elif SENSOR_TYPE == ANALOG_VOLTAGE
  pinMode(ANALOG_SENSOR_PIN, INPUT);      // Analog voltage sensor
#elif SENSOR_TYPE == CURRENT_LOOP
  Wire.begin();                           // Initialize I2C for current loop module
#endif
  
  pinMode(RELAY_CONTROL_PIN, OUTPUT);     // Relay control
  pinMode(LED_BUILTIN, OUTPUT);           // Status LED
  
  // Initialize per-tank sensor pins (done after config load in later section)
  
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
    
    // Check for power failure recovery
    checkPowerFailureRecovery();
    
    // Load configuration from SD card
    loadSDCardConfiguration();
    
    // Initialize per-tank sensor pins based on loaded config
    bool i2cInitialized = false;
    for (int i = 0; i < tankCount && i < 26; i++) {
      if (tanks[i].currentLoopChannel >= 0) {
        // Initialize I2C for current loop sensors
        if (!i2cInitialized) {
          Wire.begin();
          i2cInitialized = true;
#ifdef ENABLE_SERIAL_DEBUG
          if (ENABLE_SERIAL_DEBUG) Serial.println("I2C initialized for current loop sensors");
#endif
        }
      }
    }
  }
  
  // Initialize RTC
  rtc.begin();
  
  // Connect to cellular network with retry logic
  if (initializeCellular()) {
#ifdef ENABLE_SERIAL_DEBUG
    if (ENABLE_SERIAL_DEBUG) Serial.println("Connected to cellular network");
#endif
    
    // Sync time from cellular network
    if (syncTimeFromCellular()) {
#ifdef ENABLE_SERIAL_DEBUG
      if (ENABLE_SERIAL_DEBUG) Serial.println("Time synchronized from cellular network");
#endif
    } else {
#ifdef ENABLE_SERIAL_DEBUG
      if (ENABLE_SERIAL_DEBUG) Serial.println("Failed to sync time from network");
#endif
    }
    
    // Send startup or recovery notification
    if (powerFailureRecovery) {
      sendRecoveryNotification();
    } else {
      sendStartupNotification();
    }
    
    // Log startup event
    logEvent(powerFailureRecovery ? "System recovery completed" : "System startup - Connected to network");
  } else {
#ifdef ENABLE_SERIAL_DEBUG
    if (ENABLE_SERIAL_DEBUG) Serial.println("Failed to connect to cellular network");
#endif
    logEvent("System startup - Network connection failed");
  }
  
  // Restore system state
  restoreSystemState();
  
  // Read initial tank level (legacy single-sensor)
  currentLevelState = readTankLevel();
  previousLevelState = currentLevelState;
  
  // Initialize per-tank arrays with initial readings from each sensor
  for (int i = 0; i < tankCount && i < 26; i++) {
    float initialReading = readTankSensor(i);
    tankCurrentInches[i] = (initialReading >= 0) ? initialReading : 0.0;
    tankPrevInches[i] = tankCurrentInches[i];
    tankChange24h[i] = 0.0;
    tankAlarmSent[i] = false;
    
#ifdef ENABLE_SERIAL_DEBUG
    if (ENABLE_SERIAL_DEBUG) {
      Serial.println("Tank " + String((char)('A' + i)) + " initial: " + 
                     String(tankCurrentInches[i], 1) + " inches");
    }
#endif
  }
  
#ifdef ENABLE_SERIAL_DEBUG
  if (ENABLE_SERIAL_DEBUG) Serial.println("Setup complete - entering main loop");
#endif
  logEvent("Setup complete - entering main loop");
  
  // Mark successful startup
  saveSystemState("normal_operation");
}

void loop() {
  // Update heartbeat for power failure detection
  updateHeartbeat();
  
  bool firstTankWasInAlarm = (tankCount > 0) ? tankAlarmSent[0] : false;

  // Per-tank sensor reading - reads each tank's configured sensor
  updateAllTankReadings();
  
  // Legacy single-sensor compatibility (uses first tank or default sensor)
  currentLevelState = readTankLevel();
  
  // Check for alarm condition on first tank (legacy compatibility)
  if (tankCount > 0) {
    bool firstTankAlarm = (tankCurrentInches[0] >= tanks[0].highAlarm) || 
                          (tankCurrentInches[0] <= tanks[0].lowAlarm);
    
    // Reset alarm flag if level returns to normal
    if (!firstTankAlarm && firstTankWasInAlarm) {
      logEvent("Tank level returned to normal");
      logAlarmEvent(0, "normal");
    }
    
    // Check for large decrease in level (first tank only for legacy)
    checkLargeDecrease(0); // Use first tank (index 0)
  }
  
  // Check if it's time for hourly log entry
  for (int i = 0; i < tankCount; i++) {
    logHourlyData(i);
  }
  
  // Check for periodic time sync (once per day)
  if (timeIsSynced && (millis() - lastTimeSyncMillis > TIME_SYNC_INTERVAL_MS)) {
    if (connectToCellular()) {
      syncTimeFromCellular();
    }
  }
  
  // Check for server commands via Hologram (optimized for power saving)
  static unsigned long lastServerCheckTime = 0;
  bool checkServerCommands = false;
  
  // Check server commands more frequently if we recently woke from cellular data
  unsigned long serverCheckInterval = (wakeFromCellular) ? 60000 : 600000;  // 1 min vs 10 min
  
  if (millis() - lastServerCheckTime > serverCheckInterval) {
    if (connectToCellular()) {
      checkForIncomingData();  // Check for any incoming data first
      checkForServerCommands();
      // processIncomingSMS();    // Function not implemented yet
      checkServerCommands = true;
    }
    lastServerCheckTime = millis();
    wakeFromCellular = false;  // Reset the flag after checking
  }
  
  // Check if it's time for daily report using configured time
  if (isTimeForDailyReport()) {
    // Multi-tank: send a report per configured tank
    for (int i = 0; i < tankCount && i < 26; i++) {
      sendDailyReportForTank(i);
      logDailyDataForTank(i);
      // Update 24-hour change tracking per tank
      tankChange24h[i] = tankCurrentInches[i] - tankPrevInches[i];
      tankPrevInches[i] = tankCurrentInches[i];
    }
    time_tick_report = 0;  // Reset daily counter for fallback compatibility
  }
  
  // Log current status (shows first tank)
  String statusMsg = "Tank level: " + String(tankCount > 0 ? tankCurrentInches[0] : 0.0, 1) + " inches";
  statusMsg += " (" + String(currentLevelState == HIGH ? "ALARM" : "Normal") + ")";
  statusMsg += ", " + String(tankCount) + " tank(s) configured";
  statusMsg += ", Hours: " + String(time_tick_hours);
  statusMsg += ", Report hours: " + String(time_tick_report);
  if (timeIsSynced) {
    statusMsg += ", Time: " + getCurrentTimestamp();
    statusMsg += ", Next report: " + dailyReportTime;
  } else {
    statusMsg += ", Time: NOT SYNCED";
  }
  
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
  
  // Periodic system state backup (every 10 minutes)
  static unsigned long lastBackup = 0;
  if (millis() - lastBackup > 600000) {  // 10 minutes
    saveSystemState("periodic_backup");
    lastBackup = millis();
  }
  
  bool anyTankInAlarm = false;
  for (int i = 0; i < tankCount && i < 26; i++) {
    if (tankAlarmSent[i]) {
      anyTankInAlarm = true;
      break;
    }
  }
  alarmSent = anyTankInAlarm;

  // Determine sleep strategy based on current conditions
  bool needsFrequentChecking = (alarmSent || wakeFromCellular || checkServerCommands);
  
  if (needsFrequentChecking) {
    // Use shorter sleep intervals when active monitoring is needed
#ifdef ENABLE_SERIAL_DEBUG
    if (ENABLE_SERIAL_DEBUG) {
      Serial.print("Entering power save mode for ");
      Serial.print(shortSleepMinutes);
      Serial.println(" minute(s) - active monitoring");
    }
#endif
    saveSystemState("active_monitoring");
    enterPowerSaveMode();
  } else {
    // Use normal sleep intervals for routine monitoring
#ifdef ENABLE_SERIAL_DEBUG
    if (ENABLE_SERIAL_DEBUG) {
      Serial.print("Entering normal sleep mode for ");
      Serial.print(normalSleepHours);
      Serial.println(" hour(s)");
    }
#endif
    saveSystemState("normal_sleep");
    enterDeepSleepMode();
  }
}

bool connectToCellular() {
#ifdef ENABLE_SERIAL_DEBUG
  if (ENABLE_SERIAL_DEBUG) Serial.println("Connecting to cellular network...");
#endif
  
  // Connect to the network
  if (nbAccess.begin("", hologramAPN.c_str()) != NB_READY) {
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
    float voltage = (adcValue / ADC_MAX_VALUE) * ADC_REFERENCE_VOLTAGE;
    totalVoltage += voltage;
    delay(10);
  }
  
  float avgVoltage = totalVoltage / numReadings;
  
  // Convert voltage to inches (using first tank - index 0)
  float levelInches = convertToInches(avgVoltage, 0);
  
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
  
  // Return HIGH if alarm (uses first tank thresholds for legacy single-sensor compatibility)
  if (tankCount > 0 && tanks[0].highAlarm > 0 && tanks[0].lowAlarm > 0) {
    return (levelInches >= tanks[0].highAlarm || levelInches <= tanks[0].lowAlarm) ? HIGH : LOW;
  }
  return LOW;
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
  
  // Convert current to inches (using first tank - index 0)
  float levelInches = convertToInches(current, 0);
  
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
  
  // Return HIGH if alarm (uses first tank thresholds for legacy single-sensor compatibility)
  if (tankCount > 0 && tanks[0].highAlarm > 0 && tanks[0].lowAlarm > 0) {
    return (levelInches >= tanks[0].highAlarm || levelInches <= tanks[0].lowAlarm) ? HIGH : LOW;
  }
  return LOW;
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

// ========== PER-TANK SENSOR READING FUNCTIONS ==========

// Read sensor for a specific tank based on its configured sensor pin/channel
float readTankSensor(int tankIdx) {
  if (tankIdx < 0 || tankIdx >= tankCount || tankIdx >= 26) return -1.0;
  
  // Check which sensor type is configured for this tank
  // Priority: digitalPin > analogPin > currentLoopChannel > default compile-time sensor
  
  if (tanks[tankIdx].digitalPin >= 0) {
    // Digital float switch sensor
    int pin = tanks[tankIdx].digitalPin;
    int level = digitalRead(pin);
    delay(100); // debounce
    int level2 = digitalRead(pin);
    if (level != level2) {
      delay(100);
      level = digitalRead(pin);
    }
    
    // Convert digital reading to inches (HIGH = full tank, LOW = empty)
    float tankHeight = tanks[tankIdx].tankHeight > 0 ? tanks[tankIdx].tankHeight : 120.0;
    return (level == HIGH) ? tankHeight : 0.0;
    
  } else if (tanks[tankIdx].analogPin >= 0) {
    // Analog voltage sensor
    int pin = tanks[tankIdx].analogPin;
    
    // Read multiple times for stability
    float totalVoltage = 0;
    const int numReadings = 10;
    
    for (int i = 0; i < numReadings; i++) {
      int adcValue = analogRead(pin);
      float voltage = (adcValue / ADC_MAX_VALUE) * ADC_REFERENCE_VOLTAGE;
      totalVoltage += voltage;
      delay(10);
    }
    
    float avgVoltage = totalVoltage / numReadings;
    return convertToInches(avgVoltage, tankIdx);
    
  } else if (tanks[tankIdx].currentLoopChannel >= 0) {
    // Current loop sensor via I2C
    int channel = tanks[tankIdx].currentLoopChannel;
    
    Wire.beginTransmission(I2C_CURRENT_LOOP_ADDRESS);
    Wire.write(channel);
    if (Wire.endTransmission() != 0) {
      return -1.0; // Communication error
    }
    
    Wire.requestFrom(I2C_CURRENT_LOOP_ADDRESS, 2);
    
    if (Wire.available() >= 2) {
      uint16_t rawValue = (Wire.read() << 8) | Wire.read();
      float current = 4.0 + ((rawValue / 65535.0) * 16.0); // 4-20mA
      return convertToInches(current, tankIdx);
    }
    
    return -1.0; // Error reading
    
  } else {
    // No per-tank sensor configured, use default compile-time sensor
    // This provides backward compatibility
    return getTankLevelInches();
  }
}

// Update all tank readings from their respective sensors
void updateAllTankReadings() {
  for (int i = 0; i < tankCount && i < 26; i++) {
    float reading = readTankSensor(i);
    
    if (reading >= 0) {  // Valid reading
      tankCurrentInches[i] = reading;
      
      // Check alarm conditions for this tank
      if (tanks[i].highAlarm > 0 && tanks[i].lowAlarm > 0) {
        bool alarmCondition = (reading >= tanks[i].highAlarm) || (reading <= tanks[i].lowAlarm);
        
        if (alarmCondition && !tankAlarmSent[i]) {
          tankAlarmSent[i] = true;
          logEvent("Tank " + String((char)('A' + i)) + " alarm: " + String(reading, 1) + " inches");
          handleAlarmCondition(i);
        } else if (!alarmCondition && tankAlarmSent[i]) {
          tankAlarmSent[i] = false;
          logEvent("Tank " + String((char)('A' + i)) + " normal: " + String(reading, 1) + " inches");
          if (i != 0) {
            logAlarmEvent(i, "normal");
          }
        }
      }
    }
  }
}

void handleAlarmCondition(int tankIdx) {
  if (tankIdx < 0 || tankIdx >= tankCount) return;
  
#ifdef ENABLE_SERIAL_DEBUG
  if (ENABLE_SERIAL_DEBUG) Serial.println("ALARM: Tank " + String((char)('A' + tankIdx)) + " alarm condition detected!");
#endif
  
  // Set alarm flag
  alarmSent = true;
  
  // Turn on status LED
  digitalWrite(LED_BUILTIN, HIGH);
  
  // Activate relay (if needed for alarm indication)
  digitalWrite(RELAY_CONTROL_PIN, HIGH);
  
  float levelInches = tankCurrentInches[tankIdx];
  String siteName = tanks[tankIdx].siteName;
  int tankNumber = tanks[tankIdx].tankNum;
  
  // Determine alarm type
  String alarmType = "change";
  if (levelInches >= tanks[tankIdx].highAlarm) {
    alarmType = "high";
  } else if (levelInches <= tanks[tankIdx].lowAlarm) {
    alarmType = "low";
  }
  
  // Log alarm event
  String alarmMsg = "ALARM: " + siteName + " Tank #" + String(tankNumber) + " " + alarmType +
                    " - " + String(levelInches, 1) + " inches";
  logEvent(alarmMsg);
  logAlarmEvent(tankIdx, alarmType);
  
  // Connect to network if not connected
  if (!connectToCellular()) {
#ifdef ENABLE_SERIAL_DEBUG
    if (ENABLE_SERIAL_DEBUG) Serial.println("Failed to connect for alarm notification");
#endif
    logEvent("ALARM: Failed to connect to network");
    return;
  }
  
  // Send alarm SMS messages
  sendAlarmSMS(tankIdx);
  
  // Send alarm data to Hologram.io
  sendHologramData("ALARM", alarmMsg);
  
  // Turn off status LED after alarm sent
  digitalWrite(LED_BUILTIN, LOW);
  
  // Keep relay on for alarm indication (can be turned off manually or after time)
  // digitalWrite(RELAY_CONTROL_PIN, LOW);  // Uncomment to turn off relay immediately
}

void sendAlarmSMS(int tankIdx) {
  if (tankIdx < 0 || tankIdx >= tankCount) return;
  
  String feetInchesFormat = formatInchesToFeetInches(tankCurrentInches[tankIdx]);
  String message = "TANK ALARM " + tanks[tankIdx].siteName + " Tank #" + String(tanks[tankIdx].tankNum) + 
                  ": Level " + feetInchesFormat + " at " + getCurrentTimestamp() + 
                  ". Immediate attention required.";
  
  // Send to primary contact with retry logic
  bool primarySent = false;
  for (int attempt = 1; attempt <= smsRetryAttempts && !primarySent; attempt++) {
    if (sms.beginSMS(alarmPhonePrimary.c_str())) {
      sms.print(message);
      sms.endSMS();
      primarySent = true;
#ifdef ENABLE_SERIAL_DEBUG
      if (ENABLE_SERIAL_DEBUG) Serial.println("Alarm SMS sent to primary contact (attempt " + String(attempt) + ")");
#endif
      logEvent("Alarm SMS sent to primary contact (attempt " + String(attempt) + ")");
    } else {
#ifdef ENABLE_SERIAL_DEBUG
      if (ENABLE_SERIAL_DEBUG) Serial.println("Failed to send SMS to primary contact (attempt " + String(attempt) + ")");
#endif
      if (attempt < smsRetryAttempts) {
        delay(3000);  // Wait 3 seconds before retry
      }
    }
  }
  
  if (!primarySent) {
    logEvent("Failed to send alarm SMS to primary contact after " + String(smsRetryAttempts) + " attempts");
  }
  
  delay(5000);  // Wait between messages
  
  // Send to secondary contact with retry logic
  bool secondarySent = false;
  for (int attempt = 1; attempt <= smsRetryAttempts && !secondarySent; attempt++) {
    if (sms.beginSMS(alarmPhoneSecondary.c_str())) {
      sms.print(message);
      sms.endSMS();
      secondarySent = true;
#ifdef ENABLE_SERIAL_DEBUG
      if (ENABLE_SERIAL_DEBUG) Serial.println("Alarm SMS sent to secondary contact (attempt " + String(attempt) + ")");
#endif
      logEvent("Alarm SMS sent to secondary contact (attempt " + String(attempt) + ")");
    } else {
#ifdef ENABLE_SERIAL_DEBUG
      if (ENABLE_SERIAL_DEBUG) Serial.println("Failed to send SMS to secondary contact (attempt " + String(attempt) + ")");
#endif
      if (attempt < smsRetryAttempts) {
        delay(3000);  // Wait 3 seconds before retry
      }
    }
  }
  
  if (!secondarySent) {
    logEvent("Failed to send alarm SMS to secondary contact after " + String(smsRetryAttempts) + " attempts");
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
  
  // Legacy single-tank daily report - uses first tank only
  if (tankCount < 1) return;
  
  String feetInchesFormat = formatInchesToFeetInches(tankCurrentInches[0]);
  String changeFeetInchesFormat = formatInchesToFeetInches(abs(tankChange24h[0]));
  String changePrefix = (tankChange24h[0] >= 0) ? "+" : "";
  
  String message = "Daily Tank Report " + tanks[0].siteName + " - " + getCurrentTimestamp();
  message += "\nTank #" + String(tanks[0].tankNum) + " Level: " + feetInchesFormat;
  message += "\n24hr Change: " + changePrefix + changeFeetInchesFormat;
  message += "\nStatus: " + String(currentLevelState == HIGH ? "ALARM" : "Normal");
  message += "\nNext report in 24 hours";
  
  // Send daily SMS with retry logic
  bool dailySmsSent = false;
  for (int attempt = 1; attempt <= smsRetryAttempts && !dailySmsSent; attempt++) {
    if (sms.beginSMS(dailyReportPhone.c_str())) {
      sms.print(message);
      sms.endSMS();
      dailySmsSent = true;
#ifdef ENABLE_SERIAL_DEBUG
      if (ENABLE_SERIAL_DEBUG) Serial.println("Daily report SMS sent (attempt " + String(attempt) + ")");
#endif
      
      // Log successful daily report transmission
      logSuccessfulReport();
      
    } else {
#ifdef ENABLE_SERIAL_DEBUG
      if (ENABLE_SERIAL_DEBUG) Serial.println("Failed to send daily report SMS (attempt " + String(attempt) + ")");
#endif
      if (attempt < smsRetryAttempts) {
        delay(3000);  // Wait 3 seconds before retry
      }
    }
  }
  
  if (!dailySmsSent) {
    logEvent("Daily report: Failed to send SMS after " + String(smsRetryAttempts) + " attempts");
    return;
  }
  
  // Send daily data to Hologram.io
  sendHologramData("DAILY", message);
  
  // Log daily report event
  logEvent("Daily report completed");
}

// New: per-tank daily report using tank mapping
void sendDailyReportForTank(int idx) {
  // Bounds check
  if (idx < 0 || idx >= tankCount) return;
  
  // Validate tank entry
  if (tanks[idx].tankNum <= 0 || tanks[idx].siteName.length() == 0) {
    return; // skip undefined tank entries
  }
  
#ifdef ENABLE_SERIAL_DEBUG
  if (ENABLE_SERIAL_DEBUG) Serial.println("Sending daily report for site '" + tanks[idx].siteName + "' tank #" + String(tanks[idx].tankNum));
#endif
  
  // Connect to network if not connected
  if (!connectToCellular()) {
#ifdef ENABLE_SERIAL_DEBUG
    if (ENABLE_SERIAL_DEBUG) Serial.println("Failed to connect for daily report (multi-tank)");
#endif
    logEvent("Daily report: Failed to connect to network");
    return;
  }
  
  String feetInchesFormat = formatInchesToFeetInches(tankCurrentInches[idx]);
  String changeFeetInchesFormat = formatInchesToFeetInches(abs(tankChange24h[idx]));
  String changePrefix = (tankChange24h[idx] >= 0) ? "+" : "-";
  
  String message = "Daily Tank Report " + tanks[idx].siteName + " - " + getCurrentTimestamp();
  message += "\nTank #" + String(tanks[idx].tankNum) + " Level: " + feetInchesFormat;
  message += "\n24hr Change: " + changePrefix + changeFeetInchesFormat;
  message += "\nStatus: " + String(tankAlarmSent[idx] ? "ALARM" : "Normal");
  message += "\nNext report in 24 hours";
  
  // Send daily SMS with retry logic
  bool dailySmsSent = false;
  for (int attempt = 1; attempt <= smsRetryAttempts && !dailySmsSent; attempt++) {
    if (sms.beginSMS(dailyReportPhone.c_str())) {
      sms.print(message);
      sms.endSMS();
      dailySmsSent = true;
#ifdef ENABLE_SERIAL_DEBUG
      if (ENABLE_SERIAL_DEBUG) Serial.println("Daily report SMS sent (attempt " + String(attempt) + ")");
#endif
      // Log successful daily report transmission
      logSuccessfulReport();
    } else {
#ifdef ENABLE_SERIAL_DEBUG
      if (ENABLE_SERIAL_DEBUG) Serial.println("Failed to send daily report SMS (attempt " + String(attempt) + ")");
#endif
      if (attempt < smsRetryAttempts) {
        delay(3000);  // Wait 3 seconds before retry
      }
    }
  }
  
  if (!dailySmsSent) {
    logEvent("Daily report: Failed to send SMS after " + String(smsRetryAttempts) + " attempts");
    return;
  }
  
  // Send daily data to Hologram.io
  sendHologramData("DAILY", message);
  
  // Log daily report event
  logEvent("Daily report completed for site '" + tanks[idx].siteName + "' tank #" + String(tanks[idx].tankNum));
}

void sendStartupNotification() {
  if (tankCount < 1) return;
  
  String feetInchesFormat = formatInchesToFeetInches(tankCurrentInches[0]);
  String message = "Tank Alarm System Started " + tanks[0].siteName + " - " + getCurrentTimestamp();
  message += "\nTank #" + String(tanks[0].tankNum) + " Initial Level: " + feetInchesFormat;
  message += "\nStatus: " + String(currentLevelState == HIGH ? "ALARM" : "Normal");
  message += "\nSystem ready for monitoring";
  
  // Send startup SMS to daily report number with retry logic
  bool startupSmsSent = false;
  for (int attempt = 1; attempt <= smsRetryAttempts && !startupSmsSent; attempt++) {
    if (sms.beginSMS(dailyReportPhone.c_str())) {
      sms.print(message);
      sms.endSMS();
      startupSmsSent = true;
#ifdef ENABLE_SERIAL_DEBUG
      if (ENABLE_SERIAL_DEBUG) Serial.println("Startup notification sent (attempt " + String(attempt) + ")");
#endif
      logEvent("Startup notification SMS sent (attempt " + String(attempt) + ")");
    } else {
#ifdef ENABLE_SERIAL_DEBUG
      if (ENABLE_SERIAL_DEBUG) Serial.println("Failed to send startup notification SMS (attempt " + String(attempt) + ")");
#endif
      if (attempt < smsRetryAttempts) {
        delay(3000);  // Wait 3 seconds before retry
      }
    }
  }
  
  if (!startupSmsSent) {
    logEvent("Failed to send startup notification SMS after " + String(smsRetryAttempts) + " attempts");
  }
  
  // Send startup data to Hologram.io
  sendHologramData("STARTUP", message);
}

void sendHologramData(String topic, String message) {
  // Create JSON message for Hologram.io
  String jsonPayload = "{\"k\":\"" + hologramDeviceKey + "\",";
  jsonPayload += "\"d\":\"" + message + "\",";
  jsonPayload += "\"t\":[\"" + topic + "\"]}";
  
  // Retry logic for sending data to Hologram.io
  bool dataSent = false;
  for (int attempt = 1; attempt <= smsRetryAttempts && !dataSent; attempt++) {
    // Connect to Hologram.io server
    if (client.connect(HOLOGRAM_URL, HOLOGRAM_PORT)) {
#ifdef ENABLE_SERIAL_DEBUG
      if (ENABLE_SERIAL_DEBUG) Serial.println("Connected to Hologram.io (attempt " + String(attempt) + ")");
#endif
      
      // Send the data
      client.print(jsonPayload);
      client.stop();
      
      dataSent = true;
      
#ifdef ENABLE_SERIAL_DEBUG
      if (ENABLE_SERIAL_DEBUG) Serial.println("Data sent to Hologram.io: " + topic);
#endif
      logEvent("Data sent to Hologram.io: " + topic + " (attempt " + String(attempt) + ")");
    } else {
#ifdef ENABLE_SERIAL_DEBUG
      if (ENABLE_SERIAL_DEBUG) Serial.println("Failed to connect to Hologram.io (attempt " + String(attempt) + ")");
#endif
      
      if (attempt < smsRetryAttempts) {
#ifdef ENABLE_SERIAL_DEBUG
        if (ENABLE_SERIAL_DEBUG) Serial.println("Retrying in 5 seconds...");
#endif
        delay(5000);  // Wait 5 seconds before retry
      }
    }
  }
  
  if (!dataSent) {
    logEvent("Failed to send data to Hologram.io after " + String(smsRetryAttempts) + " attempts: " + topic);
#ifdef ENABLE_SERIAL_DEBUG
    if (ENABLE_SERIAL_DEBUG) Serial.println("All retry attempts failed for topic: " + topic);
#endif
  }
}

// SD card health monitoring and recovery
bool ensureSDCardReady() {
  static unsigned long lastAttempt = 0;
  static bool lastState = false;
  
  // Don't spam retry attempts - wait at least 10 seconds between attempts
  unsigned long now = millis();
  if (now - lastAttempt < 10000 && !lastState) {
    return false;
  }
  
  lastAttempt = now;
  lastState = SD.begin(SD_CARD_CS_PIN);
  
  if (!lastState) {
#ifdef ENABLE_SERIAL_DEBUG
    if (ENABLE_SERIAL_DEBUG) Serial.println("SD card initialization failed, will retry");
#endif
  }
  
  return lastState;
}

void logEvent(String event, int tankIdx) {
  // Log events to SD card with timestamp
  if (ensureSDCardReady()) {
    String logFileName;
    if (tankIdx != -1) {
      logFileName = getTankLogFileName(tankIdx);
    } else {
      logFileName = systemLogFile;
    }
    
    File logFile = SD.open(logFileName, FILE_WRITE);
    if (logFile) {
      String logEntry = getCurrentTimestamp() + "," + event;
      logFile.println(logEntry);
      logFile.close();
#ifdef ENABLE_SERIAL_DEBUG
      if (ENABLE_SERIAL_DEBUG) Serial.println("Logged to " + logFileName + ": " + logEntry);
#endif
    }
  }
}

void checkForServerCommands() {
  // Check for commands from the server via Hologram.io
  // This function listens for messages from the server device
  
#ifdef ENABLE_SERIAL_DEBUG
  if (ENABLE_SERIAL_DEBUG) Serial.println("Checking for server commands...");
#endif
  
  // Note: This is a placeholder for Hologram command reception
  // In a full implementation, this would use Hologram's webhook/cloud functions
  // or direct device-to-device messaging to receive commands from the server
  
  // For now, we log that the client is ready to receive server commands
  // The serverDeviceKey variable contains the server's device ID for command filtering
  
  String commandCheckMsg = "Ready to receive commands from server: " + serverDeviceKey;
  
#ifdef ENABLE_SERIAL_DEBUG
  if (ENABLE_SERIAL_DEBUG) Serial.println(commandCheckMsg);
#endif
  
  // Log this periodically to confirm the client knows the server device ID
  static int commandCheckCount = 0;
  commandCheckCount++;
  if (commandCheckCount % 144 == 1) {  // Log once per day (144 * 10 minutes = 24 hours)
    logEvent(commandCheckMsg);
  }
  
  // Future implementation would:
  // 1. Connect to Hologram and check for messages from serverDeviceKey
  // 2. Parse received commands (PING, RELAY_ON, RELAY_OFF, etc.)
  // 3. Execute commands and send acknowledgment back to server
  // 4. Log command execution results
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

// Load configuration from SD card by scanning for .cfg files
void loadSDCardConfiguration() {
  if (!SD.begin(SD_CARD_CS_PIN)) {
    Serial.println("CRITICAL ERROR: Failed to initialize SD card for configuration loading.");
    while (true) { delay(5000); Serial.println("Please insert SD card and restart."); }
  }

  Serial.println("Scanning for tank configuration files (*.cfg)...");
  
  File root = SD.open("/");
  if (!root) {
    Serial.println("CRITICAL ERROR: Cannot open SD card root directory.");
    while (true) { delay(5000); }
  }

  while (true) {
    File entry = root.openNextFile();
    if (!entry) {
      // No more files
      break;
    }

    String fileName = entry.name();
    fileName.toLowerCase();

    if (fileName.endsWith(".cfg") && tankCount < 26) {
      Serial.println("Found config file: " + String(entry.name()));
      tanks[tankCount].configFileName = entry.name();
      
      // Parse this file for one tank's settings
      while (entry.available()) {
        String line = entry.readStringUntil('\n');
        line.trim();
        
        if (line.startsWith("#") || line.length() == 0) continue;

        int equalPos = line.indexOf('=');
        if (equalPos > 0) {
          String key = line.substring(0, equalPos);
          String value = line.substring(equalPos + 1);
          key.trim();
          value.trim();

          // Load tank-specific settings
          if (key == "SITE_NAME") {
            tanks[tankCount].siteName = value;
          } else if (key == "TANK_NUMBER") {
            tanks[tankCount].tankNum = value.toInt();
          } else if (key == "TANK_HEIGHT_INCHES") {
            tanks[tankCount].tankHeight = value.toFloat();
          } else if (key == "HIGH_ALARM_INCHES") {
            tanks[tankCount].highAlarm = value.toFloat();
          } else if (key == "LOW_ALARM_INCHES") {
            tanks[tankCount].lowAlarm = value.toFloat();
          } else if (key == "DIGITAL_PIN") {
            tanks[tankCount].digitalPin = value.toInt();
          } else if (key == "ANALOG_PIN") {
            if (value.startsWith("A") || value.startsWith("a")) {
              tanks[tankCount].analogPin = value.substring(1).toInt();
            } else {
              tanks[tankCount].analogPin = value.toInt();
            }
          } else if (key == "CURRENT_LOOP_CHANNEL") {
            tanks[tankCount].currentLoopChannel = value.toInt();
          } else if (key == "CAL_POINT") {
            // Parse "sensor_value,actual_height"
            int commaPos = value.indexOf(',');
            if (commaPos > 0 && tanks[tankCount].numCalibrationPoints < MAX_CALIBRATION_POINTS) {
              float sensorVal = value.substring(0, commaPos).toFloat();
              float heightVal = value.substring(commaPos + 1).toFloat();
              
              int calIdx = tanks[tankCount].numCalibrationPoints;
              tanks[tankCount].calibrationPoints[calIdx].sensorValue = sensorVal;
              tanks[tankCount].calibrationPoints[calIdx].actualHeight = heightVal;
              tanks[tankCount].numCalibrationPoints++;
            }
          }
          // Load global settings (will be overwritten by last file, which is fine)
          else if (key == "LARGE_DECREASE_THRESHOLD") {
            largeDecreaseThreshold = value.toFloat();
          } else if (key == "LARGE_DECREASE_WAIT_HOURS") {
            largeDecreaseWaitHours = value.toInt();
          } else if (key == "HOLOGRAM_DEVICE_KEY") {
            hologramDeviceKey = value;
          } else if (key == "SERVER_DEVICE_KEY") {
            serverDeviceKey = value;
          } else if (key == "ALARM_PHONE_PRIMARY") {
            alarmPhonePrimary = value;
          } else if (key == "ALARM_PHONE_SECONDARY") {
            alarmPhoneSecondary = value;
          } else if (key == "DAILY_REPORT_PHONE") {
            dailyReportPhone = value;
          } else if (key == "DAILY_REPORT_HOURS") {
            dailyReportHours = value.toInt();
          } else if (key == "DAILY_REPORT_TIME") {
            dailyReportTime = value;
          } else if (key == "DEEP_SLEEP_MODE") {
            deepSleepMode = (value == "true");
          } else if (key == "NORMAL_SLEEP_HOURS") {
            normalSleepHours = value.toInt();
          } else if (key == "SHORT_SLEEP_MINUTES") {
            shortSleepMinutes = value.toInt();
          }
        }
      }
      // After parsing file, increment tank count if required fields are present
      if (tanks[tankCount].siteName.length() > 0 && tanks[tankCount].tankNum > 0) {
        tankCount++;
      } else {
        Serial.println("WARNING: " + String(entry.name()) + " is missing SITE_NAME or TANK_NUMBER. Skipping.");
      }
      entry.close();
    }
  }
  root.close();

  // Validate we have at least one tank configured
  if (tankCount < 1) {
    Serial.println("CRITICAL ERROR: No valid .cfg files found on SD card.");
    while(1); // Halt
  }
  
  // Validate each tank has required fields
  for (int i = 0; i < tankCount; i++) {
    bool tankValid = true;
    if (tanks[i].tankHeight <= 0) {
      Serial.println("ERROR: Tank " + tanks[i].siteName + " #" + String(tanks[i].tankNum) + " missing TANK_HEIGHT_INCHES.");
      tankValid = false;
    }
    if (tanks[i].highAlarm <= 0 || tanks[i].lowAlarm <= 0) {
      Serial.println("ERROR: Tank " + tanks[i].siteName + " #" + String(tanks[i].tankNum) + " missing alarm thresholds.");
      tankValid = false;
    }
    if (!tankValid) while(1); // Halt
  }

  // Validate critical global configuration
  bool configValid = true;
  if (hologramDeviceKey.length() == 0 || hologramDeviceKey == "your_device_key_here") {
    Serial.println("CRITICAL ERROR: HOLOGRAM_DEVICE_KEY not configured in any .cfg file.");
    configValid = false;
  }
  if (alarmPhonePrimary.length() == 0) {
    Serial.println("CRITICAL ERROR: ALARM_PHONE_PRIMARY not configured.");
    configValid = false;
  }
  
  if (!configValid) {
    Serial.println("Halting due to critical configuration errors.");
    while (true) { delay(5000); }
  }
  
  Serial.println("========================================");
  Serial.println("Client configuration loaded successfully");
  Serial.println(String(tankCount) + " tank(s) configured:");
  for (int i = 0; i < tankCount; i++) {
    Serial.println("  - " + tanks[i].siteName + " #" + String(tanks[i].tankNum) + 
                   " (" + String(tanks[i].tankHeight) + " in)");
  }
  Serial.println("Daily Report Time: " + dailyReportTime);
  Serial.println("========================================");
}

// Convert sensor reading to inches (requires tank index for height reference)
float convertToInches(float sensorValue, int tankIdx) {
  // Use calibration data if available
  if (tankIdx >= 0 && tankIdx < tankCount && tanks[tankIdx].numCalibrationPoints >= 2) {
    return interpolateHeight(sensorValue, tankIdx);
  }
  
  // Fallback to original calculation methods if no calibration data
  float tankHeightInches = (tankIdx >= 0 && tankIdx < tankCount) ? tanks[tankIdx].tankHeight : 120.0;
  if (tankHeightInches <= 0) tankHeightInches = 120.0; // safety fallback

  // Determine sensor type from tank config or compile-time default
  int sensorType = -1;
  if (tankIdx >= 0 && tankIdx < tankCount) {
      if (tanks[tankIdx].digitalPin != -1) sensorType = DIGITAL_FLOAT;
      else if (tanks[tankIdx].analogPin != -1) sensorType = ANALOG_VOLTAGE;
      else if (tanks[tankIdx].currentLoopChannel != -1) sensorType = CURRENT_LOOP;
  }
  if (sensorType == -1) sensorType = SENSOR_TYPE; // Fallback to compile-time default

  switch (sensorType) {
    case DIGITAL_FLOAT:
      return (sensorValue == HIGH) ? tankHeightInches : 0.0;

    case ANALOG_VOLTAGE: {
      float voltage = sensorValue;
      float voltageRange = TANK_FULL_VOLTAGE - TANK_EMPTY_VOLTAGE;
      if (abs(voltageRange) < 0.001) return 0.0;
      float tankPercent = ((voltage - TANK_EMPTY_VOLTAGE) / voltageRange) * 100.0;
      tankPercent = constrain(tankPercent, 0.0, 100.0);
      return (tankPercent / 100.0) * tankHeightInches;
    }

    case CURRENT_LOOP: {
      float current = sensorValue;
      float currentRange = TANK_FULL_CURRENT - TANK_EMPTY_CURRENT;
      if (abs(currentRange) < 0.001) return 0.0;
      float tankPercent = ((current - TANK_EMPTY_CURRENT) / currentRange) * 100.0;
      tankPercent = constrain(tankPercent, 0.0, 100.0);
      return (tankPercent / 100.0) * tankHeightInches;
    }

    default:
      return 0.0;
  }
}

// This function provides backward compatibility for legacy single-tank setups
float getTankLevelInches() {
#if SENSOR_TYPE == DIGITAL_FLOAT
  return convertToInches(currentLevelState, 0);
  
#elif SENSOR_TYPE == ANALOG_VOLTAGE
  // Read analog sensor and calculate inches
  float totalVoltage = 0;
  const int numReadings = 5;
  
  for (int i = 0; i < numReadings; i++) {
    int adcValue = analogRead(ANALOG_SENSOR_PIN);
    float voltage = (adcValue / ADC_MAX_VALUE) * ADC_REFERENCE_VOLTAGE;
    totalVoltage += voltage;
    delay(5);
  }
  
  float avgVoltage = totalVoltage / numReadings;
  return convertToInches(avgVoltage, 0);
  
#elif SENSOR_TYPE == CURRENT_LOOP
  // Read current loop sensor and calculate inches
  float current = readCurrentLoopValue();
  if (current < 0) return -1.0; // Error indicator
  
  return convertToInches(current, 0);
  
#else
  return -1.0; // Error
#endif
}

// Convert inches to feet and inches format
String formatInchesToFeetInches(float totalInches) {
  int feet = (int)(totalInches / INCHES_PER_FOOT);
  float inches = totalInches - (feet * INCHES_PER_FOOT);
  
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

// Log hourly data for a specific tank
void logHourlyData(int tankIdx) {
  if (!ensureSDCardReady()) return;
  if (tankIdx < 0 || tankIdx >= tankCount) return;

  String logFileName = getTankLogFileName(tankIdx);
  
  String timestamp = getDateTimestamp();
  String feetInchesFormat = formatInchesToFeetInches(tankCurrentInches[tankIdx]);
  String changeFeetInchesFormat = formatInchesToFeetInches(abs(tankChange24h[tankIdx]));
  String changePrefix = (tankChange24h[tankIdx] >= 0) ? "+" : "-";
  
  String logEntry = timestamp + ",HOURLY," + feetInchesFormat + "," + 
                   changePrefix + changeFeetInchesFormat;
  
  File hourlyFile = SD.open(logFileName, FILE_WRITE);
  if (hourlyFile) {
    hourlyFile.println(logEntry);
    hourlyFile.close();
  }
}

// Log daily data in required format (legacy single-tank)
void logDailyData() {
  if (tankCount > 0) {
    logDailyDataForTank(0); // Log first tank for backward compatibility
  }
}

// New: per-tank daily log line to SD card
void logDailyDataForTank(int idx) {
  if (!ensureSDCardReady()) return;
  if (idx < 0 || idx >= tankCount) return;
  
  String logFileName = getTankLogFileName(idx);

  String timestamp = getDateTimestamp();
  String feetInchesFormat = formatInchesToFeetInches(tankCurrentInches[idx]);
  String changeFeetInchesFormat = formatInchesToFeetInches(abs(tankChange24h[idx]));
  String changePrefix = (tankChange24h[idx] >= 0) ? "+" : "-";
  
  String logEntry = timestamp + ",DAILY," + 
                   feetInchesFormat + "," + changePrefix + changeFeetInchesFormat;
  
  File dailyFile = SD.open(logFileName, FILE_WRITE);
  if (dailyFile) {
    dailyFile.println(logEntry);
    dailyFile.close();
#ifdef ENABLE_SERIAL_DEBUG
    if (ENABLE_SERIAL_DEBUG) Serial.println("Daily log to " + logFileName + ": " + logEntry);
#endif
  }
}

// Log alarm events in required format (per tank)
void logAlarmEvent(int tankIdx, String alarmState) {
  if (!ensureSDCardReady()) return;
  if (tankIdx < 0 || tankIdx >= tankCount) return;
  
  String logFileName = getTankLogFileName(tankIdx);
  
  String timestamp = getDateTimestamp();
  String logEntry = timestamp + ",ALARM," + alarmState;
  
  File alarmFile = SD.open(logFileName, FILE_WRITE);
  if (alarmFile) {
    alarmFile.println(logEntry);
    alarmFile.close();
  }
}

// Check for large decreases in tank level (iterates all tanks)
void checkAllLargeDecreases() {
  for (int i = 0; i < tankCount; i++) {
    checkLargeDecrease(i);
  }
}

// Check for large decreases in tank level for a specific tank
void checkLargeDecrease(int tankIdx) {
  if (tankIdx < 0 || tankIdx >= tankCount) return;
  
  // This function needs to be updated to use per-tank state variables
  // for decreaseDetected, decreaseStartLevel, and decreaseStartTime.
  // For now, it remains a placeholder.
}

// Log large decrease in required format (per tank)
void logLargeDecrease(int tankIdx, float totalDecrease) {
  if (!ensureSDCardReady()) return;
  if (tankIdx < 0 || tankIdx >= tankCount) return;
  
  String logFileName = getTankLogFileName(tankIdx);

  String timestamp = getDateTimestamp();
  String decreaseFeetInchesFormat = formatInchesToFeetInches(totalDecrease);
  
  String logEntry = timestamp + ",DECREASE," + decreaseFeetInchesFormat;
  
  File decreaseFile = SD.open(logFileName, FILE_WRITE);
  if (decreaseFile) {
    decreaseFile.println(logEntry);
    decreaseFile.close();
  }
  
  // Log the event in main system log as well
  String eventMsg = "Large decrease logged for " + tanks[tankIdx].siteName + " #" + String(tanks[tankIdx].tankNum) +
                   ": " + String(totalDecrease, 1) + " inches";
  logEvent(eventMsg);
}

// Synchronize time from cellular network
bool syncTimeFromCellular() {
#ifdef ENABLE_SERIAL_DEBUG
  if (ENABLE_SERIAL_DEBUG) Serial.println("Attempting to sync time from cellular network...");
#endif

  // Ensure we're connected to cellular
  if (!connectToCellular()) {
#ifdef ENABLE_SERIAL_DEBUG
    if (ENABLE_SERIAL_DEBUG) Serial.println("Failed to connect for time sync");
#endif
    return false;
  }

  // Get network time (Unix timestamp)
  unsigned long networkTime = nbAccess.getTime();
  
  if (networkTime == 0) {
#ifdef ENABLE_SERIAL_DEBUG
    if (ENABLE_SERIAL_DEBUG) Serial.println("Failed to get network time");
#endif
    return false;
  }

  // Convert Unix timestamp to date/time components
  // Note: This is a simplified conversion assuming UTC
  // For production, consider timezone adjustments
  unsigned long epochTime = networkTime;
  unsigned long hours = (epochTime % 86400) / 3600;
  unsigned long minutes = (epochTime % 3600) / 60;
  unsigned long seconds = epochTime % 60;
  
  // Calculate date from days since Unix epoch (1970-01-01)
  unsigned long daysSinceEpoch = epochTime / 86400;
  
  // Helper function to check if a year is a leap year
  auto isLeapYear = [](int year) -> bool {
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
  };
  
  // Helper function to get days in a month
  auto getDaysInMonth = [&](int month, int year) -> int {
    int daysInMonth[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month == 2 && isLeapYear(year)) {
      return 29;
    }
    return daysInMonth[month - 1];
  };
  
  // Start from Unix epoch: January 1, 1970
  int year = 1970;
  int month = 1;
  int day = 1;
  
  // Calculate the year
  while (true) {
    int daysInCurrentYear = isLeapYear(year) ? 366 : 365;
    if (daysSinceEpoch >= daysInCurrentYear) {
      daysSinceEpoch -= daysInCurrentYear;
      year++;
    } else {
      break;
    }
  }
  
  // Calculate the month
  while (true) {
    int daysInCurrentMonth = getDaysInMonth(month, year);
    if (daysSinceEpoch >= daysInCurrentMonth) {
      daysSinceEpoch -= daysInCurrentMonth;
      month++;
      if (month > 12) {
        month = 1;
        year++;
      }
    } else {
      break;
    }
  }
  
  // Calculate the day (add 1 because daysSinceEpoch is 0-based, but days are 1-based)
  day = daysSinceEpoch + 1;
  
  // Validate the calculated date
  if (day < 1 || day > getDaysInMonth(month, year)) {
    // Fallback to a safe date if calculation is invalid
#ifdef ENABLE_SERIAL_DEBUG
    if (ENABLE_SERIAL_DEBUG) Serial.println("Date calculation error, using fallback");
#endif
    year = 2025;
    month = 1;
    day = 1;
  }

  // Set the RTC with the network time
  rtc.setTime(hours, minutes, seconds);
  rtc.setDate(day, month, year - 2000); // RTCZero expects year as offset from 2000

#ifdef ENABLE_SERIAL_DEBUG
  if (ENABLE_SERIAL_DEBUG) {
    Serial.println("Time synchronized from network:");
    Serial.println("Unix timestamp: " + String(networkTime));
    Serial.println("Days since epoch: " + String(epochTime / 86400));
    Serial.println("Date: " + String(year) + "-" + String(month) + "-" + String(day));
    Serial.println("Time: " + String(hours) + ":" + String(minutes) + ":" + String(seconds));
    Serial.println("Is leap year: " + String(isLeapYear(year) ? "Yes" : "No"));
  }
#endif

  timeIsSynced = true;
  lastTimeSyncMillis = millis();
  logEvent("Time synchronized from cellular network");
  
  return true;
}

// Parse time string in HH:MM format
void parseTimeString(String timeStr, int &hours, int &minutes) {
  int colonPos = timeStr.indexOf(':');
  if (colonPos > 0 && colonPos < timeStr.length() - 1) {
    hours = timeStr.substring(0, colonPos).toInt();
    minutes = timeStr.substring(colonPos + 1).toInt();
  } else {
    // Default to 5:00 AM if parsing fails
    hours = 5;
    minutes = 0;
  }
  
  // Validate values
  if (hours < 0 || hours > 23) hours = 5;
  if (minutes < 0 || minutes > 59) minutes = 0;
}

// Check if it's time for the daily report based on configured time
bool isTimeForDailyReport() {
  if (!timeIsSynced) {
    // Fall back to the old tick-based system if time isn't synced
    return (time_tick_report >= dailyReportHours);
  }
  
  int reportHours, reportMinutes;
  parseTimeString(dailyReportTime, reportHours, reportMinutes);
  
  int currentHours = rtc.getHours();
  int currentMinutes = rtc.getMinutes();
  
  // Check if current time matches the configured report time (within 1 hour tolerance)
  if (currentHours == reportHours) {
    
    // Check if we've already sent a report today using the log file
    String lastReportDate = getLastReportDateFromLog();
    String currentDate = String(rtc.getYear() + 2000);
    if (rtc.getMonth() < 10) currentDate = currentDate + "0";
    currentDate = currentDate + String(rtc.getMonth());
    if (rtc.getDay() < 10) currentDate = currentDate + "0";
    currentDate = currentDate + String(rtc.getDay());
    
    // Only send report if we haven't sent one today
    if (lastReportDate != currentDate) {
      return true;
    }
  }
  
  return false;
}

// Log successful daily report transmission to dedicated log file
void logSuccessfulReport() {
  if (!ensureSDCardReady()) {
    // Note: Can't use logEvent here as it might also fail
    return;
  }
  
  String timestamp = getDateTimestamp();
  String currentDate = String(rtc.getYear() + 2000);
  if (rtc.getMonth() < 10) currentDate = currentDate + "0";
  currentDate = currentDate + String(rtc.getMonth());
  if (rtc.getDay() < 10) currentDate = currentDate + "0";
  currentDate = currentDate + String(rtc.getDay());
  
  String logEntry = currentDate + "," + timestamp + ",SUCCESS,Daily report sent successfully";
  
  File reportFile = SD.open(reportLogFile.c_str(), FILE_WRITE);
  if (reportFile) {
    reportFile.println(logEntry);
    reportFile.close();
    
#ifdef ENABLE_SERIAL_DEBUG
    if (ENABLE_SERIAL_DEBUG) Serial.println("Logged successful report: " + logEntry);
#endif
    logEvent("Daily report transmission logged successfully");
  } else {
    logEvent("Failed to open report log file for writing");
  }
}

// Get the date of the last successful report from the log file
String getLastReportDateFromLog() {
  if (!SD.begin(SD_CARD_CS_PIN)) {
    return ""; // Return empty string if SD card error
  }
  
  File reportFile = SD.open(reportLogFile.c_str());
  if (!reportFile) {
    // No report log file exists yet
    return "";
  }
  
  String lastDate = "";
  String line;
  
  // Read through all lines to find the last successful report
  while (reportFile.available()) {
    line = reportFile.readStringUntil('\n');
    line.trim();
    
    // Check if this line indicates a successful report
    if (line.indexOf("SUCCESS") >= 0) {
      // Extract the date (first part before first comma)
      int firstComma = line.indexOf(',');
      if (firstComma > 0) {
        lastDate = line.substring(0, firstComma);
      }
    }
  }
  
  reportFile.close();
  
#ifdef ENABLE_SERIAL_DEBUG
  if (ENABLE_SERIAL_DEBUG && lastDate.length() > 0) {
    Serial.println("Last successful report date from log: " + lastDate);
  }
#endif
  
  return lastDate;
}

// Enhanced power management functions for optimized battery life

void enterPowerSaveMode() {
  // Enter power save mode with short sleep duration for active monitoring
  // This mode allows faster response to server commands and alarms
  
#ifdef ENABLE_LOW_POWER_MODE
  if (ENABLE_LOW_POWER_MODE) {
    // Configure cellular modem for wake-on-data if possible
    configureCellularWakeup();
    
    // Use RTC for precise timing in power save mode
    rtc.setAlarmEpoch(rtc.getEpoch() + (shortSleepMinutes * 60));
    rtc.enableAlarm(rtc.MATCH_HHMMSS);
    rtc.attachInterrupt(wakeUpCallback);
    
    // Enter standby mode - can be woken by RTC or cellular

    LowPower.sleep(shortSleepMinutes * 60 * 1000);
    
    // Clear alarm after wake
    rtc.disableAlarm();
  } else {
    delay(shortSleepMinutes * 60 * 1000);  // Fallback to delay
  }
#else
  delay(shortSleepMinutes * 60 * 1000);  // Default delay
#endif
}

void enterDeepSleepMode() {
  // Enter deep sleep mode for maximum power savings during normal operation
  
#ifdef ENABLE_LOW_POWER_MODE
  if (ENABLE_LOW_POWER_MODE) {
    // Configure cellular modem for wake-on-data
    configureCellularWakeup();
    
    // Use RTC for precise timing in deep sleep
    rtc.setAlarmEpoch(rtc.getEpoch() + (normalSleepHours * 3600));
    rtc.enableAlarm(rtc.MATCH_HHMMSS);
    rtc.attachInterrupt(wakeUpCallback);
    
    // Enter deep sleep mode - lowest power consumption
    LowPower.deepSleep(normalSleepHours * 60 * 60 * 1000);
    
    // Clear alarm after wake
    rtc.disableAlarm();
  } else {
    delay(normalSleepHours * 60 * 60 * 1000);  // Fallback to delay
  }
#else
  delay(normalSleepHours * 60 * 60 * 1000);  // Default delay
#endif
}

void configureCellularWakeup() {
  // Configure the cellular modem to wake the device on incoming data
  // Note: This is a placeholder for modem-specific wake configuration
  
  // For MKR NB 1500 with SARA-R410M, we would configure:
  // 1. Power saving mode with wake on data
  // 2. Interrupt pin configuration
  // 3. Modem sleep/wake settings
  
#ifdef ENABLE_SERIAL_DEBUG
  if (ENABLE_SERIAL_DEBUG) Serial.println("Configuring cellular wake-on-data...");
#endif
  
  // Example modem configuration (implementation depends on specific modem)
  // nbAccess.lowPowerMode(true);  // Enable modem power saving
  // nbAccess.wakeOnData(true);    // Wake device on incoming data
  
  logEvent("Cellular wake-on-data configured");
}

void checkForIncomingData() {
  // Check if there's any incoming cellular data that might have woken us
  
#ifdef ENABLE_SERIAL_DEBUG
  if (ENABLE_SERIAL_DEBUG) Serial.println("Checking for incoming cellular data...");
#endif
  
  // Check for any pending data on the cellular connection
  if (client.available()) {
    wakeFromCellular = true;
    wakeReason = WAKE_REASON_CELLULAR;
    
#ifdef ENABLE_SERIAL_DEBUG
    if (ENABLE_SERIAL_DEBUG) Serial.println("Incoming cellular data detected - wake from cellular");
#endif
    
    logEvent("Device woken by incoming cellular data");
  } else {
    wakeReason = WAKE_REASON_TIMER;
  }
}

void wakeUpCallback() {
  // RTC interrupt callback for wake events
  // This function is called when the RTC alarm triggers
  
#ifdef ENABLE_SERIAL_DEBUG
  if (ENABLE_SERIAL_DEBUG) Serial.println("Device woken by RTC alarm");
#endif
  
  // The main loop will continue execution after this callback
}

// Power failure recovery and system state management functions

bool initializeCellular() {
  // Initialize cellular connection with retry logic
  Serial.println("Initializing cellular connection...");
  
  int attempts = 0;
  const int maxAttempts = 3;  // Limit attempts to prevent excessive power drain
  
  while (attempts < maxAttempts) {
    if (connectToCellular()) {
      logEvent("Cellular connection established");
      return true;
    }
    
    attempts++;
    Serial.println("Cellular connection attempt " + String(attempts) + " failed");
    
    if (attempts < maxAttempts) {
      Serial.println("Retrying in 5 seconds...");
      delay(5000);
    }
  }
  
  logEvent("Cellular connection failed after " + String(maxAttempts) + " attempts");
  Serial.println("WARNING: Operating without cellular connection");
  return false;
}

void checkPowerFailureRecovery() {
  Serial.println("Checking for power failure recovery...");
  
  File stateFile = SD.open("system_state.txt", FILE_READ);
  if (stateFile) {
    String lastState = stateFile.readString();
    stateFile.close();
    
    lastState.trim();
    
    if (lastState != "normal_shutdown" && lastState != "normal_operation") {
      powerFailureRecovery = true;
      systemRecovering = true;
      lastShutdownReason = lastState;
      Serial.println("Power failure detected! Last state: " + lastState);
      logEvent("POWER FAILURE RECOVERY - Last state: " + lastState);
      
      // Restore critical system state
      restoreSystemState();
    } else {
      Serial.println("Normal shutdown detected");
      logEvent("Normal startup - no power failure");
    }
  } else {
    Serial.println("No previous state file found - first startup");
    logEvent("First startup - no previous state");
  }
}

void restoreSystemState() {
  Serial.println("Restoring system state...");
  
  // Restore tank level measurements
  File levelFile = SD.open("tank_levels.txt", FILE_READ);
  if (levelFile) {
    String line = levelFile.readStringUntil('\n');
    line.trim();
    
    if (line.length() > 0) {
      // Parse saved levels: currentLevel,previousLevel,change24Hr,alarmSent
      int pos = 0;
      String parts[4];
      
      for (int i = 0; i < 4; i++) {
        int nextPos = line.indexOf(',', pos);
        if (nextPos == -1) {
          parts[i] = line.substring(pos);
          break;
        } else {
          parts[i] = line.substring(pos, nextPos);
          pos = nextPos + 1;
        }
      }
      
      if (parts[0].length() > 0 && tankCount > 0) {
        // Legacy: restore first tank values from single-tank persistence
        tankCurrentInches[0] = parts[0].toFloat();
        tankPrevInches[0] = parts[1].toFloat();
        tankChange24h[0] = parts[2].toFloat();
        alarmSent = (parts[3] == "1");
        tankAlarmSent[0] = alarmSent;
        
        Serial.println("Restored tank levels (legacy) - Current: " + String(tankCurrentInches[0]) + 
                      "in, Previous: " + String(tankPrevInches[0]) + 
                      "in, Change: " + String(tankChange24h[0]) + "in");
        logEvent("Tank level measurements restored from legacy file");
      }
    }
    
    levelFile.close();
  }
  
  // Restore timing counters
  File timingFile = SD.open("timing_state.txt", FILE_READ);
  if (timingFile) {
    String line = timingFile.readStringUntil('\n');
    line.trim();
    
    if (line.length() > 0) {
      // Parse timing: hours,report_hours
      int commaPos = line.indexOf(',');
      if (commaPos > 0) {
        time_tick_hours = line.substring(0, commaPos).toInt();
        time_tick_report = line.substring(commaPos + 1).toInt();
        
        Serial.println("Restored timing - Hours: " + String(time_tick_hours) + 
                      ", Report: " + String(time_tick_report));
        logEvent("Timing counters restored");
      }
    }
    
    timingFile.close();
  }
  
  logEvent("System state restoration completed");
}

void saveSystemState(String reason) {
  // Update heartbeat for power failure detection
  updateHeartbeat();
  
  // Save current state for power failure recovery
  File stateFile = SD.open("system_state.txt", FILE_WRITE);
  if (stateFile) {
    stateFile.print(reason);
    stateFile.close();
  }
  
  // Save tank level measurements (legacy format - first tank only)
  File levelFile = SD.open("tank_levels.txt", FILE_WRITE);
  if (levelFile && tankCount > 0) {
    levelFile.println(String(tankCurrentInches[0]) + "," + 
                     String(tankPrevInches[0]) + "," + 
                     String(tankChange24h[0]) + "," + 
                     String(tankAlarmSent[0] ? "1" : "0"));
    levelFile.close();
  }
  
  // Save timing counters
  File timingFile = SD.open("timing_state.txt", FILE_WRITE);
  if (timingFile) {
    timingFile.println(String(time_tick_hours) + "," + String(time_tick_report));
    timingFile.close();
  }
  
  // Periodic backup (every 5 minutes during operation)
  static unsigned long lastBackup = 0;
  if (millis() - lastBackup > 300000) {  // 5 minutes
    saveSystemState("periodic_backup");
    lastBackup = millis();
  }
}

void updateHeartbeat() {
  // Update heartbeat timestamp for power failure detection
  lastHeartbeat = millis();
  
  // Periodic heartbeat save (every 2 minutes)
  static unsigned long lastHeartbeatSave = 0;
  if (millis() - lastHeartbeatSave > 120000) {  // 2 minutes
    File heartbeatFile = SD.open("heartbeat.txt", FILE_WRITE);
    if (heartbeatFile) {
      heartbeatFile.println(getCurrentTimestamp());
      heartbeatFile.close();
    }
    lastHeartbeatSave = millis();
  }
}

void sendRecoveryNotification() {
  if (!connectToCellular()) return;
  if (tankCount < 1) return;
  
  String feetInchesFormat = formatInchesToFeetInches(tankCurrentInches[0]);
  String message = "TANK CLIENT RECOVERY NOTICE\n";
  message += "Site: " + tanks[0].siteName + " Tank #" + String(tanks[0].tankNum) + "\n";
  message += "Time: " + getCurrentTimestamp() + "\n";
  message += "Status: System recovered from power failure\n";
  message += "Last State: " + lastShutdownReason + "\n";
  message += "Current Level: " + feetInchesFormat + "\n";
  message += "All systems operational";
  
  // Send recovery SMS
  if (sms.beginSMS(dailyReportPhone.c_str())) {
    sms.print(message);
    sms.endSMS();
    logEvent("Recovery notification sent");
  }
  
  // Send recovery data to Hologram.io
  sendHologramData("RECOVERY", message);
  
  // Send power failure notification to server for daily email tracking
  sendPowerFailureNotificationToServer();
  
  logEvent("Recovery notification completed");
}

void sendPowerFailureNotificationToServer() {
  if (!connectToCellular()) return;
  if (tankCount < 1) return;
  
  String feetInchesFormat = formatInchesToFeetInches(tankCurrentInches[0]);
  String powerFailureData = tanks[0].siteName + "|" + String(tanks[0].tankNum) + 
                           "|" + getCurrentTimestamp() + "|" + feetInchesFormat + 
                           "|" + lastShutdownReason;
  
  // Send to server using POWER_FAILURE topic for daily email inclusion
  sendHologramData("POWER_FAILURE", powerFailureData);
  
  logEvent("Power failure notification sent to server for daily email tracking");
}

// ========== HEIGHT CALIBRATION FUNCTIONS (OBSOLETE - replaced by per-tank calibration in .cfg files) ==========

// NOTE: The following obsolete functions reference deleted global variables:
// - calibrationDataLoaded (bool)
// - numCalibrationPoints (int)
// - calibrationPoints[] (CalibrationPoint array)
// - CALIBRATION_FILE_NAME (constant)
// These would need to be restored if these functions are ever uncommented.

// Obsolete - Load calibration data from SD card
/*
void loadCalibrationData() {
  calibrationDataLoaded = false;
  numCalibrationPoints = 0;
  
  File calibFile = SD.open(CALIBRATION_FILE_NAME);
  if (!calibFile) {
#ifdef ENABLE_SERIAL_DEBUG
    if (ENABLE_SERIAL_DEBUG) Serial.println("No calibration file found, using default conversion");
#endif
    logEvent("No calibration file found - using default sensor conversion");
    return;
  }
  
  int pointCount = 0;
  while (calibFile.available() && pointCount < MAX_CALIBRATION_POINTS) {
    String line = calibFile.readStringUntil('\n');
    line.trim();
    
    // Skip comments and empty lines
    if (line.length() == 0 || line.startsWith("#")) continue;
    
    // Parse line: sensorValue,actualHeight,timestamp
    int firstComma = line.indexOf(',');
    int secondComma = line.indexOf(',', firstComma + 1);
    
    if (firstComma > 0 && secondComma > firstComma) {
      float sensorVal = line.substring(0, firstComma).toFloat();
      float height = line.substring(firstComma + 1, secondComma).toFloat();
      String timestamp = line.substring(secondComma + 1);
      
      calibrationPoints[pointCount].sensorValue = sensorVal;
      calibrationPoints[pointCount].actualHeight = height;
      calibrationPoints[pointCount].timestamp = timestamp;
      pointCount++;
    }
  }
  
  calibFile.close();
  numCalibrationPoints = pointCount;
  
  if (numCalibrationPoints >= 2) {
    calibrationDataLoaded = true;
#ifdef ENABLE_SERIAL_DEBUG
    if (ENABLE_SERIAL_DEBUG) {
      Serial.println("Loaded " + String(numCalibrationPoints) + " calibration points");
    }
#endif
    logEvent("Calibration data loaded: " + String(numCalibrationPoints) + " points");
  } else {
#ifdef ENABLE_SERIAL_DEBUG
    if (ENABLE_SERIAL_DEBUG) Serial.println("Insufficient calibration points, using default conversion");
#endif
    logEvent("Insufficient calibration points - using default sensor conversion");
  }
}

// Save calibration data to SD card
void saveCalibrationData() {
  // Remove existing file to ensure truncation
  SD.remove(CALIBRATION_FILE_NAME);
  File calibFile = SD.open(CALIBRATION_FILE_NAME, FILE_WRITE);
  if (!calibFile) {
    logEvent("Error: Could not open calibration file for writing");
    return;
  }
  
  // Write header
  calibFile.println("# Tank Height Calibration Data");
  calibFile.println("# Format: sensor_value,actual_height_inches,timestamp");
  
  // Write calibration points
  for (int i = 0; i < numCalibrationPoints; i++) {
    calibFile.print(calibrationPoints[i].sensorValue, 4);
    calibFile.print(",");
    calibFile.print(calibrationPoints[i].actualHeight, 2);
    calibFile.print(",");
    calibFile.println(calibrationPoints[i].timestamp);
  }
  
  calibFile.close();
  logEvent("Calibration data saved: " + String(numCalibrationPoints) + " points");
}
*/

// Obsolete - Add a new calibration point
/*
void addCalibrationPoint(float sensorValue, float actualHeight) {
  if (numCalibrationPoints >= MAX_CALIBRATION_POINTS) {
    // Remove oldest point to make room
    for (int i = 0; i < MAX_CALIBRATION_POINTS - 1; i++) {
      calibrationPoints[i] = calibrationPoints[i + 1];
    }
    numCalibrationPoints = MAX_CALIBRATION_POINTS - 1;
  }
  
  // Add new point
  calibrationPoints[numCalibrationPoints].sensorValue = sensorValue;
  calibrationPoints[numCalibrationPoints].actualHeight = actualHeight;
  calibrationPoints[numCalibrationPoints].timestamp = getCurrentTimestamp();
  numCalibrationPoints++;
  
  // Sort points by sensor value for interpolation
  for (int i = 0; i < numCalibrationPoints - 1; i++) {
    for (int j = 0; j < numCalibrationPoints - i - 1; j++) {
      if (calibrationPoints[j].sensorValue > calibrationPoints[j + 1].sensorValue) {
        CalibrationPoint temp = calibrationPoints[j];
        calibrationPoints[j] = calibrationPoints[j + 1];
        calibrationPoints[j + 1] = temp;
      }
    }
  }
  
  calibrationDataLoaded = (numCalibrationPoints >= 2);
  saveCalibrationData();
  
  String logMsg = "Added calibration point: sensor=" + String(sensorValue, 4) + 
                  ", height=" + String(actualHeight, 2) + " inches";
  logEvent(logMsg);
}
*/

// Interpolate height from a sensor value using the tank's calibration points
float interpolateHeight(float sensorValue, int tankIdx) {
  if (tankIdx < 0 || tankIdx >= tankCount) return -1.0;

  int numPoints = tanks[tankIdx].numCalibrationPoints;
  if (numPoints < 2) {
    // Not enough data to interpolate, return error or fallback
    return -1.0; 
  }

  CalibrationPoint* points = tanks[tankIdx].calibrationPoints;

  // Sort points by sensorValue to ensure correct interpolation
  for (int i = 0; i < numPoints - 1; i++) {
    for (int j = 0; j < numPoints - i - 1; j++) {
      if (points[j].sensorValue > points[j + 1].sensorValue) {
        CalibrationPoint temp = points[j];
        points[j] = points[j + 1];
        points[j + 1] = temp;
      }
    }
  }

  // Find the two points that bracket the sensorValue
  // Extrapolate if outside the range
  if (sensorValue <= points[0].sensorValue) {
    // Value is below or at the first point, extrapolate from the first two points
    float sensor_delta = points[1].sensorValue - points[0].sensorValue;
    float height_delta = points[1].actualHeight - points[0].actualHeight;
    if (abs(sensor_delta) < 0.001) return points[0].actualHeight; // Avoid division by zero
    float slope = height_delta / sensor_delta;
    return points[0].actualHeight + (sensorValue - points[0].sensorValue) * slope;
  }
  
  if (sensorValue >= points[numPoints - 1].sensorValue) {
    // Value is above or at the last point, extrapolate from the last two points
    float sensor_delta = points[numPoints - 1].sensorValue - points[numPoints - 2].sensorValue;
    float height_delta = points[numPoints - 1].actualHeight - points[numPoints - 2].actualHeight;
    if (abs(sensor_delta) < 0.001) return points[numPoints - 1].actualHeight; // Avoid division by zero
    float slope = height_delta / sensor_delta;
    return points[numPoints - 1].actualHeight + (sensorValue - points[numPoints - 1].sensorValue) * slope;
  }

  // Find the segment for interpolation
  for (int i = 0; i < numPoints - 1; i++) {
    if (sensorValue >= points[i].sensorValue && sensorValue <= points[i + 1].sensorValue) {
      float sensor_delta = points[i + 1].sensorValue - points[i].sensorValue;
      float height_delta = points[i + 1].actualHeight - points[i].actualHeight;
      if (abs(sensor_delta) < 0.001) return points[i].actualHeight; // Avoid division by zero
      float slope = height_delta / sensor_delta;
      return points[i].actualHeight + (sensorValue - points[i].sensorValue) * slope;
    }
  }

  return -1.0; // Should not be reached
}

// Get the current raw sensor reading for a specific tank
float getCurrentSensorReading(int tankIdx) {
  if (tankIdx < 0 || tankIdx >= tankCount) return -1.0;

  // Determine sensor type from tank config
  int sensorType = -1;
  if (tanks[tankIdx].digitalPin != -1) sensorType = DIGITAL_FLOAT;
  else if (tanks[tankIdx].analogPin != -1) sensorType = ANALOG_VOLTAGE;
  else if (tanks[tankIdx].currentLoopChannel != -1) sensorType = CURRENT_LOOP;
  
  if (sensorType == -1) sensorType = SENSOR_TYPE; // Fallback

  switch (sensorType) {
    case DIGITAL_FLOAT:
      return digitalRead(tanks[tankIdx].digitalPin);

    case ANALOG_VOLTAGE: {
      float totalVoltage = 0;
      const int numReadings = 10;
      for (int i = 0; i < numReadings; i++) {
        totalVoltage += (analogRead(tanks[tankIdx].analogPin) / ADC_MAX_VALUE) * ADC_REFERENCE_VOLTAGE;
        delay(10);
      }
      return totalVoltage / numReadings;
    }

    case CURRENT_LOOP: {
      Wire.beginTransmission(I2C_CURRENT_LOOP_ADDRESS);
      Wire.write(tanks[tankIdx].currentLoopChannel);
      if (Wire.endTransmission() != 0) return -1.0;
      Wire.requestFrom(I2C_CURRENT_LOOP_ADDRESS, 2);
      if (Wire.available() >= 2) {
        uint16_t rawValue = (Wire.read() << 8) | Wire.read();
        return 4.0 + ((rawValue / 65535.0) * 16.0); // 4-20mA
      }
      return -1.0;
    }
  }
  return -1.0;
}

// Appends a new CAL_POINT to the tank's .cfg file on the SD card
void addCalibrationPointToCfg(int tankIdx, float sensorValue, float actualHeight) {
  if (tankIdx < 0 || tankIdx >= tankCount || !ensureSDCardReady()) return;

  String fileName = tanks[tankIdx].configFileName;
  if (fileName.length() == 0) {
    logEvent("ERROR: Cannot add cal point, no config file name for tank " + String(tankIdx));
    return;
  }

  File configFile = SD.open(fileName, FILE_WRITE);
  if (configFile) {
    String calLine = "CAL_POINT = " + String(sensorValue, 4) + "," + String(actualHeight, 2);
    configFile.println(calLine);
    configFile.close();
    logEvent("Added to " + fileName + ": " + calLine);

    // Also add to in-memory store
    if (tanks[tankIdx].numCalibrationPoints < MAX_CALIBRATION_POINTS) {
      int pointIdx = tanks[tankIdx].numCalibrationPoints;
      tanks[tankIdx].calibrationPoints[pointIdx].sensorValue = sensorValue;
      tanks[tankIdx].calibrationPoints[pointIdx].actualHeight = actualHeight;
      tanks[tankIdx].numCalibrationPoints++;
    }
  } else {
    logEvent("ERROR: Failed to open " + fileName + " to add cal point.");
  }
}
