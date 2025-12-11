/*
  Tank Alarm Client 112025 - Arduino Opta + Blues Notecard
  Version: 1.0.0

  Hardware:
  - Arduino Opta Lite (STM32H747XI dual-core)
  - Arduino Pro Opta Ext A0602 (analog and current-loop expansion)
  - Blues Wireless Notecard for Opta adapter (cellular + GPS)

  Features:
  - Multi-tank level monitoring with per-tank alarm thresholds
  - Blues Notecard telemetry for server ingestion
  - SMS alarm escalation via server
  - Daily report schedule aligned with server
  - Configuration persisted to internal flash (LittleFS)
  - Remote configuration updates pushed from server via Notecard
  - Compile output includes hardware requirement summary based on configuration

  Created: November 2025
  Using GitHub Copilot for code generation
*/

#include <Arduino.h>
#include <Wire.h>
#include <ArduinoJson.h>
#include <Notecard.h>
#include <math.h>
#include <string.h>

// Firmware version for production tracking
#define FIRMWARE_VERSION "1.0.0"
#define FIRMWARE_BUILD_DATE __DATE__

// Filesystem and Watchdog support
// Note: Arduino Opta uses Mbed OS, which has different APIs than STM32duino
#if defined(ARDUINO_ARCH_STM32) && !defined(ARDUINO_ARCH_MBED)
  // STM32duino platform (non-Mbed)
  #include <LittleFS.h>
  #include <IWatchdog.h>
  #define FILESYSTEM_AVAILABLE
  #define WATCHDOG_AVAILABLE
  #define WATCHDOG_TIMEOUT_SECONDS 30
#elif defined(ARDUINO_OPTA) || defined(ARDUINO_ARCH_MBED)
  // Arduino Opta with Mbed OS - use Mbed OS APIs
  #include <LittleFileSystem.h>
  #include <BlockDevice.h>
  #include <mbed.h>
  using namespace mbed;
  using namespace std::chrono_literals;  // For chrono duration literals (e.g., 100ms)
  #define FILESYSTEM_AVAILABLE
  #define WATCHDOG_AVAILABLE
  #define WATCHDOG_TIMEOUT_SECONDS 30
  
  // Mbed OS filesystem instance
  static LittleFileSystem *mbedFS = nullptr;
  static BlockDevice *mbedBD = nullptr;
  static Watchdog &mbedWatchdog = Watchdog::get_instance();
#endif

// Debug mode - controls Serial output and Notecard debug logging
// For PRODUCTION: Leave commented out (default) to save 5-10% power consumption
// For DEVELOPMENT: Uncomment the line below for troubleshooting and monitoring
//#define DEBUG_MODE

// Debug output macros - no-op when DEBUG_MODE is disabled
#ifdef DEBUG_MODE
  #define DEBUG_BEGIN(baud) Serial.begin(baud)
  #define DEBUG_PRINT(x) Serial.print(x)
  #define DEBUG_PRINTLN(x) Serial.println(x)
  #define DEBUG_PRINTF(x, y) Serial.print(x, y)
#else
  #define DEBUG_BEGIN(baud)
  #define DEBUG_PRINT(x)
  #define DEBUG_PRINTLN(x)
  #define DEBUG_PRINTF(x, y)
#endif

// strlcpy is provided by Notecard library on Mbed platforms
#if !defined(ARDUINO_ARCH_MBED) && !defined(strlcpy)
static size_t strlcpy(char *dst, const char *src, size_t size) {
  if (!dst || !src || size == 0) {
    return 0;
  }
  size_t len = strlen(src);
  size_t copyLen = (len >= size) ? (size - 1) : len;
  memcpy(dst, src, copyLen);
  dst[copyLen] = '\0';
  return len;
}
#endif

#ifndef PRODUCT_UID
#define PRODUCT_UID "com.senax.tankalarm112025"
#endif

// Power saving configuration for solar-powered installations
#ifndef SOLAR_OUTBOUND_INTERVAL_MINUTES
#define SOLAR_OUTBOUND_INTERVAL_MINUTES 360  // Sync every 6 hours for solar installations
#endif

#ifndef SOLAR_INBOUND_INTERVAL_MINUTES
#define SOLAR_INBOUND_INTERVAL_MINUTES 60    // Check for inbound every hour for solar installations
#endif

#ifndef CLIENT_CONFIG_PATH
#define CLIENT_CONFIG_PATH "/client_config.json"
#endif

#ifndef TELEMETRY_FILE
#define TELEMETRY_FILE "telemetry.qi"
#endif

#ifndef ALARM_FILE
#define ALARM_FILE "alarm.qi"
#endif

#ifndef DAILY_FILE
#define DAILY_FILE "daily.qi"
#endif

#ifndef CONFIG_INBOX_FILE
#define CONFIG_INBOX_FILE "config.qi"
#endif

#ifndef CONFIG_OUTBOX_FILE
#define CONFIG_OUTBOX_FILE "config.qo"
#endif

#ifndef RELAY_CONTROL_FILE
#define RELAY_CONTROL_FILE "relay.qi"
#endif

#ifndef SERIAL_LOG_FILE
#define SERIAL_LOG_FILE "serial_log.qi"  // Send serial logs to server
#endif

#ifndef SERIAL_REQUEST_FILE
#define SERIAL_REQUEST_FILE "serial_request.qi"  // Receive serial log requests
#endif

#ifndef CLIENT_SERIAL_BUFFER_SIZE
#define CLIENT_SERIAL_BUFFER_SIZE 50  // Buffer up to 50 log messages
#endif

#ifndef NOTE_BUFFER_PATH
#define NOTE_BUFFER_PATH "/pending_notes.log"
#endif

#ifndef NOTE_BUFFER_TEMP_PATH
#define NOTE_BUFFER_TEMP_PATH "/pending_notes.tmp"
#endif

#ifndef NOTE_BUFFER_MAX_BYTES
#define NOTE_BUFFER_MAX_BYTES 16384
#endif

#ifndef NOTE_BUFFER_MIN_HEADROOM
#define NOTE_BUFFER_MIN_HEADROOM 2048
#endif

#ifndef MAX_TANKS
#define MAX_TANKS 8
#endif

#ifndef DEFAULT_SAMPLE_SECONDS
#define DEFAULT_SAMPLE_SECONDS 1800
#endif

#ifndef DEFAULT_LEVEL_CHANGE_THRESHOLD_INCHES
#define DEFAULT_LEVEL_CHANGE_THRESHOLD_INCHES 0.0f
#endif

#ifndef DEFAULT_REPORT_HOUR
#define DEFAULT_REPORT_HOUR 5
#endif

#ifndef DEFAULT_REPORT_MINUTE
#define DEFAULT_REPORT_MINUTE 0
#endif

#ifndef CURRENT_LOOP_I2C_ADDRESS
#define CURRENT_LOOP_I2C_ADDRESS 0x64
#endif

#ifndef ALARM_DEBOUNCE_COUNT
#define ALARM_DEBOUNCE_COUNT 3  // Require 3 consecutive samples to trigger/clear alarm
#endif

#ifndef SENSOR_STUCK_THRESHOLD
#define SENSOR_STUCK_THRESHOLD 10  // Same reading 10 times = stuck sensor
#endif

#ifndef SENSOR_FAILURE_THRESHOLD
#define SENSOR_FAILURE_THRESHOLD 5  // 5 consecutive read failures = sensor failed
#endif

#ifndef MAX_ALARMS_PER_HOUR
#define MAX_ALARMS_PER_HOUR 10  // Maximum alarms per tank per hour
#endif

#ifndef MIN_ALARM_INTERVAL_SECONDS
#define MIN_ALARM_INTERVAL_SECONDS 300  // Minimum 5 minutes between same alarm type
#endif

// Default momentary relay duration (30 minutes in seconds)
#ifndef DEFAULT_RELAY_MOMENTARY_SECONDS
#define DEFAULT_RELAY_MOMENTARY_SECONDS 1800  // 30 minutes
#endif

// Digital sensor (float switch) constants
#ifndef DIGITAL_SWITCH_THRESHOLD
#define DIGITAL_SWITCH_THRESHOLD 0.5f  // Threshold to determine activated vs not-activated state
#endif

#ifndef DIGITAL_SENSOR_ACTIVATED_VALUE
#define DIGITAL_SENSOR_ACTIVATED_VALUE 1.0f  // Value returned when switch is activated
#endif

#ifndef DIGITAL_SENSOR_NOT_ACTIVATED_VALUE
#define DIGITAL_SENSOR_NOT_ACTIVATED_VALUE 0.0f  // Value returned when switch is not activated
#endif

// Unit conversion constants for 4-20mA sensors
#ifndef METERS_TO_INCHES
#define METERS_TO_INCHES 39.3701f           // 1 meter = 39.3701 inches
#endif

#ifndef CENTIMETERS_TO_INCHES
#define CENTIMETERS_TO_INCHES 0.393701f     // 1 centimeter = 0.393701 inches
#endif

#ifndef FEET_TO_INCHES
#define FEET_TO_INCHES 12.0f                // 1 foot = 12 inches
#endif

// Pressure-to-height conversion factors (for water at standard conditions)
#ifndef PSI_TO_INCHES_WATER
#define PSI_TO_INCHES_WATER 27.68f          // 1 PSI = 27.68 inches of water column
#endif

#ifndef BAR_TO_INCHES_WATER
#define BAR_TO_INCHES_WATER 401.5f          // 1 bar = 401.5 inches of water
#endif

#ifndef KPA_TO_INCHES_WATER
#define KPA_TO_INCHES_WATER 4.015f          // 1 kPa = 4.015 inches of water
#endif

#ifndef MBAR_TO_INCHES_WATER
#define MBAR_TO_INCHES_WATER 0.4015f        // 1 mbar = 0.4015 inches of water
#endif

// Helper function: Get pressure-to-inches conversion factor based on unit
static float getPressureConversionFactor(const char* unit) {
  if (strcmp(unit, "bar") == 0) return BAR_TO_INCHES_WATER;
  if (strcmp(unit, "kPa") == 0) return KPA_TO_INCHES_WATER;
  if (strcmp(unit, "mbar") == 0) return MBAR_TO_INCHES_WATER;
  if (strcmp(unit, "inH2O") == 0) return 1.0f;
  return PSI_TO_INCHES_WATER; // Default: PSI
}

// Helper function: Get distance-to-inches conversion factor based on unit
static float getDistanceConversionFactor(const char* unit) {
  if (strcmp(unit, "m") == 0) return METERS_TO_INCHES;
  if (strcmp(unit, "cm") == 0) return CENTIMETERS_TO_INCHES;
  if (strcmp(unit, "ft") == 0) return FEET_TO_INCHES;
  return 1.0f; // Default: assume inches
}

static const uint8_t NOTECARD_I2C_ADDRESS = 0x17;
static const uint32_t NOTECARD_I2C_FREQUENCY = 400000UL;

enum SensorType : uint8_t {
  SENSOR_DIGITAL = 0,
  SENSOR_ANALOG = 1,
  SENSOR_CURRENT_LOOP = 2,
  SENSOR_HALL_EFFECT_RPM = 3
};

// 4-20mA current loop sensor subtypes
enum CurrentLoopSensorType : uint8_t {
  CURRENT_LOOP_PRESSURE = 0,    // Pressure sensor mounted near bottom of tank (e.g., Dwyer 626-06-CB-P1-E5-S1)
                                // 4mA = empty (0 PSI), 20mA = full (max PSI)
  CURRENT_LOOP_ULTRASONIC = 1   // Ultrasonic sensor mounted on top of tank (e.g., Siemens Sitrans LU240)
                                // 4mA = full (sensor close to liquid), 20mA = empty (sensor far from liquid)
};

// Hall effect sensor types for RPM measurement
enum HallEffectSensorType : uint8_t {
  HALL_EFFECT_UNIPOLAR = 0,     // Triggered by single pole (usually South), reset when field removed
  HALL_EFFECT_BIPOLAR = 1,      // Latching: South pole turns ON, North pole turns OFF
  HALL_EFFECT_OMNIPOLAR = 2,    // Responds to either North or South pole
  HALL_EFFECT_ANALOG = 3        // Linear/analog: outputs voltage proportional to magnetic field strength
};

// Hall effect detection method
enum HallEffectDetectionMethod : uint8_t {
  HALL_DETECT_PULSE = 0,        // Count pulses (transitions) - traditional method
  HALL_DETECT_TIME_BASED = 1    // Measure time between pulses - more flexible for different magnet types
};

// Relay trigger conditions - which alarm type triggers the relay
enum RelayTrigger : uint8_t {
  RELAY_TRIGGER_ANY = 0,   // Trigger on any alarm (high or low)
  RELAY_TRIGGER_HIGH = 1,  // Trigger only on high alarm
  RELAY_TRIGGER_LOW = 2    // Trigger only on low alarm
};

// Relay engagement mode - how long the relay stays on
enum RelayMode : uint8_t {
  RELAY_MODE_MOMENTARY = 0,     // Momentary on for configurable duration, then auto-off
  RELAY_MODE_UNTIL_CLEAR = 1,   // Stay on until alarm clears
  RELAY_MODE_MANUAL_RESET = 2   // Stay on until manually reset from server
};

// Default relay engagement duration (30 minutes in seconds)
#define RELAY_DEFAULT_MOMENTARY_SECONDS 1800

struct TankConfig {
  char id;                 // Friendly identifier (A, B, C ...)
  char name[24];           // Site/tank label shown in reports
  uint8_t tankNumber;      // Numeric tank reference for legacy formatting
  SensorType sensorType;   // Digital, analog, current loop, or Hall effect RPM
  int16_t primaryPin;      // Digital pin or analog channel
  int16_t secondaryPin;    // Optional secondary pin (unused by default)
  int16_t currentLoopChannel; // 4-20mA channel index (-1 if unused)
  int16_t rpmPin;          // Hall effect RPM sensor pin (-1 if unused)
  uint8_t pulsesPerRevolution; // For RPM sensors: pulses per revolution (default 1)
  HallEffectSensorType hallEffectType; // Type of hall effect sensor (unipolar, bipolar, omnipolar, analog)
  HallEffectDetectionMethod hallEffectDetection; // Detection method (pulse counting or time-based)
  float highAlarmThreshold;   // High threshold for triggering alarm (inches or RPM)
  float lowAlarmThreshold;    // Low threshold for triggering alarm (inches or RPM)
  float hysteresisValue;   // Hysteresis band (default 2.0)
  bool enableDailyReport;  // Include in daily summary
  bool enableAlarmSms;     // Escalate SMS when alarms trigger
  bool enableServerUpload; // Send telemetry to server
  char relayTargetClient[48]; // Client UID to trigger relays on (empty = none)
  uint8_t relayMask;       // Bitmask of relays to trigger (bit 0=relay 1, etc.)
  RelayTrigger relayTrigger; // Which alarm type triggers the relay (any, high, low)
  RelayMode relayMode;     // How long relay stays on (momentary, until_clear, manual_reset)
  uint16_t relayMomentarySeconds[4]; // Per-relay momentary duration in seconds (0 = use default 30 min)
  // Digital sensor (float switch) specific settings
  char digitalTrigger[16]; // 'activated' or 'not_activated' - when to trigger alarm for digital sensors
  char digitalSwitchMode[4]; // 'NO' for normally-open, 'NC' for normally-closed (default: NO)
  // 4-20mA current loop sensor settings
  CurrentLoopSensorType currentLoopType; // Pressure (bottom-mounted) or Ultrasonic (top-mounted)
  float sensorMountHeight; // For ultrasonic: distance from sensor to tank bottom (inches)
                           // For pressure: height of sensor above tank bottom (inches, usually 0-2)
  float sensorRangeMin;    // Minimum native sensor range (e.g., 0 for 0-5 PSI or 0-10m)
  float sensorRangeMax;    // Maximum native sensor range (e.g., 5 for 0-5 PSI, 10 for 0-10m)
  char sensorRangeUnit[8]; // Unit for sensor range: "PSI", "bar", "m", "ft", "in", etc.
  // Analog voltage sensor settings (for sensors like Dwyer 626 with voltage output)
  float analogVoltageMin;  // Minimum voltage output (e.g., 0.0 for 0-10V, 1.0 for 1-5V)
  float analogVoltageMax;  // Maximum voltage output (e.g., 10.0 for 0-10V, 5.0 for 1-5V)
};

struct ClientConfig {
  char siteName[32];
  char deviceLabel[24];
  char serverFleet[32]; // Target fleet name for server (e.g., "tankalarm-server")
  char dailyEmail[64];
  uint16_t sampleSeconds;
  float minLevelChangeInches;
  uint8_t reportHour;
  uint8_t reportMinute;
  uint8_t tankCount;
  TankConfig tanks[MAX_TANKS];
  // Optional clear button configuration
  int8_t clearButtonPin;        // Pin for physical clear button (-1 = disabled)
  bool clearButtonActiveHigh;   // true = button active when HIGH, false = active when LOW (with pullup)
  // Power saving configuration
  bool solarPowered;            // true = solar powered (use power saving features), false = grid-tied
};

struct TankRuntime {
  float currentInches;
  float currentSensorMa;        // Raw sensor reading in milliamps (for 4-20mA sensors)
  float lastReportedInches;
  float lastDailySentInches;
  bool highAlarmLatched;
  bool lowAlarmLatched;
  unsigned long lastSampleMillis;
  unsigned long lastAlarmSendMillis;
  // Debouncing state
  uint8_t highAlarmDebounceCount;
  uint8_t lowAlarmDebounceCount;
  uint8_t clearDebounceCount;
  // Sensor failure detection
  float lastValidReading;
  bool hasLastValidReading;
  uint8_t consecutiveFailures;
  uint8_t stuckReadingCount;
  bool sensorFailed;
  // Rate limiting
  unsigned long alarmTimestamps[MAX_ALARMS_PER_HOUR];
  uint8_t alarmCount;
  unsigned long lastHighAlarmMillis;
  unsigned long lastLowAlarmMillis;
  unsigned long lastClearAlarmMillis;
  unsigned long lastSensorFaultMillis;
};

static ClientConfig gConfig;
static TankRuntime gTankState[MAX_TANKS];

static Notecard notecard;
static char gDeviceUID[48] = {0};
static unsigned long gLastTelemetryMillis = 0;
static unsigned long gLastConfigCheckMillis = 0;
static unsigned long gLastTimeSyncMillis = 0;
static double gLastSyncedEpoch = 0.0;
static double gNextDailyReportEpoch = 0.0;

static bool gConfigDirty = false;
static bool gHardwareSummaryPrinted = false;

// Network failure handling
static unsigned long gLastSuccessfulNotecardComm = 0;
static uint8_t gNotecardFailureCount = 0;
static bool gNotecardAvailable = true;
#define NOTECARD_FAILURE_THRESHOLD 5
#define NOTECARD_RETRY_INTERVAL 60000UL  // Retry after 60 seconds

static const size_t DAILY_NOTE_PAYLOAD_LIMIT = 960U;

// Relay control state
#define MAX_RELAYS 4
static bool gRelayState[MAX_RELAYS] = {false, false, false, false};
static unsigned long gLastRelayCheckMillis = 0;

// Per-tank relay activation tracking for momentary mode timeout
static unsigned long gRelayActivationTime[MAX_TANKS] = {0};
static bool gRelayActiveForTank[MAX_TANKS] = {false};

// Clear button state for debouncing
static unsigned long gClearButtonLastPressTime = 0;
static bool gClearButtonLastState = false;
static bool gClearButtonInitialized = false;
#define CLEAR_BUTTON_DEBOUNCE_MS 50
#define CLEAR_BUTTON_MIN_PRESS_MS 500  // Require 500ms press to clear (prevent accidental triggers)

// RPM sensor state for Hall effect pulse counting
// We track pulses per tank that uses an RPM sensor
static unsigned long gRpmLastSampleMillis[MAX_TANKS] = {0};
static float gRpmLastReading[MAX_TANKS] = {0.0f};
static int gRpmLastPinState[MAX_TANKS];  // Initialized dynamically in setup()
// For time-based detection: track time between pulses
static unsigned long gRpmLastPulseTime[MAX_TANKS] = {0};
static unsigned long gRpmPulsePeriodMs[MAX_TANKS] = {0};

// RPM sampling duration in milliseconds (sample for a few seconds each period)
#ifndef RPM_SAMPLE_DURATION_MS
#define RPM_SAMPLE_DURATION_MS 3000
#endif

// Serial log buffer structure for client
struct SerialLogEntry {
  double timestamp;
  char message[160];
};

struct ClientSerialLog {
  SerialLogEntry entries[CLIENT_SERIAL_BUFFER_SIZE];
  uint8_t writeIndex;
  uint8_t count;
};

static ClientSerialLog gSerialLog;
static unsigned long gLastSerialRequestCheckMillis = 0;

// Forward declarations
static void initializeStorage();
static void ensureConfigLoaded();
static void createDefaultConfig(ClientConfig &cfg);
static bool loadConfigFromFlash(ClientConfig &cfg);
static bool saveConfigToFlash(const ClientConfig &cfg);
static void printHardwareRequirements(const ClientConfig &cfg);
static void initializeNotecard();
static void configureNotecardHubMode();
static void syncTimeFromNotecard();
static double currentEpoch();
static void scheduleNextDailyReport();
static void pollForConfigUpdates();
static void applyConfigUpdate(const JsonDocument &doc);
static void persistConfigIfDirty();
static void sampleTanks();
static float readTankSensor(uint8_t idx);
static void evaluateAlarms(uint8_t idx);
static void sendTelemetry(uint8_t idx, const char *reason, bool syncNow);
static void sendAlarm(uint8_t idx, const char *alarmType, float inches);
static void sendDailyReport();
static void publishNote(const char *fileName, const JsonDocument &doc, bool syncNow);
static void bufferNoteForRetry(const char *fileName, const char *payload, bool syncNow);
static void flushBufferedNotes();
static void pruneNoteBufferIfNeeded();
static void ensureTimeSync();
static void updateDailyScheduleIfNeeded();
static bool checkNotecardHealth();
static bool appendDailyTank(DynamicJsonDocument &doc, JsonArray &array, uint8_t tankIndex, size_t payloadLimit);
static void pollForRelayCommands();
static void processRelayCommand(const JsonDocument &doc);
static void setRelayState(uint8_t relayNum, bool state);
static void initializeRelays();
static void triggerRemoteRelays(const char *targetClient, uint8_t relayMask, bool activate);
static int getRelayPin(uint8_t relayIndex);
static float readNotecardVinVoltage();
static void checkRelayMomentaryTimeout(unsigned long now);
static void resetRelayForTank(uint8_t idx);
static void initializeClearButton();
static void checkClearButton(unsigned long now);
static void clearAllRelayAlarms();
static void addSerialLog(const char *message);
static void pollForSerialRequests();
static void sendSerialLogs();

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 2000) {
    delay(10);
  }

  Serial.println();
  Serial.print(F("Tank Alarm Client 112025 v"));
  Serial.print(F(FIRMWARE_VERSION));
  Serial.print(F(" ("));
  Serial.print(F(FIRMWARE_BUILD_DATE));
  Serial.println(F(")"));

  // Initialize serial log buffer
  memset(&gSerialLog, 0, sizeof(ClientSerialLog));

  // Set analog resolution to 12-bit to match the /4095.0f divisor used in readTankSensor
  analogReadResolution(12);

  initializeStorage();
  ensureConfigLoaded();
  printHardwareRequirements(gConfig);

  Wire.begin();
  Wire.setClock(NOTECARD_I2C_FREQUENCY);

  initializeNotecard();
  ensureTimeSync();
  scheduleNextDailyReport();

#ifdef WATCHDOG_AVAILABLE
  // Initialize watchdog timer
  #if defined(ARDUINO_OPTA) || defined(ARDUINO_ARCH_MBED)
    // Mbed OS Watchdog
    uint32_t timeoutMs = WATCHDOG_TIMEOUT_SECONDS * 1000;
    if (mbedWatchdog.start(timeoutMs)) {
      Serial.print(F("Mbed Watchdog enabled: "));
      Serial.print(WATCHDOG_TIMEOUT_SECONDS);
      Serial.println(F(" seconds"));
    } else {
      Serial.println(F("Warning: Watchdog initialization failed"));
    }
  #else
    // STM32duino Watchdog
    IWatchdog.begin(WATCHDOG_TIMEOUT_SECONDS * 1000000UL);
    Serial.print(F("Watchdog timer enabled: "));
    Serial.print(WATCHDOG_TIMEOUT_SECONDS);
    Serial.println(F(" seconds"));
  #endif
#else
  Serial.println(F("Warning: Watchdog timer not available on this platform"));
#endif

  // Initialize RPM sensor state arrays dynamically
  for (uint8_t i = 0; i < MAX_TANKS; ++i) {
    gRpmLastPinState[i] = HIGH;
    gRpmLastSampleMillis[i] = 0;
    gRpmLastReading[i] = 0.0f;
    gRpmLastPulseTime[i] = 0;
    gRpmPulsePeriodMs[i] = 0;
  }

  // Explicitly initialize relay state tracking arrays for clarity and consistency
  for (uint8_t i = 0; i < MAX_TANKS; ++i) {
    gRelayActivationTime[i] = 0;
    gRelayActiveForTank[i] = false;
  }

  for (uint8_t i = 0; i < gConfig.tankCount; ++i) {
    gTankState[i].currentInches = 0.0f;
    gTankState[i].lastReportedInches = -9999.0f;
    gTankState[i].lastDailySentInches = -9999.0f;
    gTankState[i].highAlarmLatched = false;
    gTankState[i].lowAlarmLatched = false;
    gTankState[i].lastSampleMillis = 0;
    gTankState[i].lastAlarmSendMillis = 0;
    gTankState[i].highAlarmDebounceCount = 0;
    gTankState[i].lowAlarmDebounceCount = 0;
    gTankState[i].clearDebounceCount = 0;
    gTankState[i].lastValidReading = 0.0f;
    gTankState[i].hasLastValidReading = false;
    gTankState[i].consecutiveFailures = 0;
    gTankState[i].stuckReadingCount = 0;
    gTankState[i].sensorFailed = false;
    gTankState[i].alarmCount = 0;
    gTankState[i].lastHighAlarmMillis = 0;
    gTankState[i].lastLowAlarmMillis = 0;
    gTankState[i].lastClearAlarmMillis = 0;
    gTankState[i].lastSensorFaultMillis = 0;
    for (uint8_t j = 0; j < MAX_ALARMS_PER_HOUR; ++j) {
      gTankState[i].alarmTimestamps[j] = 0;
    }
  }

  initializeRelays();
  initializeClearButton();

  Serial.println(F("Client setup complete"));
  addSerialLog("Client started successfully");
}

void loop() {
  unsigned long now = millis();

#ifdef WATCHDOG_AVAILABLE
  // Reset watchdog timer to prevent system reset
  #if defined(ARDUINO_OPTA) || defined(ARDUINO_ARCH_MBED)
    mbedWatchdog.kick();
  #else
    IWatchdog.reload();
  #endif
#endif

  // Check notecard health periodically
  static unsigned long lastHealthCheck = 0;
  if (now - lastHealthCheck > 300000UL) {  // Check every 5 minutes
    lastHealthCheck = now;
    if (!gNotecardAvailable) {
      checkNotecardHealth();
    }
  }

  if (now - gLastTelemetryMillis >= (unsigned long)gConfig.sampleSeconds * 1000UL) {
    gLastTelemetryMillis = now;
    sampleTanks();
  }

  if (now - gLastConfigCheckMillis >= 600000UL) {  // Check every 10 minutes
    gLastConfigCheckMillis = now;
    pollForConfigUpdates();
  }

  if (now - gLastRelayCheckMillis >= 600000UL) {  // Check every 10 minutes
    gLastRelayCheckMillis = now;
    pollForRelayCommands();
  }

  if (now - gLastSerialRequestCheckMillis >= 600000UL) {  // Check every 10 minutes
    gLastSerialRequestCheckMillis = now;
    pollForSerialRequests();
  }

  // Check for momentary relay timeout (30 minutes)
  checkRelayMomentaryTimeout(now);
  
  // Check for physical clear button press
  checkClearButton(now);

  persistConfigIfDirty();
  ensureTimeSync();
  updateDailyScheduleIfNeeded();

  if (gNextDailyReportEpoch > 0.0 && currentEpoch() >= gNextDailyReportEpoch) {
    sendDailyReport();
    scheduleNextDailyReport();
  }

  // Sleep to reduce power consumption between loop iterations
  // Use Mbed OS thread sleep for power efficiency - allows CPU to enter low-power states during sleep periods
  #if defined(ARDUINO_OPTA) || defined(ARDUINO_ARCH_MBED)
    ThisThread::sleep_for(100ms);  // Thread sleep for 100ms - enables power saving
  #else
    delay(100);  // Fallback for non-Mbed platforms
  #endif
}

static void initializeStorage() {
#ifdef FILESYSTEM_AVAILABLE
  #if defined(ARDUINO_OPTA) || defined(ARDUINO_ARCH_MBED)
    // Mbed OS LittleFileSystem initialization
    mbedBD = BlockDevice::get_default_instance();
    if (!mbedBD) {
      Serial.println(F("Error: No default block device found"));
      Serial.println(F("Warning: Filesystem not available - configuration will not persist"));
      return;
    }
    
    mbedFS = new LittleFileSystem("fs");
    int err = mbedFS->mount(mbedBD);
    if (err) {
      // Try to reformat if mount fails
      Serial.println(F("Filesystem mount failed, attempting to reformat..."));
      err = mbedFS->reformat(mbedBD);
      if (err) {
        Serial.println(F("LittleFS format failed; halting"));
        delete mbedFS;
        mbedFS = nullptr;
        while (true) {
          delay(1000);
        }
      }
    }
    Serial.println(F("Mbed OS LittleFileSystem initialized"));
  #else
    // STM32duino LittleFS
    if (!LittleFS.begin()) {
      Serial.println(F("LittleFS init failed; halting"));
      while (true) {
        delay(1000);
      }
    }
  #endif
#else
  Serial.println(F("Warning: Filesystem not available on this platform - configuration will not persist"));
#endif
}

static void ensureConfigLoaded() {
#ifdef FILESYSTEM_AVAILABLE
  if (!loadConfigFromFlash(gConfig)) {
    createDefaultConfig(gConfig);
    gConfigDirty = true;
    persistConfigIfDirty();
    Serial.println(F("Default configuration written to flash"));
  }
#else
  // Filesystem not available - create default config in RAM only
  createDefaultConfig(gConfig);
  Serial.println(F("Warning: Using default config (no persistence available)"));
#endif
}

static void createDefaultConfig(ClientConfig &cfg) {
  memset(&cfg, 0, sizeof(ClientConfig));
  strlcpy(cfg.siteName, "Opta Tank Site", sizeof(cfg.siteName));
  strlcpy(cfg.deviceLabel, "Client-112025", sizeof(cfg.deviceLabel));
  strlcpy(cfg.serverFleet, "tankalarm-server", sizeof(cfg.serverFleet));
  strlcpy(cfg.dailyEmail, "reports@example.com", sizeof(cfg.dailyEmail));
  cfg.sampleSeconds = DEFAULT_SAMPLE_SECONDS;
  cfg.minLevelChangeInches = DEFAULT_LEVEL_CHANGE_THRESHOLD_INCHES;
  cfg.reportHour = DEFAULT_REPORT_HOUR;
  cfg.reportMinute = DEFAULT_REPORT_MINUTE;
  cfg.tankCount = 1;

  cfg.tanks[0].id = 'A';
  strlcpy(cfg.tanks[0].name, "Primary Tank", sizeof(cfg.tanks[0].name));
  cfg.tanks[0].tankNumber = 1;
  cfg.tanks[0].sensorType = SENSOR_ANALOG;
  cfg.tanks[0].primaryPin = 0; // A0 on Opta Ext
  cfg.tanks[0].secondaryPin = -1;
  cfg.tanks[0].currentLoopChannel = -1;
  cfg.tanks[0].rpmPin = -1; // No RPM sensor by default
  cfg.tanks[0].pulsesPerRevolution = 1; // Default: 1 pulse per revolution
  cfg.tanks[0].hallEffectType = HALL_EFFECT_UNIPOLAR; // Default: unipolar sensor
  cfg.tanks[0].hallEffectDetection = HALL_DETECT_PULSE; // Default: pulse counting method
  cfg.tanks[0].highAlarmThreshold = 100.0f;
  cfg.tanks[0].lowAlarmThreshold = 20.0f;
  cfg.tanks[0].hysteresisValue = 2.0f; // 2 unit hysteresis band
  cfg.tanks[0].enableDailyReport = true;
  cfg.tanks[0].enableAlarmSms = true;
  cfg.tanks[0].enableServerUpload = true;
  cfg.tanks[0].relayTargetClient[0] = '\0'; // No relay target by default
  cfg.tanks[0].relayMask = 0; // No relays triggered by default
  cfg.tanks[0].relayTrigger = RELAY_TRIGGER_ANY; // Default: trigger on any alarm
  cfg.tanks[0].relayMode = RELAY_MODE_MOMENTARY; // Default: momentary activation
  // Default: all relays use 30 minutes (0 = use default)
  for (uint8_t r = 0; r < 4; ++r) {
    cfg.tanks[0].relayMomentarySeconds[r] = 0;
  }
  cfg.tanks[0].digitalTrigger[0] = '\0'; // Not a digital sensor by default
  strlcpy(cfg.tanks[0].digitalSwitchMode, "NO", sizeof(cfg.tanks[0].digitalSwitchMode)); // Default: normally-open
  cfg.tanks[0].currentLoopType = CURRENT_LOOP_PRESSURE; // Default: pressure sensor (most common)
  cfg.tanks[0].sensorMountHeight = 0.0f; // Default: sensor at tank bottom
  cfg.tanks[0].sensorRangeMin = 0.0f;    // Default: 0 (e.g., 0 PSI or 0 meters)
  cfg.tanks[0].sensorRangeMax = 5.0f;    // Default: 5 (e.g., 5 PSI for typical pressure sensor)
  strlcpy(cfg.tanks[0].sensorRangeUnit, "PSI", sizeof(cfg.tanks[0].sensorRangeUnit)); // Default: PSI
  cfg.tanks[0].analogVoltageMin = 0.0f;  // Default: 0V (for 0-10V sensors)
  cfg.tanks[0].analogVoltageMax = 10.0f; // Default: 10V (for 0-10V sensors)
  
  // Clear button defaults (disabled)
  cfg.clearButtonPin = -1;           // -1 = disabled
  cfg.clearButtonActiveHigh = false; // Active LOW with pullup (button connects to GND)
  
  // Power saving defaults (grid-tied, no special power saving)
  cfg.solarPowered = false;          // false = grid-tied (default)
}

static bool loadConfigFromFlash(ClientConfig &cfg) {
#ifdef FILESYSTEM_AVAILABLE
  #if defined(ARDUINO_OPTA) || defined(ARDUINO_ARCH_MBED)
    // Mbed OS file operations
    if (!mbedFS) return false;
    
    FILE *file = fopen("/fs/client_config.json", "r");
    if (!file) {
      return false;
    }
    
    // Read file into buffer
    fseek(file, 0, SEEK_END);
    long fileSize = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    if (fileSize <= 0 || fileSize > 8192) {
      fclose(file);
      return false;
    }
    
    char *buffer = (char *)malloc(fileSize + 1);
    if (!buffer) {
      fclose(file);
      return false;
    }
    
    size_t bytesRead = fread(buffer, 1, fileSize, file);
    buffer[bytesRead] = '\0';
    fclose(file);
    
    DynamicJsonDocument doc(4096);
    DeserializationError err = deserializeJson(doc, buffer);
    free(buffer);
  #else
    // STM32duino file operations
    if (!LittleFS.exists(CLIENT_CONFIG_PATH)) {
      return false;
    }

    File file = LittleFS.open(CLIENT_CONFIG_PATH, "r");
    if (!file) {
      return false;
    }

    DynamicJsonDocument doc(4096);
    DeserializationError err = deserializeJson(doc, file);
    file.close();
  #endif

  if (err) {
    Serial.println(F("Config deserialization failed"));
    return false;
  }

  memset(&cfg, 0, sizeof(ClientConfig));

  strlcpy(cfg.siteName, doc["site"].as<const char *>() ? doc["site"].as<const char *>() : "", sizeof(cfg.siteName));
  strlcpy(cfg.deviceLabel, doc["deviceLabel"].as<const char *>() ? doc["deviceLabel"].as<const char *>() : "", sizeof(cfg.deviceLabel));
  strlcpy(cfg.serverFleet, doc["serverFleet"].as<const char *>() ? doc["serverFleet"].as<const char *>() : "", sizeof(cfg.serverFleet));
  strlcpy(cfg.dailyEmail, doc["dailyEmail"].as<const char *>() ? doc["dailyEmail"].as<const char *>() : "", sizeof(cfg.dailyEmail));

  cfg.sampleSeconds = doc["sampleSeconds"].is<uint16_t>() ? doc["sampleSeconds"].as<uint16_t>() : DEFAULT_SAMPLE_SECONDS;
  cfg.minLevelChangeInches = doc["levelChangeThreshold"].is<float>() ? doc["levelChangeThreshold"].as<float>() : DEFAULT_LEVEL_CHANGE_THRESHOLD_INCHES;
  if (cfg.minLevelChangeInches < 0.0f) {
    cfg.minLevelChangeInches = 0.0f;
  }
  cfg.reportHour = doc["reportHour"].is<uint8_t>() ? doc["reportHour"].as<uint8_t>() : DEFAULT_REPORT_HOUR;
  cfg.reportMinute = doc["reportMinute"].is<uint8_t>() ? doc["reportMinute"].as<uint8_t>() : DEFAULT_REPORT_MINUTE;
  
  // Load clear button configuration
  cfg.clearButtonPin = doc["clearButtonPin"].is<int>() ? doc["clearButtonPin"].as<int8_t>() : -1;
  cfg.clearButtonActiveHigh = doc["clearButtonActiveHigh"].is<bool>() ? doc["clearButtonActiveHigh"].as<bool>() : false;
  
  // Load power saving configuration
  cfg.solarPowered = doc["solarPowered"].is<bool>() ? doc["solarPowered"].as<bool>() : false;

  cfg.tankCount = doc["tanks"].is<JsonArray>() ? min<uint8_t>(doc["tanks"].size(), MAX_TANKS) : 0;

  for (uint8_t i = 0; i < cfg.tankCount; ++i) {
    JsonObject t = doc["tanks"][i];
    cfg.tanks[i].id = t["id"].as<const char *>() ? t["id"].as<const char *>()[0] : ('A' + i);
    strlcpy(cfg.tanks[i].name, t["name"].as<const char *>() ? t["name"].as<const char *>() : "Tank", sizeof(cfg.tanks[i].name));
    cfg.tanks[i].tankNumber = t["number"].is<uint8_t>() ? t["number"].as<uint8_t>() : (i + 1);
    const char *sensor = t["sensor"].as<const char *>();
    if (sensor && strcmp(sensor, "digital") == 0) {
      cfg.tanks[i].sensorType = SENSOR_DIGITAL;
    } else if (sensor && strcmp(sensor, "current") == 0) {
      cfg.tanks[i].sensorType = SENSOR_CURRENT_LOOP;
    } else if (sensor && strcmp(sensor, "rpm") == 0) {
      cfg.tanks[i].sensorType = SENSOR_HALL_EFFECT_RPM;
    } else {
      cfg.tanks[i].sensorType = SENSOR_ANALOG;
    }
    cfg.tanks[i].primaryPin = t["primaryPin"].is<int>() ? t["primaryPin"].as<int>() : (cfg.tanks[i].sensorType == SENSOR_DIGITAL ? 2 : 0);
    cfg.tanks[i].secondaryPin = t["secondaryPin"].is<int>() ? t["secondaryPin"].as<int>() : -1;
    cfg.tanks[i].currentLoopChannel = t["loopChannel"].is<int>() ? t["loopChannel"].as<int>() : -1;
    cfg.tanks[i].rpmPin = t["rpmPin"].is<int>() ? t["rpmPin"].as<int>() : -1;
    cfg.tanks[i].pulsesPerRevolution = t["pulsesPerRev"].is<uint8_t>() ? max((uint8_t)1, t["pulsesPerRev"].as<uint8_t>()) : 1;
    // Load hall effect sensor type
    const char *hallType = t["hallEffectType"].as<const char *>();
    if (hallType && strcmp(hallType, "bipolar") == 0) {
      cfg.tanks[i].hallEffectType = HALL_EFFECT_BIPOLAR;
    } else if (hallType && strcmp(hallType, "omnipolar") == 0) {
      cfg.tanks[i].hallEffectType = HALL_EFFECT_OMNIPOLAR;
    } else if (hallType && strcmp(hallType, "analog") == 0) {
      cfg.tanks[i].hallEffectType = HALL_EFFECT_ANALOG;
    } else if (hallType && strcmp(hallType, "unipolar") == 0) {
      cfg.tanks[i].hallEffectType = HALL_EFFECT_UNIPOLAR;
    } else {
      cfg.tanks[i].hallEffectType = HALL_EFFECT_UNIPOLAR; // Default
    }
    // Load hall effect detection method
    const char *hallDetect = t["hallEffectDetection"].as<const char *>();
    if (hallDetect && strcmp(hallDetect, "time") == 0) {
      cfg.tanks[i].hallEffectDetection = HALL_DETECT_TIME_BASED;
    } else if (hallDetect && strcmp(hallDetect, "pulse") == 0) {
      cfg.tanks[i].hallEffectDetection = HALL_DETECT_PULSE;
    } else {
      cfg.tanks[i].hallEffectDetection = HALL_DETECT_PULSE; // Default
    }
    cfg.tanks[i].highAlarmThreshold = t["highAlarm"].is<float>() ? t["highAlarm"].as<float>() : 100.0f;
    cfg.tanks[i].lowAlarmThreshold = t["lowAlarm"].is<float>() ? t["lowAlarm"].as<float>() : 20.0f;
    cfg.tanks[i].hysteresisValue = t["hysteresis"].is<float>() ? t["hysteresis"].as<float>() : 2.0f;
    cfg.tanks[i].enableDailyReport = t["daily"].is<bool>() ? t["daily"].as<bool>() : true;
    cfg.tanks[i].enableAlarmSms = t["alarmSms"].is<bool>() ? t["alarmSms"].as<bool>() : true;
    cfg.tanks[i].enableServerUpload = t["upload"].is<bool>() ? t["upload"].as<bool>() : true;
    // Load relay control settings
    const char *relayTarget = t["relayTargetClient"].as<const char *>();
    strlcpy(cfg.tanks[i].relayTargetClient, relayTarget ? relayTarget : "", sizeof(cfg.tanks[i].relayTargetClient));
    cfg.tanks[i].relayMask = t["relayMask"].is<uint8_t>() ? t["relayMask"].as<uint8_t>() : 0;
    // Load relay trigger condition (defaults to 'any' for backwards compatibility)
    const char *relayTriggerStr = t["relayTrigger"].as<const char *>();
    if (relayTriggerStr && strcmp(relayTriggerStr, "high") == 0) {
      cfg.tanks[i].relayTrigger = RELAY_TRIGGER_HIGH;
    } else if (relayTriggerStr && strcmp(relayTriggerStr, "low") == 0) {
      cfg.tanks[i].relayTrigger = RELAY_TRIGGER_LOW;
    } else {
      cfg.tanks[i].relayTrigger = RELAY_TRIGGER_ANY;
    }
    // Load relay mode (defaults to momentary for backwards compatibility)
    const char *relayModeStr = t["relayMode"].as<const char *>();
    if (relayModeStr && strcmp(relayModeStr, "until_clear") == 0) {
      cfg.tanks[i].relayMode = RELAY_MODE_UNTIL_CLEAR;
    } else if (relayModeStr && strcmp(relayModeStr, "manual_reset") == 0) {
      cfg.tanks[i].relayMode = RELAY_MODE_MANUAL_RESET;
    } else {
      cfg.tanks[i].relayMode = RELAY_MODE_MOMENTARY;
    }
    // Load per-relay momentary durations (0 = use default 30 min)
    JsonArrayConst durations = t["relayMomentaryDurations"].as<JsonArrayConst>();
    for (uint8_t r = 0; r < 4; ++r) {
      if (durations && r < durations.size()) {
        cfg.tanks[i].relayMomentarySeconds[r] = durations[r].as<uint16_t>();
      } else {
        cfg.tanks[i].relayMomentarySeconds[r] = 0; // Default
      }
    }
    // Load digital sensor trigger state (for float switches)
    const char *digitalTriggerStr = t["digitalTrigger"].as<const char *>();
    strlcpy(cfg.tanks[i].digitalTrigger, digitalTriggerStr ? digitalTriggerStr : "", sizeof(cfg.tanks[i].digitalTrigger));
    // Load digital switch mode (NO = normally-open, NC = normally-closed)
    const char *digitalSwitchModeStr = t["digitalSwitchMode"].as<const char *>();
    if (digitalSwitchModeStr && strcmp(digitalSwitchModeStr, "NC") == 0) {
      strlcpy(cfg.tanks[i].digitalSwitchMode, "NC", sizeof(cfg.tanks[i].digitalSwitchMode));
    } else {
      strlcpy(cfg.tanks[i].digitalSwitchMode, "NO", sizeof(cfg.tanks[i].digitalSwitchMode)); // Default: normally-open
    }
    // Load 4-20mA current loop sensor type (pressure or ultrasonic)
    const char *currentLoopTypeStr = t["currentLoopType"].as<const char *>();
    if (currentLoopTypeStr && strcmp(currentLoopTypeStr, "ultrasonic") == 0) {
      cfg.tanks[i].currentLoopType = CURRENT_LOOP_ULTRASONIC;
    } else {
      cfg.tanks[i].currentLoopType = CURRENT_LOOP_PRESSURE; // Default: pressure sensor
    }
    // Load sensor mount height (for calibration) - validate non-negative
    cfg.tanks[i].sensorMountHeight = t["sensorMountHeight"].is<float>() ? fmaxf(0.0f, t["sensorMountHeight"].as<float>()) : 0.0f;
    // Load sensor native range settings
    cfg.tanks[i].sensorRangeMin = t["sensorRangeMin"].is<float>() ? t["sensorRangeMin"].as<float>() : 0.0f;
    cfg.tanks[i].sensorRangeMax = t["sensorRangeMax"].is<float>() ? t["sensorRangeMax"].as<float>() : 5.0f;
    const char *rangeUnitStr = t["sensorRangeUnit"].as<const char *>();
    strlcpy(cfg.tanks[i].sensorRangeUnit, rangeUnitStr ? rangeUnitStr : "PSI", sizeof(cfg.tanks[i].sensorRangeUnit));
    // Load analog voltage range settings (for 0-10V, 1-5V, etc. sensors)
    cfg.tanks[i].analogVoltageMin = t["analogVoltageMin"].is<float>() ? t["analogVoltageMin"].as<float>() : 0.0f;
    cfg.tanks[i].analogVoltageMax = t["analogVoltageMax"].is<float>() ? t["analogVoltageMax"].as<float>() : 10.0f;
  }

  return true;
#else
  return false; // Filesystem not available
#endif
}

static bool saveConfigToFlash(const ClientConfig &cfg) {
#ifdef FILESYSTEM_AVAILABLE
  #if defined(ARDUINO_OPTA) || defined(ARDUINO_ARCH_MBED)
    if (!mbedFS) return false;
  #endif
  
  DynamicJsonDocument doc(4096);
  doc["site"] = cfg.siteName;
  doc["deviceLabel"] = cfg.deviceLabel;
  doc["serverFleet"] = cfg.serverFleet;
  doc["sampleSeconds"] = cfg.sampleSeconds;
  doc["levelChangeThreshold"] = cfg.minLevelChangeInches;
  doc["reportHour"] = cfg.reportHour;
  doc["reportMinute"] = cfg.reportMinute;
  doc["dailyEmail"] = cfg.dailyEmail;
  
  // Save clear button configuration
  doc["clearButtonPin"] = cfg.clearButtonPin;
  doc["clearButtonActiveHigh"] = cfg.clearButtonActiveHigh;
  
  // Save power saving configuration
  doc["solarPowered"] = cfg.solarPowered;

  JsonArray tanks = doc.createNestedArray("tanks");
  for (uint8_t i = 0; i < cfg.tankCount; ++i) {
    JsonObject t = tanks.createNestedObject();
    char idBuffer[2] = {cfg.tanks[i].id, '\0'};
    t["id"] = idBuffer;
    t["name"] = cfg.tanks[i].name;
    t["number"] = cfg.tanks[i].tankNumber;
    switch (cfg.tanks[i].sensorType) {
      case SENSOR_DIGITAL: t["sensor"] = "digital"; break;
      case SENSOR_CURRENT_LOOP: t["sensor"] = "current"; break;
      case SENSOR_HALL_EFFECT_RPM: t["sensor"] = "rpm"; break;
      default: t["sensor"] = "analog"; break;
    }
    t["primaryPin"] = cfg.tanks[i].primaryPin;
    t["secondaryPin"] = cfg.tanks[i].secondaryPin;
    t["loopChannel"] = cfg.tanks[i].currentLoopChannel;
    t["rpmPin"] = cfg.tanks[i].rpmPin;
    t["pulsesPerRev"] = cfg.tanks[i].pulsesPerRevolution;
    // Save hall effect sensor type
    switch (cfg.tanks[i].hallEffectType) {
      case HALL_EFFECT_BIPOLAR: t["hallEffectType"] = "bipolar"; break;
      case HALL_EFFECT_OMNIPOLAR: t["hallEffectType"] = "omnipolar"; break;
      case HALL_EFFECT_ANALOG: t["hallEffectType"] = "analog"; break;
      case HALL_EFFECT_UNIPOLAR:
      default: t["hallEffectType"] = "unipolar"; break;
    }
    // Save hall effect detection method
    switch (cfg.tanks[i].hallEffectDetection) {
      case HALL_DETECT_TIME_BASED: t["hallEffectDetection"] = "time"; break;
      case HALL_DETECT_PULSE:
      default: t["hallEffectDetection"] = "pulse"; break;
    }
    t["highAlarm"] = cfg.tanks[i].highAlarmThreshold;
    t["lowAlarm"] = cfg.tanks[i].lowAlarmThreshold;
    t["hysteresis"] = cfg.tanks[i].hysteresisValue;
    t["daily"] = cfg.tanks[i].enableDailyReport;
    t["alarmSms"] = cfg.tanks[i].enableAlarmSms;
    t["upload"] = cfg.tanks[i].enableServerUpload;
    // Save relay control settings
    t["relayTargetClient"] = cfg.tanks[i].relayTargetClient;
    t["relayMask"] = cfg.tanks[i].relayMask;
    // Save relay trigger condition as string
    switch (cfg.tanks[i].relayTrigger) {
      case RELAY_TRIGGER_HIGH: t["relayTrigger"] = "high"; break;
      case RELAY_TRIGGER_LOW: t["relayTrigger"] = "low"; break;
      default: t["relayTrigger"] = "any"; break;
    }
    // Save relay mode as string
    switch (cfg.tanks[i].relayMode) {
      case RELAY_MODE_UNTIL_CLEAR: t["relayMode"] = "until_clear"; break;
      case RELAY_MODE_MANUAL_RESET: t["relayMode"] = "manual_reset"; break;
      default: t["relayMode"] = "momentary"; break;
    }
    // Save per-relay momentary durations
    JsonArray durations = t.createNestedArray("relayMomentaryDurations");
    for (uint8_t r = 0; r < 4; ++r) {
      durations.add(cfg.tanks[i].relayMomentarySeconds[r]);
    }
    // Save digital sensor trigger state (for float switches)
    if (cfg.tanks[i].digitalTrigger[0] != '\0') {
      t["digitalTrigger"] = cfg.tanks[i].digitalTrigger;
    }
    // Save digital switch mode (NO/NC)
    t["digitalSwitchMode"] = cfg.tanks[i].digitalSwitchMode;
    // Save 4-20mA current loop sensor type
    switch (cfg.tanks[i].currentLoopType) {
      case CURRENT_LOOP_ULTRASONIC: t["currentLoopType"] = "ultrasonic"; break;
      default: t["currentLoopType"] = "pressure"; break;
    }
    // Save sensor mount height (for calibration)
    t["sensorMountHeight"] = cfg.tanks[i].sensorMountHeight;
    // Save sensor native range settings
    t["sensorRangeMin"] = cfg.tanks[i].sensorRangeMin;
    t["sensorRangeMax"] = cfg.tanks[i].sensorRangeMax;
    t["sensorRangeUnit"] = cfg.tanks[i].sensorRangeUnit;
    // Save analog voltage range settings
    t["analogVoltageMin"] = cfg.tanks[i].analogVoltageMin;
    t["analogVoltageMax"] = cfg.tanks[i].analogVoltageMax;
  }

  #if defined(ARDUINO_OPTA) || defined(ARDUINO_ARCH_MBED)
    // Mbed OS file operations
    FILE *file = fopen("/fs/client_config.json", "w");
    if (!file) {
      Serial.println(F("Failed to open config for writing"));
      return false;
    }
    
    // Serialize to buffer first, then write
    String jsonStr;
    size_t len = serializeJson(doc, jsonStr);
    if (len == 0) {
      fclose(file);
      Serial.println(F("Failed to serialize config"));
      return false;
    }
    
    size_t written = fwrite(jsonStr.c_str(), 1, jsonStr.length(), file);
    fclose(file);
    if (written != jsonStr.length()) {
      Serial.println(F("Failed to write config (incomplete)"));
      return false;
    }
    return true;
  #else
    File file = LittleFS.open(CLIENT_CONFIG_PATH, "w");
    if (!file) {
      Serial.println(F("Failed to open config for writing"));
      return false;
    }

    if (serializeJson(doc, file) == 0) {
      file.close();
      Serial.println(F("Failed to serialize config"));
      return false;
    }

    file.close();
    return true;
  #endif
#else
  return false; // Filesystem not available
#endif
}

static void printHardwareRequirements(const ClientConfig &cfg) {
  if (gHardwareSummaryPrinted) {
    return;
  }
  gHardwareSummaryPrinted = true;

  bool needsAnalogExpansion = false;
  bool needsCurrentLoop = false;
  bool needsRelayOutput = false;
  bool needsRpmSensor = false;
  bool hasPressureSensor = false;
  bool hasUltrasonicSensor = false;

  for (uint8_t i = 0; i < cfg.tankCount; ++i) {
    if (cfg.tanks[i].sensorType == SENSOR_ANALOG) {
      needsAnalogExpansion = true;
    }
    if (cfg.tanks[i].sensorType == SENSOR_CURRENT_LOOP) {
      needsCurrentLoop = true;
      if (cfg.tanks[i].currentLoopType == CURRENT_LOOP_PRESSURE) {
        hasPressureSensor = true;
      } else if (cfg.tanks[i].currentLoopType == CURRENT_LOOP_ULTRASONIC) {
        hasUltrasonicSensor = true;
      }
    }
    if (cfg.tanks[i].sensorType == SENSOR_HALL_EFFECT_RPM) {
      needsRpmSensor = true;
    }
    if (cfg.tanks[i].enableAlarmSms) {
      needsRelayOutput = true; // indicates coil notifications on Opta outputs
    }
  }

  Serial.println(F("--- Hardware Requirements ---"));
  Serial.println(F("Base: Arduino Opta Lite"));
  Serial.println(F("Connectivity: Blues Wireless Notecard (I2C 0x17)"));
  if (needsAnalogExpansion || needsCurrentLoop) {
    Serial.println(F("Analog/Current: Arduino Pro Opta Ext A0602"));
  }
  if (needsCurrentLoop) {
    Serial.println(F("Current loop interface required (4-20mA module)"));
    if (hasPressureSensor) {
      Serial.println(F("  - Pressure sensor (bottom-mounted, e.g., Dwyer 626-06-CB-P1-E5-S1)"));
    }
    if (hasUltrasonicSensor) {
      Serial.println(F("  - Ultrasonic sensor (top-mounted, e.g., Siemens Sitrans LU240)"));
    }
  }
  if (needsRpmSensor) {
    Serial.println(F("Hall effect RPM sensor connected to digital input"));
  }
  if (needsRelayOutput) {
    Serial.println(F("Relay outputs wired for audible/visual alarm"));
  }
  Serial.println(F("-----------------------------"));
}

static void configureNotecardHubMode() {
  // Configure Notecard hub mode based on power source
  J *req = notecard.newRequest("hub.set");
  if (req) {
    JAddStringToObject(req, "product", PRODUCT_UID);
    
    // Power saving configuration based on power source
    if (gConfig.solarPowered) {
      // Solar powered: Use periodic mode with extended inbound check to save power
      JAddStringToObject(req, "mode", "periodic");
      JAddIntToObject(req, "outbound", SOLAR_OUTBOUND_INTERVAL_MINUTES);  // Sync every 6 hours
      JAddIntToObject(req, "inbound", SOLAR_INBOUND_INTERVAL_MINUTES);    // Check for inbound every hour (power saving)
    } else {
      // Grid-tied: Use continuous mode for faster response times
      JAddStringToObject(req, "mode", "continuous");
      // In continuous mode, outbound/inbound are not used - always connected
    }
    
    notecard.sendRequest(req);
  }
}

static void initializeNotecard() {
#ifdef DEBUG_MODE
  notecard.setDebugOutputStream(Serial);
#endif
  notecard.begin(NOTECARD_I2C_ADDRESS);

  J *req = notecard.newRequest("card.wire");
  if (req) {
    JAddIntToObject(req, "speed", (int)NOTECARD_I2C_FREQUENCY);
    notecard.sendRequest(req);
  }

  // Configure hub mode based on power configuration
  configureNotecardHubMode();

  req = notecard.newRequest("card.uuid");
  J *rsp = notecard.requestAndResponse(req);
  if (rsp) {
    const char *uid = JGetString(rsp, "uuid");
    if (uid) {
      strlcpy(gDeviceUID, uid, sizeof(gDeviceUID));
    }
    notecard.deleteResponse(rsp);
  }

  if (gDeviceUID[0] == '\0') {
    strlcpy(gDeviceUID, gConfig.deviceLabel, sizeof(gDeviceUID));
  }

  Serial.print(F("Notecard UID: "));
  Serial.println(gDeviceUID);
}

static bool checkNotecardHealth() {
  J *req = notecard.newRequest("card.wireless");
  if (!req) {
    gNotecardFailureCount++;
    if (gNotecardFailureCount >= NOTECARD_FAILURE_THRESHOLD && gNotecardAvailable) {
      gNotecardAvailable = false;
      Serial.println(F("Notecard unavailable - entering offline mode"));
    }
    return false;
  }
  
  J *rsp = notecard.requestAndResponse(req);
  if (!rsp) {
    gNotecardFailureCount++;
    if (gNotecardFailureCount >= NOTECARD_FAILURE_THRESHOLD && gNotecardAvailable) {
      gNotecardAvailable = false;
      Serial.println(F("Notecard unavailable - entering offline mode"));
    }
    return false;
  }
  
  notecard.deleteResponse(rsp);
  
  // Notecard is responding
  if (!gNotecardAvailable) {
    Serial.println(F("Notecard recovered - online mode restored"));
  }
  gNotecardAvailable = true;
  gNotecardFailureCount = 0;
  gLastSuccessfulNotecardComm = millis();
  flushBufferedNotes();
  return true;
}

static void syncTimeFromNotecard() {
  J *req = notecard.newRequest("card.time");
  if (!req) {
    gNotecardFailureCount++;
    return;
  }
  JAddStringToObject(req, "mode", "auto");
  J *rsp = notecard.requestAndResponse(req);
  if (!rsp) {
    gNotecardFailureCount++;
    return;
  }

  // Check for error response (e.g., "time is not yet set {no-time}")
  // This is normal during startup before Notecard syncs with cloud
  const char *err = JGetString(rsp, "err");
  if (err && strlen(err) > 0) {
    // Time not yet available - this is expected during startup
    // Will retry on next call - don't count this as a failure
    notecard.deleteResponse(rsp);
    return;
  }

  double time = JGetNumber(rsp, "time");
  if (time > 0) {
    gLastSyncedEpoch = time;
    gLastTimeSyncMillis = millis();
    gLastSuccessfulNotecardComm = millis();
    gNotecardFailureCount = 0;
  }
  notecard.deleteResponse(rsp);
}

static double currentEpoch() {
  if (gLastSyncedEpoch <= 0.0) {
    return 0.0;
  }
  unsigned long deltaMs = millis() - gLastTimeSyncMillis;
  return gLastSyncedEpoch + (double)deltaMs / 1000.0;
}

static void ensureTimeSync() {
  if (millis() - gLastTimeSyncMillis > 6UL * 60UL * 60UL * 1000UL || gLastSyncedEpoch <= 0.0) {
    syncTimeFromNotecard();
  }
}

static void scheduleNextDailyReport() {
  double epoch = currentEpoch();
  if (epoch <= 0.0) {
    gNextDailyReportEpoch = 0.0;
    return;
  }

  double dayStart = floor(epoch / 86400.0) * 86400.0;
  double scheduled = dayStart + (double)gConfig.reportHour * 3600.0 + (double)gConfig.reportMinute * 60.0;
  if (scheduled <= epoch) {
    scheduled += 86400.0;
  }

  gNextDailyReportEpoch = scheduled;
}

static void updateDailyScheduleIfNeeded() {
  if (gNextDailyReportEpoch <= 0.0 && gLastSyncedEpoch > 0.0) {
    scheduleNextDailyReport();
  }
}

static void pollForConfigUpdates() {
  // Skip if notecard is known to be offline
  if (!gNotecardAvailable) {
    unsigned long now = millis();
    if (now - gLastSuccessfulNotecardComm > NOTECARD_RETRY_INTERVAL) {
      // Periodically retry to see if notecard is back
      checkNotecardHealth();
    }
    return;
  }

  J *req = notecard.newRequest("note.get");
  if (!req) {
    gNotecardFailureCount++;
    return;
  }

  JAddStringToObject(req, "file", CONFIG_INBOX_FILE);
  JAddBoolToObject(req, "delete", true);

  J *rsp = notecard.requestAndResponse(req);
  if (!rsp) {
    gNotecardFailureCount++;
    if (gNotecardFailureCount >= NOTECARD_FAILURE_THRESHOLD) {
      checkNotecardHealth();
    }
    return;
  }

  gLastSuccessfulNotecardComm = millis();
  gNotecardFailureCount = 0;

  J *body = JGetObject(rsp, "body");
  if (body) {
    char *json = JConvertToJSONString(body);
    if (json) {
      DynamicJsonDocument doc(4096);
      DeserializationError err = deserializeJson(doc, json);
      NoteFree(json);
      if (!err) {
        applyConfigUpdate(doc);
      } else {
        Serial.println(F("Config update invalid JSON"));
      }
    }
  }

  notecard.deleteResponse(rsp);
}

static void reinitializeHardware() {
  // Reinitialize all tank sensors with new configuration
  for (uint8_t i = 0; i < gConfig.tankCount; ++i) {
    const TankConfig &cfg = gConfig.tanks[i];
    
    // Configure digital pins if needed
    if (cfg.sensorType == SENSOR_DIGITAL) {
      if (cfg.primaryPin >= 0 && cfg.primaryPin < 255) {
        pinMode(cfg.primaryPin, INPUT_PULLUP);
      }
      if (cfg.secondaryPin >= 0 && cfg.secondaryPin < 255) {
        pinMode(cfg.secondaryPin, INPUT_PULLUP);
      }
    }
    
    // Reset tank runtime state for hardware changes
    gTankState[i].highAlarmDebounceCount = 0;
    gTankState[i].lowAlarmDebounceCount = 0;
    gTankState[i].clearDebounceCount = 0;
    gTankState[i].consecutiveFailures = 0;
    gTankState[i].stuckReadingCount = 0;
    gTankState[i].sensorFailed = false;
    gTankState[i].lastValidReading = 0.0f;
    gTankState[i].hasLastValidReading = false;
    gTankState[i].lastReportedInches = -9999.0f;
  }
  
  // Reconfigure Notecard hub settings (may have changed due to power mode)
  configureNotecardHubMode();
  
  Serial.println(F("Hardware reinitialized after config update"));
}

static void resetTelemetryBaselines() {
  for (uint8_t i = 0; i < MAX_TANKS; ++i) {
    gTankState[i].lastReportedInches = -9999.0f;
  }
}

static void applyConfigUpdate(const JsonDocument &doc) {
  bool hardwareChanged = false;
  bool telemetryPolicyChanged = false;
  float previousThreshold = gConfig.minLevelChangeInches;
  
  if (doc.containsKey("site")) {
    strlcpy(gConfig.siteName, doc["site"].as<const char *>(), sizeof(gConfig.siteName));
  }
  if (doc.containsKey("deviceLabel")) {
    strlcpy(gConfig.deviceLabel, doc["deviceLabel"].as<const char *>(), sizeof(gConfig.deviceLabel));
  }
  if (doc.containsKey("serverFleet")) {
    strlcpy(gConfig.serverFleet, doc["serverFleet"].as<const char *>(), sizeof(gConfig.serverFleet));
  }
  if (doc.containsKey("sampleSeconds")) {
    gConfig.sampleSeconds = doc["sampleSeconds"].as<uint16_t>();
  }
  if (doc.containsKey("levelChangeThreshold")) {
    gConfig.minLevelChangeInches = doc["levelChangeThreshold"].as<float>();
    if (gConfig.minLevelChangeInches < 0.0f) {
      gConfig.minLevelChangeInches = 0.0f;
    }
    telemetryPolicyChanged = (fabsf(previousThreshold - gConfig.minLevelChangeInches) > 0.0001f);
  }
  if (doc.containsKey("reportHour")) {
    gConfig.reportHour = doc["reportHour"].as<uint8_t>();
  }
  if (doc.containsKey("reportMinute")) {
    gConfig.reportMinute = doc["reportMinute"].as<uint8_t>();
  }
  if (doc.containsKey("dailyEmail")) {
    strlcpy(gConfig.dailyEmail, doc["dailyEmail"].as<const char *>(), sizeof(gConfig.dailyEmail));
  }
  
  // Handle clear button configuration
  if (doc.containsKey("clearButtonPin")) {
    int8_t newPin = doc["clearButtonPin"].as<int8_t>();
    if (newPin != gConfig.clearButtonPin) {
      gConfig.clearButtonPin = newPin;
      hardwareChanged = true;  // Need to reinitialize button pin
    }
  }
  if (doc.containsKey("clearButtonActiveHigh")) {
    gConfig.clearButtonActiveHigh = doc["clearButtonActiveHigh"].as<bool>();
  }
  
  // Handle power saving configuration
  if (doc.containsKey("solarPowered")) {
    bool newSolarPowered = doc["solarPowered"].as<bool>();
    if (newSolarPowered != gConfig.solarPowered) {
      gConfig.solarPowered = newSolarPowered;
      hardwareChanged = true;  // Need to reconfigure Notecard hub settings
    }
  }

  if (doc.containsKey("tanks")) {
    hardwareChanged = true;  // Tank configuration affects hardware
    JsonArrayConst tanks = doc["tanks"].as<JsonArrayConst>();
    gConfig.tankCount = min<uint8_t>(tanks.size(), MAX_TANKS);
    for (uint8_t i = 0; i < gConfig.tankCount; ++i) {
      JsonObjectConst t = tanks[i];
      gConfig.tanks[i].id = t["id"].as<const char *>() ? t["id"].as<const char *>()[0] : ('A' + i);
      strlcpy(gConfig.tanks[i].name, t["name"].as<const char *>() ? t["name"].as<const char *>() : "Tank", sizeof(gConfig.tanks[i].name));
      gConfig.tanks[i].tankNumber = t["number"].is<uint8_t>() ? t["number"].as<uint8_t>() : (i + 1);
      const char *sensor = t["sensor"].as<const char *>();
      if (sensor && strcmp(sensor, "digital") == 0) {
        gConfig.tanks[i].sensorType = SENSOR_DIGITAL;
      } else if (sensor && strcmp(sensor, "current") == 0) {
        gConfig.tanks[i].sensorType = SENSOR_CURRENT_LOOP;
      } else if (sensor && strcmp(sensor, "rpm") == 0) {
        gConfig.tanks[i].sensorType = SENSOR_HALL_EFFECT_RPM;
      } else {
        gConfig.tanks[i].sensorType = SENSOR_ANALOG;
      }
      gConfig.tanks[i].primaryPin = t["primaryPin"].is<int>() ? t["primaryPin"].as<int>() : gConfig.tanks[i].primaryPin;
      gConfig.tanks[i].secondaryPin = t["secondaryPin"].is<int>() ? t["secondaryPin"].as<int>() : gConfig.tanks[i].secondaryPin;
      gConfig.tanks[i].currentLoopChannel = t["loopChannel"].is<int>() ? t["loopChannel"].as<int>() : gConfig.tanks[i].currentLoopChannel;
      gConfig.tanks[i].rpmPin = t["rpmPin"].is<int>() ? t["rpmPin"].as<int>() : gConfig.tanks[i].rpmPin;
      if (t.containsKey("pulsesPerRev")) {
        gConfig.tanks[i].pulsesPerRevolution = max((uint8_t)1, t["pulsesPerRev"].as<uint8_t>());
      }
      // Update hall effect sensor type if provided
      if (t.containsKey("hallEffectType")) {
        const char *hallType = t["hallEffectType"].as<const char *>();
        if (hallType && strcmp(hallType, "bipolar") == 0) {
          gConfig.tanks[i].hallEffectType = HALL_EFFECT_BIPOLAR;
        } else if (hallType && strcmp(hallType, "omnipolar") == 0) {
          gConfig.tanks[i].hallEffectType = HALL_EFFECT_OMNIPOLAR;
        } else if (hallType && strcmp(hallType, "analog") == 0) {
          gConfig.tanks[i].hallEffectType = HALL_EFFECT_ANALOG;
        } else if (hallType && strcmp(hallType, "unipolar") == 0) {
          gConfig.tanks[i].hallEffectType = HALL_EFFECT_UNIPOLAR;
        }
      }
      // Update hall effect detection method if provided
      if (t.containsKey("hallEffectDetection")) {
        const char *hallDetect = t["hallEffectDetection"].as<const char *>();
        if (hallDetect && strcmp(hallDetect, "time") == 0) {
          gConfig.tanks[i].hallEffectDetection = HALL_DETECT_TIME_BASED;
        } else if (hallDetect && strcmp(hallDetect, "pulse") == 0) {
          gConfig.tanks[i].hallEffectDetection = HALL_DETECT_PULSE;
        }
      }
      gConfig.tanks[i].highAlarmThreshold = t["highAlarm"].is<float>() ? t["highAlarm"].as<float>() : gConfig.tanks[i].highAlarmThreshold;
      gConfig.tanks[i].lowAlarmThreshold = t["lowAlarm"].is<float>() ? t["lowAlarm"].as<float>() : gConfig.tanks[i].lowAlarmThreshold;
      gConfig.tanks[i].hysteresisValue = t["hysteresis"].is<float>() ? t["hysteresis"].as<float>() : gConfig.tanks[i].hysteresisValue;
      if (t.containsKey("daily")) {
        gConfig.tanks[i].enableDailyReport = t["daily"].as<bool>();
      }
      if (t.containsKey("alarmSms")) {
        gConfig.tanks[i].enableAlarmSms = t["alarmSms"].as<bool>();
      }
      if (t.containsKey("upload")) {
        gConfig.tanks[i].enableServerUpload = t["upload"].as<bool>();
      }
      if (t.containsKey("relayTargetClient")) {
        const char *relayTargetStr = t["relayTargetClient"].as<const char *>();
        strlcpy(gConfig.tanks[i].relayTargetClient, 
                relayTargetStr ? relayTargetStr : "", 
                sizeof(gConfig.tanks[i].relayTargetClient));
      }
      if (t.containsKey("relayMask")) {
        gConfig.tanks[i].relayMask = t["relayMask"].as<uint8_t>();
      }
      if (t.containsKey("relayTrigger")) {
        const char *triggerStr = t["relayTrigger"].as<const char *>();
        if (triggerStr && strcmp(triggerStr, "high") == 0) {
          gConfig.tanks[i].relayTrigger = RELAY_TRIGGER_HIGH;
        } else if (triggerStr && strcmp(triggerStr, "low") == 0) {
          gConfig.tanks[i].relayTrigger = RELAY_TRIGGER_LOW;
        } else {
          gConfig.tanks[i].relayTrigger = RELAY_TRIGGER_ANY;
        }
      }
      if (t.containsKey("relayMode")) {
        const char *modeStr = t["relayMode"].as<const char *>();
        if (modeStr && strcmp(modeStr, "until_clear") == 0) {
          gConfig.tanks[i].relayMode = RELAY_MODE_UNTIL_CLEAR;
        } else if (modeStr && strcmp(modeStr, "manual_reset") == 0) {
          gConfig.tanks[i].relayMode = RELAY_MODE_MANUAL_RESET;
        } else {
          gConfig.tanks[i].relayMode = RELAY_MODE_MOMENTARY;
        }
      }
      // Handle per-relay momentary durations (in seconds)
      if (t.containsKey("relayMomentaryDurations")) {
        JsonArrayConst durations = t["relayMomentaryDurations"].as<JsonArrayConst>();
        for (size_t r = 0; r < 4 && r < durations.size(); r++) {
          uint16_t dur = durations[r].as<uint16_t>();
          // Enforce minimum of 1 second, max of 86400 (24 hours)
          gConfig.tanks[i].relayMomentarySeconds[r] = constrain(dur, 1, 86400);
        }
      }
      // Handle digital sensor trigger state (for float switches)
      if (t.containsKey("digitalTrigger")) {
        const char *digitalTriggerStr = t["digitalTrigger"].as<const char *>();
        strlcpy(gConfig.tanks[i].digitalTrigger,
                digitalTriggerStr ? digitalTriggerStr : "",
                sizeof(gConfig.tanks[i].digitalTrigger));
      }
      // Handle digital switch mode (NO/NC) for float switches
      if (t.containsKey("digitalSwitchMode")) {
        const char *digitalSwitchModeStr = t["digitalSwitchMode"].as<const char *>();
        if (digitalSwitchModeStr && strcmp(digitalSwitchModeStr, "NC") == 0) {
          strlcpy(gConfig.tanks[i].digitalSwitchMode, "NC", sizeof(gConfig.tanks[i].digitalSwitchMode));
        } else {
          strlcpy(gConfig.tanks[i].digitalSwitchMode, "NO", sizeof(gConfig.tanks[i].digitalSwitchMode));
        }
      }
      // Handle 4-20mA current loop sensor type (pressure or ultrasonic)
      if (t.containsKey("currentLoopType")) {
        const char *currentLoopTypeStr = t["currentLoopType"].as<const char *>();
        if (currentLoopTypeStr && strcmp(currentLoopTypeStr, "ultrasonic") == 0) {
          gConfig.tanks[i].currentLoopType = CURRENT_LOOP_ULTRASONIC;
        } else {
          gConfig.tanks[i].currentLoopType = CURRENT_LOOP_PRESSURE;
        }
      }
      // Handle sensor mount height (for calibration) - validate non-negative
      if (t.containsKey("sensorMountHeight")) {
        gConfig.tanks[i].sensorMountHeight = fmaxf(0.0f, t["sensorMountHeight"].as<float>());
      }
      // Handle sensor native range settings
      if (t.containsKey("sensorRangeMin")) {
        gConfig.tanks[i].sensorRangeMin = t["sensorRangeMin"].as<float>();
      }
      if (t.containsKey("sensorRangeMax")) {
        gConfig.tanks[i].sensorRangeMax = t["sensorRangeMax"].as<float>();
      }
      if (t.containsKey("sensorRangeUnit")) {
        const char *unitStr = t["sensorRangeUnit"].as<const char *>();
        strlcpy(gConfig.tanks[i].sensorRangeUnit, unitStr ? unitStr : "PSI", sizeof(gConfig.tanks[i].sensorRangeUnit));
      }
      // Handle analog voltage range settings
      if (t.containsKey("analogVoltageMin")) {
        gConfig.tanks[i].analogVoltageMin = t["analogVoltageMin"].as<float>();
      }
      if (t.containsKey("analogVoltageMax")) {
        gConfig.tanks[i].analogVoltageMax = t["analogVoltageMax"].as<float>();
      }
    }
  }

  gConfigDirty = true;
  
  if (hardwareChanged) {
    reinitializeHardware();
  } else if (telemetryPolicyChanged) {
    resetTelemetryBaselines();
  }
  
  printHardwareRequirements(gConfig);
  scheduleNextDailyReport();
  Serial.println(F("Configuration updated from server"));
  addSerialLog("Config updated from server");
}

static void persistConfigIfDirty() {
  if (!gConfigDirty) {
    return;
  }
  if (saveConfigToFlash(gConfig)) {
    gConfigDirty = false;
  }
}

static float readCurrentLoopMilliamps(int16_t channel) {
  if (channel < 0) {
    return -1.0f;
  }

  Wire.beginTransmission(CURRENT_LOOP_I2C_ADDRESS);
  Wire.write((uint8_t)channel);
  if (Wire.endTransmission(false) != 0) {
    return -1.0f;
  }

  if (Wire.requestFrom(CURRENT_LOOP_I2C_ADDRESS, (uint8_t)2) != 2) {
    return -1.0f;
  }

  uint16_t raw = ((uint16_t)Wire.read() << 8) | Wire.read();
  return 4.0f + (raw / 65535.0f) * 16.0f;
}

static float linearMap(float value, float inMin, float inMax, float outMin, float outMax) {
  if (fabs(inMax - inMin) < 0.0001f) {
    return outMin;
  }
  float pct = (value - inMin) / (inMax - inMin);
  pct = constrain(pct, 0.0f, 1.0f);
  return outMin + pct * (outMax - outMin);
}

static bool validateSensorReading(uint8_t idx, float reading) {
  if (idx >= gConfig.tankCount) {
    return false;
  }

  const TankConfig &cfg = gConfig.tanks[idx];
  TankRuntime &state = gTankState[idx];

  // Calculate valid range based on sensor type
  float minValid;
  float maxValid;
  
  // Check if sensor has native range configured (current loop or analog with voltage range)
  bool hasNativeRange = (cfg.sensorRangeMax > cfg.sensorRangeMin);
  bool isCurrentLoop = (cfg.sensorType == SENSOR_CURRENT_LOOP);
  bool isAnalogWithVoltageRange = (cfg.sensorType == SENSOR_ANALOG && 
                                    cfg.analogVoltageMax > cfg.analogVoltageMin &&
                                    hasNativeRange);
  
  if ((isCurrentLoop || isAnalogWithVoltageRange) && hasNativeRange) {
    // For sensors with native range, calculate max from sensor range
    if (isCurrentLoop && cfg.currentLoopType == CURRENT_LOOP_ULTRASONIC) {
      // Ultrasonic: max level is sensorMountHeight (when tank is full)
      maxValid = cfg.sensorMountHeight * 1.1f;
    } else {
      // Pressure: calculate max from pressure range
      float conversionFactor = getPressureConversionFactor(cfg.sensorRangeUnit);
      maxValid = (cfg.sensorRangeMax * conversionFactor + cfg.sensorMountHeight) * 1.1f;
    }
    minValid = -maxValid * 0.1f;
  } else if (cfg.sensorType == SENSOR_DIGITAL) {
    // Digital sensors have simple 0/1 values
    minValid = -0.5f;
    maxValid = 1.5f;
  } else {
    // For RPM and other sensors without native range, use alarm thresholds as reference
    maxValid = cfg.highAlarmThreshold * 2.0f; // Allow up to 2x high alarm as valid
    minValid = -maxValid * 0.1f;
  }
  
  if (reading < minValid || reading > maxValid) {
    state.consecutiveFailures++;
    if (state.consecutiveFailures >= SENSOR_FAILURE_THRESHOLD) {
      if (!state.sensorFailed) {
        state.sensorFailed = true;
        Serial.print(F("Sensor failure detected for tank "));
        Serial.println(cfg.name);
        // Send sensor failure alert with rate limiting
        if (checkAlarmRateLimit(idx, "sensor-fault")) {
          DynamicJsonDocument doc(512);
          doc["client"] = gDeviceUID;
          doc["site"] = gConfig.siteName;
          doc["label"] = cfg.name;
          doc["tank"] = cfg.tankNumber;
          doc["type"] = "sensor-fault";
          doc["reading"] = reading;
          doc["time"] = currentEpoch();
          publishNote(ALARM_FILE, doc, true);
        }
      }
    }
    return false;
  }

  // Check for stuck sensor (same reading multiple times)
  if (state.hasLastValidReading && fabs(reading - state.lastValidReading) < 0.05f) {
    state.stuckReadingCount++;
    if (state.stuckReadingCount >= SENSOR_STUCK_THRESHOLD) {
      if (!state.sensorFailed) {
        state.sensorFailed = true;
        Serial.print(F("Stuck sensor detected for tank "));
        Serial.println(cfg.name);
        // Send stuck sensor alert with rate limiting
        if (checkAlarmRateLimit(idx, "sensor-stuck")) {
          DynamicJsonDocument doc(512);
          doc["client"] = gDeviceUID;
          doc["site"] = gConfig.siteName;
          doc["label"] = cfg.name;
          doc["tank"] = cfg.tankNumber;
          doc["type"] = "sensor-stuck";
          doc["reading"] = reading;
          doc["time"] = currentEpoch();
          publishNote(ALARM_FILE, doc, true);
        }
      }
      return false;
    }
  } else {
    state.stuckReadingCount = 0;
  }

  // Reading is valid - reset failure counters
  state.consecutiveFailures = 0;
  if (state.sensorFailed) {
    state.sensorFailed = false;
    Serial.print(F("Sensor recovered for tank "));
    Serial.println(cfg.name);
    // Send recovery notification (no rate limit on recovery)
    DynamicJsonDocument doc(512);
    doc["client"] = gDeviceUID;
    doc["site"] = gConfig.siteName;
    doc["label"] = cfg.name;
    doc["tank"] = cfg.tankNumber;
    doc["type"] = "sensor-recovered";
    doc["reading"] = reading;
    doc["time"] = currentEpoch();
    publishNote(ALARM_FILE, doc, true);
  }
  state.lastValidReading = reading;
  state.hasLastValidReading = true;
  return true;
}

static float readTankSensor(uint8_t idx) {
  if (idx >= gConfig.tankCount) {
    return 0.0f;
  }

  const TankConfig &cfg = gConfig.tanks[idx];

  switch (cfg.sensorType) {
    case SENSOR_DIGITAL: {
      // Float switch sensor - returns activated/not-activated state
      // The digitalSwitchMode field controls how the hardware is interpreted:
      // - "NO" (normally-open): Switch is open by default, closes when fluid is present
      //   - With INPUT_PULLUP: HIGH = switch open (no fluid), LOW = switch closed (fluid present)
      //   - activated when pin is LOW
      // - "NC" (normally-closed): Switch is closed by default, opens when fluid is present
      //   - With INPUT_PULLUP: LOW = switch closed (no fluid), HIGH = switch open (fluid present)
      //   - activated when pin is HIGH
      int pin = (cfg.primaryPin >= 0 && cfg.primaryPin < 255) ? cfg.primaryPin : (2 + idx);
      pinMode(pin, INPUT_PULLUP);
      int level = digitalRead(pin);
      
      // Determine if switch is configured as normally-closed
      bool isNormallyClosed = (strcmp(cfg.digitalSwitchMode, "NC") == 0);
      
      // For NO switches: LOW = activated (switch closed, fluid present)
      // For NC switches: HIGH = activated (switch opened, fluid present)
      bool isActivated;
      if (isNormallyClosed) {
        isActivated = (level == HIGH); // NC switch opens (goes HIGH) when activated
      } else {
        isActivated = (level == LOW);  // NO switch closes (goes LOW) when activated
      }
      
      return isActivated ? DIGITAL_SENSOR_ACTIVATED_VALUE : DIGITAL_SENSOR_NOT_ACTIVATED_VALUE;
    }
    case SENSOR_ANALOG: {
      // Analog voltage sensor (e.g., Dwyer 626 with 0-10V, 1-5V, 0-5V output)
      // Uses pressure-to-height conversion based on sensor native range
      // 
      // Configuration:
      // - analogVoltageMin/Max: Voltage output range (e.g., 0-10V, 1-5V)
      // - sensorRangeMin/Max: Pressure range in sensorRangeUnit (e.g., 0-5 PSI)
      // - sensorMountHeight: Height of sensor above tank bottom (inches)
      
      // Use explicit bounds check for channel (A0602 has channels 0-7)
      int channel = (cfg.primaryPin >= 0 && cfg.primaryPin < 8) ? cfg.primaryPin : 0;
      
      // Validate that we have a valid sensor range configured
      if (cfg.sensorRangeMax <= cfg.sensorRangeMin || cfg.analogVoltageMax <= cfg.analogVoltageMin) {
        return 0.0f; // Invalid configuration
      }
      
      // Read voltage (Opta A0602 analog inputs: 0-10V mapped to 0-4095)
      float total = 0.0f;
      const uint8_t samples = 8;
      for (uint8_t i = 0; i < samples; ++i) {
        int raw = analogRead(channel);
        total += (float)raw / 4095.0f * 10.0f; // Convert to 0-10V
        delay(2);
      }
      float voltage = total / samples;
      
      // Map voltage to sensor's native pressure units
      float pressure = linearMap(voltage, cfg.analogVoltageMin, cfg.analogVoltageMax,
                                 cfg.sensorRangeMin, cfg.sensorRangeMax);
      
      // Convert pressure to liquid height in inches using appropriate conversion factor
      float conversionFactor = getPressureConversionFactor(cfg.sensorRangeUnit);
      float liquidAboveSensor = pressure * conversionFactor;
      
      // Total height from tank bottom = liquid above sensor + sensor mount height
      float levelInches = liquidAboveSensor + cfg.sensorMountHeight;
      
      // Clamp: minimum is 0 (empty tank)
      if (levelInches < 0.0f) levelInches = 0.0f;
      
      return levelInches;
    }
    case SENSOR_CURRENT_LOOP: {
      // Use explicit bounds check for current loop channel
      int16_t channel = (cfg.currentLoopChannel >= 0 && cfg.currentLoopChannel < 8) ? cfg.currentLoopChannel : 0;
      // Validate that we have a valid sensor range configured
      if (cfg.sensorRangeMax <= cfg.sensorRangeMin) {
        gTankState[idx].currentSensorMa = 0.0f;
        return 0.0f;
      }
      float milliamps = readCurrentLoopMilliamps(channel);
      if (milliamps < 0.0f) {
        return gTankState[idx].currentInches; // keep previous on failure
      }
      
      // Store raw mA reading for telemetry
      gTankState[idx].currentSensorMa = milliamps;
      
      // Handle different 4-20mA sensor types using native sensor range
      float levelInches;
      if (cfg.currentLoopType == CURRENT_LOOP_ULTRASONIC) {
        // Ultrasonic sensor mounted on TOP of tank (e.g., Siemens Sitrans LU240)
        // 4mA = minimum distance (sensorRangeMin), 20mA = maximum distance (sensorRangeMax)
        // sensorMountHeight = distance from sensor to tank bottom when empty
        // The sensor measures distance from sensor to liquid surface in native units
        
        // Convert mA to sensor's native distance units
        float distanceNative = linearMap(milliamps, 4.0f, 20.0f, 
                                         cfg.sensorRangeMin, cfg.sensorRangeMax);
        
        // Convert distance to inches based on sensorRangeUnit
        float distanceInches = distanceNative * getDistanceConversionFactor(cfg.sensorRangeUnit);
        
        // Calculate liquid level: tank height - distance from sensor to surface
        levelInches = cfg.sensorMountHeight - distanceInches;
        
        // Clamp to valid range (0 minimum)
        if (levelInches < 0.0f) levelInches = 0.0f;
      } else {
        // Pressure sensor mounted near BOTTOM of tank (e.g., Dwyer 626-06-CB-P1-E5-S1)
        // 4mA = sensorRangeMin (e.g., 0 PSI), 20mA = sensorRangeMax (e.g., 5 PSI)
        // sensorMountHeight = height of sensor above tank bottom (usually 0-2 inches)
        // The sensor measures liquid pressure above the sensor
        
        // Convert mA to sensor's native pressure units
        float pressure = linearMap(milliamps, 4.0f, 20.0f,
                                   cfg.sensorRangeMin, cfg.sensorRangeMax);
        
        // Convert pressure to liquid height in inches using appropriate conversion factor
        float conversionFactor = getPressureConversionFactor(cfg.sensorRangeUnit);
        float liquidAboveSensor = pressure * conversionFactor;
        
        // Total height from tank bottom = liquid above sensor + sensor mount height
        levelInches = liquidAboveSensor + cfg.sensorMountHeight;
        
        // Clamp: minimum is 0 (empty tank)
        if (levelInches < 0.0f) levelInches = 0.0f;
      }
      return levelInches;
    }
    case SENSOR_HALL_EFFECT_RPM: {
      // Hall effect RPM sensor - supports multiple sensor types and detection methods
      // Use rpmPin if available, otherwise use primaryPin
      int pin = (cfg.rpmPin >= 0 && cfg.rpmPin < 255) ? cfg.rpmPin : 
                ((cfg.primaryPin >= 0 && cfg.primaryPin < 255) ? cfg.primaryPin : (2 + idx));
      
      // Configure pin as input with pullup for digital Hall effect sensors
      // Analog sensors would typically use analog input, but we support digital threshold mode here
      pinMode(pin, INPUT_PULLUP);
      
      // Validate pulses per revolution (common validation for both methods)
      uint8_t pulsesPerRev = (cfg.pulsesPerRevolution > 0) ? cfg.pulsesPerRevolution : 1;
      const float MS_PER_MINUTE = 60000.0f;
      
      // Common constants for both detection methods
      const unsigned long DEBOUNCE_MS = 2;
      const uint32_t MAX_ITERATIONS = RPM_SAMPLE_DURATION_MS * 2;
      
      float rpm = 0.0f;
      
      // Choose detection method: pulse counting or time-based
      if (cfg.hallEffectDetection == HALL_DETECT_TIME_BASED) {
        // Time-based detection: measure period between pulses
        // More flexible for different magnet types and orientations
        // Requires fewer pulses to get a reading
        
        unsigned long sampleStart = millis();
        int lastState = digitalRead(pin);
        gRpmLastPinState[idx] = lastState;
        unsigned long firstPulseTime = 0;
        unsigned long secondPulseTime = 0;
        unsigned long cycleLastPulseTime = 0; // Track last pulse within this measurement cycle for debounce
        uint32_t iterationCount = 0;
        bool firstPulseDetected = false;
        bool secondPulseDetected = false;
        
        // Detect edge transitions based on sensor type
        while ((millis() - sampleStart) < (unsigned long)RPM_SAMPLE_DURATION_MS && iterationCount < MAX_ITERATIONS) {
          int currentState = digitalRead(pin);
          bool edgeDetected = false;
          
          // Determine edge detection based on hall effect sensor type
          switch (cfg.hallEffectType) {
            case HALL_EFFECT_UNIPOLAR:
            case HALL_EFFECT_ANALOG:
              // Unipolar and Analog: triggers on one pole (active low), detect falling edge
              edgeDetected = (lastState == HIGH && currentState == LOW);
              break;
            case HALL_EFFECT_BIPOLAR:
            case HALL_EFFECT_OMNIPOLAR:
              // Bipolar/Latching and Omnipolar: detect both edges (state changes)
              edgeDetected = (lastState != currentState);
              break;
            default:
              // Default to unipolar behavior if value is invalid/corrupted
              edgeDetected = (lastState == HIGH && currentState == LOW);
              break;
          }
          
          if (edgeDetected) {
            unsigned long now = millis();
            // Debounce using cycle-local tracking within this measurement
            if (now - cycleLastPulseTime >= DEBOUNCE_MS) {
              cycleLastPulseTime = now;
              if (!firstPulseDetected) {
                firstPulseTime = now;
                firstPulseDetected = true;
              } else if (!secondPulseDetected) {
                secondPulseTime = now;
                gRpmPulsePeriodMs[idx] = secondPulseTime - firstPulseTime;
                gRpmLastPulseTime[idx] = secondPulseTime;
                secondPulseDetected = true;
                break; // Got our measurement, exit early
              }
            }
          }
          lastState = currentState;
          delay(1);
          iterationCount++;
          
#ifdef WATCHDOG_AVAILABLE
          #if defined(ARDUINO_OPTA) || defined(ARDUINO_ARCH_MBED)
            mbedWatchdog.kick();
          #else
            IWatchdog.reload();
          #endif
#endif
        }
        
        gRpmLastPinState[idx] = lastState;
        gRpmLastSampleMillis[idx] = millis();
        
        // Calculate RPM from pulse period
        if (secondPulseDetected && gRpmPulsePeriodMs[idx] > 0) {
          // Got a valid measurement this cycle
          // RPM = (60000 ms/min) / (period_ms * pulses_per_rev)
          rpm = MS_PER_MINUTE / ((float)gRpmPulsePeriodMs[idx] * (float)pulsesPerRev);
        } else if (firstPulseDetected && !secondPulseDetected) {
          // Only one pulse detected - RPM is very low or stopped
          // If we didn't get a second pulse within the sample duration, 
          // RPM is below: 60000ms / (RPM_SAMPLE_DURATION_MS * pulsesPerRev)
          // For 3s sampling with 1 pulse/rev: < 20 RPM
          // For 3s sampling with 4 pulses/rev: < 5 RPM
          // Keep last reading to avoid false zero during temporary signal loss
          rpm = gRpmLastReading[idx];
        } else {
          // No pulses detected, keep last reading
          rpm = gRpmLastReading[idx];
        }
        
      } else {
        // Pulse counting method (traditional approach)
        // Sample pulses for RPM_SAMPLE_DURATION_MS (default 3 seconds)
        // This provides accurate RPM measurement by counting multiple pulses
        
        unsigned long sampleStart = millis();
        uint32_t pulseCount = 0;
        
        // Always read current pin state first to establish baseline
        int lastState = digitalRead(pin);
        gRpmLastPinState[idx] = lastState;
        
        unsigned long lastPulseTime = 0;
        uint32_t iterationCount = 0;
        
        while ((millis() - sampleStart) < (unsigned long)RPM_SAMPLE_DURATION_MS && iterationCount < MAX_ITERATIONS) {
          int currentState = digitalRead(pin);
          bool edgeDetected = false;
          
          // Determine edge detection based on hall effect sensor type
          switch (cfg.hallEffectType) {
            case HALL_EFFECT_UNIPOLAR:
            case HALL_EFFECT_ANALOG:
              // Unipolar and Analog: triggers on one pole, detect falling edge (active low)
              edgeDetected = (lastState == HIGH && currentState == LOW);
              break;
            case HALL_EFFECT_BIPOLAR:
            case HALL_EFFECT_OMNIPOLAR:
              // Bipolar/Latching and Omnipolar: count both edges (state changes)
              edgeDetected = (lastState != currentState);
              break;
            default:
              // Default to unipolar behavior for safety and consistency
              edgeDetected = (lastState == HIGH && currentState == LOW);
              break;
          }
          
          if (edgeDetected) {
            unsigned long now = millis();
            if (now - lastPulseTime >= DEBOUNCE_MS) {
              pulseCount++;
              lastPulseTime = now;
            }
          }
          lastState = currentState;
          delay(1);
          iterationCount++;
          
#ifdef WATCHDOG_AVAILABLE
          #if defined(ARDUINO_OPTA) || defined(ARDUINO_ARCH_MBED)
            mbedWatchdog.kick();
          #else
            IWatchdog.reload();
          #endif
#endif
        }
        
        gRpmLastPinState[idx] = lastState;
        gRpmLastSampleMillis[idx] = millis();
        
        // Calculate RPM from pulse count (using pre-validated pulsesPerRev)
        rpm = ((float)pulseCount * MS_PER_MINUTE) / ((float)RPM_SAMPLE_DURATION_MS * (float)pulsesPerRev);
      }
      
      gRpmLastReading[idx] = rpm;
      
      // Return RPM value (use highAlarmThreshold for max expected RPM)
      return rpm;
    }
    default:
      return 0.0f;
  }
}

static void sampleTanks() {
  for (uint8_t i = 0; i < gConfig.tankCount; ++i) {
    float inches = readTankSensor(i);
    
    // Validate sensor reading
    if (!validateSensorReading(i, inches)) {
      // Keep previous valid reading if sensor failed
      inches = gTankState[i].currentInches;
    } else {
      gTankState[i].currentInches = inches;
    }
    
    evaluateAlarms(i);

    if (gConfig.tanks[i].enableServerUpload && !gTankState[i].sensorFailed) {
      const float threshold = gConfig.minLevelChangeInches;
      const bool needBaseline = (gTankState[i].lastReportedInches < 0.0f);
      const bool thresholdEnabled = (threshold > 0.0f);
      const bool changeExceeded = thresholdEnabled && (fabs(inches - gTankState[i].lastReportedInches) >= threshold);
      if (needBaseline || changeExceeded) {
        sendTelemetry(i, "sample", false);
        gTankState[i].lastReportedInches = inches;
      }
    }
  }
}

static void evaluateAlarms(uint8_t idx) {
  const TankConfig &cfg = gConfig.tanks[idx];
  TankRuntime &state = gTankState[idx];

  // Skip alarm evaluation if sensor has failed
  if (state.sensorFailed) {
    return;
  }

  // Handle digital sensors (float switches) differently
  if (cfg.sensorType == SENSOR_DIGITAL) {
    // For digital sensors, currentInches is either DIGITAL_SENSOR_ACTIVATED_VALUE (1.0) or DIGITAL_SENSOR_NOT_ACTIVATED_VALUE (0.0)
    bool isActivated = (state.currentInches > DIGITAL_SWITCH_THRESHOLD);
    bool shouldAlarm = false;
    bool triggerOnActivated = true;  // Track what condition triggers the alarm
    
    // Determine if we should alarm based on trigger configuration
    if (cfg.digitalTrigger[0] != '\0') {
      if (strcmp(cfg.digitalTrigger, "activated") == 0) {
        shouldAlarm = isActivated;  // Alarm when switch is activated
        triggerOnActivated = true;
      } else if (strcmp(cfg.digitalTrigger, "not_activated") == 0) {
        shouldAlarm = !isActivated;  // Alarm when switch is NOT activated
        triggerOnActivated = false;
      }
    } else {
      // Legacy behavior: use highAlarm/lowAlarm thresholds
      // Only one of these should be configured for a digital sensor
      // highAlarm = 1 means trigger when reading is 1.0 (switch activated)
      // lowAlarm = 0 means trigger when reading is 0.0 (switch not activated)
      bool hasHighAlarm = (cfg.highAlarmThreshold >= DIGITAL_SENSOR_ACTIVATED_VALUE);
      bool hasLowAlarm = (cfg.lowAlarmThreshold == DIGITAL_SENSOR_NOT_ACTIVATED_VALUE);
      
      if (hasHighAlarm && !hasLowAlarm) {
        shouldAlarm = isActivated;
        triggerOnActivated = true;
      } else if (hasLowAlarm && !hasHighAlarm) {
        shouldAlarm = !isActivated;
        triggerOnActivated = false;
      } else if (hasHighAlarm) {
        // Default to high alarm behavior if both are set
        shouldAlarm = isActivated;
        triggerOnActivated = true;
      }
    }
    
    // Handle alarm state with debouncing
    if (shouldAlarm && !state.highAlarmLatched) {
      state.highAlarmDebounceCount++;
      state.clearDebounceCount = 0;
      if (state.highAlarmDebounceCount >= ALARM_DEBOUNCE_COUNT) {
        state.highAlarmLatched = true;
        state.highAlarmDebounceCount = 0;
        // Send alarm with descriptive type based on configured trigger condition
        const char *alarmType = triggerOnActivated ? "triggered" : "not_triggered";
        sendAlarm(idx, alarmType, state.currentInches);
      }
    } else if (!shouldAlarm && state.highAlarmLatched) {
      state.clearDebounceCount++;
      state.highAlarmDebounceCount = 0;
      if (state.clearDebounceCount >= ALARM_DEBOUNCE_COUNT) {
        state.highAlarmLatched = false;
        state.clearDebounceCount = 0;
        sendAlarm(idx, "clear", state.currentInches);
      }
    } else if (!shouldAlarm) {
      state.highAlarmDebounceCount = 0;
    } else {
      state.clearDebounceCount = 0;
    }
    return;  // Skip the standard analog threshold evaluation
  }

  // Standard analog/current loop sensor alarm evaluation with hysteresis
  float highTrigger = cfg.highAlarmThreshold;
  float highClear = cfg.highAlarmThreshold - cfg.hysteresisValue;
  float lowTrigger = cfg.lowAlarmThreshold;
  float lowClear = cfg.lowAlarmThreshold + cfg.hysteresisValue;

  bool highCondition = state.currentInches >= highTrigger;
  bool lowCondition = state.currentInches <= lowTrigger;
  bool clearCondition = (state.currentInches < highClear) && (state.currentInches > lowClear);

  // Handle high alarm with debouncing
  if (highCondition && !state.highAlarmLatched) {
    state.highAlarmDebounceCount++;
    state.lowAlarmDebounceCount = 0;
    state.clearDebounceCount = 0;
    if (state.highAlarmDebounceCount >= ALARM_DEBOUNCE_COUNT) {
      state.highAlarmLatched = true;
      state.lowAlarmLatched = false;
      state.highAlarmDebounceCount = 0;
      sendAlarm(idx, "high", state.currentInches);
    }
  } else if (state.highAlarmLatched && clearCondition) {
    state.clearDebounceCount++;
    state.highAlarmDebounceCount = 0;
    state.lowAlarmDebounceCount = 0;
    if (state.clearDebounceCount >= ALARM_DEBOUNCE_COUNT) {
      state.highAlarmLatched = false;
      state.clearDebounceCount = 0;
      sendAlarm(idx, "clear", state.currentInches);
    }
  } else if (!highCondition && !clearCondition) {
    state.highAlarmDebounceCount = 0;
  }

  // Handle low alarm with debouncing
  if (lowCondition && !state.lowAlarmLatched) {
    state.lowAlarmDebounceCount++;
    state.highAlarmDebounceCount = 0;
    state.clearDebounceCount = 0;
    if (state.lowAlarmDebounceCount >= ALARM_DEBOUNCE_COUNT) {
      state.lowAlarmLatched = true;
      state.highAlarmLatched = false;
      state.lowAlarmDebounceCount = 0;
      sendAlarm(idx, "low", state.currentInches);
    }
  } else if (state.lowAlarmLatched && clearCondition) {
    state.clearDebounceCount++;
    state.highAlarmDebounceCount = 0;
    state.lowAlarmDebounceCount = 0;
    if (state.clearDebounceCount >= ALARM_DEBOUNCE_COUNT) {
      state.lowAlarmLatched = false;
      state.clearDebounceCount = 0;
      sendAlarm(idx, "clear", state.currentInches);
    }
  } else if (!lowCondition && !clearCondition) {
    state.lowAlarmDebounceCount = 0;
  }

  // Reset clear counter if we're back in alarm territory
  if (highCondition || lowCondition) {
    state.clearDebounceCount = 0;
  }
}

static void sendTelemetry(uint8_t idx, const char *reason, bool syncNow) {
  if (idx >= gConfig.tankCount) {
    return;
  }

  const TankConfig &cfg = gConfig.tanks[idx];
  TankRuntime &state = gTankState[idx];

  DynamicJsonDocument doc(768);
  doc["client"] = gDeviceUID;
  doc["site"] = gConfig.siteName;
  doc["label"] = cfg.name;
  doc["tank"] = cfg.tankNumber;
  doc["id"] = String(cfg.id);
  
  // Handle digital sensors differently in telemetry
  if (cfg.sensorType == SENSOR_DIGITAL) {
    doc["sensorType"] = "digital";
    bool activated = (state.currentInches > DIGITAL_SWITCH_THRESHOLD);
    doc["activated"] = activated;  // Boolean state: true = switch activated
    doc["levelInches"] = state.currentInches;  // 1.0 or 0.0
  } else if (cfg.sensorType == SENSOR_CURRENT_LOOP) {
    doc["sensorType"] = "currentLoop";
    doc["levelInches"] = state.currentInches;
    doc["sensorMa"] = state.currentSensorMa;  // Raw 4-20mA reading
  } else {
    doc["levelInches"] = state.currentInches;
  }
  doc["reason"] = reason;
  doc["time"] = currentEpoch();

  publishNote(TELEMETRY_FILE, doc, syncNow);
}

static bool checkAlarmRateLimit(uint8_t idx, const char *alarmType) {
  if (idx >= gConfig.tankCount) {
    return false;
  }

  TankRuntime &state = gTankState[idx];
  unsigned long now = millis();

  // Check minimum interval between same alarm type
  unsigned long minInterval = MIN_ALARM_INTERVAL_SECONDS * 1000UL;
  
  if (strcmp(alarmType, "high") == 0) {
    if (now - state.lastHighAlarmMillis < minInterval) {
      Serial.print(F("Rate limit: High alarm suppressed for tank "));
      Serial.println(idx);
      return false;
    }
  } else if (strcmp(alarmType, "low") == 0) {
    if (now - state.lastLowAlarmMillis < minInterval) {
      Serial.print(F("Rate limit: Low alarm suppressed for tank "));
      Serial.println(idx);
      return false;
    }
  } else if (strcmp(alarmType, "clear") == 0) {
    if (now - state.lastClearAlarmMillis < minInterval) {
      Serial.print(F("Rate limit: Clear alarm suppressed for tank "));
      Serial.println(idx);
      return false;
    }
  } else if (strcmp(alarmType, "sensor-fault") == 0 || strcmp(alarmType, "sensor-stuck") == 0) {
    if (now - state.lastSensorFaultMillis < minInterval) {
      Serial.print(F("Rate limit: Sensor fault suppressed for tank "));
      Serial.println(idx);
      return false;
    }
  }

  // Check hourly rate limit - remove timestamps older than 1 hour
  unsigned long oneHourAgo = now - 3600000UL;
  uint8_t validCount = 0;
  for (uint8_t i = 0; i < state.alarmCount; ++i) {
    if (state.alarmTimestamps[i] > oneHourAgo) {
      state.alarmTimestamps[validCount++] = state.alarmTimestamps[i];
    }
  }
  state.alarmCount = validCount;

  // Check if we've exceeded the hourly limit
  if (state.alarmCount >= MAX_ALARMS_PER_HOUR) {
    Serial.print(F("Rate limit: Hourly limit exceeded for tank "));
    Serial.print(idx);
    Serial.print(F(" ("));
    Serial.print(state.alarmCount);
    Serial.print(F("/"));
    Serial.print(MAX_ALARMS_PER_HOUR);
    Serial.println(F(")"));
    return false;
  }

  // Add current timestamp
  if (state.alarmCount < MAX_ALARMS_PER_HOUR) {
    state.alarmTimestamps[state.alarmCount++] = now;
  }

  // Update last alarm time for this type
  if (strcmp(alarmType, "high") == 0) {
    state.lastHighAlarmMillis = now;
  } else if (strcmp(alarmType, "low") == 0) {
    state.lastLowAlarmMillis = now;
  } else if (strcmp(alarmType, "clear") == 0) {
    state.lastClearAlarmMillis = now;
  } else if (strcmp(alarmType, "sensor-fault") == 0 || strcmp(alarmType, "sensor-stuck") == 0) {
    state.lastSensorFaultMillis = now;
  }

  return true;
}

static void activateLocalAlarm(uint8_t idx, bool active) {
  // Use Opta's built-in relay outputs for local alarm indication
  // Opta has 4 relay outputs - map tanks to relays (tank 0->relay 0, etc.)
  int relayPin = getRelayPin(idx);
  if (relayPin >= 0) {
    pinMode(relayPin, OUTPUT);
    digitalWrite(relayPin, active ? HIGH : LOW);
  }
  
  if (active) {
    Serial.print(F("LOCAL ALARM ACTIVE - Tank "));
    Serial.println(idx);
  } else {
    Serial.print(F("LOCAL ALARM CLEARED - Tank "));
    Serial.println(idx);
  }
}

static void sendAlarm(uint8_t idx, const char *alarmType, float inches) {
  if (idx >= gConfig.tankCount) {
    return;
  }

  const TankConfig &cfg = gConfig.tanks[idx];
  bool allowSmsEscalation = cfg.enableAlarmSms;

  // Always activate local alarm regardless of rate limits
  bool isAlarm = (strcmp(alarmType, "clear") != 0);
  activateLocalAlarm(idx, isAlarm);

  // Check rate limit before sending remote alarm
  if (!checkAlarmRateLimit(idx, alarmType)) {
    return;  // Rate limit exceeded
  }

  TankRuntime &state = gTankState[idx];
  state.lastAlarmSendMillis = millis();

  // Try to send via network if available
  if (gNotecardAvailable) {
    DynamicJsonDocument doc(768);
    doc["client"] = gDeviceUID;
    doc["site"] = gConfig.siteName;
    doc["label"] = cfg.name;
    doc["tank"] = cfg.tankNumber;
    doc["type"] = alarmType;
    doc["levelInches"] = inches;
    doc["highThreshold"] = cfg.highAlarmThreshold;
    doc["lowThreshold"] = cfg.lowAlarmThreshold;
    doc["smsEnabled"] = allowSmsEscalation;
    doc["time"] = currentEpoch();

    publishNote(ALARM_FILE, doc, true);
    Serial.print(F("Alarm sent for tank "));
    Serial.print(cfg.name);
    Serial.print(F(" type "));
    Serial.println(alarmType);
    
    char logMsg[128];
    snprintf(logMsg, sizeof(logMsg), "Alarm: %s - %s - %.1fin", cfg.name, alarmType, inches);
    addSerialLog(logMsg);
    
    // Handle relay control based on mode
    if (cfg.relayTargetClient[0] != '\0' && cfg.relayMask != 0) {
      bool shouldActivateRelay = false;
      bool shouldDeactivateRelay = false;
      
      // Check if this alarm type matches the relay trigger condition
      if (isAlarm) {
        if (cfg.relayTrigger == RELAY_TRIGGER_ANY) {
          shouldActivateRelay = true;
        } else if (cfg.relayTrigger == RELAY_TRIGGER_HIGH && strcmp(alarmType, "high") == 0) {
          shouldActivateRelay = true;
        } else if (cfg.relayTrigger == RELAY_TRIGGER_LOW && strcmp(alarmType, "low") == 0) {
          shouldActivateRelay = true;
        }
      } else {
        // Alarm cleared - check if we should deactivate relay
        // Only deactivate if mode is UNTIL_CLEAR and the clearing alarm matches trigger
        if (cfg.relayMode == RELAY_MODE_UNTIL_CLEAR && gRelayActiveForTank[idx]) {
          bool shouldClear = false;
          if (cfg.relayTrigger == RELAY_TRIGGER_ANY) {
            shouldClear = true; // Any alarm clearing will deactivate
          } else if (cfg.relayTrigger == RELAY_TRIGGER_HIGH && strcmp(alarmType, "high") == 0) {
            shouldClear = true;
          } else if (cfg.relayTrigger == RELAY_TRIGGER_LOW && strcmp(alarmType, "low") == 0) {
            shouldClear = true;
          }
          if (shouldClear) {
            shouldDeactivateRelay = true;
          }
        }
      }
      
      if (shouldActivateRelay && !gRelayActiveForTank[idx]) {
        triggerRemoteRelays(cfg.relayTargetClient, cfg.relayMask, true);
        gRelayActiveForTank[idx] = true;
        gRelayActivationTime[idx] = millis();
        Serial.print(F("Relay activated for "));
        Serial.print(alarmType);
        Serial.print(F(" alarm (mode: "));
        switch (cfg.relayMode) {
          case RELAY_MODE_MOMENTARY: Serial.print(F("momentary 30min")); break;
          case RELAY_MODE_UNTIL_CLEAR: Serial.print(F("until clear")); break;
          case RELAY_MODE_MANUAL_RESET: Serial.print(F("manual reset")); break;
        }
        Serial.println(F(")"));
      } else if (shouldDeactivateRelay) {
        triggerRemoteRelays(cfg.relayTargetClient, cfg.relayMask, false);
        gRelayActiveForTank[idx] = false;
        gRelayActivationTime[idx] = 0;
        Serial.println(F("Relay deactivated on alarm clear"));
      }
    }
  } else {
    Serial.print(F("Network offline - local alarm only for tank "));
    Serial.print(cfg.name);
    Serial.print(F(" type "));
    Serial.println(alarmType);
  }
}

static void sendDailyReport() {
  uint8_t eligibleIndices[MAX_TANKS];
  uint8_t eligibleCount = 0;
  for (uint8_t i = 0; i < gConfig.tankCount; ++i) {
    if (gConfig.tanks[i].enableDailyReport) {
      eligibleIndices[eligibleCount++] = i;
    }
  }

  if (eligibleCount == 0) {
    return;
  }

  double reportEpoch = currentEpoch();
  size_t tankCursor = 0;
  uint8_t part = 0;
  bool queuedAny = false;

  // Read VIN voltage once for the daily report
  float vinVoltage = readNotecardVinVoltage();

  while (tankCursor < eligibleCount) {
    DynamicJsonDocument doc(1024);
    doc["client"] = gDeviceUID;
    doc["site"] = gConfig.siteName;
    doc["email"] = gConfig.dailyEmail;
    doc["time"] = reportEpoch;
    doc["part"] = static_cast<uint8_t>(part + 1);

    // Include VIN voltage in the first part of the daily report
    if (part == 0 && vinVoltage > 0.0f) {
      doc["vinVoltage"] = vinVoltage;
    }

    JsonArray tanks = doc.createNestedArray("tanks");
    bool addedTank = false;

    while (tankCursor < eligibleCount) {
      uint8_t tankIndex = eligibleIndices[tankCursor];
      if (appendDailyTank(doc, tanks, tankIndex, DAILY_NOTE_PAYLOAD_LIMIT)) {
        ++tankCursor;
        addedTank = true;
      } else {
        if (!addedTank) {
          // Allow a single large entry with minimal headroom so it still publishes.
          if (appendDailyTank(doc, tanks, tankIndex, DAILY_NOTE_PAYLOAD_LIMIT + 48U)) {
            ++tankCursor;
            addedTank = true;
          } else {
            Serial.println(F("Daily report entry skipped; payload still exceeds limit"));
            ++tankCursor;
          }
        }
        break;
      }
    }

    if (!addedTank) {
      continue;
    }

    doc["more"] = (tankCursor < eligibleCount);
    bool syncNow = (tankCursor >= eligibleCount);
    publishNote(DAILY_FILE, doc, syncNow);
    queuedAny = true;
    ++part;
  }

  if (queuedAny) {
    Serial.println(F("Daily report queued"));
  }
}

static bool appendDailyTank(DynamicJsonDocument &doc, JsonArray &array, uint8_t tankIndex, size_t payloadLimit) {
  if (tankIndex >= gConfig.tankCount) {
    return false;
  }

  const TankConfig &cfg = gConfig.tanks[tankIndex];
  TankRuntime &state = gTankState[tankIndex];

  JsonObject t = array.createNestedObject();
  t["label"] = cfg.name;
  t["tank"] = cfg.tankNumber;
  t["levelInches"] = state.currentInches;
  // Include raw sensor mA for current loop sensors
  if (cfg.sensorType == SENSOR_CURRENT_LOOP && state.currentSensorMa >= 4.0f) {
    t["sensorMa"] = state.currentSensorMa;
  }
  t["high"] = cfg.highAlarmThreshold;
  t["low"] = cfg.lowAlarmThreshold;

  if (measureJson(doc) > payloadLimit) {
    size_t currentSize = array.size();
    if (currentSize > 0) {
      array.remove(currentSize - 1);
    }
    return false;
  }

  state.lastDailySentInches = state.currentInches;
  return true;
}

static void publishNote(const char *fileName, const JsonDocument &doc, bool syncNow) {
  // Build target file string and serialized payload once for both live send and buffering
  char targetFile[80];
  const char *fleetName = (strlen(gConfig.serverFleet) > 0) ? gConfig.serverFleet : "tankalarm-server";
  snprintf(targetFile, sizeof(targetFile), "fleet:%s:%s", fleetName, fileName);

  char buffer[1024];
  size_t len = serializeJson(doc, buffer, sizeof(buffer));
  if (len == 0 || len >= sizeof(buffer)) {
    return;
  }
  buffer[len] = '\0';

  if (!gNotecardAvailable) {
    bufferNoteForRetry(targetFile, buffer, syncNow);
    return;
  }

  J *req = notecard.newRequest("note.add");
  if (!req) {
    gNotecardFailureCount++;
    bufferNoteForRetry(targetFile, buffer, syncNow);
    return;
  }

  JAddStringToObject(req, "file", targetFile);
  if (syncNow) {
    JAddBoolToObject(req, "sync", true);
  }

  J *body = JParse(buffer);
  if (!body) {
    JDelete(req);
    bufferNoteForRetry(targetFile, buffer, syncNow);
    return;
  }

  JAddItemToObject(req, "body", body);
  bool success = notecard.sendRequest(req);
  if (success) {
    gLastSuccessfulNotecardComm = millis();
    gNotecardFailureCount = 0;
    flushBufferedNotes();
  } else {
    gNotecardFailureCount++;
    bufferNoteForRetry(targetFile, buffer, syncNow);
  }
}

static void bufferNoteForRetry(const char *fileName, const char *payload, bool syncNow) {
#ifdef FILESYSTEM_AVAILABLE
  #if defined(ARDUINO_OPTA) || defined(ARDUINO_ARCH_MBED)
    if (!mbedFS) {
      Serial.println(F("Warning: Filesystem not available; note dropped"));
      return;
    }
    FILE *file = fopen("/fs/pending_notes.log", "a");
    if (!file) {
      Serial.println(F("Failed to open note buffer; dropping payload"));
      return;
    }
    fprintf(file, "%s\t%c\t%s\n", fileName, syncNow ? '1' : '0', payload);
    fclose(file);
  #else
    File file = LittleFS.open(NOTE_BUFFER_PATH, "a");
    if (!file) {
      Serial.println(F("Failed to open note buffer; dropping payload"));
      return;
    }
    file.print(fileName);
    file.print('\t');
    file.print(syncNow ? '1' : '0');
    file.print('\t');
    file.println(payload);
    file.close();
  #endif
  Serial.println(F("Note buffered for retry"));
  pruneNoteBufferIfNeeded();
#else
  Serial.println(F("Warning: Filesystem not available; note dropped"));
#endif
}

static void flushBufferedNotes() {
#ifdef FILESYSTEM_AVAILABLE
  if (!gNotecardAvailable) {
    return;
  }
  #if defined(ARDUINO_OPTA) || defined(ARDUINO_ARCH_MBED)
    if (!mbedFS) {
      return;
    }
    
    FILE *src = fopen("/fs/pending_notes.log", "r");
    if (!src) {
      return;
    }
    
    FILE *tmp = fopen("/fs/pending_notes.tmp", "w");
    if (!tmp) {
      fclose(src);
      return;
    }
    
    bool wroteFailures = false;
    char lineBuffer[1024];  // Larger buffer to accommodate payload data
    while (fgets(lineBuffer, sizeof(lineBuffer), src) != nullptr) {
      // Check if line was truncated (no newline at end of non-empty buffer)
      size_t len = strlen(lineBuffer);
      if (len == sizeof(lineBuffer) - 1 && lineBuffer[len - 1] != '\n') {
        #ifdef DEBUG_MODE
        Serial.println(F("Warning: truncated line in note buffer, skipping"));
        #endif
        // Skip rest of the truncated line
        int ch;
        while ((ch = fgetc(src)) != EOF && ch != '\n') {}
        continue;
      }
      String line = String(lineBuffer);
      line.trim();
      if (line.length() == 0) {
        continue;
      }

      int firstTab = line.indexOf('\t');
      int secondTab = (firstTab >= 0) ? line.indexOf('\t', firstTab + 1) : -1;
      if (firstTab < 0 || secondTab < 0) {
        continue;
      }

      String fileName = line.substring(0, firstTab);
      String syncToken = line.substring(firstTab + 1, secondTab);
      bool syncNow = (syncToken == "1");
      String payload = line.substring(secondTab + 1);

      J *req = notecard.newRequest("note.add");
      if (!req) {
        wroteFailures = true;
        fprintf(tmp, "%s\n", line.c_str());
        continue;
      }
      JAddStringToObject(req, "file", fileName.c_str());
      if (syncNow) {
        JAddBoolToObject(req, "sync", true);
      }

      J *body = JParse(payload.c_str());
      if (!body) {
        JDelete(req);
        continue;
      }
      JAddItemToObject(req, "body", body);

      if (!notecard.sendRequest(req)) {
        wroteFailures = true;
        fprintf(tmp, "%s\n", line.c_str());
      }
    }
    
    fclose(src);
    fclose(tmp);
    
    if (wroteFailures) {
      remove("/fs/pending_notes.log");
      rename("/fs/pending_notes.tmp", "/fs/pending_notes.log");
    } else {
      remove("/fs/pending_notes.log");
      remove("/fs/pending_notes.tmp");
    }
  #else
    if (!LittleFS.exists(NOTE_BUFFER_PATH)) {
      return;
    }

    File src = LittleFS.open(NOTE_BUFFER_PATH, "r");
    if (!src) {
      return;
    }

    File tmp = LittleFS.open(NOTE_BUFFER_TEMP_PATH, "w");
    if (!tmp) {
      src.close();
      return;
    }

    bool wroteFailures = false;
    while (src.available()) {
      String line = src.readStringUntil('\n');
      line.trim();
      if (line.length() == 0) {
        continue;
      }

      int firstTab = line.indexOf('\t');
      int secondTab = (firstTab >= 0) ? line.indexOf('\t', firstTab + 1) : -1;
      if (firstTab < 0 || secondTab < 0) {
        continue;
      }

      String fileName = line.substring(0, firstTab);
      String syncToken = line.substring(firstTab + 1, secondTab);
      bool syncNow = (syncToken == "1");
      String payload = line.substring(secondTab + 1);

      J *req = notecard.newRequest("note.add");
      if (!req) {
        wroteFailures = true;
        tmp.println(line);
        continue;
      }
      JAddStringToObject(req, "file", fileName.c_str());
      if (syncNow) {
        JAddBoolToObject(req, "sync", true);
      }

      J *body = JParse(payload.c_str());
      if (!body) {
        JDelete(req);
        continue;
      }
      JAddItemToObject(req, "body", body);

      if (!notecard.sendRequest(req)) {
        wroteFailures = true;
        tmp.println(line);
      }
    }

    src.close();
    tmp.close();

    if (wroteFailures) {
      LittleFS.remove(NOTE_BUFFER_PATH);
      LittleFS.rename(NOTE_BUFFER_TEMP_PATH, NOTE_BUFFER_PATH);
    } else {
      LittleFS.remove(NOTE_BUFFER_PATH);
      LittleFS.remove(NOTE_BUFFER_TEMP_PATH);
    }
  #endif
#endif
}

static void pruneNoteBufferIfNeeded() {
#ifdef FILESYSTEM_AVAILABLE
  #if defined(ARDUINO_OPTA) || defined(ARDUINO_ARCH_MBED)
    if (!mbedFS) {
      return;
    }
    
    FILE *file = fopen("/fs/pending_notes.log", "r");
    if (!file) {
      return;
    }
    
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    if (size <= NOTE_BUFFER_MAX_BYTES) {
      fclose(file);
      return;
    }
    
    size_t targetSize = NOTE_BUFFER_MAX_BYTES > NOTE_BUFFER_MIN_HEADROOM ? (NOTE_BUFFER_MAX_BYTES - NOTE_BUFFER_MIN_HEADROOM) : (NOTE_BUFFER_MAX_BYTES / 2);
    if (targetSize == 0) {
      targetSize = NOTE_BUFFER_MAX_BYTES / 2;
    }
    long startOffset = (size > (long)targetSize) ? (size - (long)targetSize) : 0;

    if (fseek(file, startOffset, SEEK_SET) != 0) {
      fclose(file);
      remove("/fs/pending_notes.log");
      return;
    }

    // Skip partial line if we seeked into the middle
    if (startOffset > 0) {
      char ch;
      while (fread(&ch, 1, 1, file) == 1 && ch != '\n') {
        // consume until newline
      }
    }

    FILE *tmp = fopen("/fs/pending_notes.tmp", "w");
    if (!tmp) {
      fclose(file);
      return;
    }

    // Use buffered copy for better performance
    char copyBuffer[256];
    size_t bytesRead;
    bool writeError = false;
    while ((bytesRead = fread(copyBuffer, 1, sizeof(copyBuffer), file)) > 0) {
      if (fwrite(copyBuffer, 1, bytesRead, tmp) != bytesRead) {
        writeError = true;
        break;
      }
    }

    fclose(file);
    fclose(tmp);
    
    if (writeError) {
      remove("/fs/pending_notes.tmp");
      Serial.println(F("Failed to copy note buffer"));
      return;
    }
    remove("/fs/pending_notes.log");
    rename("/fs/pending_notes.tmp", "/fs/pending_notes.log");
    Serial.println(F("Note buffer pruned"));
  #else
    if (!LittleFS.exists(NOTE_BUFFER_PATH)) {
      return;
    }

    File file = LittleFS.open(NOTE_BUFFER_PATH, "r");
    if (!file) {
      return;
    }

    size_t size = file.size();
    if (size <= NOTE_BUFFER_MAX_BYTES) {
      file.close();
      return;
    }

    size_t targetSize = NOTE_BUFFER_MAX_BYTES > NOTE_BUFFER_MIN_HEADROOM ? (NOTE_BUFFER_MAX_BYTES - NOTE_BUFFER_MIN_HEADROOM) : (NOTE_BUFFER_MAX_BYTES / 2);
    if (targetSize == 0) {
      targetSize = NOTE_BUFFER_MAX_BYTES / 2;
    }
    size_t startOffset = (size > targetSize) ? (size - targetSize) : 0;

    if (!file.seek(startOffset)) {
      file.close();
      LittleFS.remove(NOTE_BUFFER_PATH);
      return;
    }

    if (startOffset > 0) {
      file.readStringUntil('\n');
    }

    File tmp = LittleFS.open(NOTE_BUFFER_TEMP_PATH, "w");
    if (!tmp) {
      file.close();
      return;
    }

    while (file.available()) {
      tmp.write(file.read());
    }

    file.close();
    tmp.close();
    LittleFS.remove(NOTE_BUFFER_PATH);
    LittleFS.rename(NOTE_BUFFER_TEMP_PATH, NOTE_BUFFER_PATH);
    Serial.println(F("Note buffer pruned"));
  #endif
#endif
}

// ============================================================================
// Relay Control Functions
// ============================================================================

// Get the Arduino pin number for a relay (0-based index: 0=D0, 1=D1, 2=D2, 3=D3)
// Note: On Arduino Opta, LED_D0-LED_D3 constants are used for relay control
static int getRelayPin(uint8_t relayIndex) {
#if defined(ARDUINO_OPTA)
  if (relayIndex < 4) {
    return LED_D0 + relayIndex;
  }
#endif
  return -1;
}

static void initializeRelays() {
  // Initialize Opta relay outputs (D0-D3)
  for (uint8_t i = 0; i < MAX_RELAYS; ++i) {
    int relayPin = getRelayPin(i);
    if (relayPin >= 0) {
      pinMode(relayPin, OUTPUT);
      digitalWrite(relayPin, LOW);
      gRelayState[i] = false;
    }
  }
#if defined(ARDUINO_OPTA)
  Serial.println(F("Relay control initialized: 4 relays (D0-D3)"));
#else
  Serial.println(F("Warning: Relay control not available on this platform"));
#endif
}

// Set relay state (relayNum is 0-based: 0=relay1, 1=relay2, etc.)
static void setRelayState(uint8_t relayNum, bool state) {
  if (relayNum >= MAX_RELAYS) {
    Serial.print(F("Invalid relay number: "));
    Serial.println(relayNum);
    return;
  }

  int relayPin = getRelayPin(relayNum);
  if (relayPin >= 0) {
    digitalWrite(relayPin, state ? HIGH : LOW);
    gRelayState[relayNum] = state;
    
    Serial.print(F("Relay "));
    Serial.print(relayNum + 1);
    Serial.print(F(" (D"));
    Serial.print(relayNum);
    Serial.print(F(") set to "));
    Serial.println(state ? "ON" : "OFF");
  } else {
    Serial.println(F("Warning: Relay control not available on this platform"));
  }
}

static void pollForRelayCommands() {
  // Skip if notecard is known to be offline
  if (!gNotecardAvailable) {
    unsigned long now = millis();
    if (now - gLastSuccessfulNotecardComm > NOTECARD_RETRY_INTERVAL) {
      checkNotecardHealth();
    }
    return;
  }

  J *req = notecard.newRequest("note.get");
  if (!req) {
    gNotecardFailureCount++;
    return;
  }

  JAddStringToObject(req, "file", RELAY_CONTROL_FILE);
  JAddBoolToObject(req, "delete", true);

  J *rsp = notecard.requestAndResponse(req);
  if (!rsp) {
    gNotecardFailureCount++;
    if (gNotecardFailureCount >= NOTECARD_FAILURE_THRESHOLD) {
      checkNotecardHealth();
    }
    return;
  }

  gLastSuccessfulNotecardComm = millis();
  gNotecardFailureCount = 0;

  J *body = JGetObject(rsp, "body");
  if (body) {
    char *json = JConvertToJSONString(body);
    if (json) {
      DynamicJsonDocument doc(1024);
      DeserializationError err = deserializeJson(doc, json);
      NoteFree(json);
      if (!err) {
        processRelayCommand(doc);
      } else {
        Serial.println(F("Relay command invalid JSON"));
      }
    }
  }

  notecard.deleteResponse(rsp);
}

static void processRelayCommand(const JsonDocument &doc) {
  // Handle tank relay reset command from server first
  // Command format: { "relay_reset_tank": 0-7 }
  // This is a standalone command that doesn't require relay/state fields
  if (doc.containsKey("relay_reset_tank")) {
    uint8_t tankIdx = doc["relay_reset_tank"].as<uint8_t>();
    if (tankIdx < MAX_TANKS) {
      resetRelayForTank(tankIdx);
    }
    return;  // This is a complete command
  }
  
  // Command format for standard relay control:
  // {
  //   "relay": 1-4,           // Relay number (1-based)
  //   "state": true/false,    // ON/OFF
  //   "duration": 0,          // Optional: auto-off duration in seconds (0 = manual)
  //   "source": "server"      // Optional: source of command (server, client, alarm)
  // }

  if (!doc.containsKey("relay") || !doc.containsKey("state")) {
    Serial.println(F("Invalid relay command: missing relay or state"));
    return;
  }

  uint8_t relayNum = doc["relay"].as<uint8_t>();
  if (relayNum < 1 || relayNum > MAX_RELAYS) {
    Serial.print(F("Invalid relay number in command: "));
    Serial.println(relayNum);
    return;
  }

  // Convert from 1-based to 0-based
  relayNum = relayNum - 1;

  bool state = doc["state"].as<bool>();
  const char *source = doc["source"] | "unknown";
  
  Serial.print(F("Relay command received from "));
  Serial.print(source);
  Serial.print(F(": Relay "));
  Serial.print(relayNum + 1);
  Serial.print(F(" -> "));
  Serial.println(state ? "ON" : "OFF");

  setRelayState(relayNum, state);

  // Handle timed auto-off if duration specified
  // Note: Custom duration is not implemented in v1.0.0 - use relay modes instead:
  //   - RELAY_MODE_MOMENTARY: 30-minute auto-off
  //   - RELAY_MODE_UNTIL_CLEAR: Stays on until alarm clears
  //   - RELAY_MODE_MANUAL_RESET: Stays on until server reset
  // The duration parameter in relay commands is reserved for future use.
  if (doc.containsKey("duration") && state) {
    uint16_t duration = doc["duration"].as<uint16_t>();
    if (duration > 0) {
      Serial.print(F("Note: Custom duration ("));
      Serial.print(duration);
      Serial.println(F(" sec) ignored - use relay modes instead"));
    }
  }
}

static void triggerRemoteRelays(const char *targetClient, uint8_t relayMask, bool activate) {
  if (!targetClient || targetClient[0] == '\0' || relayMask == 0) {
    return;
  }

  if (!gNotecardAvailable) {
    Serial.println(F("Cannot trigger remote relays - notecard offline"));
    return;
  }

  // Send commands for each relay bit set in the mask
  for (uint8_t relayNum = 1; relayNum <= 4; ++relayNum) {
    uint8_t bit = relayNum - 1;
    if (relayMask & (1 << bit)) {
      J *req = notecard.newRequest("note.add");
      if (!req) {
        continue;
      }

      // Use device-specific targeting: send directly to target client's relay.qi inbox
      char targetFile[80];
      snprintf(targetFile, sizeof(targetFile), "device:%s:relay.qi", targetClient);
      JAddStringToObject(req, "file", targetFile);
      JAddBoolToObject(req, "sync", true);

      J *body = JCreateObject();
      if (!body) {
        continue;
      }

      JAddNumberToObject(body, "relay", relayNum);
      JAddBoolToObject(body, "state", activate);
      JAddStringToObject(body, "source", "client-alarm");
      
      JAddItemToObject(req, "body", body);
      
      bool queued = notecard.sendRequest(req);
      if (queued) {
        Serial.print(F("Queued relay command for client "));
        Serial.print(targetClient);
        Serial.print(F(": Relay "));
        Serial.print(relayNum);
        Serial.print(F(" -> "));
        Serial.println(activate ? "ON" : "OFF");
      } else {
        Serial.print(F("Failed to queue relay command for relay "));
        Serial.println(relayNum);
      }
    }
  }
}

// Check and deactivate relays that have exceeded the momentary timeout
// Uses the minimum duration among the active relays in the mask
static void checkRelayMomentaryTimeout(unsigned long now) {
  for (uint8_t i = 0; i < gConfig.tankCount; ++i) {
    const TankConfig &cfg = gConfig.tanks[i];
    
    // Only check tanks with active relays in momentary mode
    if (!gRelayActiveForTank[i] || cfg.relayMode != RELAY_MODE_MOMENTARY) {
      continue;
    }
    
    // Find the minimum duration among the relays in this tank's mask
    // 0 means "use default" (30 minutes)
    uint32_t minDurationMs = 0xFFFFFFFF; // Start with max value
    for (uint8_t r = 0; r < 4; r++) {
      if (cfg.relayMask & (1 << r)) {
        uint16_t seconds = cfg.relayMomentarySeconds[r];
        if (seconds == 0) {
          seconds = DEFAULT_RELAY_MOMENTARY_SECONDS; // Use default for 0
        }
        uint32_t durationMs = (uint32_t)seconds * 1000UL;
        if (durationMs < minDurationMs) {
          minDurationMs = durationMs;
        }
      }
    }
    
    // Default to 30 minutes if no relays in mask (shouldn't happen)
    if (minDurationMs == 0xFFFFFFFF) {
      minDurationMs = DEFAULT_RELAY_MOMENTARY_SECONDS * 1000UL;
    }
    
    // Check if the duration has elapsed
    // Note: Unsigned subtraction correctly handles millis() overflow due to modular arithmetic
    if (now - gRelayActivationTime[i] >= minDurationMs) {
      // Deactivate the relay
      if (cfg.relayTargetClient[0] != '\0' && cfg.relayMask != 0) {
        triggerRemoteRelays(cfg.relayTargetClient, cfg.relayMask, false);
        Serial.print(F("Momentary relay timeout ("));
        Serial.print(minDurationMs / 60000UL);
        Serial.print(F(" min) for tank "));
        Serial.println(i);
      }
      gRelayActiveForTank[i] = false;
      gRelayActivationTime[i] = 0;
    }
  }
}

// Reset relay for a specific tank (called from server manual reset command)
static void resetRelayForTank(uint8_t idx) {
  if (idx >= gConfig.tankCount) {
    return;
  }
  
  const TankConfig &cfg = gConfig.tanks[idx];
  
  if (gRelayActiveForTank[idx] && cfg.relayTargetClient[0] != '\0' && cfg.relayMask != 0) {
    triggerRemoteRelays(cfg.relayTargetClient, cfg.relayMask, false);
    Serial.print(F("Manual relay reset for tank "));
    Serial.println(idx);
  }
  gRelayActiveForTank[idx] = false;
  gRelayActivationTime[idx] = 0;
}

// ============================================================================
// Clear Button Support (Physical Button to Clear All Relay Alarms)
// ============================================================================

// Initialize the clear button pin if configured
static void initializeClearButton() {
  if (gConfig.clearButtonPin < 0) {
    // Clear button disabled
    gClearButtonInitialized = false;
    return;
  }
  
  // Configure the button pin
  if (gConfig.clearButtonActiveHigh) {
    // Button is active HIGH - use INPUT (external pull-down required)
    pinMode(gConfig.clearButtonPin, INPUT);
  } else {
    // Button is active LOW - use INPUT_PULLUP (button connects to GND)
    pinMode(gConfig.clearButtonPin, INPUT_PULLUP);
  }
  
  gClearButtonInitialized = true;
  gClearButtonLastState = false;
  gClearButtonLastPressTime = 0;
  
  Serial.print(F("Clear button initialized on pin "));
  Serial.print(gConfig.clearButtonPin);
  Serial.println(gConfig.clearButtonActiveHigh ? F(" (active HIGH)") : F(" (active LOW with pullup)"));
}

// Check for clear button press (with debouncing)
static void checkClearButton(unsigned long now) {
  if (!gClearButtonInitialized || gConfig.clearButtonPin < 0) {
    return;
  }
  
  // Read the button state
  bool buttonPhysical = digitalRead(gConfig.clearButtonPin);
  bool buttonPressed = gConfig.clearButtonActiveHigh ? buttonPhysical : !buttonPhysical;
  
  // Debounce: only register state change after stable for CLEAR_BUTTON_DEBOUNCE_MS
  if (buttonPressed != gClearButtonLastState) {
    // State changed - reset the timer
    gClearButtonLastPressTime = now;
    gClearButtonLastState = buttonPressed;
    return;
  }
  
  // Button state is stable
  if (buttonPressed && (now - gClearButtonLastPressTime >= CLEAR_BUTTON_MIN_PRESS_MS)) {
    // Button has been held for minimum press time - trigger clear
    Serial.println(F("Clear button pressed - clearing all relay alarms"));
    addSerialLog("Clear button pressed - clearing all relay alarms");
    clearAllRelayAlarms();
    
    // Reset the timer to require release before next trigger
    gClearButtonLastPressTime = now;
    gClearButtonLastState = false;  // Require button release before next action
    
    // Wait for button release to prevent repeated triggers
    unsigned long releaseWaitStart = millis();
    while (millis() - releaseWaitStart < 2000) {  // Wait up to 2 seconds
      bool stillPressed = gConfig.clearButtonActiveHigh ? 
                          digitalRead(gConfig.clearButtonPin) : 
                          !digitalRead(gConfig.clearButtonPin);
      if (!stillPressed) {
        break;
      }
      delay(50);
    }
  }
}

// Clear all relay alarms for all tanks (turn off all relays and reset state)
static void clearAllRelayAlarms() {
  bool anyCleared = false;
  
  for (uint8_t i = 0; i < gConfig.tankCount; ++i) {
    if (gRelayActiveForTank[i]) {
      resetRelayForTank(i);
      anyCleared = true;
    }
  }
  
  // Also turn off any locally controlled relays
  for (uint8_t r = 0; r < MAX_RELAYS; ++r) {
    if (gRelayState[r]) {
      setRelayState(r, false);
      anyCleared = true;
    }
  }
  
  if (anyCleared) {
    Serial.println(F("All relay alarms cleared"));
  } else {
    Serial.println(F("No active relay alarms to clear"));
  }
}

// ============================================================================
// Serial Logging for Remote Debugging
// ============================================================================

static void addSerialLog(const char *message) {
  if (!message || strlen(message) == 0) {
    return;
  }

  SerialLogEntry &entry = gSerialLog.entries[gSerialLog.writeIndex];
  entry.timestamp = currentEpoch();
  
  // Truncate if necessary
  size_t len = strlen(message);
  if (len >= sizeof(entry.message)) {
    len = sizeof(entry.message) - 1;
  }
  memcpy(entry.message, message, len);
  entry.message[len] = '\0';

  gSerialLog.writeIndex = (gSerialLog.writeIndex + 1) % CLIENT_SERIAL_BUFFER_SIZE;
  if (gSerialLog.count < CLIENT_SERIAL_BUFFER_SIZE) {
    gSerialLog.count++;
  }
}

static void pollForSerialRequests() {
  // Check for serial log requests from server
  while (true) {
    J *req = notecard.newRequest("note.get");
    if (!req) {
      return;
    }
    
    JAddStringToObject(req, "file", SERIAL_REQUEST_FILE);
    JAddBoolToObject(req, "delete", true);
    
    J *rsp = notecard.requestAndResponse(req);
    if (!rsp) {
      return;
    }

    J *body = JGetObject(rsp, "body");
    if (!body) {
      notecard.deleteResponse(rsp);
      break;
    }

    const char *request = JGetString(body, "request");
    if (request && strcmp(request, "send_logs") == 0) {
      DEBUG_PRINTLN(F("Serial log request received from server"));
      addSerialLog("Serial log request received");
      sendSerialLogs();
    }

    notecard.deleteResponse(rsp);
  }
}

static void sendSerialLogs() {
  if (gSerialLog.count == 0) {
    DEBUG_PRINTLN(F("No serial logs to send"));
    return;
  }

  // Create a note with an array of log entries
  J *req = notecard.newRequest("note.add");
  if (!req) {
    return;
  }

  JAddStringToObject(req, "file", SERIAL_LOG_FILE);
  JAddBoolToObject(req, "sync", true);

  J *body = JCreateObject();
  if (!body) {
    return;
  }

  JAddStringToObject(body, "client", gDeviceUID);

  J *logsArray = JCreateArray();
  if (!logsArray) {
    JDelete(body);
    return;
  }

  // Add logs from oldest to newest (circular buffer)
  uint8_t startIdx = (gSerialLog.count < CLIENT_SERIAL_BUFFER_SIZE) ? 0 : gSerialLog.writeIndex;
  uint8_t sentCount = 0;
  
  for (uint8_t i = 0; i < gSerialLog.count && sentCount < 20; ++i) {  // Limit to 20 most recent logs
    uint8_t idx = (startIdx + i) % CLIENT_SERIAL_BUFFER_SIZE;
    SerialLogEntry &entry = gSerialLog.entries[idx];
    
    if (entry.message[0] == '\0') {
      continue;
    }

    J *logEntry = JCreateObject();
    if (!logEntry) {
      break;
    }

    JAddNumberToObject(logEntry, "timestamp", entry.timestamp);
    JAddStringToObject(logEntry, "message", entry.message);
    JAddItemToArray(logsArray, logEntry);
    sentCount++;
  }

  JAddItemToObject(body, "logs", logsArray);
  JAddItemToObject(req, "body", body);

  bool queued = notecard.sendRequest(req);
  if (queued) {
    DEBUG_PRINT(F("Sent "));
    DEBUG_PRINT(sentCount);
    DEBUG_PRINTLN(F(" serial logs to server"));
  } else {
    DEBUG_PRINTLN(F("Failed to queue serial logs"));
  }
}

// ============================================================================
// Notecard VIN Voltage Reading
// ============================================================================

static float readNotecardVinVoltage() {
  J *req = notecard.newRequest("card.voltage");
  if (!req) {
    Serial.println(F("Failed to create card.voltage request"));
    return -1.0f;
  }

  J *rsp = notecard.requestAndResponse(req);
  if (!rsp) {
    Serial.println(F("No response from card.voltage"));
    return -1.0f;
  }

  // Check for error response
  const char *err = JGetString(rsp, "err");
  if (err && strlen(err) > 0) {
    Serial.print(F("card.voltage error: "));
    Serial.println(err);
    notecard.deleteResponse(rsp);
    return -1.0f;
  }

  double voltage = JGetNumber(rsp, "value");
  notecard.deleteResponse(rsp);

  if (voltage > 0.0) {
    Serial.print(F("Notecard VIN voltage: "));
    Serial.print(voltage);
    Serial.println(F(" V"));
  }

  return (float)voltage;
}
