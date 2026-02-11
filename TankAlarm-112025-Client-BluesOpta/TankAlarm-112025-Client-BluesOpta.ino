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

// Shared library - common constants and utilities
#include <TankAlarm_Common.h>

#include <Arduino.h>
#include <Wire.h>
#include <ArduinoJson.h>
#include <memory>
#include <math.h>
#include <string.h>

// POSIX-compliant standard library headers
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/types.h>

// POSIX file I/O types (for platforms that support it)
#if defined(ARDUINO_OPTA) || defined(ARDUINO_ARCH_MBED)
  #include <fcntl.h>
  #include <sys/stat.h>
#endif

// Filesystem support - Mbed OS filesystem instance
#if defined(ARDUINO_OPTA) || defined(ARDUINO_ARCH_MBED)
  #include "rtos/ThisThread.h"
  using namespace std::chrono;
  using namespace std::chrono_literals;
  
  // Mbed OS filesystem instance - mounted at "/fs" for POSIX path compatibility
  static LittleFileSystem *mbedFS = nullptr;
  static BlockDevice *mbedBD = nullptr;
  static MbedWatchdogHelper mbedWatchdog;
  static bool gStorageAvailable = false;
  
  // POSIX-compatible file path prefix for Mbed OS VFS
  #define POSIX_FS_PREFIX "/fs"
  #define FILESYSTEM_AVAILABLE
  #define POSIX_FILE_IO_AVAILABLE
#elif defined(ARDUINO_ARCH_STM32)
  #include <LittleFS.h>
  #include <IWatchdog.h>
  #define FILESYSTEM_AVAILABLE
  static bool gStorageAvailable = false;
#endif

static inline bool isStorageAvailable() {
#ifdef FILESYSTEM_AVAILABLE
  #if defined(ARDUINO_OPTA) || defined(ARDUINO_ARCH_MBED)
    return gStorageAvailable && (mbedFS != nullptr);
  #else
    return gStorageAvailable;
  #endif
#else
  return false;
#endif
}

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

// POSIX file helpers - use tankalarm_ prefixed versions from shared library
#if defined(POSIX_FILE_IO_AVAILABLE)
static inline long posix_file_size(FILE *fp) { return tankalarm_posix_file_size(fp); }
static inline bool posix_file_exists(const char *path) { return tankalarm_posix_file_exists(path); }
static inline void posix_log_error(const char *op, const char *path) { tankalarm_posix_log_error(op, path); }
#endif

// Wrapper for shared library roundTo function
static inline float roundTo(float val, int decimals) { return tankalarm_roundTo(val, decimals); }

#ifndef DEFAULT_PRODUCT_UID
#define DEFAULT_PRODUCT_UID "com.senax.tankalarm112025"
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

#ifndef UNLOAD_FILE
#define UNLOAD_FILE "unload.qi"
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

#ifndef LOCATION_REQUEST_FILE
#define LOCATION_REQUEST_FILE "location_request.qi"  // Server requests GPS location
#endif

#ifndef LOCATION_RESPONSE_FILE
#define LOCATION_RESPONSE_FILE "location.qo"  // Client sends GPS location response
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

// Tank unload detection constants
#ifndef UNLOAD_DEFAULT_DROP_PERCENT
#define UNLOAD_DEFAULT_DROP_PERCENT 50.0f  // Default: 50% drop from peak = unload event
#endif

#ifndef UNLOAD_DEFAULT_EMPTY_HEIGHT
#define UNLOAD_DEFAULT_EMPTY_HEIGHT 2.0f  // Default empty height when at/below sensor (inches)
#endif

#ifndef UNLOAD_MIN_PEAK_HEIGHT
#define UNLOAD_MIN_PEAK_HEIGHT 12.0f  // Minimum peak height before tracking starts (inches)
#endif

#ifndef UNLOAD_DEBOUNCE_COUNT
#define UNLOAD_DEBOUNCE_COUNT 3  // Require 3 consecutive low readings to confirm unload
#endif

// Default momentary relay duration (30 minutes in seconds)
#ifndef DEFAULT_RELAY_MOMENTARY_SECONDS
#define DEFAULT_RELAY_MOMENTARY_SECONDS 1800  // 30 minutes
#endif

// ============================================================================
// Power Conservation State Machine
// Progressive duty-cycle reduction based on battery voltage.
// Uses hysteresis thresholds to prevent oscillation during charge/discharge.
// Voltage source: best of SunSaver MPPT (Modbus) and Notecard card.voltage.
// ============================================================================
enum PowerState : uint8_t {
  POWER_STATE_NORMAL            = 0,  // Full operation
  POWER_STATE_ECO               = 1,  // Reduced polling, longer sleep
  POWER_STATE_LOW_POWER         = 2,  // Minimal polling, relays frozen
  POWER_STATE_CRITICAL_HIBERNATE = 3  // Essential monitoring only, relays OFF
};

// --- Entry thresholds (voltage falling) ---
// We enter a worse state when battery drops BELOW these values.
#ifndef POWER_ECO_ENTER_VOLTAGE
#define POWER_ECO_ENTER_VOLTAGE            12.0f   // Enter ECO below 12.0V (~25% SOC lead-acid)
#endif
#ifndef POWER_LOW_ENTER_VOLTAGE
#define POWER_LOW_ENTER_VOLTAGE            11.8f   // Enter LOW_POWER below 11.8V (~10% SOC)
#endif
#ifndef POWER_CRITICAL_ENTER_VOLTAGE
#define POWER_CRITICAL_ENTER_VOLTAGE       11.5f   // Enter CRITICAL below 11.5V (risk of damage)
#endif

// --- Exit thresholds (voltage rising, with hysteresis) ---
// We return to a better state when battery rises ABOVE these values.
// The gap between enter and exit prevents rapid state toggling.
#ifndef POWER_CRITICAL_EXIT_VOLTAGE
#define POWER_CRITICAL_EXIT_VOLTAGE        12.2f   // Exit CRITICAL above 12.2V (+0.7V hysteresis)
#endif
#ifndef POWER_LOW_EXIT_VOLTAGE
#define POWER_LOW_EXIT_VOLTAGE             12.3f   // Exit LOW_POWER above 12.3V (+0.5V hysteresis)
#endif
#ifndef POWER_ECO_EXIT_VOLTAGE
#define POWER_ECO_EXIT_VOLTAGE             12.4f   // Exit ECO above 12.4V (+0.4V hysteresis)
#endif

// --- Timing for each power state ---
// Loop sleep duration (rtos::ThisThread::sleep_for)
#ifndef POWER_NORMAL_SLEEP_MS
#define POWER_NORMAL_SLEEP_MS              100       // 100ms (existing default)
#endif
#ifndef POWER_ECO_SLEEP_MS
#define POWER_ECO_SLEEP_MS                 5000      // 5 seconds
#endif
#ifndef POWER_LOW_SLEEP_MS
#define POWER_LOW_SLEEP_MS                 30000     // 30 seconds
#endif
#ifndef POWER_CRITICAL_SLEEP_MS
#define POWER_CRITICAL_SLEEP_MS            300000    // 5 minutes
#endif

// Outbound sync multipliers (applied to base outbound interval)
#ifndef POWER_ECO_OUTBOUND_MULTIPLIER
#define POWER_ECO_OUTBOUND_MULTIPLIER      2    // 2x slower (e.g., 12h instead of 6h)
#endif
#ifndef POWER_LOW_OUTBOUND_MULTIPLIER
#define POWER_LOW_OUTBOUND_MULTIPLIER      4    // 4x slower (e.g., 24h instead of 6h)
#endif

// Inbound check multipliers
#ifndef POWER_ECO_INBOUND_MULTIPLIER
#define POWER_ECO_INBOUND_MULTIPLIER       4    // 4x slower
#endif
#ifndef POWER_LOW_INBOUND_MULTIPLIER
#define POWER_LOW_INBOUND_MULTIPLIER       12   // 12x slower
#endif

// Sample interval multiplier
#ifndef POWER_ECO_SAMPLE_MULTIPLIER
#define POWER_ECO_SAMPLE_MULTIPLIER        2    // 2x slower
#endif
#ifndef POWER_LOW_SAMPLE_MULTIPLIER
#define POWER_LOW_SAMPLE_MULTIPLIER        4    // 4x slower
#endif

// Minimum consecutive readings before changing power state (debounce)
#ifndef POWER_STATE_DEBOUNCE_COUNT
#define POWER_STATE_DEBOUNCE_COUNT         3
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

// Object types - what is being monitored
enum ObjectType : uint8_t {
  OBJECT_TANK = 0,        // Liquid storage tank (level monitoring)
  OBJECT_ENGINE = 1,      // Engine or motor (RPM monitoring)
  OBJECT_PUMP = 2,        // Pump (status or flow monitoring)
  OBJECT_GAS = 3,         // Gas pressure system (propane, natural gas, etc.)
  OBJECT_FLOW = 4,        // Flow meter (liquid or gas flow rate)
  OBJECT_CUSTOM = 255     // User-defined/other
};

// Sensor interface types - how the measurement is taken
enum SensorInterface : uint8_t {
  SENSOR_DIGITAL = 0,       // Binary on/off (float switch, relay contact)
  SENSOR_ANALOG = 1,        // Voltage output (0-10V, 1-5V)
  SENSOR_CURRENT_LOOP = 2,  // 4-20mA current loop
  SENSOR_PULSE = 3          // Pulse/frequency counting (hall effect, flow meter)
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

struct MonitorConfig {
  char id;                 // Friendly identifier (A, B, C ...)
  char name[24];           // Label shown in reports (e.g., "North Tank", "Main Pump")
  char contents[24];       // What the tank contains (e.g., "Diesel", "Water") - not used for RPM monitors
  uint8_t monitorNumber;   // Numeric reference (1, 2, 3...)
  ObjectType objectType;   // What is being monitored (tank, engine, pump, gas, flow)
  SensorInterface sensorInterface; // How measurement is taken (digital, analog, currentLoop, pulse)
  int16_t primaryPin;      // Digital pin or analog channel
  int16_t secondaryPin;    // Optional secondary pin (unused by default)
  int16_t currentLoopChannel; // 4-20mA channel index (-1 if unused)
  int16_t pulsePin;        // Pulse sensor pin for RPM/flow (-1 if unused)
  uint8_t pulsesPerUnit;   // Pulses per revolution (RPM) or per gallon (flow), default 1
  HallEffectSensorType hallEffectType; // Type of hall effect sensor (unipolar, bipolar, omnipolar, analog)
  HallEffectDetectionMethod hallEffectDetection; // Detection method (pulse counting or time-based)
  uint32_t pulseSampleDurationMs; // Sample duration for pulse measurement (default 60000ms = 60s)
  bool pulseAccumulatedMode; // If true, count pulses between telemetry reports for very low rates
  float highAlarmThreshold;   // High threshold for triggering alarm
  float lowAlarmThreshold;    // Low threshold for triggering alarm
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
  // Measurement unit for display/reporting
  char measurementUnit[8]; // "inches", "psi", "rpm", "gpm", etc.
  // Expected pulse rate for baseline comparison (by object type)
  float expectedPulseRate; // Expected RPM for engines, GPM for flow, etc. (0 = not configured)
  // Tank unload tracking configuration
  bool trackUnloads;          // true = this tank is regularly emptied, track unload events
  float unloadEmptyHeight;    // Default empty height when level drops to/below sensor height (inches)
  float unloadDropThreshold;  // Minimum drop to consider as unload (inches, default 50% of tank height)
  float unloadDropPercent;    // Alternative: minimum drop as percentage of peak height (0-100, default 50)
  bool unloadAlarmSms;        // Send SMS notification when tank is unloaded
  bool unloadAlarmEmail;      // Include unload events in daily email
};

struct ClientConfig {
  char siteName[32];
  char deviceUid[32];   // Device UID (e.g., dev:...)
  char deviceLabel[24];
  char clientFleet[32]; // Fleet for this device context
  char serverFleet[32]; // Target fleet name for server (e.g., "tankalarm-server")
  char productUid[64];  // Notehub product UID (configurable for different fleets)
  char dailyEmail[64];
  uint16_t sampleSeconds;
  float minLevelChangeInches;
  uint8_t reportHour;
  uint8_t reportMinute;
  uint8_t monitorCount;
  MonitorConfig monitors[MAX_TANKS];
  // Optional clear button configuration
  int8_t clearButtonPin;        // Pin for physical clear button (-1 = disabled)
  bool clearButtonActiveHigh;   // true = button active when HIGH, false = active when LOW (with pullup)
  // Power saving configuration
  bool solarPowered;            // true = solar powered (use power saving features), false = grid-tied
  // I2C sensor configuration  
  uint8_t currentLoopI2cAddress; // I2C address for 4-20mA current loop sensor (default 0x64)
  // Solar/Battery charger monitoring configuration (SunSaver MPPT via RS-485)
  // Requires: Arduino Opta with RS485 + Morningstar MRC-1 adapter
  SolarConfig solarCharger;     // Solar charger monitoring configuration
  // Battery voltage monitoring via Notecard (when wired directly to battery)
  // Provides low voltage alerts and trend analysis
  BatteryConfig batteryMonitor; // Notecard battery voltage monitoring
};

struct MonitorRuntime {
  float currentInches;
  float currentSensorMa;        // Raw sensor reading in milliamps (for 4-20mA sensors)
  float currentSensorVoltage;   // Raw sensor reading in volts (for analog voltage sensors)
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
  // Tank unload tracking state
  float unloadPeakInches;         // Highest level seen since last unload event
  float unloadPeakSensorMa;       // Sensor mA at peak level (for logging)
  double unloadPeakEpoch;         // Timestamp of peak reading
  bool unloadTracking;            // true = currently tracking fill cycle
};

static ClientConfig gConfig;
static MonitorRuntime gMonitorState[MAX_TANKS];

// Solar/Battery charger monitoring (SunSaver MPPT via RS-485)
static SolarManager gSolarManager;
static unsigned long gLastSolarAlarmMillis = 0;
static SolarAlertType gLastSolarAlert = SOLAR_ALERT_NONE;
#define SOLAR_ALARM_MIN_INTERVAL_MS 3600000UL  // Min 1 hour between same solar alarm

// Battery voltage monitoring via Notecard (when wired directly to battery)
static BatteryData gBatteryData;
static unsigned long gLastBatteryPollMillis = 0;
static unsigned long gLastBatteryAlarmMillis = 0;
static BatteryAlertType gLastBatteryAlert = BATTERY_ALERT_NONE;
static float gLastBatteryAlertVoltage = 0.0f;

// Power conservation state machine
static PowerState gPowerState = POWER_STATE_NORMAL;
static PowerState gPreviousPowerState = POWER_STATE_NORMAL;
static float gEffectiveBatteryVoltage = 0.0f;  // Best voltage from either source
static uint8_t gPowerStateDebounce = 0;        // Consecutive readings at proposed new state
static unsigned long gPowerStateChangeMillis = 0; // When the current power state was entered
static unsigned long gLastPowerStateLogMillis = 0; // Rate-limit power state log messages

static Notecard notecard;
static char gDeviceUID[48] = {0};
static unsigned long gLastTelemetryMillis = 0;
static unsigned long gLastConfigCheckMillis = 0;
static unsigned long gLastTimeSyncMillis = 0;
static double gLastSyncedEpoch = 0.0;
static double gNextDailyReportEpoch = 0.0;

// DFU (Device Firmware Update) state tracking
static unsigned long gLastDfuCheckMillis = 0;
#define DFU_CHECK_INTERVAL_MS 3600000UL  // Check for firmware updates every hour
static bool gDfuUpdateAvailable = false;
static char gDfuVersion[32] = {0};
static bool gDfuInProgress = false;

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
// For accumulated mode: count pulses between telemetry reports
static volatile uint32_t gRpmAccumulatedPulses[MAX_TANKS] = {0};
static unsigned long gRpmAccumulatedStartMillis[MAX_TANKS] = {0};
static bool gRpmAccumulatedInitialized[MAX_TANKS] = {false};

// Atomic access helpers for volatile pulse counter (protects against future interrupt use)
// On 32-bit ARM (Cortex-M7 in STM32H747XI), 32-bit aligned reads/writes are atomic,
// but we use interrupt guards for portability and read-modify-write safety
static inline uint32_t atomicReadAndResetPulses(uint8_t idx) {
  noInterrupts();
  uint32_t count = gRpmAccumulatedPulses[idx];
  gRpmAccumulatedPulses[idx] = 0;
  interrupts();
  return count;
}

static inline void atomicResetPulses(uint8_t idx) {
  noInterrupts();
  gRpmAccumulatedPulses[idx] = 0;
  interrupts();
}

static inline void atomicIncrementPulses(uint8_t idx) {
  noInterrupts();
  gRpmAccumulatedPulses[idx]++;
  interrupts();
}

static inline uint32_t atomicReadPulses(uint8_t idx) {
  noInterrupts();
  uint32_t count = gRpmAccumulatedPulses[idx];
  interrupts();
  return count;
}

// Default RPM sampling duration in milliseconds (60 seconds for 1 RPM minimum detection)
// To detect 0.1 RPM, use pulseAccumulatedMode=true with sampleSeconds >= 600
#ifndef RPM_SAMPLE_DURATION_MS
#define RPM_SAMPLE_DURATION_MS 60000
#endif

// Helper: Get recommended pulse sampling parameters based on expected rate
// This helps configure optimal sampling for the expected RPM/flow rate range
// Returns: pulseSampleDurationMs, pulseAccumulatedMode recommendations
struct PulseSamplingRecommendation {
  uint32_t sampleDurationMs;  // Recommended sample duration
  bool accumulatedMode;       // Whether to use accumulated mode
  const char *description;    // Human-readable description
};

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
static unsigned long gLastLocationRequestCheckMillis = 0;

// Forward declarations
static PulseSamplingRecommendation getRecommendedPulseSampling(float expectedRate);
static float getMonitorHeight(const MonitorConfig &cfg);
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
static void checkForFirmwareUpdate();
static void enableDfuMode();
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
static bool appendDailyTank(JsonDocument &doc, JsonArray &array, uint8_t tankIndex, size_t payloadLimit);
static void pollForRelayCommands();
static void processRelayCommand(const JsonDocument &doc);
static void setRelayState(uint8_t relayNum, bool state);
static void initializeRelays();
static void triggerRemoteRelays(const char *targetClient, uint8_t relayMask, bool activate);
static int getRelayPin(uint8_t relayIndex);
static float readNotecardVinVoltage();
static void checkRelayMomentaryTimeout(unsigned long now);
static bool fetchNotecardLocation(float &latitude, float &longitude);
static void resetRelayForTank(uint8_t idx);
static void initializeClearButton();
static void checkClearButton(unsigned long now);
static void clearAllRelayAlarms();
static void addSerialLog(const char *message);
static void pollForSerialRequests();
static void pollForLocationRequests();
static void sendSerialLogs();
static void evaluateUnload(uint8_t idx);
static void sendUnloadEvent(uint8_t idx, float peakInches, float currentInches, double peakEpoch);
static void sendSolarAlarm(SolarAlertType alertType);
static bool appendSolarDataToDaily(JsonDocument &doc);
// Battery voltage monitoring via Notecard (when wired directly to battery)
static bool pollBatteryVoltage(BatteryData &data, const BatteryConfig &cfg);
static void checkBatteryAlerts(const BatteryData &data, const BatteryConfig &cfg);
static void sendBatteryAlarm(BatteryAlertType alertType, float voltage);
static bool appendBatteryDataToDaily(JsonDocument &doc);
static void configureBatteryMonitoring(const BatteryConfig &cfg);
// Power conservation
static void updatePowerState();
static void sendPowerStateChange(PowerState oldState, PowerState newState, float voltage);
static const char* getPowerStateDescription(PowerState state);
static unsigned long getPowerStateSleepMs(PowerState state);

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

  for (uint8_t i = 0; i < gConfig.monitorCount; ++i) {
    gMonitorState[i].currentInches = 0.0f;
    gMonitorState[i].currentSensorMa = 0.0f;
    gMonitorState[i].currentSensorVoltage = 0.0f;
    gMonitorState[i].lastReportedInches = -9999.0f;
    gMonitorState[i].lastDailySentInches = -9999.0f;
    gMonitorState[i].highAlarmLatched = false;
    gMonitorState[i].lowAlarmLatched = false;
    gMonitorState[i].lastSampleMillis = 0;
    gMonitorState[i].lastAlarmSendMillis = 0;
    gMonitorState[i].highAlarmDebounceCount = 0;
    gMonitorState[i].lowAlarmDebounceCount = 0;
    gMonitorState[i].clearDebounceCount = 0;
    gMonitorState[i].lastValidReading = 0.0f;
    gMonitorState[i].hasLastValidReading = false;
    gMonitorState[i].consecutiveFailures = 0;
    gMonitorState[i].stuckReadingCount = 0;
    gMonitorState[i].sensorFailed = false;
    gMonitorState[i].alarmCount = 0;
    gMonitorState[i].lastHighAlarmMillis = 0;
    gMonitorState[i].lastLowAlarmMillis = 0;
    gMonitorState[i].lastClearAlarmMillis = 0;
    gMonitorState[i].lastSensorFaultMillis = 0;
    // Initialize unload tracking state
    gMonitorState[i].unloadPeakInches = 0.0f;
    gMonitorState[i].unloadPeakSensorMa = 0.0f;
    gMonitorState[i].unloadPeakEpoch = 0.0;
    gMonitorState[i].unloadTracking = false;
    for (uint8_t j = 0; j < MAX_ALARMS_PER_HOUR; ++j) {
      gMonitorState[i].alarmTimestamps[j] = 0;
    }
  }

  initializeRelays();
  initializeClearButton();
  
  // Initialize solar/battery charger monitoring (SunSaver MPPT via RS-485)
  if (gConfig.solarCharger.enabled) {
    if (gSolarManager.begin(gConfig.solarCharger)) {
      Serial.println(F("Solar charger monitoring enabled"));
      addSerialLog("Solar charger monitoring initialized");
    } else {
      Serial.println(F("Warning: Solar charger initialization failed"));
      addSerialLog("Solar charger init failed");
    }
  }

  // Initialize battery voltage monitoring (Notecard direct to battery)
  if (gConfig.batteryMonitor.enabled) {
    configureBatteryMonitoring(gConfig.batteryMonitor);
    Serial.println(F("Battery voltage monitoring enabled"));
    addSerialLog("Battery voltage monitoring initialized");
    // Initialize battery data structure
    memset(&gBatteryData, 0, sizeof(BatteryData));
    // Do initial poll
    pollBatteryVoltage(gBatteryData, gConfig.batteryMonitor);
  }

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

  // ---- Power-state-aware sample interval ----
  // In ECO/LOW_POWER states, sample less frequently to conserve energy
  unsigned long sampleInterval = (unsigned long)gConfig.sampleSeconds * 1000UL;
  if (gPowerState == POWER_STATE_ECO) {
    sampleInterval *= POWER_ECO_SAMPLE_MULTIPLIER;
  } else if (gPowerState == POWER_STATE_LOW_POWER) {
    sampleInterval *= POWER_LOW_SAMPLE_MULTIPLIER;
  }
  // In CRITICAL_HIBERNATE, skip sampling entirely
  if (gPowerState != POWER_STATE_CRITICAL_HIBERNATE) {
    if (now - gLastTelemetryMillis >= sampleInterval) {
      gLastTelemetryMillis = now;
      sampleTanks();
    }
  }

  // ---- Power-state-aware polling intervals ----
  // Determine base polling interval based on power source
  unsigned long baseInboundInterval = gConfig.solarPowered ? 
      (unsigned long)SOLAR_INBOUND_INTERVAL_MINUTES * 60000UL : 
      600000UL; // 10 minutes for grid power

  // Apply power-state multiplier
  unsigned long inboundInterval = baseInboundInterval;
  if (gPowerState == POWER_STATE_ECO) {
    inboundInterval *= POWER_ECO_INBOUND_MULTIPLIER;
  } else if (gPowerState == POWER_STATE_LOW_POWER) {
    inboundInterval *= POWER_LOW_INBOUND_MULTIPLIER;
  }
  // In CRITICAL_HIBERNATE, skip all inbound polling

  if (gPowerState != POWER_STATE_CRITICAL_HIBERNATE) {
    if (now - gLastConfigCheckMillis >= inboundInterval) {
      gLastConfigCheckMillis = now;
      pollForConfigUpdates();
    }

    if (now - gLastRelayCheckMillis >= inboundInterval) {
      gLastRelayCheckMillis = now;
      pollForRelayCommands();
    }

    if (now - gLastSerialRequestCheckMillis >= inboundInterval) {
      gLastSerialRequestCheckMillis = now;
      pollForSerialRequests();
    }

    if (now - gLastLocationRequestCheckMillis >= inboundInterval) {
      gLastLocationRequestCheckMillis = now;
      pollForLocationRequests();
    }
  }

  // Check for momentary relay timeout (30 minutes) — skip in CRITICAL (relays are off)
  if (gPowerState != POWER_STATE_CRITICAL_HIBERNATE) {
    checkRelayMomentaryTimeout(now);
  }
  
  // Check for physical clear button press
  checkClearButton(now);
  
  // Poll solar charger for battery health data (SunSaver MPPT via RS-485)
  // In CRITICAL_HIBERNATE we still poll the solar charger (if enabled) to detect
  // battery recovery, but at reduced frequency (controlled by sleep duration).
  if (gSolarManager.isEnabled()) {
    if (gSolarManager.poll(now)) {
      // New data available - check for alerts (suppress alarm sending in CRITICAL to save power)
      SolarAlertType alert = gSolarManager.checkAlerts();
      if (alert != SOLAR_ALERT_NONE && gNotecardAvailable && gPowerState != POWER_STATE_CRITICAL_HIBERNATE) {
        // Only send alert if different from last, or enough time has passed
        if (alert != gLastSolarAlert || 
            (now - gLastSolarAlarmMillis >= SOLAR_ALARM_MIN_INTERVAL_MS)) {
          sendSolarAlarm(alert);
          gLastSolarAlert = alert;
          gLastSolarAlarmMillis = now;
        }
      } else if (alert == SOLAR_ALERT_NONE) {
        gLastSolarAlert = SOLAR_ALERT_NONE;  // Clear last alert state
      }
    }
  }
  
  // Poll battery voltage via Notecard (when wired directly to battery)
  // Always poll even in CRITICAL — needed to detect battery recovery.
  if (gConfig.batteryMonitor.enabled && gNotecardAvailable) {
    unsigned long batteryPollInterval = (unsigned long)gConfig.batteryMonitor.pollIntervalSec * 1000UL;
    // In reduced power states, poll less often (but still poll for recovery detection)
    if (gPowerState >= POWER_STATE_LOW_POWER) {
      batteryPollInterval *= 2;  // 2x slower when conserving
    }
    if (now - gLastBatteryPollMillis >= batteryPollInterval) {
      gLastBatteryPollMillis = now;
      if (pollBatteryVoltage(gBatteryData, gConfig.batteryMonitor)) {
        // Suppress normal battery alert processing in CRITICAL to avoid redundant alarms
        if (gPowerState != POWER_STATE_CRITICAL_HIBERNATE) {
          checkBatteryAlerts(gBatteryData, gConfig.batteryMonitor);
        }
      }
    }
  }
  
  // ---- Update power conservation state (after polling battery sources) ----
  updatePowerState();
  
  // Periodic firmware update check via Notecard DFU
  // Skip in LOW_POWER and CRITICAL — firmware updates are not urgent
  if (gPowerState <= POWER_STATE_ECO) {
    if (now - gLastDfuCheckMillis > DFU_CHECK_INTERVAL_MS) {
      gLastDfuCheckMillis = now;
      if (!gDfuInProgress && gNotecardAvailable) {
        checkForFirmwareUpdate();
        // Auto-enable DFU if update is available (can be disabled for manual control)
        // Comment out next 3 lines to require manual trigger
        if (gDfuUpdateAvailable) {
          enableDfuMode();
        }
      }
    }
  }

  persistConfigIfDirty();
  
  // Skip time sync and daily reports in CRITICAL_HIBERNATE
  if (gPowerState != POWER_STATE_CRITICAL_HIBERNATE) {
    ensureTimeSync();
    updateDailyScheduleIfNeeded();

    if (gNextDailyReportEpoch > 0.0 && currentEpoch() >= gNextDailyReportEpoch) {
      sendDailyReport();
      scheduleNextDailyReport();
    }
  }

  // Sleep to reduce power consumption between loop iterations
  // Duration is controlled by the power conservation state machine.
  // Higher states = longer sleep = lower power draw.
  // For sleep durations longer than the watchdog timeout, we sleep in chunks
  // and kick the watchdog between each chunk to prevent a hardware reset.
  unsigned long sleepMs = getPowerStateSleepMs(gPowerState);
  #if defined(ARDUINO_OPTA) || defined(ARDUINO_ARCH_MBED)
    // Watchdog timeout is WATCHDOG_TIMEOUT_SECONDS (default 30s).
    // Sleep in chunks of at most half the watchdog timeout to stay safe.
    const unsigned long maxChunk = (WATCHDOG_TIMEOUT_SECONDS * 1000UL) / 2;  // 15s default
    unsigned long remaining = sleepMs;
    while (remaining > 0) {
      unsigned long chunk = (remaining > maxChunk) ? maxChunk : remaining;
      rtos::ThisThread::sleep_for(std::chrono::milliseconds(chunk));
      #ifdef WATCHDOG_AVAILABLE
        mbedWatchdog.kick();
      #endif
      remaining -= chunk;
    }
  #else
    delay(sleepMs);
  #endif
}

// Helper: Get recommended pulse sampling parameters based on expected rate
// This helps configure optimal sampling for the expected RPM/flow rate range
// Returns: pulseSampleDurationMs, pulseAccumulatedMode recommendations
static PulseSamplingRecommendation getRecommendedPulseSampling(float expectedRate) {
  PulseSamplingRecommendation rec;
  
  if (expectedRate <= 0.0f) {
    // No expected rate configured - use defaults
    rec.sampleDurationMs = RPM_SAMPLE_DURATION_MS;
    rec.accumulatedMode = false;
    rec.description = "Default (60s sample)";
  } else if (expectedRate < 1.0f) {
    // Very low rate (< 1 RPM/GPM): use accumulated mode
    // Count pulses over entire telemetry interval for accuracy
    rec.sampleDurationMs = 60000;  // 60s sample within each interval
    rec.accumulatedMode = true;
    rec.description = "Accumulated mode (very low rate)";
  } else if (expectedRate < 10.0f) {
    // Low rate (1-10 RPM/GPM): longer sample for accuracy
    rec.sampleDurationMs = 60000;  // 60 seconds
    rec.accumulatedMode = false;
    rec.description = "60s sample (low rate)";
  } else if (expectedRate < 100.0f) {
    // Medium rate (10-100 RPM/GPM): moderate sample
    rec.sampleDurationMs = 30000;  // 30 seconds
    rec.accumulatedMode = false;
    rec.description = "30s sample (medium rate)";
  } else if (expectedRate < 1000.0f) {
    // High rate (100-1000 RPM/GPM): shorter sample is sufficient
    rec.sampleDurationMs = 10000;  // 10 seconds
    rec.accumulatedMode = false;
    rec.description = "10s sample (high rate)";
  } else {
    // Very high rate (> 1000 RPM): quick sample
    rec.sampleDurationMs = 3000;   // 3 seconds
    rec.accumulatedMode = false;
    rec.description = "3s sample (very high rate)";
  }
  
  return rec;
}

// Helper function: Get tank height/capacity based on sensor configuration
static float getMonitorHeight(const MonitorConfig &cfg) {
  if (cfg.sensorInterface == SENSOR_CURRENT_LOOP) {
    if (cfg.currentLoopType == CURRENT_LOOP_ULTRASONIC) {
      // For ultrasonic sensors, mount height IS the tank height (distance to bottom)
      return cfg.sensorMountHeight;
    } else {
      // For pressure sensors, max range + mount height approximates full tank height
      float rangeInches = cfg.sensorRangeMax * getPressureConversionFactor(cfg.sensorRangeUnit);
      return rangeInches + cfg.sensorMountHeight;
    }
  } else if (cfg.sensorInterface == SENSOR_ANALOG) {
    // For analog sensors, max range + mount height approximates full tank height
    float rangeInches = cfg.sensorRangeMax * getPressureConversionFactor(cfg.sensorRangeUnit);
    return rangeInches + cfg.sensorMountHeight;
  } else if (cfg.sensorInterface == SENSOR_DIGITAL) {
    // Digital sensors are binary, treat 1.0 as full
    return 1.0f;
  }
  return 0.0f;
}

static void initializeStorage() {
#ifdef FILESYSTEM_AVAILABLE
  #if defined(ARDUINO_OPTA) || defined(ARDUINO_ARCH_MBED)
    // Mbed OS LittleFileSystem initialization
    gStorageAvailable = false;
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
        Serial.println(F("LittleFS format failed; running without persistence"));
        delete mbedFS;
        mbedFS = nullptr;
        return;
      }
    }
    gStorageAvailable = true;
    Serial.println(F("Mbed OS LittleFileSystem initialized"));
  #else
    // STM32duino LittleFS
    if (!LittleFS.begin()) {
      gStorageAvailable = false;
      Serial.println(F("LittleFS init failed; running without persistence"));
      return;
    }
    gStorageAvailable = true;
  #endif
#else
  Serial.println(F("Warning: Filesystem not available on this platform - configuration will not persist"));
#endif
}

static void ensureConfigLoaded() {
#ifdef FILESYSTEM_AVAILABLE
  if (!isStorageAvailable()) {
    // Degraded mode: run with defaults in RAM only.
    createDefaultConfig(gConfig);
    Serial.println(F("Warning: Using default config (filesystem unavailable)"));
    return;
  }
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
  cfg.monitorCount = 1;

  cfg.monitors[0].id = 'A';
  strlcpy(cfg.monitors[0].name, "Primary Tank", sizeof(cfg.monitors[0].name));
  cfg.monitors[0].contents[0] = '\0'; // Empty by default
  cfg.monitors[0].monitorNumber = 1;
  cfg.monitors[0].objectType = OBJECT_TANK;          // Default: tank level monitoring
  cfg.monitors[0].sensorInterface = SENSOR_ANALOG;   // Default: analog voltage sensor
  cfg.monitors[0].primaryPin = 0; // A0 on Opta Ext
  cfg.monitors[0].secondaryPin = -1;
  cfg.monitors[0].currentLoopChannel = -1;
  cfg.monitors[0].pulsePin = -1; // No pulse sensor by default
  cfg.monitors[0].pulsesPerUnit = 1; // Default: 1 pulse per revolution/gallon
  cfg.monitors[0].hallEffectType = HALL_EFFECT_UNIPOLAR; // Default: unipolar sensor
  cfg.monitors[0].hallEffectDetection = HALL_DETECT_PULSE; // Default: pulse counting method
  cfg.monitors[0].pulseSampleDurationMs = RPM_SAMPLE_DURATION_MS; // Default: 60 seconds
  cfg.monitors[0].pulseAccumulatedMode = false; // Default: single sample mode
  cfg.monitors[0].highAlarmThreshold = 100.0f;
  cfg.monitors[0].lowAlarmThreshold = 20.0f;
  cfg.monitors[0].hysteresisValue = 2.0f; // 2 unit hysteresis band
  cfg.monitors[0].enableDailyReport = true;
  cfg.monitors[0].enableAlarmSms = true;
  cfg.monitors[0].enableServerUpload = true;
  cfg.monitors[0].relayTargetClient[0] = '\0'; // No relay target by default
  cfg.monitors[0].relayMask = 0; // No relays triggered by default
  cfg.monitors[0].relayTrigger = RELAY_TRIGGER_ANY; // Default: trigger on any alarm
  cfg.monitors[0].relayMode = RELAY_MODE_MOMENTARY; // Default: momentary activation
  // Default: all relays use 30 minutes (0 = use default)
  for (uint8_t r = 0; r < 4; ++r) {
    cfg.monitors[0].relayMomentarySeconds[r] = 0;
  }
  cfg.monitors[0].digitalTrigger[0] = '\0'; // Not a digital sensor by default
  strlcpy(cfg.monitors[0].digitalSwitchMode, "NO", sizeof(cfg.monitors[0].digitalSwitchMode)); // Default: normally-open
  cfg.monitors[0].currentLoopType = CURRENT_LOOP_PRESSURE; // Default: pressure sensor (most common)
  cfg.monitors[0].sensorMountHeight = 0.0f; // Default: sensor at tank bottom
  cfg.monitors[0].sensorRangeMin = 0.0f;    // Default: 0 (e.g., 0 PSI or 0 meters)
  cfg.monitors[0].sensorRangeMax = 5.0f;    // Default: 5 (e.g., 5 PSI for typical pressure sensor)
  strlcpy(cfg.monitors[0].sensorRangeUnit, "PSI", sizeof(cfg.monitors[0].sensorRangeUnit)); // Default: PSI
  cfg.monitors[0].analogVoltageMin = 0.0f;  // Default: 0V (for 0-10V sensors)
  cfg.monitors[0].analogVoltageMax = 10.0f; // Default: 10V (for 0-10V sensors)
  strlcpy(cfg.monitors[0].measurementUnit, "inches", sizeof(cfg.monitors[0].measurementUnit)); // Default: inches
  cfg.monitors[0].expectedPulseRate = 0.0f; // Default: not configured (0 = no baseline)
  // Tank unload tracking defaults (disabled)
  cfg.monitors[0].trackUnloads = false;     // Default: not a fill-and-empty tank
  cfg.monitors[0].unloadEmptyHeight = UNLOAD_DEFAULT_EMPTY_HEIGHT; // Default empty reading
  cfg.monitors[0].unloadDropThreshold = 0.0f; // Default: use percentage instead
  cfg.monitors[0].unloadDropPercent = UNLOAD_DEFAULT_DROP_PERCENT; // Default: 50% drop = unload
  cfg.monitors[0].unloadAlarmSms = false;   // Default: no SMS on unload
  cfg.monitors[0].unloadAlarmEmail = true;  // Default: include in email summary
  
  // Clear button defaults (disabled)
  cfg.clearButtonPin = -1;           // -1 = disabled
  cfg.clearButtonActiveHigh = false; // Active LOW with pullup (button connects to GND)
  
  // Power saving defaults (grid-tied, no special power saving)
  cfg.solarPowered = false;          // false = grid-tied (default)
  
  // I2C address defaults
  cfg.currentLoopI2cAddress = CURRENT_LOOP_I2C_ADDRESS; // Default 0x64
  
  // Solar/Battery charger monitoring defaults (disabled)
  // Requires: Arduino Opta RS485 + Morningstar MRC-1 adapter + SunSaver MPPT
  cfg.solarCharger.enabled = false;                          // Disabled by default
  cfg.solarCharger.modbusSlaveId = SOLAR_DEFAULT_SLAVE_ID;   // Default: 1
  cfg.solarCharger.modbusBaudRate = SOLAR_DEFAULT_BAUD_RATE; // Default: 9600
  cfg.solarCharger.modbusTimeoutMs = SOLAR_DEFAULT_TIMEOUT_MS; // Default: 200ms
  cfg.solarCharger.pollIntervalSec = SOLAR_DEFAULT_POLL_INTERVAL_SEC; // Default: 60s
  cfg.solarCharger.batteryLowVoltage = BATTERY_VOLTAGE_LOW;  // Default: 11.8V
  cfg.solarCharger.batteryCriticalVoltage = BATTERY_VOLTAGE_CRITICAL; // Default: 11.5V
  cfg.solarCharger.batteryHighVoltage = BATTERY_VOLTAGE_HIGH; // Default: 14.8V
  cfg.solarCharger.alertOnLowBattery = true;                 // Send alerts for low battery
  cfg.solarCharger.alertOnFault = true;                      // Send alerts for charger faults
  cfg.solarCharger.alertOnCommFailure = false;               // Don't alert on comm failures (too noisy)
  cfg.solarCharger.includeInDailyReport = true;              // Include in daily report
  
  // Battery voltage monitoring defaults (Notecard direct to battery)
  // Requires: Notecard VIN wired directly to 12V battery (not through 5V regulator)
  initBatteryConfig(&cfg.batteryMonitor, BATTERY_TYPE_LEAD_ACID_12V);
  cfg.batteryMonitor.enabled = false;                        // Disabled by default
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
    
    std::unique_ptr<JsonDocument> docPtr(new JsonDocument());
    if (!docPtr) {
      free(buffer);
      return false;
    }
    JsonDocument &doc = *docPtr;
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

    std::unique_ptr<JsonDocument> docPtr(new JsonDocument());
    if (!docPtr) {
      file.close();
      return false;
    }
    JsonDocument &doc = *docPtr;
    DeserializationError err = deserializeJson(doc, file);
    file.close();
  #endif

  if (err) {
    Serial.println(F("Config deserialization failed"));
    return false;
  }

  memset(&cfg, 0, sizeof(ClientConfig));

  strlcpy(cfg.siteName, doc["site"].as<const char *>() ? doc["site"].as<const char *>() : "", sizeof(cfg.siteName));
  strlcpy(cfg.deviceUid, doc["deviceUid"].as<const char *>() ? doc["deviceUid"].as<const char *>() : "", sizeof(cfg.deviceUid));
  strlcpy(cfg.deviceLabel, doc["deviceLabel"].as<const char *>() ? doc["deviceLabel"].as<const char *>() : "", sizeof(cfg.deviceLabel));
  strlcpy(cfg.serverFleet, doc["serverFleet"].as<const char *>() ? doc["serverFleet"].as<const char *>() : "", sizeof(cfg.serverFleet));
  strlcpy(cfg.clientFleet, doc["clientFleet"].as<const char *>() ? doc["clientFleet"].as<const char *>() : "", sizeof(cfg.clientFleet));
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
  
  // Load I2C address configuration (allows runtime override of compile-time default)
  cfg.currentLoopI2cAddress = doc["currentLoopI2cAddress"].is<int>() 
    ? (uint8_t)doc["currentLoopI2cAddress"].as<int>() 
    : CURRENT_LOOP_I2C_ADDRESS;

  // Load solar charger configuration (SunSaver MPPT via RS-485)
  JsonObject solarCfg = doc["solarCharger"].as<JsonObject>();
  if (solarCfg) {
    cfg.solarCharger.enabled = solarCfg["enabled"].is<bool>() ? solarCfg["enabled"].as<bool>() : false;
    cfg.solarCharger.modbusSlaveId = solarCfg["slaveId"].is<int>() ? (uint8_t)solarCfg["slaveId"].as<int>() : SOLAR_DEFAULT_SLAVE_ID;
    cfg.solarCharger.modbusBaudRate = solarCfg["baudRate"].is<int>() ? (uint16_t)solarCfg["baudRate"].as<int>() : SOLAR_DEFAULT_BAUD_RATE;
    cfg.solarCharger.modbusTimeoutMs = solarCfg["timeoutMs"].is<int>() ? (uint16_t)solarCfg["timeoutMs"].as<int>() : SOLAR_DEFAULT_TIMEOUT_MS;
    cfg.solarCharger.pollIntervalSec = solarCfg["pollIntervalSec"].is<int>() ? (uint16_t)solarCfg["pollIntervalSec"].as<int>() : SOLAR_DEFAULT_POLL_INTERVAL_SEC;
    cfg.solarCharger.batteryLowVoltage = solarCfg["batteryLowV"].is<float>() ? solarCfg["batteryLowV"].as<float>() : BATTERY_VOLTAGE_LOW;
    cfg.solarCharger.batteryCriticalVoltage = solarCfg["batteryCriticalV"].is<float>() ? solarCfg["batteryCriticalV"].as<float>() : BATTERY_VOLTAGE_CRITICAL;
    cfg.solarCharger.batteryHighVoltage = solarCfg["batteryHighV"].is<float>() ? solarCfg["batteryHighV"].as<float>() : BATTERY_VOLTAGE_HIGH;
    cfg.solarCharger.alertOnLowBattery = solarCfg["alertOnLow"].is<bool>() ? solarCfg["alertOnLow"].as<bool>() : true;
    cfg.solarCharger.alertOnFault = solarCfg["alertOnFault"].is<bool>() ? solarCfg["alertOnFault"].as<bool>() : true;
    cfg.solarCharger.alertOnCommFailure = solarCfg["alertOnCommFail"].is<bool>() ? solarCfg["alertOnCommFail"].as<bool>() : false;
    cfg.solarCharger.includeInDailyReport = solarCfg["includeInDaily"].is<bool>() ? solarCfg["includeInDaily"].as<bool>() : true;
  } else {
    // Default values if solarCharger object not present
    cfg.solarCharger.enabled = false;
    cfg.solarCharger.modbusSlaveId = SOLAR_DEFAULT_SLAVE_ID;
    cfg.solarCharger.modbusBaudRate = SOLAR_DEFAULT_BAUD_RATE;
    cfg.solarCharger.modbusTimeoutMs = SOLAR_DEFAULT_TIMEOUT_MS;
    cfg.solarCharger.pollIntervalSec = SOLAR_DEFAULT_POLL_INTERVAL_SEC;
    cfg.solarCharger.batteryLowVoltage = BATTERY_VOLTAGE_LOW;
    cfg.solarCharger.batteryCriticalVoltage = BATTERY_VOLTAGE_CRITICAL;
    cfg.solarCharger.batteryHighVoltage = BATTERY_VOLTAGE_HIGH;
    cfg.solarCharger.alertOnLowBattery = true;
    cfg.solarCharger.alertOnFault = true;
    cfg.solarCharger.alertOnCommFailure = false;
    cfg.solarCharger.includeInDailyReport = true;
  }

  // Load battery voltage monitoring configuration (Notecard direct to battery)
  JsonObject batCfg = doc["batteryMonitor"].as<JsonObject>();
  if (batCfg) {
    cfg.batteryMonitor.enabled = batCfg["enabled"].is<bool>() ? batCfg["enabled"].as<bool>() : false;
    cfg.batteryMonitor.batteryType = batCfg["type"].is<int>() ? (BatteryType)batCfg["type"].as<int>() : BATTERY_TYPE_LEAD_ACID_12V;
    cfg.batteryMonitor.highVoltage = batCfg["highV"].is<float>() ? batCfg["highV"].as<float>() : 14.8f;
    cfg.batteryMonitor.normalVoltage = batCfg["normalV"].is<float>() ? batCfg["normalV"].as<float>() : LEAD_ACID_12V_NORMAL;
    cfg.batteryMonitor.lowVoltage = batCfg["lowV"].is<float>() ? batCfg["lowV"].as<float>() : LEAD_ACID_12V_LOW;
    cfg.batteryMonitor.criticalVoltage = batCfg["criticalV"].is<float>() ? batCfg["criticalV"].as<float>() : LEAD_ACID_12V_CRITICAL;
    cfg.batteryMonitor.calibrationOffset = batCfg["calibration"].is<float>() ? batCfg["calibration"].as<float>() : BATTERY_DEFAULT_CALIBRATION;
    cfg.batteryMonitor.pollIntervalSec = batCfg["pollIntervalSec"].is<int>() ? (uint16_t)batCfg["pollIntervalSec"].as<int>() : BATTERY_DEFAULT_POLL_INTERVAL_SEC;
    cfg.batteryMonitor.trendAnalysisHours = batCfg["trendHours"].is<int>() ? (uint16_t)batCfg["trendHours"].as<int>() : BATTERY_DEFAULT_TREND_HOURS;
    cfg.batteryMonitor.alertOnLow = batCfg["alertOnLow"].is<bool>() ? batCfg["alertOnLow"].as<bool>() : true;
    cfg.batteryMonitor.alertOnCritical = batCfg["alertOnCritical"].is<bool>() ? batCfg["alertOnCritical"].as<bool>() : true;
    cfg.batteryMonitor.alertOnDeclining = batCfg["alertOnDecline"].is<bool>() ? batCfg["alertOnDecline"].as<bool>() : true;
    cfg.batteryMonitor.alertOnRecovery = batCfg["alertOnRecovery"].is<bool>() ? batCfg["alertOnRecovery"].as<bool>() : false;
    cfg.batteryMonitor.declineAlertThreshold = batCfg["declineThreshold"].is<float>() ? batCfg["declineThreshold"].as<float>() : BATTERY_DEFAULT_DECLINE_THRESHOLD;
    cfg.batteryMonitor.includeInDailyReport = batCfg["includeInDaily"].is<bool>() ? batCfg["includeInDaily"].as<bool>() : true;
  } else {
    // Default values if batteryMonitor object not present
    initBatteryConfig(&cfg.batteryMonitor, BATTERY_TYPE_LEAD_ACID_12V);
    cfg.batteryMonitor.enabled = false;
  }

  // Support both old "tanks" and new "monitors" array names
  JsonArray monitorsArray = doc["monitors"].as<JsonArray>();
  if (!monitorsArray) {
    monitorsArray = doc["tanks"].as<JsonArray>();
  }
  cfg.monitorCount = monitorsArray ? min<uint8_t>(monitorsArray.size(), MAX_TANKS) : 0;

  for (uint8_t i = 0; i < cfg.monitorCount; ++i) {
    JsonObject t = monitorsArray[i];
    cfg.monitors[i].id = t["id"].as<const char *>() ? t["id"].as<const char *>()[0] : ('A' + i);
    strlcpy(cfg.monitors[i].name, t["name"].as<const char *>() ? t["name"].as<const char *>() : "Tank", sizeof(cfg.monitors[i].name));
    strlcpy(cfg.monitors[i].contents, t["contents"].as<const char *>() ? t["contents"].as<const char *>() : "", sizeof(cfg.monitors[i].contents));
    cfg.monitors[i].monitorNumber = t["number"].is<uint8_t>() ? t["number"].as<uint8_t>() : (i + 1);
    
    // Load object type (what is being monitored)
    const char *objType = t["objectType"].as<const char *>();
    if (objType && strcmp(objType, "engine") == 0) {
      cfg.monitors[i].objectType = OBJECT_ENGINE;
    } else if (objType && strcmp(objType, "pump") == 0) {
      cfg.monitors[i].objectType = OBJECT_PUMP;
    } else if (objType && strcmp(objType, "gas") == 0) {
      cfg.monitors[i].objectType = OBJECT_GAS;
    } else if (objType && strcmp(objType, "flow") == 0) {
      cfg.monitors[i].objectType = OBJECT_FLOW;
    } else {
      cfg.monitors[i].objectType = OBJECT_TANK; // Default
    }
    
    // Load sensor interface (how measurement is taken)
    // Support both old "sensor" and new "sensorInterface" field names
    const char *sensor = t["sensorInterface"].as<const char *>();
    if (!sensor) sensor = t["sensor"].as<const char *>();
    if (sensor && strcmp(sensor, "digital") == 0) {
      cfg.monitors[i].sensorInterface = SENSOR_DIGITAL;
    } else if (sensor && (strcmp(sensor, "current") == 0 || strcmp(sensor, "currentLoop") == 0)) {
      cfg.monitors[i].sensorInterface = SENSOR_CURRENT_LOOP;
    } else if (sensor && (strcmp(sensor, "rpm") == 0 || strcmp(sensor, "pulse") == 0)) {
      cfg.monitors[i].sensorInterface = SENSOR_PULSE;
    } else {
      cfg.monitors[i].sensorInterface = SENSOR_ANALOG;
    }
    cfg.monitors[i].primaryPin = t["primaryPin"].is<int>() ? t["primaryPin"].as<int>() : (cfg.monitors[i].sensorInterface == SENSOR_DIGITAL ? 2 : 0);
    cfg.monitors[i].secondaryPin = t["secondaryPin"].is<int>() ? t["secondaryPin"].as<int>() : -1;
    cfg.monitors[i].currentLoopChannel = t["loopChannel"].is<int>() ? t["loopChannel"].as<int>() : -1;
    // Support both old "rpmPin" and new "pulsePin" field names
    cfg.monitors[i].pulsePin = t["pulsePin"].is<int>() ? t["pulsePin"].as<int>() : 
                               (t["rpmPin"].is<int>() ? t["rpmPin"].as<int>() : -1);
    // Support both old "pulsesPerRev" and new "pulsesPerUnit" field names
    cfg.monitors[i].pulsesPerUnit = t["pulsesPerUnit"].is<uint8_t>() ? max((uint8_t)1, t["pulsesPerUnit"].as<uint8_t>()) :
                                    (t["pulsesPerRev"].is<uint8_t>() ? max((uint8_t)1, t["pulsesPerRev"].as<uint8_t>()) : 1);
    // Load hall effect sensor type
    const char *hallType = t["hallEffectType"].as<const char *>();
    if (hallType && strcmp(hallType, "bipolar") == 0) {
      cfg.monitors[i].hallEffectType = HALL_EFFECT_BIPOLAR;
    } else if (hallType && strcmp(hallType, "omnipolar") == 0) {
      cfg.monitors[i].hallEffectType = HALL_EFFECT_OMNIPOLAR;
    } else if (hallType && strcmp(hallType, "analog") == 0) {
      cfg.monitors[i].hallEffectType = HALL_EFFECT_ANALOG;
    } else if (hallType && strcmp(hallType, "unipolar") == 0) {
      cfg.monitors[i].hallEffectType = HALL_EFFECT_UNIPOLAR;
    } else {
      cfg.monitors[i].hallEffectType = HALL_EFFECT_UNIPOLAR; // Default
    }
    // Load hall effect detection method
    const char *hallDetect = t["hallEffectDetection"].as<const char *>();
    if (hallDetect && strcmp(hallDetect, "time") == 0) {
      cfg.monitors[i].hallEffectDetection = HALL_DETECT_TIME_BASED;
    } else if (hallDetect && strcmp(hallDetect, "pulse") == 0) {
      cfg.monitors[i].hallEffectDetection = HALL_DETECT_PULSE;
    } else {
      cfg.monitors[i].hallEffectDetection = HALL_DETECT_PULSE; // Default
    }
    // Load RPM sampling configuration
    cfg.monitors[i].pulseSampleDurationMs = t["pulseSampleDurationMs"].is<uint32_t>() ? 
        t["pulseSampleDurationMs"].as<uint32_t>() : 
        (t["rpmSampleDurationMs"].is<uint32_t>() ? t["rpmSampleDurationMs"].as<uint32_t>() : RPM_SAMPLE_DURATION_MS);
    cfg.monitors[i].pulseAccumulatedMode = t["pulseAccumulatedMode"].is<bool>() ? 
        t["pulseAccumulatedMode"].as<bool>() : 
        (t["rpmAccumulatedMode"].is<bool>() ? t["rpmAccumulatedMode"].as<bool>() : false);
    // Expected pulse rate for baseline comparison (by object type)
    cfg.monitors[i].expectedPulseRate = t["expectedPulseRate"].is<float>() ? 
        t["expectedPulseRate"].as<float>() : 0.0f;
    cfg.monitors[i].highAlarmThreshold = t["highAlarm"].is<float>() ? t["highAlarm"].as<float>() : 100.0f;
    cfg.monitors[i].lowAlarmThreshold = t["lowAlarm"].is<float>() ? t["lowAlarm"].as<float>() : 20.0f;
    cfg.monitors[i].hysteresisValue = t["hysteresis"].is<float>() ? t["hysteresis"].as<float>() : 2.0f;
    cfg.monitors[i].enableDailyReport = t["daily"].is<bool>() ? t["daily"].as<bool>() : true;
    cfg.monitors[i].enableAlarmSms = t["alarmSms"].is<bool>() ? t["alarmSms"].as<bool>() : true;
    cfg.monitors[i].enableServerUpload = t["upload"].is<bool>() ? t["upload"].as<bool>() : true;
    // Load relay control settings
    const char *relayTarget = t["relayTargetClient"].as<const char *>();
    strlcpy(cfg.monitors[i].relayTargetClient, relayTarget ? relayTarget : "", sizeof(cfg.monitors[i].relayTargetClient));
    cfg.monitors[i].relayMask = t["relayMask"].is<uint8_t>() ? t["relayMask"].as<uint8_t>() : 0;
    // Load relay trigger condition (defaults to 'any' for backwards compatibility)
    const char *relayTriggerStr = t["relayTrigger"].as<const char *>();
    if (relayTriggerStr && strcmp(relayTriggerStr, "high") == 0) {
      cfg.monitors[i].relayTrigger = RELAY_TRIGGER_HIGH;
    } else if (relayTriggerStr && strcmp(relayTriggerStr, "low") == 0) {
      cfg.monitors[i].relayTrigger = RELAY_TRIGGER_LOW;
    } else {
      cfg.monitors[i].relayTrigger = RELAY_TRIGGER_ANY;
    }
    // Load relay mode (defaults to momentary for backwards compatibility)
    const char *relayModeStr = t["relayMode"].as<const char *>();
    if (relayModeStr && strcmp(relayModeStr, "until_clear") == 0) {
      cfg.monitors[i].relayMode = RELAY_MODE_UNTIL_CLEAR;
    } else if (relayModeStr && strcmp(relayModeStr, "manual_reset") == 0) {
      cfg.monitors[i].relayMode = RELAY_MODE_MANUAL_RESET;
    } else {
      cfg.monitors[i].relayMode = RELAY_MODE_MOMENTARY;
    }
    // Load per-relay momentary durations (0 = use default 30 min)
    JsonArrayConst durations = t["relayMomentaryDurations"].as<JsonArrayConst>();
    for (uint8_t r = 0; r < 4; ++r) {
      if (durations && r < durations.size()) {
        cfg.monitors[i].relayMomentarySeconds[r] = durations[r].as<uint16_t>();
      } else {
        cfg.monitors[i].relayMomentarySeconds[r] = 0; // Default
      }
    }
    // Load digital sensor trigger state (for float switches)
    const char *digitalTriggerStr = t["digitalTrigger"].as<const char *>();
    strlcpy(cfg.monitors[i].digitalTrigger, digitalTriggerStr ? digitalTriggerStr : "", sizeof(cfg.monitors[i].digitalTrigger));
    // Load digital switch mode (NO = normally-open, NC = normally-closed)
    const char *digitalSwitchModeStr = t["digitalSwitchMode"].as<const char *>();
    if (digitalSwitchModeStr && strcmp(digitalSwitchModeStr, "NC") == 0) {
      strlcpy(cfg.monitors[i].digitalSwitchMode, "NC", sizeof(cfg.monitors[i].digitalSwitchMode));
    } else {
      strlcpy(cfg.monitors[i].digitalSwitchMode, "NO", sizeof(cfg.monitors[i].digitalSwitchMode)); // Default: normally-open
    }
    // Load 4-20mA current loop sensor type (pressure or ultrasonic)
    const char *currentLoopTypeStr = t["currentLoopType"].as<const char *>();
    if (currentLoopTypeStr && strcmp(currentLoopTypeStr, "ultrasonic") == 0) {
      cfg.monitors[i].currentLoopType = CURRENT_LOOP_ULTRASONIC;
    } else {
      cfg.monitors[i].currentLoopType = CURRENT_LOOP_PRESSURE; // Default: pressure sensor
    }
    // Load sensor mount height (for calibration) - validate non-negative
    cfg.monitors[i].sensorMountHeight = t["sensorMountHeight"].is<float>() ? fmaxf(0.0f, t["sensorMountHeight"].as<float>()) : 0.0f;
    // Load sensor native range settings
    cfg.monitors[i].sensorRangeMin = t["sensorRangeMin"].is<float>() ? t["sensorRangeMin"].as<float>() : 0.0f;
    cfg.monitors[i].sensorRangeMax = t["sensorRangeMax"].is<float>() ? t["sensorRangeMax"].as<float>() : 5.0f;
    const char *rangeUnitStr = t["sensorRangeUnit"].as<const char *>();
    strlcpy(cfg.monitors[i].sensorRangeUnit, rangeUnitStr ? rangeUnitStr : "PSI", sizeof(cfg.monitors[i].sensorRangeUnit));
    // Load analog voltage range settings (for 0-10V, 1-5V, etc. sensors)
    cfg.monitors[i].analogVoltageMin = t["analogVoltageMin"].is<float>() ? t["analogVoltageMin"].as<float>() : 0.0f;
    cfg.monitors[i].analogVoltageMax = t["analogVoltageMax"].is<float>() ? t["analogVoltageMax"].as<float>() : 10.0f;
    // Load tank unload tracking settings
    cfg.monitors[i].trackUnloads = t["trackUnloads"].is<bool>() ? t["trackUnloads"].as<bool>() : false;
    cfg.monitors[i].unloadEmptyHeight = t["unloadEmptyHeight"].is<float>() ? t["unloadEmptyHeight"].as<float>() : UNLOAD_DEFAULT_EMPTY_HEIGHT;
    cfg.monitors[i].unloadDropThreshold = t["unloadDropThreshold"].is<float>() ? t["unloadDropThreshold"].as<float>() : 0.0f;
    cfg.monitors[i].unloadDropPercent = t["unloadDropPercent"].is<float>() ? t["unloadDropPercent"].as<float>() : UNLOAD_DEFAULT_DROP_PERCENT;
    cfg.monitors[i].unloadAlarmSms = t["unloadAlarmSms"].is<bool>() ? t["unloadAlarmSms"].as<bool>() : false;
    cfg.monitors[i].unloadAlarmEmail = t["unloadAlarmEmail"].is<bool>() ? t["unloadAlarmEmail"].as<bool>() : true;
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
  
  std::unique_ptr<JsonDocument> docPtr(new JsonDocument());
  if (!docPtr) return false;
  JsonDocument &doc = *docPtr;

  doc["site"] = cfg.siteName;
  doc["deviceUid"] = cfg.deviceUid;
  doc["deviceLabel"] = cfg.deviceLabel;
  doc["clientFleet"] = cfg.clientFleet;
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
  
  // Save I2C address configuration
  doc["currentLoopI2cAddress"] = cfg.currentLoopI2cAddress;

  // Save solar charger configuration (SunSaver MPPT via RS-485)
  JsonObject solarCfg = doc["solarCharger"].to<JsonObject>();
  solarCfg["enabled"] = cfg.solarCharger.enabled;
  solarCfg["slaveId"] = cfg.solarCharger.modbusSlaveId;
  solarCfg["baudRate"] = cfg.solarCharger.modbusBaudRate;
  solarCfg["timeoutMs"] = cfg.solarCharger.modbusTimeoutMs;
  solarCfg["pollIntervalSec"] = cfg.solarCharger.pollIntervalSec;
  solarCfg["batteryLowV"] = cfg.solarCharger.batteryLowVoltage;
  solarCfg["batteryCriticalV"] = cfg.solarCharger.batteryCriticalVoltage;
  solarCfg["batteryHighV"] = cfg.solarCharger.batteryHighVoltage;
  solarCfg["alertOnLow"] = cfg.solarCharger.alertOnLowBattery;
  solarCfg["alertOnFault"] = cfg.solarCharger.alertOnFault;
  solarCfg["alertOnCommFail"] = cfg.solarCharger.alertOnCommFailure;
  solarCfg["includeInDaily"] = cfg.solarCharger.includeInDailyReport;

  // Save battery voltage monitoring configuration (Notecard direct to battery)
  JsonObject batCfg = doc["batteryMonitor"].to<JsonObject>();
  batCfg["enabled"] = cfg.batteryMonitor.enabled;
  batCfg["type"] = (int)cfg.batteryMonitor.batteryType;
  batCfg["highV"] = cfg.batteryMonitor.highVoltage;
  batCfg["normalV"] = cfg.batteryMonitor.normalVoltage;
  batCfg["lowV"] = cfg.batteryMonitor.lowVoltage;
  batCfg["criticalV"] = cfg.batteryMonitor.criticalVoltage;
  batCfg["calibration"] = cfg.batteryMonitor.calibrationOffset;
  batCfg["pollIntervalSec"] = cfg.batteryMonitor.pollIntervalSec;
  batCfg["trendHours"] = cfg.batteryMonitor.trendAnalysisHours;
  batCfg["alertOnLow"] = cfg.batteryMonitor.alertOnLow;
  batCfg["alertOnCritical"] = cfg.batteryMonitor.alertOnCritical;
  batCfg["alertOnDecline"] = cfg.batteryMonitor.alertOnDeclining;
  batCfg["alertOnRecovery"] = cfg.batteryMonitor.alertOnRecovery;
  batCfg["declineThreshold"] = cfg.batteryMonitor.declineAlertThreshold;
  batCfg["includeInDaily"] = cfg.batteryMonitor.includeInDailyReport;

  JsonArray tanks = doc["tanks"].to<JsonArray>();
  for (uint8_t i = 0; i < cfg.monitorCount; ++i) {
    JsonObject t = tanks.add<JsonObject>();
    char idBuffer[2] = {cfg.monitors[i].id, '\0'};
    t["id"] = idBuffer;
    t["name"] = cfg.monitors[i].name;
    if (cfg.monitors[i].contents[0] != '\0') {
      t["contents"] = cfg.monitors[i].contents;
    }
    t["number"] = cfg.monitors[i].monitorNumber;
    switch (cfg.monitors[i].sensorInterface) {
      case SENSOR_DIGITAL: t["sensor"] = "digital"; break;
      case SENSOR_CURRENT_LOOP: t["sensor"] = "current"; break;
      case SENSOR_PULSE: t["sensor"] = "rpm"; break;
      default: t["sensor"] = "analog"; break;
    }
    t["primaryPin"] = cfg.monitors[i].primaryPin;
    t["secondaryPin"] = cfg.monitors[i].secondaryPin;
    t["loopChannel"] = cfg.monitors[i].currentLoopChannel;
    t["rpmPin"] = cfg.monitors[i].pulsePin;
    t["pulsesPerRev"] = cfg.monitors[i].pulsesPerUnit;
    // Save hall effect sensor type
    switch (cfg.monitors[i].hallEffectType) {
      case HALL_EFFECT_BIPOLAR: t["hallEffectType"] = "bipolar"; break;
      case HALL_EFFECT_OMNIPOLAR: t["hallEffectType"] = "omnipolar"; break;
      case HALL_EFFECT_ANALOG: t["hallEffectType"] = "analog"; break;
      case HALL_EFFECT_UNIPOLAR:
      default: t["hallEffectType"] = "unipolar"; break;
    }
    // Save hall effect detection method
    switch (cfg.monitors[i].hallEffectDetection) {
      case HALL_DETECT_TIME_BASED: t["hallEffectDetection"] = "time"; break;
      case HALL_DETECT_PULSE:
      default: t["hallEffectDetection"] = "pulse"; break;
    }
    // Save RPM sampling configuration
    t["pulseSampleDurationMs"] = cfg.monitors[i].pulseSampleDurationMs;
    t["pulseAccumulatedMode"] = cfg.monitors[i].pulseAccumulatedMode;
    if (cfg.monitors[i].expectedPulseRate > 0.0f) {
      t["expectedPulseRate"] = cfg.monitors[i].expectedPulseRate;
    }
    t["highAlarm"] = cfg.monitors[i].highAlarmThreshold;
    t["lowAlarm"] = cfg.monitors[i].lowAlarmThreshold;
    t["hysteresis"] = cfg.monitors[i].hysteresisValue;
    t["daily"] = cfg.monitors[i].enableDailyReport;
    t["alarmSms"] = cfg.monitors[i].enableAlarmSms;
    t["upload"] = cfg.monitors[i].enableServerUpload;
    // Save relay control settings
    t["relayTargetClient"] = cfg.monitors[i].relayTargetClient;
    t["relayMask"] = cfg.monitors[i].relayMask;
    // Save relay trigger condition as string
    switch (cfg.monitors[i].relayTrigger) {
      case RELAY_TRIGGER_HIGH: t["relayTrigger"] = "high"; break;
      case RELAY_TRIGGER_LOW: t["relayTrigger"] = "low"; break;
      default: t["relayTrigger"] = "any"; break;
    }
    // Save relay mode as string
    switch (cfg.monitors[i].relayMode) {
      case RELAY_MODE_UNTIL_CLEAR: t["relayMode"] = "until_clear"; break;
      case RELAY_MODE_MANUAL_RESET: t["relayMode"] = "manual_reset"; break;
      default: t["relayMode"] = "momentary"; break;
    }
    // Save per-relay momentary durations
    JsonArray durations = t["relayMomentaryDurations"].to<JsonArray>();
    for (uint8_t r = 0; r < 4; ++r) {
      durations.add(cfg.monitors[i].relayMomentarySeconds[r]);
    }
    // Save digital sensor trigger state (for float switches)
    if (cfg.monitors[i].digitalTrigger[0] != '\0') {
      t["digitalTrigger"] = cfg.monitors[i].digitalTrigger;
    }
    // Save digital switch mode (NO/NC)
    t["digitalSwitchMode"] = cfg.monitors[i].digitalSwitchMode;
    // Save 4-20mA current loop sensor type
    switch (cfg.monitors[i].currentLoopType) {
      case CURRENT_LOOP_ULTRASONIC: t["currentLoopType"] = "ultrasonic"; break;
      default: t["currentLoopType"] = "pressure"; break;
    }
    // Save sensor mount height (for calibration)
    t["sensorMountHeight"] = cfg.monitors[i].sensorMountHeight;
    // Save sensor native range settings
    t["sensorRangeMin"] = cfg.monitors[i].sensorRangeMin;
    t["sensorRangeMax"] = cfg.monitors[i].sensorRangeMax;
    t["sensorRangeUnit"] = cfg.monitors[i].sensorRangeUnit;
    // Save analog voltage range settings
    t["analogVoltageMin"] = cfg.monitors[i].analogVoltageMin;
    t["analogVoltageMax"] = cfg.monitors[i].analogVoltageMax;
    // Save tank unload tracking settings
    t["trackUnloads"] = cfg.monitors[i].trackUnloads;
    t["unloadEmptyHeight"] = cfg.monitors[i].unloadEmptyHeight;
    t["unloadDropThreshold"] = cfg.monitors[i].unloadDropThreshold;
    t["unloadDropPercent"] = cfg.monitors[i].unloadDropPercent;
    t["unloadAlarmSms"] = cfg.monitors[i].unloadAlarmSms;
    t["unloadAlarmEmail"] = cfg.monitors[i].unloadAlarmEmail;
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

  for (uint8_t i = 0; i < cfg.monitorCount; ++i) {
    if (cfg.monitors[i].sensorInterface == SENSOR_ANALOG) {
      needsAnalogExpansion = true;
    }
    if (cfg.monitors[i].sensorInterface == SENSOR_CURRENT_LOOP) {
      needsCurrentLoop = true;
      if (cfg.monitors[i].currentLoopType == CURRENT_LOOP_PRESSURE) {
        hasPressureSensor = true;
      } else if (cfg.monitors[i].currentLoopType == CURRENT_LOOP_ULTRASONIC) {
        hasUltrasonicSensor = true;
      }
    }
    if (cfg.monitors[i].sensorInterface == SENSOR_PULSE) {
      needsRpmSensor = true;
    }
    if (cfg.monitors[i].enableAlarmSms) {
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
  if (cfg.solarCharger.enabled) {
    Serial.println(F("Solar Charger Monitoring (SunSaver MPPT):"));
    Serial.println(F("  - Arduino Opta with RS485 (WiFi/RS485 model or RS485 shield)"));
    Serial.println(F("  - Morningstar MRC-1 (MeterBus to EIA-485 Adapter)"));
    Serial.println(F("    Powered by SunSaver via RJ-11, no external power needed"));
    Serial.println(F("  - RS485 Wiring: Opta A(-) to MRC-1 B(-), Opta B(+) to MRC-1 A(+)"));
    Serial.print(F("  - Modbus: Slave ID "));
    Serial.print(cfg.solarCharger.modbusSlaveId);
    Serial.print(F(", "));
    Serial.print(cfg.solarCharger.modbusBaudRate);
    Serial.println(F(" baud"));
  }
  if (cfg.batteryMonitor.enabled) {
    Serial.println(F("Battery Voltage Monitoring (Notecard direct):"));
    Serial.println(F("  - Notecard VIN wired directly to 12V battery"));
    Serial.println(F("  - Optional: Schottky diode for reverse polarity protection"));
    Serial.print(F("  - Battery type: "));
    switch (cfg.batteryMonitor.batteryType) {
      case BATTERY_TYPE_LEAD_ACID_12V: Serial.println(F("12V Lead-Acid (AGM/Flooded/Gel)")); break;
      case BATTERY_TYPE_LIFEPO4_12V:   Serial.println(F("12V LiFePO4 (4S)")); break;
      case BATTERY_TYPE_LIPO:          Serial.println(F("LiPo")); break;
      default:                         Serial.println(F("Custom")); break;
    }
    Serial.print(F("  - Thresholds: Low="));
    Serial.print(cfg.batteryMonitor.lowVoltage, 1);
    Serial.print(F("V, Critical="));
    Serial.print(cfg.batteryMonitor.criticalVoltage, 1);
    Serial.println(F("V"));
  }
  Serial.println(F("-----------------------------"));
}

static void configureNotecardHubMode() {
  // Configure Notecard hub mode based on power source
  J *req = notecard.newRequest("hub.set");
  if (req) {
    // Use configurable product UID - allows fleet-specific deployments without recompilation
    const char *productUid = (gConfig.productUid[0] != '\0') ? gConfig.productUid : DEFAULT_PRODUCT_UID;
    JAddStringToObject(req, "product", productUid);
    if (gConfig.clientFleet[0] != '\0') {
      JAddStringToObject(req, "fleet", gConfig.clientFleet);
    }
    Serial.print(F("Product UID: "));
    Serial.println(productUid);
    
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
  
  // Disable GPS location tracking for power savings
  // GPS is one of the most power-hungry components on the Notecard and is not used by this application
  req = notecard.newRequest("card.location.mode");
  if (req) {
    JAddStringToObject(req, "mode", "off");
    notecard.sendRequest(req);
  }
  
  // Disable accelerometer motion tracking for power savings
  // The accelerometer is not used by this tank monitoring application
  req = notecard.newRequest("card.motion.mode");
  if (req) {
    JAddStringToObject(req, "mode", "off");
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

// ============================================================================
// Device Firmware Update (DFU) via Blues Notecard
// ============================================================================

static void checkForFirmwareUpdate() {
  // Query Notecard for DFU status
  J *req = notecard.newRequest("dfu.status");
  if (!req) {
    return;
  }
  
  J *rsp = notecard.requestAndResponse(req);
  if (!rsp) {
    return;
  }
  
  // Check if firmware update is available
  // DFU status response fields:
  // - "on": true if DFU mode is active
  // - "version": firmware version available from Notehub
  // - "body": any additional status info
  bool dfuActive = JGetBool(rsp, "on");
  const char *version = JGetString(rsp, "version");
  
  if (dfuActive && version && strlen(version) > 0) {
    // Update available
    if (!gDfuUpdateAvailable || strcmp(gDfuVersion, version) != 0) {
      // New update detected
      gDfuUpdateAvailable = true;
      strlcpy(gDfuVersion, version, sizeof(gDfuVersion));
      
      Serial.println(F("========================================"));
      Serial.print(F("FIRMWARE UPDATE AVAILABLE: v"));
      Serial.println(gDfuVersion);
      Serial.print(F("Current version: "));
      Serial.println(F(FIRMWARE_VERSION));
      Serial.println(F("Device will auto-update on next check"));
      Serial.println(F("========================================"));
      
      addSerialLog("Firmware update available");
    }
  } else if (gDfuUpdateAvailable) {
    // Update was available but is now gone (applied or cancelled)
    gDfuUpdateAvailable = false;
    gDfuVersion[0] = '\0';
  }
  
  notecard.deleteResponse(rsp);
}

static void enableDfuMode() {
  if (gDfuInProgress) {
    Serial.println(F("DFU already in progress"));
    return;
  }
  
  Serial.println(F("========================================"));
  Serial.println(F("ENABLING DFU MODE"));
  Serial.println(F("Device will download and apply update"));
  Serial.println(F("System will reset when complete"));
  Serial.println(F("========================================"));
  
  // Mark DFU in progress to prevent multiple triggers
  gDfuInProgress = true;
  addSerialLog("DFU mode enabled - updating firmware");
  
  // Save any pending config before rebooting
  if (gConfigDirty) {
    persistConfigIfDirty();
  }
  
  // Enable DFU mode on Notecard
  J *req = notecard.newRequest("dfu.mode");
  if (!req) {
    Serial.println(F("ERROR: Failed to create DFU request"));
    gDfuInProgress = false;
    return;
  }
  
  JAddBoolToObject(req, "on", true);
  
  // Send request - device will reboot after download completes
  if (!notecard.sendRequest(req)) {
    Serial.println(F("ERROR: Failed to enable DFU mode"));
    gDfuInProgress = false;
  }
  
  // Device will now download firmware and reset automatically
  // No need to return from this function - reset is imminent
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
      std::unique_ptr<JsonDocument> docPtr(new JsonDocument());
      if (docPtr) {
        JsonDocument &doc = *docPtr;
        DeserializationError err = deserializeJson(doc, json);
        NoteFree(json);
        if (!err) {
          applyConfigUpdate(doc);
        } else {
          Serial.println(F("Config update invalid JSON"));
        }
      } else {
        NoteFree(json);
        Serial.println(F("OOM processing config update"));
      }
    }
  }

  notecard.deleteResponse(rsp);
}

static void reinitializeHardware() {
  // Reinitialize all tank sensors with new configuration
  for (uint8_t i = 0; i < gConfig.monitorCount; ++i) {
    const MonitorConfig &cfg = gConfig.monitors[i];
    
    // Configure digital pins if needed
    if (cfg.sensorInterface == SENSOR_DIGITAL) {
      if (cfg.primaryPin >= 0 && cfg.primaryPin < 255) {
        pinMode(cfg.primaryPin, INPUT_PULLUP);
      }
      if (cfg.secondaryPin >= 0 && cfg.secondaryPin < 255) {
        pinMode(cfg.secondaryPin, INPUT_PULLUP);
      }
    }
    
    // Reset tank runtime state for hardware changes
    gMonitorState[i].highAlarmDebounceCount = 0;
    gMonitorState[i].lowAlarmDebounceCount = 0;
    gMonitorState[i].clearDebounceCount = 0;
    gMonitorState[i].consecutiveFailures = 0;
    gMonitorState[i].stuckReadingCount = 0;
    gMonitorState[i].sensorFailed = false;
    gMonitorState[i].lastValidReading = 0.0f;
    gMonitorState[i].hasLastValidReading = false;
    gMonitorState[i].lastReportedInches = -9999.0f;
  }
  
  // Reconfigure Notecard hub settings (may have changed due to power mode)
  configureNotecardHubMode();
  
  Serial.println(F("Hardware reinitialized after config update"));
}

static void resetTelemetryBaselines() {
  for (uint8_t i = 0; i < MAX_TANKS; ++i) {
    gMonitorState[i].lastReportedInches = -9999.0f;
  }
}

static void applyConfigUpdate(const JsonDocument &doc) {
  bool hardwareChanged = false;
  bool telemetryPolicyChanged = false;
  float previousThreshold = gConfig.minLevelChangeInches;
  
  if (!doc["site"].isNull()) {
    strlcpy(gConfig.siteName, doc["site"].as<const char *>(), sizeof(gConfig.siteName));
  }
  if (!doc["deviceLabel"].isNull()) {
    strlcpy(gConfig.deviceLabel, doc["deviceLabel"].as<const char *>(), sizeof(gConfig.deviceLabel));
  }
  if (!doc["serverFleet"].isNull()) {
   
  if (!doc["clientFleet"].isNull()) {
    strlcpy(gConfig.clientFleet, doc["clientFleet"].as<const char *>(), sizeof(gConfig.clientFleet));
  } strlcpy(gConfig.serverFleet, doc["serverFleet"].as<const char *>(), sizeof(gConfig.serverFleet));
  }
  if (!doc["sampleSeconds"].isNull()) {
    gConfig.sampleSeconds = doc["sampleSeconds"].as<uint16_t>();
  }
  if (!doc["levelChangeThreshold"].isNull()) {
    gConfig.minLevelChangeInches = doc["levelChangeThreshold"].as<float>();
    if (gConfig.minLevelChangeInches < 0.0f) {
      gConfig.minLevelChangeInches = 0.0f;
    }
    telemetryPolicyChanged = (fabsf(previousThreshold - gConfig.minLevelChangeInches) > 0.0001f);
  }
  if (!doc["reportHour"].isNull()) {
    gConfig.reportHour = doc["reportHour"].as<uint8_t>();
  }
  if (!doc["reportMinute"].isNull()) {
    gConfig.reportMinute = doc["reportMinute"].as<uint8_t>();
  }
  if (!doc["dailyEmail"].isNull()) {
    strlcpy(gConfig.dailyEmail, doc["dailyEmail"].as<const char *>(), sizeof(gConfig.dailyEmail));
  }
  
  // Handle clear button configuration
  if (!doc["clearButtonPin"].isNull()) {
    int8_t newPin = doc["clearButtonPin"].as<int8_t>();
    if (newPin != gConfig.clearButtonPin) {
      gConfig.clearButtonPin = newPin;
      hardwareChanged = true;  // Need to reinitialize button pin
    }
  }
  if (!doc["clearButtonActiveHigh"].isNull()) {
    gConfig.clearButtonActiveHigh = doc["clearButtonActiveHigh"].as<bool>();
  }
  
  // Handle power saving configuration
  if (!doc["solarPowered"].isNull()) {
    bool newSolarPowered = doc["solarPowered"].as<bool>();
    if (newSolarPowered != gConfig.solarPowered) {
      gConfig.solarPowered = newSolarPowered;
      hardwareChanged = true;  // Need to reconfigure Notecard hub settings
    }
  }

  if (!doc["tanks"].isNull()) {
    hardwareChanged = true;  // Tank configuration affects hardware
    JsonArrayConst tanks = doc["tanks"].as<JsonArrayConst>();
    gConfig.monitorCount = min<uint8_t>(tanks.size(), MAX_TANKS);
    for (uint8_t i = 0; i < gConfig.monitorCount; ++i) {
      JsonObjectConst t = tanks[i];
      gConfig.monitors[i].id = t["id"].as<const char *>() ? t["id"].as<const char *>()[0] : ('A' + i);
      strlcpy(gConfig.monitors[i].name, t["name"].as<const char *>() ? t["name"].as<const char *>() : "Tank", sizeof(gConfig.monitors[i].name));
      strlcpy(gConfig.monitors[i].contents, t["contents"].as<const char *>() ? t["contents"].as<const char *>() : "", sizeof(gConfig.monitors[i].contents));
      gConfig.monitors[i].monitorNumber = t["number"].is<uint8_t>() ? t["number"].as<uint8_t>() : (i + 1);
      const char *sensor = t["sensor"].as<const char *>();
      if (sensor && strcmp(sensor, "digital") == 0) {
        gConfig.monitors[i].sensorInterface = SENSOR_DIGITAL;
      } else if (sensor && strcmp(sensor, "current") == 0) {
        gConfig.monitors[i].sensorInterface = SENSOR_CURRENT_LOOP;
      } else if (sensor && strcmp(sensor, "rpm") == 0) {
        gConfig.monitors[i].sensorInterface = SENSOR_PULSE;
      } else {
        gConfig.monitors[i].sensorInterface = SENSOR_ANALOG;
      }
      gConfig.monitors[i].primaryPin = t["primaryPin"].is<int>() ? t["primaryPin"].as<int>() : gConfig.monitors[i].primaryPin;
      gConfig.monitors[i].secondaryPin = t["secondaryPin"].is<int>() ? t["secondaryPin"].as<int>() : gConfig.monitors[i].secondaryPin;
      gConfig.monitors[i].currentLoopChannel = t["loopChannel"].is<int>() ? t["loopChannel"].as<int>() : gConfig.monitors[i].currentLoopChannel;
      gConfig.monitors[i].pulsePin = t["rpmPin"].is<int>() ? t["rpmPin"].as<int>() : gConfig.monitors[i].pulsePin;
      if (!t["pulsesPerRev"].isNull()) {
        gConfig.monitors[i].pulsesPerUnit = max((uint8_t)1, t["pulsesPerRev"].as<uint8_t>());
      }
      // Update hall effect sensor type if provided
      if (!t["hallEffectType"].isNull()) {
        const char *hallType = t["hallEffectType"].as<const char *>();
        if (hallType && strcmp(hallType, "bipolar") == 0) {
          gConfig.monitors[i].hallEffectType = HALL_EFFECT_BIPOLAR;
        } else if (hallType && strcmp(hallType, "omnipolar") == 0) {
          gConfig.monitors[i].hallEffectType = HALL_EFFECT_OMNIPOLAR;
        } else if (hallType && strcmp(hallType, "analog") == 0) {
          gConfig.monitors[i].hallEffectType = HALL_EFFECT_ANALOG;
        } else if (hallType && strcmp(hallType, "unipolar") == 0) {
          gConfig.monitors[i].hallEffectType = HALL_EFFECT_UNIPOLAR;
        }
      }
      // Update hall effect detection method if provided
      if (!t["hallEffectDetection"].isNull()) {
        const char *hallDetect = t["hallEffectDetection"].as<const char *>();
        if (hallDetect && strcmp(hallDetect, "time") == 0) {
          gConfig.monitors[i].hallEffectDetection = HALL_DETECT_TIME_BASED;
        } else if (hallDetect && strcmp(hallDetect, "pulse") == 0) {
          gConfig.monitors[i].hallEffectDetection = HALL_DETECT_PULSE;
        }
      }
      // Update RPM sampling configuration if provided
      if (!t["pulseSampleDurationMs"].isNull()) {
        gConfig.monitors[i].pulseSampleDurationMs = t["pulseSampleDurationMs"].as<uint32_t>();
      }
      if (!t["pulseAccumulatedMode"].isNull()) {
        gConfig.monitors[i].pulseAccumulatedMode = t["pulseAccumulatedMode"].as<bool>();
        // Reset accumulated state when mode changes (use atomic access)
        atomicResetPulses(i);
        gRpmAccumulatedStartMillis[i] = millis();
        gRpmAccumulatedInitialized[i] = false;
      }
      // Expected pulse rate for baseline (by object type: RPM for engines, GPM for flow, etc.)
      if (!t["expectedPulseRate"].isNull()) {
        gConfig.monitors[i].expectedPulseRate = t["expectedPulseRate"].as<float>();
      }
      gConfig.monitors[i].highAlarmThreshold = t["highAlarm"].is<float>() ? t["highAlarm"].as<float>() : gConfig.monitors[i].highAlarmThreshold;
      gConfig.monitors[i].lowAlarmThreshold = t["lowAlarm"].is<float>() ? t["lowAlarm"].as<float>() : gConfig.monitors[i].lowAlarmThreshold;
      gConfig.monitors[i].hysteresisValue = t["hysteresis"].is<float>() ? t["hysteresis"].as<float>() : gConfig.monitors[i].hysteresisValue;
      if (!t["daily"].isNull()) {
        gConfig.monitors[i].enableDailyReport = t["daily"].as<bool>();
      }
      if (!t["alarmSms"].isNull()) {
        gConfig.monitors[i].enableAlarmSms = t["alarmSms"].as<bool>();
      }
      if (!t["upload"].isNull()) {
        gConfig.monitors[i].enableServerUpload = t["upload"].as<bool>();
      }
      if (!t["relayTargetClient"].isNull()) {
        const char *relayTargetStr = t["relayTargetClient"].as<const char *>();
        strlcpy(gConfig.monitors[i].relayTargetClient, 
                relayTargetStr ? relayTargetStr : "", 
                sizeof(gConfig.monitors[i].relayTargetClient));
      }
      if (!t["relayMask"].isNull()) {
        gConfig.monitors[i].relayMask = t["relayMask"].as<uint8_t>();
      }
      if (!t["relayTrigger"].isNull()) {
        const char *triggerStr = t["relayTrigger"].as<const char *>();
        if (triggerStr && strcmp(triggerStr, "high") == 0) {
          gConfig.monitors[i].relayTrigger = RELAY_TRIGGER_HIGH;
        } else if (triggerStr && strcmp(triggerStr, "low") == 0) {
          gConfig.monitors[i].relayTrigger = RELAY_TRIGGER_LOW;
        } else {
          gConfig.monitors[i].relayTrigger = RELAY_TRIGGER_ANY;
        }
      }
      if (!t["relayMode"].isNull()) {
        const char *modeStr = t["relayMode"].as<const char *>();
        if (modeStr && strcmp(modeStr, "until_clear") == 0) {
          gConfig.monitors[i].relayMode = RELAY_MODE_UNTIL_CLEAR;
        } else if (modeStr && strcmp(modeStr, "manual_reset") == 0) {
          gConfig.monitors[i].relayMode = RELAY_MODE_MANUAL_RESET;
        } else {
          gConfig.monitors[i].relayMode = RELAY_MODE_MOMENTARY;
        }
      }
      // Handle per-relay momentary durations (in seconds)
      if (!t["relayMomentaryDurations"].isNull()) {
        JsonArrayConst durations = t["relayMomentaryDurations"].as<JsonArrayConst>();
        for (size_t r = 0; r < 4 && r < durations.size(); r++) {
          uint16_t dur = durations[r].as<uint16_t>();
          // Enforce minimum of 1 second, max of 86400 (24 hours)
          gConfig.monitors[i].relayMomentarySeconds[r] = constrain(dur, 1, 86400);
        }
      }
      // Handle digital sensor trigger state (for float switches)
      if (!t["digitalTrigger"].isNull()) {
        const char *digitalTriggerStr = t["digitalTrigger"].as<const char *>();
        strlcpy(gConfig.monitors[i].digitalTrigger,
                digitalTriggerStr ? digitalTriggerStr : "",
                sizeof(gConfig.monitors[i].digitalTrigger));
      }
      // Handle digital switch mode (NO/NC) for float switches
      if (!t["digitalSwitchMode"].isNull()) {
        const char *digitalSwitchModeStr = t["digitalSwitchMode"].as<const char *>();
        if (digitalSwitchModeStr && strcmp(digitalSwitchModeStr, "NC") == 0) {
          strlcpy(gConfig.monitors[i].digitalSwitchMode, "NC", sizeof(gConfig.monitors[i].digitalSwitchMode));
        } else {
          strlcpy(gConfig.monitors[i].digitalSwitchMode, "NO", sizeof(gConfig.monitors[i].digitalSwitchMode));
        }
      }
      // Handle 4-20mA current loop sensor type (pressure or ultrasonic)
      if (!t["currentLoopType"].isNull()) {
        const char *currentLoopTypeStr = t["currentLoopType"].as<const char *>();
        if (currentLoopTypeStr && strcmp(currentLoopTypeStr, "ultrasonic") == 0) {
          gConfig.monitors[i].currentLoopType = CURRENT_LOOP_ULTRASONIC;
        } else {
          gConfig.monitors[i].currentLoopType = CURRENT_LOOP_PRESSURE;
        }
      }
      // Handle sensor mount height (for calibration) - validate non-negative
      if (!t["sensorMountHeight"].isNull()) {
        gConfig.monitors[i].sensorMountHeight = fmaxf(0.0f, t["sensorMountHeight"].as<float>());
      }
      // Handle sensor native range settings
      if (!t["sensorRangeMin"].isNull()) {
        gConfig.monitors[i].sensorRangeMin = t["sensorRangeMin"].as<float>();
      }
      if (!t["sensorRangeMax"].isNull()) {
        gConfig.monitors[i].sensorRangeMax = t["sensorRangeMax"].as<float>();
      }
      if (!t["sensorRangeUnit"].isNull()) {
        const char *unitStr = t["sensorRangeUnit"].as<const char *>();
        strlcpy(gConfig.monitors[i].sensorRangeUnit, unitStr ? unitStr : "PSI", sizeof(gConfig.monitors[i].sensorRangeUnit));
      }
      // Handle analog voltage range settings
      if (!t["analogVoltageMin"].isNull()) {
        gConfig.monitors[i].analogVoltageMin = t["analogVoltageMin"].as<float>();
      }
      if (!t["analogVoltageMax"].isNull()) {
        gConfig.monitors[i].analogVoltageMax = t["analogVoltageMax"].as<float>();
      }
      // Handle tank unload tracking settings
      if (!t["trackUnloads"].isNull()) {
        gConfig.monitors[i].trackUnloads = t["trackUnloads"].as<bool>();
        // Reset unload tracking state when config changes
        if (gConfig.monitors[i].trackUnloads) {
          gMonitorState[i].unloadTracking = false;
          gMonitorState[i].unloadPeakInches = 0.0f;
          gMonitorState[i].unloadPeakEpoch = 0.0;
        }
      }
      if (!t["unloadEmptyHeight"].isNull()) {
        gConfig.monitors[i].unloadEmptyHeight = fmaxf(0.0f, t["unloadEmptyHeight"].as<float>());
      }
      if (!t["unloadDropThreshold"].isNull()) {
        gConfig.monitors[i].unloadDropThreshold = fmaxf(0.0f, t["unloadDropThreshold"].as<float>());
      }
      if (!t["unloadDropPercent"].isNull()) {
        float pct = t["unloadDropPercent"].as<float>();
        gConfig.monitors[i].unloadDropPercent = constrain(pct, 10.0f, 95.0f);  // Clamp to 10-95%
      }
      if (!t["unloadAlarmSms"].isNull()) {
        gConfig.monitors[i].unloadAlarmSms = t["unloadAlarmSms"].as<bool>();
      }
      if (!t["unloadAlarmEmail"].isNull()) {
        gConfig.monitors[i].unloadAlarmEmail = t["unloadAlarmEmail"].as<bool>();
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

#ifdef FILESYSTEM_AVAILABLE
  static bool warned = false;
  if (!isStorageAvailable()) {
    if (!warned) {
      Serial.println(F("Warning: Filesystem unavailable - skipping config persistence"));
      warned = true;
    }
    return;
  }
#endif

  if (saveConfigToFlash(gConfig)) {
    gConfigDirty = false;
  }
}

static float readCurrentLoopMilliamps(int16_t channel) {
  if (channel < 0) {
    return -1.0f;
  }
  
  // Use runtime-configurable I2C address (falls back to compile-time default)
  uint8_t i2cAddr = gConfig.currentLoopI2cAddress ? gConfig.currentLoopI2cAddress : CURRENT_LOOP_I2C_ADDRESS;

  Wire.beginTransmission(i2cAddr);
  Wire.write((uint8_t)channel);
  if (Wire.endTransmission(false) != 0) {
    return -1.0f;
  }

  if (Wire.requestFrom(i2cAddr, (uint8_t)2) != 2) {
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
  if (idx >= gConfig.monitorCount) {
    return false;
  }

  const MonitorConfig &cfg = gConfig.monitors[idx];
  MonitorRuntime &state = gMonitorState[idx];

  // Calculate valid range based on sensor type
  float minValid;
  float maxValid;
  
  // Check if sensor has native range configured (current loop or analog with voltage range)
  bool hasNativeRange = (cfg.sensorRangeMax > cfg.sensorRangeMin);
  bool isCurrentLoop = (cfg.sensorInterface == SENSOR_CURRENT_LOOP);
  bool isAnalogWithVoltageRange = (cfg.sensorInterface == SENSOR_ANALOG && 
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
  } else if (cfg.sensorInterface == SENSOR_DIGITAL) {
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
          JsonDocument doc;
          doc["c"] = gDeviceUID;
          doc["s"] = gConfig.siteName;
          doc["k"] = cfg.monitorNumber;
          doc["y"] = "sensor-fault";
          doc["rd"] = reading;
          doc["t"] = currentEpoch();
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
          JsonDocument doc;
          doc["c"] = gDeviceUID;
          doc["s"] = gConfig.siteName;
          doc["k"] = cfg.monitorNumber;
          doc["y"] = "sensor-stuck";
          doc["rd"] = reading;
          doc["t"] = currentEpoch();
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
    JsonDocument doc;
    doc["c"] = gDeviceUID;
    doc["s"] = gConfig.siteName;
    doc["k"] = cfg.monitorNumber;
    doc["y"] = "sensor-recovered";
    doc["rd"] = reading;
    doc["t"] = currentEpoch();
    publishNote(ALARM_FILE, doc, true);
  }
  state.lastValidReading = reading;
  state.hasLastValidReading = true;
  return true;
}

static float readTankSensor(uint8_t idx) {
  if (idx >= gConfig.monitorCount) {
    return 0.0f;
  }

  const MonitorConfig &cfg = gConfig.monitors[idx];

  switch (cfg.sensorInterface) {
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
      
      // Store raw voltage reading for telemetry
      gMonitorState[idx].currentSensorVoltage = voltage;
      
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
        gMonitorState[idx].currentSensorMa = 0.0f;
        return 0.0f;
      }
      float milliamps = readCurrentLoopMilliamps(channel);
      if (milliamps < 0.0f) {
        return gMonitorState[idx].currentInches; // keep previous on failure
      }
      
      // Store raw mA reading for telemetry
      gMonitorState[idx].currentSensorMa = milliamps;
      
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
    case SENSOR_PULSE: {
      // Hall effect RPM sensor - supports multiple sensor types and detection methods
      // Now supports configurable sample duration and accumulated mode for very low RPM
      // Uses expectedPulseRate to auto-configure optimal sampling if not explicitly set
      
      // Use pulsePin if available, otherwise use primaryPin
      int pin = (cfg.pulsePin >= 0 && cfg.pulsePin < 255) ? cfg.pulsePin : 
                ((cfg.primaryPin >= 0 && cfg.primaryPin < 255) ? cfg.primaryPin : (2 + idx));
      
      // Configure pin as input with pullup for digital Hall effect sensors
      pinMode(pin, INPUT_PULLUP);
      
      // Validate pulses per revolution/unit
      uint8_t pulsesPerRev = (cfg.pulsesPerUnit > 0) ? cfg.pulsesPerUnit : 1;
      const float MS_PER_MINUTE = 60000.0f;
      
      // Determine sampling parameters:
      // 1. If explicitly configured, use those values
      // 2. If expectedPulseRate is set, use recommended values
      // 3. Otherwise, use defaults
      uint32_t sampleDurationMs;
      bool useAccumulatedMode;
      
      if (cfg.pulseSampleDurationMs > 0) {
        // Explicitly configured - use as-is
        sampleDurationMs = cfg.pulseSampleDurationMs;
        useAccumulatedMode = cfg.pulseAccumulatedMode;
      } else if (cfg.expectedPulseRate > 0.0f) {
        // Use recommendation based on expected rate
        PulseSamplingRecommendation rec = getRecommendedPulseSampling(cfg.expectedPulseRate);
        sampleDurationMs = rec.sampleDurationMs;
        useAccumulatedMode = rec.accumulatedMode;
      } else {
        // Use defaults
        sampleDurationMs = RPM_SAMPLE_DURATION_MS;
        useAccumulatedMode = cfg.pulseAccumulatedMode;
      }
      
      // Common constants
      const unsigned long DEBOUNCE_MS = 2;
      const uint32_t MAX_ITERATIONS = sampleDurationMs * 2;
      
      float rpm = 0.0f;
      
      // ACCUMULATED MODE: Count pulses between telemetry reports
      // Useful for very low RPM (< 1 RPM) where sample duration would be impractical
      // With sampleSeconds=1800 (30 min) and 1 pulse/rev, can detect down to 0.033 RPM
      // Can be auto-enabled based on expectedPulseRate
      if (useAccumulatedMode) {
        unsigned long now = millis();
        
        // Initialize accumulated counting on first call
        if (!gRpmAccumulatedInitialized[idx]) {
          atomicResetPulses(idx);
          gRpmAccumulatedStartMillis[idx] = now;
          gRpmLastPinState[idx] = digitalRead(pin);
          gRpmAccumulatedInitialized[idx] = true;
          Serial.print(F("Pulse accumulated mode initialized for monitor "));
          Serial.println(idx);
        }
        
        // Sample for a short burst to catch any recent pulses
        // This supplements the main loop polling
        unsigned long burstStart = millis();
        const unsigned long BURST_DURATION_MS = 1000; // 1 second burst sample
        int lastState = gRpmLastPinState[idx];
        unsigned long lastPulseTime = 0;
        
        while ((millis() - burstStart) < BURST_DURATION_MS) {
          int currentState = digitalRead(pin);
          bool edgeDetected = false;
          
          switch (cfg.hallEffectType) {
            case HALL_EFFECT_UNIPOLAR:
            case HALL_EFFECT_ANALOG:
              edgeDetected = (lastState == HIGH && currentState == LOW);
              break;
            case HALL_EFFECT_BIPOLAR:
            case HALL_EFFECT_OMNIPOLAR:
              edgeDetected = (lastState != currentState);
              break;
            default:
              edgeDetected = (lastState == HIGH && currentState == LOW);
              break;
          }
          
          if (edgeDetected) {
            unsigned long pulseTime = millis();
            if (pulseTime - lastPulseTime >= DEBOUNCE_MS) {
              atomicIncrementPulses(idx);
              lastPulseTime = pulseTime;
            }
          }
          lastState = currentState;
          delay(1);
          
#ifdef WATCHDOG_AVAILABLE
          #if defined(ARDUINO_OPTA) || defined(ARDUINO_ARCH_MBED)
            mbedWatchdog.kick();
          #else
            IWatchdog.reload();
          #endif
#endif
        }
        gRpmLastPinState[idx] = lastState;
        
        // Calculate RPM from accumulated pulses over elapsed time
        // Use atomic read to get consistent pulse count
        unsigned long elapsedMs = now - gRpmAccumulatedStartMillis[idx];
        uint32_t pulseCount = atomicReadPulses(idx);
        if (elapsedMs > 1000 && pulseCount > 0) {
          // RPM = (pulses * 60000) / (elapsed_ms * pulses_per_rev)
          rpm = ((float)pulseCount * MS_PER_MINUTE) / 
                ((float)elapsedMs * (float)pulsesPerRev);
        } else if (elapsedMs > sampleDurationMs && pulseCount == 0) {
          // No pulses for longer than sample duration - report 0 RPM
          rpm = 0.0f;
        } else {
          // Not enough time elapsed, use last reading
          rpm = gRpmLastReading[idx];
        }
        
        // Reset accumulated count after reading (start fresh for next period)
        atomicResetPulses(idx);
        gRpmAccumulatedStartMillis[idx] = now;
        
        Serial.print(F("RPM accumulated: "));
        Serial.print(rpm, 2);
        Serial.print(F(" ("));
        Serial.print(elapsedMs / 1000);
        Serial.println(F("s period)"));
      }
      // TIME-BASED MODE: Measure period between consecutive pulses
      else if (cfg.hallEffectDetection == HALL_DETECT_TIME_BASED) {
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
        // Use configurable sample duration
        while ((millis() - sampleStart) < sampleDurationMs && iterationCount < MAX_ITERATIONS) {
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
          // RPM is below: 60000ms / (sampleDurationMs * pulsesPerRev)
          // For 60s sampling with 1 pulse/rev: < 1 RPM
          // Consider using rpmAccumulatedMode=true for sub-1 RPM measurement
          rpm = gRpmLastReading[idx];
        } else {
          // No pulses detected, keep last reading
          rpm = gRpmLastReading[idx];
        }
        
      } else {
        // PULSE COUNTING MODE (traditional approach)
        // Sample pulses for configurable duration (default 60 seconds)
        // This provides accurate RPM measurement by counting multiple pulses
        
        unsigned long sampleStart = millis();
        uint32_t pulseCount = 0;
        
        // Always read current pin state first to establish baseline
        int lastState = digitalRead(pin);
        gRpmLastPinState[idx] = lastState;
        
        unsigned long lastPulseTime = 0;
        uint32_t iterationCount = 0;
        
        while ((millis() - sampleStart) < sampleDurationMs && iterationCount < MAX_ITERATIONS) {
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
        // RPM = (pulses * 60000) / (sample_duration_ms * pulses_per_rev)
        rpm = ((float)pulseCount * MS_PER_MINUTE) / ((float)sampleDurationMs * (float)pulsesPerRev);
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
  for (uint8_t i = 0; i < gConfig.monitorCount; ++i) {
    float inches = readTankSensor(i);
    
    // Validate sensor reading
    if (!validateSensorReading(i, inches)) {
      // Keep previous valid reading if sensor failed
      inches = gMonitorState[i].currentInches;
    } else {
      gMonitorState[i].currentInches = inches;
    }
    
    evaluateAlarms(i);
    
    // Evaluate tank unload if tracking is enabled for this tank
    if (gConfig.monitors[i].trackUnloads && !gMonitorState[i].sensorFailed) {
      evaluateUnload(i);
    }

    if (gConfig.monitors[i].enableServerUpload && !gMonitorState[i].sensorFailed) {
      const float threshold = gConfig.minLevelChangeInches;
      const bool needBaseline = (gMonitorState[i].lastReportedInches < 0.0f);
      const bool thresholdEnabled = (threshold > 0.0f);
      const bool changeExceeded = thresholdEnabled && (fabs(inches - gMonitorState[i].lastReportedInches) >= threshold);
      if (needBaseline || changeExceeded) {
        sendTelemetry(i, "sample", false);
        gMonitorState[i].lastReportedInches = inches;
      }
    }
  }
}

static void evaluateAlarms(uint8_t idx) {
  const MonitorConfig &cfg = gConfig.monitors[idx];
  MonitorRuntime &state = gMonitorState[idx];

  // Skip alarm evaluation if sensor has failed
  if (state.sensorFailed) {
    return;
  }

  // Handle digital sensors (float switches) differently
  if (cfg.sensorInterface == SENSOR_DIGITAL) {
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
  if (idx >= gConfig.monitorCount) {
    return;
  }

  const MonitorConfig &cfg = gConfig.monitors[idx];
  MonitorRuntime &state = gMonitorState[idx];

  JsonDocument doc;
  doc["c"] = gDeviceUID;
  doc["s"] = gConfig.siteName;
  doc["k"] = cfg.monitorNumber;
  // Note: object type (ot), measurement unit (mu), and monitor id (i) are
  // omitted from telemetry to reduce payload — server already knows these from config/daily.
  
  // Include sensor interface type and type-specific raw data only
  // Server converts to display units using its stored config
  switch (cfg.sensorInterface) {
    case SENSOR_DIGITAL:
      doc["st"] = "digital";
      doc["fl"] = state.currentInches;  // 1.0 or 0.0 (float switch state)
      break;
    case SENSOR_CURRENT_LOOP:
      doc["st"] = "currentLoop";
      if (state.currentSensorMa >= 4.0f) {
        doc["ma"] = roundTo(state.currentSensorMa, 2);
      }
      break;
    case SENSOR_ANALOG:
      doc["st"] = "analog";
      if (state.currentSensorVoltage > 0.0f) {
        doc["vt"] = roundTo(state.currentSensorVoltage, 3);
      }
      break;
    case SENSOR_PULSE:
    default:
      doc["st"] = "pulse";
      doc["rm"] = roundTo(state.currentInches, 1);
      break;
  }
  doc["r"] = reason;
  doc["t"] = currentEpoch();

  publishNote(TELEMETRY_FILE, doc, syncNow);
}

static bool checkAlarmRateLimit(uint8_t idx, const char *alarmType) {
  if (idx >= gConfig.monitorCount) {
    return false;
  }

  MonitorRuntime &state = gMonitorState[idx];
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
  if (idx >= gConfig.monitorCount) {
    return;
  }

  const MonitorConfig &cfg = gConfig.monitors[idx];
  bool allowSmsEscalation = cfg.enableAlarmSms;

  // Always activate local alarm regardless of rate limits
  bool isAlarm = (strcmp(alarmType, "clear") != 0);
  activateLocalAlarm(idx, isAlarm);

  // Check rate limit before sending remote alarm
  if (!checkAlarmRateLimit(idx, alarmType)) {
    return;  // Rate limit exceeded
  }

  MonitorRuntime &state = gMonitorState[idx];
  state.lastAlarmSendMillis = millis();

  // Try to send via network if available
  if (gNotecardAvailable) {
    JsonDocument doc;
    doc["c"] = gDeviceUID;
    doc["s"] = gConfig.siteName;
    doc["k"] = cfg.monitorNumber;
    doc["y"] = alarmType;
    // Note: object type (ot) and measurement unit (mu) omitted — server knows from config/daily.
    
    // Send sensor-type-appropriate raw data only
    if (cfg.sensorInterface == SENSOR_CURRENT_LOOP) {
      if (state.currentSensorMa >= 4.0f) {
        doc["ma"] = roundTo(state.currentSensorMa, 2);
      }
    } else if (cfg.sensorInterface == SENSOR_ANALOG) {
      if (state.currentSensorVoltage > 0.0f) {
        doc["vt"] = roundTo(state.currentSensorVoltage, 3);
      }
    } else if (cfg.sensorInterface == SENSOR_DIGITAL) {
      doc["fl"] = roundTo(inches, 1);
    } else {
      doc["rm"] = roundTo(inches, 1);
    }
    doc["th"] = roundTo(cfg.highAlarmThreshold, 1);
    doc["tl"] = roundTo(cfg.lowAlarmThreshold, 1);
    if (allowSmsEscalation) {
      doc["se"] = true;  // Only include when true (false is default)
    }
    doc["t"] = currentEpoch();

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

// ============================================================================
// Tank Unload Detection
// ============================================================================
// Detects when a tank has been emptied/unloaded and logs the event.
// 
// Algorithm:
// 1. Track the peak (highest) level seen since the last unload event
// 2. When level drops significantly (configurable %) from peak, trigger unload event
// 3. If level drops to/below sensor height, use default empty height
// 4. Log: peak height, new low height, timestamps
// 5. Optionally send SMS/email notification
//
// Use cases:
// - Fill-and-empty tanks (fuel delivery tanks, milk tanks, etc.)
// - NOT for tanks that fluctuate through in/out ports
// ============================================================================

static uint8_t gUnloadDebounceCount[MAX_TANKS] = {0};

static void evaluateUnload(uint8_t idx) {
  if (idx >= gConfig.monitorCount) {
    return;
  }

  const MonitorConfig &cfg = gConfig.monitors[idx];
  MonitorRuntime &state = gMonitorState[idx];

  // Skip if not tracking unloads or sensor failed
  if (!cfg.trackUnloads || state.sensorFailed) {
    return;
  }

  float currentInches = state.currentInches;
  
  // Determine the unload threshold (use percentage or absolute, whichever is configured)
  float dropPercent = (cfg.unloadDropPercent > 0.0f) ? cfg.unloadDropPercent : UNLOAD_DEFAULT_DROP_PERCENT;
  float minPeakHeight = UNLOAD_MIN_PEAK_HEIGHT;
  
  // If we haven't started tracking yet, wait for tank to reach minimum peak
  if (!state.unloadTracking) {
    if (currentInches >= minPeakHeight) {
      // Start tracking - tank has reached minimum fill level
      state.unloadTracking = true;
      state.unloadPeakInches = currentInches;
      state.unloadPeakSensorMa = state.currentSensorMa;
      state.unloadPeakEpoch = currentEpoch();
      Serial.print(F("Unload tracking started for "));
      Serial.print(cfg.name);
      Serial.print(F(" at "));
      Serial.print(currentInches);
      Serial.println(F(" inches"));
    }
    return;
  }

  // Update peak if current level is higher
  if (currentInches > state.unloadPeakInches) {
    state.unloadPeakInches = currentInches;
    state.unloadPeakSensorMa = state.currentSensorMa;
    state.unloadPeakEpoch = currentEpoch();
    gUnloadDebounceCount[idx] = 0;  // Reset debounce on new peak
    return;
  }

  // Calculate threshold for unload detection
  float dropThreshold = state.unloadPeakInches * (dropPercent / 100.0f);
  float unloadTriggerLevel = state.unloadPeakInches - dropThreshold;
  
  // Use configured absolute threshold if set and lower than percentage-based
  if (cfg.unloadDropThreshold > 0.0f) {
    float absoluteTrigger = state.unloadPeakInches - cfg.unloadDropThreshold;
    if (absoluteTrigger < unloadTriggerLevel) {
      unloadTriggerLevel = absoluteTrigger;
    }
  }
  
  // Check if level has dropped enough to be considered an unload
  if (currentInches <= unloadTriggerLevel) {
    // Debounce: require consecutive low readings
    gUnloadDebounceCount[idx]++;
    
    if (gUnloadDebounceCount[idx] >= UNLOAD_DEBOUNCE_COUNT) {
      // Determine the "empty" level to report
      float emptyHeight = currentInches;
      
      // If level is at or below sensor mount height, use default empty height
      if (currentInches <= cfg.sensorMountHeight) {
        emptyHeight = (cfg.unloadEmptyHeight > 0.0f) ? cfg.unloadEmptyHeight : UNLOAD_DEFAULT_EMPTY_HEIGHT;
      }
      
      // Send unload event
      sendUnloadEvent(idx, state.unloadPeakInches, emptyHeight, state.unloadPeakEpoch);
      
      // Reset tracking for next fill cycle - tank must refill above minimum before next unload
      state.unloadTracking = false;
      gUnloadDebounceCount[idx] = 0;
    }
  } else {
    // Level not low enough - reset debounce counter
    gUnloadDebounceCount[idx] = 0;
  }
}

static void sendUnloadEvent(uint8_t idx, float peakInches, float currentInches, double peakEpoch) {
  if (idx >= gConfig.monitorCount) {
    return;
  }

  const MonitorConfig &cfg = gConfig.monitors[idx];
  MonitorRuntime &state = gMonitorState[idx];

  Serial.print(F("Tank unload detected: "));
  Serial.print(cfg.name);
  Serial.print(F(" peak="));
  Serial.print(peakInches);
  Serial.print(F("in, current="));
  Serial.print(currentInches);
  Serial.println(F("in"));

  // Log to serial
  char logMsg[128];
  snprintf(logMsg, sizeof(logMsg), "Unload: %s peak=%.1fin, empty=%.1fin", 
           cfg.name, peakInches, currentInches);
  addSerialLog(logMsg);

  // Send unload event via Notecard if network available
  if (gNotecardAvailable) {
    JsonDocument doc;
    doc["c"] = gDeviceUID;
    doc["s"] = gConfig.siteName;
    doc["k"] = cfg.monitorNumber;
    // Note: "type" = "unload" omitted — routing is by file (unload.qi)
    doc["pk"] = roundTo(peakInches, 1);      // Peak height
    doc["em"] = roundTo(currentInches, 1);   // Empty/low height
    doc["pt"] = peakEpoch;                    // Peak timestamp
    doc["t"] = currentEpoch();               // Event timestamp
    
    // Include raw sensor readings only if available
    if (state.unloadPeakSensorMa >= 4.0f) {
      doc["pma"] = roundTo(state.unloadPeakSensorMa, 2);
    }
    if (state.currentSensorMa >= 4.0f) {
      doc["ema"] = roundTo(state.currentSensorMa, 2);
    }
    // Note: sms/email flags and measurement unit omitted — server has config

    publishNote(UNLOAD_FILE, doc, true);
    Serial.println(F("Unload event sent to server"));
  } else {
    Serial.println(F("Network offline - unload event not sent"));
  }
}

// ============================================================================
// Solar/Battery Charger Alarm Functions (SunSaver MPPT via RS-485)
// ============================================================================

static void sendSolarAlarm(SolarAlertType alertType) {
  if (!gSolarManager.isEnabled() || alertType == SOLAR_ALERT_NONE) {
    return;
  }
  
  const SolarData &data = gSolarManager.getData();
  const char *alertDesc = gSolarManager.getAlertDescription(alertType);
  
  Serial.print(F("Solar alert: "));
  Serial.println(alertDesc);
  
  char logMsg[128];
  snprintf(logMsg, sizeof(logMsg), "Solar: %s (%.2fV)", alertDesc, data.batteryVoltage);
  addSerialLog(logMsg);
  
  if (!gNotecardAvailable) {
    Serial.println(F("Network offline - solar alarm not sent"));
    return;
  }
  
  JsonDocument doc;
  doc["c"] = gDeviceUID;
  doc["s"] = gConfig.siteName;
  doc["y"] = "solar";
  doc["t"] = currentEpoch();
  
  // Alert type ("desc" omitted — derivable from alert enum on server)
  switch (alertType) {
    case SOLAR_ALERT_BATTERY_LOW:     doc["alert"] = "battery_low"; break;
    case SOLAR_ALERT_BATTERY_CRITICAL: doc["alert"] = "battery_critical"; break;
    case SOLAR_ALERT_BATTERY_HIGH:    doc["alert"] = "battery_high"; break;
    case SOLAR_ALERT_FAULT:           doc["alert"] = "fault"; break;
    case SOLAR_ALERT_ALARM:           doc["alert"] = "alarm"; break;
    case SOLAR_ALERT_COMM_FAILURE:    doc["alert"] = "comm_fail"; break;
    case SOLAR_ALERT_HEATSINK_TEMP:   doc["alert"] = "heatsink_temp"; break;
    case SOLAR_ALERT_NO_CHARGE:       doc["alert"] = "no_charge"; break;
    default:                          doc["alert"] = "unknown"; break;
  }
  
  // Battery and solar data (essential only)
  doc["bv"] = roundTo(data.batteryVoltage, 2);       // Battery voltage
  doc["av"] = roundTo(data.arrayVoltage, 2);         // Array (solar) voltage
  doc["ic"] = roundTo(data.chargeCurrent, 2);        // Charge current
  
  // Include faults/alarms descriptions only if present
  if (data.hasFault) {
    doc["faults"] = gSolarManager.getFaultDescription();
  }
  if (data.hasAlarm) {
    doc["alarms"] = gSolarManager.getAlarmDescription();
  }
  
  // SMS escalation if critical
  bool critical = (alertType == SOLAR_ALERT_BATTERY_CRITICAL || 
                   alertType == SOLAR_ALERT_FAULT);
  if (critical && gConfig.solarCharger.alertOnLowBattery) {
    doc["se"] = true;
  }
  
  publishNote(ALARM_FILE, doc, true);
  Serial.println(F("Solar alarm sent to server"));
}

// Append solar charger data to daily report (called from sendDailyReport)
static bool appendSolarDataToDaily(JsonDocument &doc) {
  if (!gSolarManager.isEnabled() || !gConfig.solarCharger.includeInDailyReport) {
    return false;
  }
  
  const SolarData &data = gSolarManager.getData();
  
  // Only include if we have valid communication
  if (!data.communicationOk && data.lastReadMillis == 0) {
    return false;
  }
  
  JsonObject solar = doc["solar"].to<JsonObject>();
  
  // Current readings
  solar["bv"] = roundTo(data.batteryVoltage, 2);       // Battery voltage
  solar["av"] = roundTo(data.arrayVoltage, 2);         // Array voltage
  solar["ic"] = roundTo(data.chargeCurrent, 2);        // Charge current
  
  // Daily statistics
  solar["bvMin"] = roundTo(data.batteryVoltageMinDaily, 2);
  solar["bvMax"] = roundTo(data.batteryVoltageMaxDaily, 2);
  if (data.ampHoursDaily > 0.0f) {
    solar["ah"] = roundTo(data.ampHoursDaily, 1);      // Amp-hours today (only if non-zero)
  }
  
  // Omitted: healthy, battOk (derivable from bv thresholds); cs (charge state string)
  // commOk is implicit (if this data exists, comm is OK)
  
  // Include any active faults/alarms (only when present)
  if (data.hasFault) {
    solar["faults"] = gSolarManager.getFaultDescription();
  }
  if (data.hasAlarm) {
    solar["alarms"] = gSolarManager.getAlarmDescription();
  }
  
  // Temperature
  solar["ht"] = data.heatsinkTemp;  // Heatsink temp °C
  
  return true;
}

// ============================================================================
// Battery Voltage Monitoring Functions (Notecard direct to battery)
// ============================================================================

/**
 * Configure Notecard card.voltage with appropriate battery thresholds.
 * Must be called once during setup when battery monitoring is enabled.
 */
static void configureBatteryMonitoring(const BatteryConfig &cfg) {
  // Set calibration offset (diode voltage drop compensation)
  J *req = notecard.newRequest("card.voltage");
  if (!req) return;
  
  JAddBoolToObject(req, "set", true);
  JAddNumberToObject(req, "calibration", cfg.calibrationOffset);
  
  // Create custom mode string for voltage thresholds
  char modeStr[80];
  snprintf(modeStr, sizeof(modeStr), 
           "usb:%.1f;high:%.1f;normal:%.1f;low:%.1f;dead:0",
           cfg.highVoltage + 0.5f,
           cfg.highVoltage,
           cfg.normalVoltage,
           cfg.criticalVoltage);
  JAddStringToObject(req, "mode", modeStr);
  
  // Enable voltage trend tracking
  JAddBoolToObject(req, "on", true);
  
  J *rsp = notecard.requestAndResponse(req);
  if (rsp) {
    const char *err = JGetString(rsp, "err");
    if (err && strlen(err) > 0) {
      Serial.print(F("Battery config error: "));
      Serial.println(err);
    } else {
      Serial.print(F("Battery thresholds configured: "));
      Serial.println(modeStr);
    }
    notecard.deleteResponse(rsp);
  }
}

/**
 * Poll battery voltage and trend data from Notecard.
 * Returns true if data was successfully retrieved.
 */
static bool pollBatteryVoltage(BatteryData &data, const BatteryConfig &cfg) {
  J *req = notecard.newRequest("card.voltage");
  if (!req) {
    data.valid = false;
    return false;
  }
  
  // Request trend analysis data
  JAddNumberToObject(req, "hours", cfg.trendAnalysisHours);
  
  J *rsp = notecard.requestAndResponse(req);
  if (!rsp) {
    data.valid = false;
    Serial.println(F("No response from card.voltage"));
    return false;
  }
  
  const char *err = JGetString(rsp, "err");
  if (err && strlen(err) > 0) {
    Serial.print(F("card.voltage error: "));
    Serial.println(err);
    notecard.deleteResponse(rsp);
    data.valid = false;
    return false;
  }
  
  // Parse response
  data.voltage = (float)JGetNumber(rsp, "value");
  data.mode = JGetString(rsp, "mode");
  data.usbPowered = JGetBool(rsp, "usb");
  data.uptimeMinutes = (uint32_t)JGetNumber(rsp, "minutes");
  
  // Trend analysis data
  data.voltageMin = (float)JGetNumber(rsp, "vmin");
  data.voltageMax = (float)JGetNumber(rsp, "vmax");
  data.voltageAvg = (float)JGetNumber(rsp, "vavg");
  data.analysisHours = (uint16_t)JGetNumber(rsp, "hours");
  data.dailyChange = (float)JGetNumber(rsp, "daily");
  data.weeklyChange = (float)JGetNumber(rsp, "weekly");
  data.monthlyChange = (float)JGetNumber(rsp, "monthly");
  
  notecard.deleteResponse(rsp);
  
  // Derive status flags
  data.isHealthy = (data.voltage >= cfg.normalVoltage && data.voltage <= cfg.highVoltage);
  data.isCharging = (data.dailyChange > 0.0f);
  data.isDeclining = (data.weeklyChange < -cfg.declineAlertThreshold);
  
  data.valid = true;
  data.lastReadMillis = millis();
  
  // Log voltage reading
  Serial.print(F("Battery: "));
  Serial.print(data.voltage, 2);
  Serial.print(F("V ("));
  Serial.print(getBatteryStateDescription(data.voltage, &cfg));
  Serial.print(F(")"));
  if (data.weeklyChange != 0.0f) {
    Serial.print(F(" 7d: "));
    Serial.print(data.weeklyChange > 0 ? "+" : "");
    Serial.print(data.weeklyChange, 2);
    Serial.print(F("V"));
  }
  Serial.println();
  
  return true;
}

/**
 * Check battery data against thresholds and trigger alerts if needed.
 */
static void checkBatteryAlerts(const BatteryData &data, const BatteryConfig &cfg) {
  if (!data.valid) return;
  
  unsigned long now = millis();
  BatteryAlertType alert = BATTERY_ALERT_NONE;
  
  // Check voltage thresholds (most critical first)
  if (data.voltage <= cfg.criticalVoltage) {
    if (cfg.alertOnCritical) {
      alert = BATTERY_ALERT_CRITICAL;
    }
  } else if (data.voltage <= cfg.lowVoltage) {
    if (cfg.alertOnLow) {
      alert = BATTERY_ALERT_LOW;
    }
  } else if (data.voltage >= cfg.highVoltage) {
    // High voltage (overcharge) alert
    alert = BATTERY_ALERT_HIGH;
  } else if (data.isDeclining && cfg.alertOnDeclining) {
    // Significant declining trend
    alert = BATTERY_ALERT_DECLINING;
  } else if (data.voltage >= cfg.normalVoltage && gLastBatteryAlert != BATTERY_ALERT_NONE) {
    // Battery recovered to normal
    if (cfg.alertOnRecovery && gLastBatteryAlert == BATTERY_ALERT_LOW) {
      alert = BATTERY_ALERT_RECOVERED;
    }
  }
  
  // Send alert if needed (with rate limiting)
  if (alert != BATTERY_ALERT_NONE) {
    bool shouldSend = false;
    
    // Always send critical alerts immediately
    if (alert == BATTERY_ALERT_CRITICAL) {
      shouldSend = true;
    }
    // For other alerts, check rate limiting
    else if (alert != gLastBatteryAlert || 
             now - gLastBatteryAlarmMillis >= BATTERY_ALARM_MIN_INTERVAL_MS) {
      shouldSend = true;
    }
    
    if (shouldSend && gNotecardAvailable) {
      sendBatteryAlarm(alert, data.voltage);
      gLastBatteryAlert = alert;
      gLastBatteryAlarmMillis = now;
      gLastBatteryAlertVoltage = data.voltage;
    }
  } else if (gLastBatteryAlert != BATTERY_ALERT_NONE && data.voltage >= cfg.normalVoltage) {
    // Clear alert state when voltage returns to normal
    gLastBatteryAlert = BATTERY_ALERT_NONE;
  }
}

/**
 * Send battery voltage alert to server.
 */
static void sendBatteryAlarm(BatteryAlertType alertType, float voltage) {
  const char *alertDesc;
  switch (alertType) {
    case BATTERY_ALERT_LOW:       alertDesc = "Battery voltage low"; break;
    case BATTERY_ALERT_CRITICAL:  alertDesc = "Battery voltage CRITICAL"; break;
    case BATTERY_ALERT_HIGH:      alertDesc = "Battery overvoltage"; break;
    case BATTERY_ALERT_DECLINING: alertDesc = "Battery voltage declining"; break;
    case BATTERY_ALERT_RECOVERED: alertDesc = "Battery voltage recovered"; break;
    default:                      alertDesc = "Battery alert"; break;
  }
  
  Serial.print(F("Battery alert: "));
  Serial.print(alertDesc);
  Serial.print(F(" ("));
  Serial.print(voltage, 2);
  Serial.println(F("V)"));
  
  char logMsg[128];
  snprintf(logMsg, sizeof(logMsg), "Battery: %s (%.2fV)", alertDesc, voltage);
  addSerialLog(logMsg);
  
  if (!gNotecardAvailable) {
    Serial.println(F("Network offline - battery alarm not sent"));
    return;
  }
  
  JsonDocument doc;
  doc["c"] = gDeviceUID;
  doc["s"] = gConfig.siteName;
  doc["y"] = "battery";
  doc["t"] = currentEpoch();
  
  // Alert type ("desc" and "state" omitted — derivable from alert + voltage on server)
  switch (alertType) {
    case BATTERY_ALERT_LOW:       doc["alert"] = "low"; break;
    case BATTERY_ALERT_CRITICAL:  doc["alert"] = "critical"; break;
    case BATTERY_ALERT_HIGH:      doc["alert"] = "high"; break;
    case BATTERY_ALERT_DECLINING: doc["alert"] = "declining"; break;
    case BATTERY_ALERT_RECOVERED: doc["alert"] = "recovered"; break;
    default:                      doc["alert"] = "unknown"; break;
  }
  
  // Voltage only — server can derive state description and SOC
  doc["v"] = roundTo(voltage, 2);
  
  // Include weekly trend only if meaningful
  if (gBatteryData.valid && gBatteryData.weeklyChange != 0.0f) {
    doc["weekly"] = roundTo(gBatteryData.weeklyChange, 2);
  }
  
  // SMS escalation for critical alerts only
  if (alertType == BATTERY_ALERT_CRITICAL) {
    doc["se"] = true;
  }
  
  publishNote(ALARM_FILE, doc, true);
  Serial.println(F("Battery alarm sent to server"));
}

/**
 * Append battery voltage data to daily report.
 */
static bool appendBatteryDataToDaily(JsonDocument &doc) {
  if (!gConfig.batteryMonitor.enabled || !gConfig.batteryMonitor.includeInDailyReport) {
    return false;
  }
  
  if (!gBatteryData.valid) {
    // Try to get fresh data
    pollBatteryVoltage(gBatteryData, gConfig.batteryMonitor);
  }
  
  if (!gBatteryData.valid) {
    return false;
  }
  
  JsonObject battery = doc["battery"].to<JsonObject>();
  
  // Current voltage (state/healthy derivable from voltage + config thresholds on server)
  battery["v"] = roundTo(gBatteryData.voltage, 2);
  
  // Stats over analysis period
  if (gBatteryData.voltageMin > 0.0f) {
    battery["vMin"] = roundTo(gBatteryData.voltageMin, 2);
    battery["vMax"] = roundTo(gBatteryData.voltageMax, 2);
    // vAvg omitted — derivable from vMin/vMax approximation on server
  }
  
  // Trend data (only include non-zero trends)
  if (gBatteryData.weeklyChange != 0.0f) {
    battery["weekly"] = roundTo(gBatteryData.weeklyChange, 2);
  }
  
  // Omitted: state (string), healthy (bool), daily/monthly trends, soc, uptime
  // Server derives these from voltage + configured thresholds
  
  return true;
}

// ============================================================================
// Power Conservation State Machine
// Progressive duty-cycle reduction with hysteresis-based recovery.
// Merges voltage data from both SunSaver MPPT and Notecard card.voltage 
// to drive a single power-state decision.
// ============================================================================

/**
 * Get human-readable description of a power state.
 */
static const char* getPowerStateDescription(PowerState state) {
  switch (state) {
    case POWER_STATE_NORMAL:             return "NORMAL";
    case POWER_STATE_ECO:                return "ECO";
    case POWER_STATE_LOW_POWER:          return "LOW_POWER";
    case POWER_STATE_CRITICAL_HIBERNATE: return "CRITICAL_HIBERNATE";
    default:                             return "UNKNOWN";
  }
}

/**
 * Get the loop sleep duration (ms) for a given power state.
 */
static unsigned long getPowerStateSleepMs(PowerState state) {
  switch (state) {
    case POWER_STATE_ECO:                return POWER_ECO_SLEEP_MS;
    case POWER_STATE_LOW_POWER:          return POWER_LOW_SLEEP_MS;
    case POWER_STATE_CRITICAL_HIBERNATE: return POWER_CRITICAL_SLEEP_MS;
    case POWER_STATE_NORMAL:
    default:                             return POWER_NORMAL_SLEEP_MS;
  }
}

/**
 * Send a server notification when the power state changes.
 * Logs the transition so the server has a clear record of hibernation
 * entry and exit times. Sends a "returning to normal" message on recovery.
 */
static void sendPowerStateChange(PowerState oldState, PowerState newState, float voltage) {
  const char *oldDesc = getPowerStateDescription(oldState);
  const char *newDesc = getPowerStateDescription(newState);
  
  // Determine if this is an improvement (recovering) or degradation
  bool recovering = (newState < oldState);
  
  // Build alert description
  char alertDesc[96];
  if (recovering) {
    snprintf(alertDesc, sizeof(alertDesc), "Power recovered: %s -> %s (%.2fV)", oldDesc, newDesc, voltage);
  } else {
    snprintf(alertDesc, sizeof(alertDesc), "Power reduced: %s -> %s (%.2fV)", oldDesc, newDesc, voltage);
  }
  
  Serial.print(F("Power state change: "));
  Serial.println(alertDesc);
  addSerialLog(alertDesc);
  
  if (!gNotecardAvailable) {
    Serial.println(F("Network offline - power state change not sent"));
    return;
  }
  
  JsonDocument doc;
  doc["c"] = gDeviceUID;
  doc["s"] = gConfig.siteName;
  doc["y"] = "power";
  doc["t"] = currentEpoch();
  
  // State transition (compact: "from"/"to" encode direction, no need for "recovering" or "desc")
  doc["from"] = oldDesc;
  doc["to"] = newDesc;
  doc["v"] = roundTo(voltage, 2);
  
  // Duration in previous state (seconds) — only if meaningful
  if (gPowerStateChangeMillis > 0) {
    doc["dur"] = (millis() - gPowerStateChangeMillis) / 1000UL;
  }
  
  // SMS escalation for critical hibernation entry only
  if (newState == POWER_STATE_CRITICAL_HIBERNATE && !recovering) {
    doc["se"] = true;
  }
  
  publishNote(ALARM_FILE, doc, true);
  Serial.println(F("Power state change sent to server"));
}

/**
 * Determine the best available battery voltage from both monitoring sources.
 * Returns the lower of the two (conservative approach — protect the battery).
 * A source is only considered if it has valid recent data.
 */
static float getEffectiveBatteryVoltage() {
  float voltage = 0.0f;
  bool hasVoltage = false;
  
  // Source 1: SunSaver MPPT via Modbus RS-485
  if (gSolarManager.isEnabled() && gSolarManager.isCommunicationOk()) {
    const SolarData &solar = gSolarManager.getData();
    if (solar.batteryVoltage > 0.0f) {
      voltage = solar.batteryVoltage;
      hasVoltage = true;
    }
  }
  
  // Source 2: Notecard card.voltage (direct battery connection)
  if (gConfig.batteryMonitor.enabled && gBatteryData.valid && gBatteryData.voltage > 0.0f) {
    if (!hasVoltage) {
      voltage = gBatteryData.voltage;
      hasVoltage = true;
    } else {
      // Use the LOWER of the two readings (conservative — protect battery)
      voltage = min(voltage, gBatteryData.voltage);
    }
  }
  
  return hasVoltage ? voltage : 0.0f;
}

/**
 * Evaluate battery voltage and update the power conservation state.
 * Uses hysteresis thresholds to prevent rapid oscillation:
 *   - Enter a worse state at the ENTER threshold (falling voltage)
 *   - Exit back to a better state at the EXIT threshold (rising voltage, higher than enter)
 * Requires POWER_STATE_DEBOUNCE_COUNT consecutive readings at the new state before transitioning.
 *
 * Called once per loop iteration, after battery/solar polling.
 */
static void updatePowerState() {
  float voltage = getEffectiveBatteryVoltage();
  
  // If no battery monitoring is active, stay in NORMAL
  if (voltage <= 0.0f) {
    gPowerState = POWER_STATE_NORMAL;
    return;
  }
  
  gEffectiveBatteryVoltage = voltage;
  
  // Determine the proposed state based on current voltage and hysteresis direction
  PowerState proposed;
  
  if (gPowerState == POWER_STATE_NORMAL) {
    // Currently NORMAL — check if we should degrade
    if (voltage < POWER_CRITICAL_ENTER_VOLTAGE) {
      proposed = POWER_STATE_CRITICAL_HIBERNATE;
    } else if (voltage < POWER_LOW_ENTER_VOLTAGE) {
      proposed = POWER_STATE_LOW_POWER;
    } else if (voltage < POWER_ECO_ENTER_VOLTAGE) {
      proposed = POWER_STATE_ECO;
    } else {
      proposed = POWER_STATE_NORMAL;
    }
  } else if (gPowerState == POWER_STATE_ECO) {
    // Currently ECO — can degrade further or recover
    if (voltage < POWER_CRITICAL_ENTER_VOLTAGE) {
      proposed = POWER_STATE_CRITICAL_HIBERNATE;
    } else if (voltage < POWER_LOW_ENTER_VOLTAGE) {
      proposed = POWER_STATE_LOW_POWER;
    } else if (voltage >= POWER_ECO_EXIT_VOLTAGE) {
      proposed = POWER_STATE_NORMAL;  // Recovered
    } else {
      proposed = POWER_STATE_ECO;     // Stay in ECO
    }
  } else if (gPowerState == POWER_STATE_LOW_POWER) {
    // Currently LOW_POWER — can degrade to CRITICAL or recover
    if (voltage < POWER_CRITICAL_ENTER_VOLTAGE) {
      proposed = POWER_STATE_CRITICAL_HIBERNATE;
    } else if (voltage >= POWER_LOW_EXIT_VOLTAGE) {
      proposed = POWER_STATE_ECO;     // Step up one level (not straight to NORMAL)
    } else {
      proposed = POWER_STATE_LOW_POWER;
    }
  } else {
    // Currently CRITICAL_HIBERNATE — only recover if voltage is high enough
    if (voltage >= POWER_CRITICAL_EXIT_VOLTAGE) {
      proposed = POWER_STATE_LOW_POWER;  // Step up one level at a time
    } else {
      proposed = POWER_STATE_CRITICAL_HIBERNATE;
    }
  }
  
  // Debounce: require consecutive readings at the proposed new state
  if (proposed != gPowerState) {
    gPowerStateDebounce++;
    if (gPowerStateDebounce >= POWER_STATE_DEBOUNCE_COUNT) {
      // State transition confirmed
      PowerState oldState = gPowerState;
      gPowerState = proposed;
      gPowerStateDebounce = 0;
      
      // Handle relay safety on entering CRITICAL
      if (gPowerState == POWER_STATE_CRITICAL_HIBERNATE) {
        // De-energize all relays to eliminate coil current draw (~100mA each)
        for (uint8_t i = 0; i < MAX_RELAYS; i++) {
          setRelayState(i, false);
        }
        Serial.println(F("CRITICAL HIBERNATE: All relays de-energized for battery protection"));
        addSerialLog("Relays off - critical battery");
      }
      
      // Notify server of the state change (entry or recovery)
      sendPowerStateChange(oldState, gPowerState, voltage);
      gPowerStateChangeMillis = millis();
      gPreviousPowerState = oldState;
      
      // Log to serial
      Serial.print(F("Power state: "));
      Serial.print(getPowerStateDescription(oldState));
      Serial.print(F(" -> "));
      Serial.print(getPowerStateDescription(gPowerState));
      Serial.print(F(" ("));
      Serial.print(voltage, 2);
      Serial.println(F("V)"));
    }
  } else {
    gPowerStateDebounce = 0;  // Reset debounce if proposed matches current
  }
  
  // Periodic power state log (every 30 minutes, only when not NORMAL)
  if (gPowerState != POWER_STATE_NORMAL) {
    unsigned long now = millis();
    if (now - gLastPowerStateLogMillis >= 1800000UL) {
      gLastPowerStateLogMillis = now;
      char logMsg[96];
      snprintf(logMsg, sizeof(logMsg), "Power: %s (%.2fV, sleep=%lums)", 
               getPowerStateDescription(gPowerState), voltage, getPowerStateSleepMs(gPowerState));
      Serial.println(logMsg);
      addSerialLog(logMsg);
    }
  }
}

static void sendDailyReport() {
  uint8_t eligibleIndices[MAX_TANKS];
  uint8_t eligibleCount = 0;
  for (uint8_t i = 0; i < gConfig.monitorCount; ++i) {
    if (gConfig.monitors[i].enableDailyReport) {
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
    JsonDocument doc;
    doc["c"] = gDeviceUID;
    doc["s"] = gConfig.siteName;
    doc["t"] = reportEpoch;
    doc["p"] = part;  // 0-based part number

    // Include VIN voltage in the first part of the daily report
    if (part == 0 && vinVoltage > 0.0f) {
      doc["v"] = vinVoltage;
    }
    
    // Include solar charger data in the first part of the daily report
    if (part == 0) {
      appendSolarDataToDaily(doc);
      appendBatteryDataToDaily(doc);  // Notecard battery voltage monitoring
      // Include power conservation state in daily report
      if (gPowerState != POWER_STATE_NORMAL) {
        JsonObject powerObj = doc["power"].to<JsonObject>();
        powerObj["state"] = getPowerStateDescription(gPowerState);
        powerObj["v"] = roundTo(gEffectiveBatteryVoltage, 2);
        powerObj["sleepMs"] = (long)getPowerStateSleepMs(gPowerState);
        if (gPowerStateChangeMillis > 0) {
          powerObj["stateDurSec"] = (millis() - gPowerStateChangeMillis) / 1000UL;
        }
      }
    }

    JsonArray tanks = doc["tanks"].to<JsonArray>();
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

    doc["m"] = (tankCursor < eligibleCount);  // more parts follow
    bool syncNow = (tankCursor >= eligibleCount);
    publishNote(DAILY_FILE, doc, syncNow);
    queuedAny = true;
    ++part;
  }

  if (queuedAny) {
    Serial.println(F("Daily report queued"));
  }
}

static bool appendDailyTank(JsonDocument &doc, JsonArray &array, uint8_t tankIndex, size_t payloadLimit) {
  if (tankIndex >= gConfig.monitorCount) {
    return false;
  }

  const MonitorConfig &cfg = gConfig.monitors[tankIndex];
  MonitorRuntime &state = gMonitorState[tankIndex];

  JsonObject t = array.add<JsonObject>();
  t["n"] = cfg.name;                              // label/name
  t["k"] = cfg.monitorNumber;                     // monitor number
  
  // Include object type in daily report (server's metadata refresh after restarts)
  switch (cfg.objectType) {
    case OBJECT_TANK:   t["ot"] = "tank";   break;
    case OBJECT_ENGINE: t["ot"] = "engine"; break;
    case OBJECT_PUMP:   t["ot"] = "pump";   break;
    case OBJECT_GAS:    t["ot"] = "gas";    break;
    case OBJECT_FLOW:   t["ot"] = "flow";   break;
    default:            t["ot"] = "custom"; break;
  }
  
  // Include measurement unit in daily (server refresh, omitted from frequent telemetry/alarms)
  if (strlen(cfg.measurementUnit) > 0) {
    t["mu"] = cfg.measurementUnit;
  }
  
  // Send sensor-type-appropriate raw data - server converts using config
  if (cfg.sensorInterface == SENSOR_CURRENT_LOOP) {
    // Current loop: send raw mA only - server converts to display units
    if (state.currentSensorMa >= 4.0f) {
      t["ma"] = roundTo(state.currentSensorMa, 2);
    }
  } else if (cfg.sensorInterface == SENSOR_ANALOG) {
    // Analog voltage: send raw voltage only - server converts to display units
    if (state.currentSensorVoltage > 0.0f) {
      t["vt"] = roundTo(state.currentSensorVoltage, 3);
    }
  } else if (cfg.sensorInterface == SENSOR_DIGITAL) {
    // Digital float switch: send float state
    t["fl"] = roundTo(state.currentInches, 1);
  } else {
    // Pulse/RPM: send value
    t["rm"] = roundTo(state.currentInches, 1);
  }
  // Note: sensor type (st), alarm thresholds (high/low), and email are
  // already known by the server from telemetry and config - not sent in daily

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
      JsonDocument doc;
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
  if (!doc["relay_reset_tank"].isNull()) {
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

  if (doc["relay"].isNull() || doc["state"].isNull()) {
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
  if (!doc["duration"].isNull() && state) {
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
  for (uint8_t i = 0; i < gConfig.monitorCount; ++i) {
    const MonitorConfig &cfg = gConfig.monitors[i];
    
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
  if (idx >= gConfig.monitorCount) {
    return;
  }
  
  const MonitorConfig &cfg = gConfig.monitors[idx];
  
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
  
  for (uint8_t i = 0; i < gConfig.monitorCount; ++i) {
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
// Location Request Handling
// ============================================================================
// Server can request the client's GPS location (e.g., for NWS weather lookup during calibration)
// Client responds with location via cell tower triangulation (low power)

static void pollForLocationRequests() {
  // Check for location requests from server
  J *req = notecard.newRequest("note.get");
  if (!req) {
    return;
  }
  
  JAddStringToObject(req, "file", LOCATION_REQUEST_FILE);
  JAddBoolToObject(req, "delete", true);
  
  J *rsp = notecard.requestAndResponse(req);
  if (!rsp) {
    return;
  }

  J *body = JGetObject(rsp, "body");
  if (!body) {
    notecard.deleteResponse(rsp);
    return;
  }

  const char *request = JGetString(body, "request");
  if (request && strcmp(request, "get_location") == 0) {
    Serial.println(F("Location request received from server"));
    
    // Fetch location and send response
    float latitude = 0.0f, longitude = 0.0f;
    bool hasLocation = fetchNotecardLocation(latitude, longitude);
    
    // Send location response
    J *respReq = notecard.newRequest("note.add");
    if (respReq) {
      JAddStringToObject(respReq, "file", LOCATION_RESPONSE_FILE);
      JAddBoolToObject(respReq, "sync", true);
      
      J *respBody = JCreateObject();
      if (respBody) {
        JAddStringToObject(respBody, "client", gDeviceUID);
        if (hasLocation) {
          JAddNumberToObject(respBody, "lat", latitude);
          JAddNumberToObject(respBody, "lon", longitude);
          JAddBoolToObject(respBody, "valid", true);
        } else {
          JAddBoolToObject(respBody, "valid", false);
          JAddStringToObject(respBody, "error", "Location unavailable");
        }
        JAddItemToObject(respReq, "body", respBody);
        
        if (notecard.sendRequest(respReq)) {
          Serial.println(F("Location response sent to server"));
        } else {
          Serial.println(F("Failed to send location response"));
        }
      }
    }
  }

  notecard.deleteResponse(rsp);
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

// ============================================================================
// Notecard Location Fetching (Cell Tower Triangulation)
// ============================================================================
// Fetches the device's location from the Notecard using cell tower triangulation
// This is much lower power than GPS and provides approximate location for weather lookups

static bool fetchNotecardLocation(float &latitude, float &longitude) {
  // First, request a location fix using cell tower triangulation
  J *req = notecard.newRequest("card.location");
  if (!req) {
    Serial.println(F("Failed to create card.location request"));
    return false;
  }
  
  J *rsp = notecard.requestAndResponse(req);
  if (!rsp) {
    Serial.println(F("No response from card.location"));
    return false;
  }
  
  // Check for error response
  const char *err = JGetString(rsp, "err");
  if (err && strlen(err) > 0) {
    // Location may not be available yet (device just powered on, no cell signal, etc.)
    Serial.print(F("card.location: "));
    Serial.println(err);
    notecard.deleteResponse(rsp);
    return false;
  }
  
  // Extract latitude and longitude
  // Response format: { "lat": 38.8894, "lon": -77.0352, "status": "GPS,WiFi,Triangulated", ... }
  double lat = JGetNumber(rsp, "lat");
  double lon = JGetNumber(rsp, "lon");
  const char *status = JGetString(rsp, "status");
  
  notecard.deleteResponse(rsp);
  
  // Validate coordinates (must be non-zero and in valid range)
  if (lat == 0.0 && lon == 0.0) {
    Serial.println(F("card.location: No valid coordinates"));
    return false;
  }
  
  if (lat < -90.0 || lat > 90.0 || lon < -180.0 || lon > 180.0) {
    Serial.println(F("card.location: Coordinates out of range"));
    return false;
  }
  
  latitude = (float)lat;
  longitude = (float)lon;
  
  Serial.print(F("Location: "));
  Serial.print(latitude, 4);
  Serial.print(F(", "));
  Serial.print(longitude, 4);
  if (status) {
    Serial.print(F(" ("));
    Serial.print(status);
    Serial.print(F(")"));
  }
  Serial.println();
  
  return true;
}


