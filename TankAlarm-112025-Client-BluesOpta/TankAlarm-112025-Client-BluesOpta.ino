/*
  Tank Alarm Client 112025 - Arduino Opta + Blues Notecard

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
  // Arduino Opta with Mbed OS - filesystem and watchdog disabled for now
  // TODO: Implement Mbed OS LittleFileSystem and mbed::Watchdog support
  #warning "LittleFS and Watchdog features disabled on Mbed OS platform"
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

static const uint8_t NOTECARD_I2C_ADDRESS = 0x17;
static const uint32_t NOTECARD_I2C_FREQUENCY = 400000UL;

enum SensorType : uint8_t {
  SENSOR_DIGITAL = 0,
  SENSOR_ANALOG = 1,
  SENSOR_CURRENT_LOOP = 2
};

struct TankConfig {
  char id;                 // Friendly identifier (A, B, C ...)
  char name[24];           // Site/tank label shown in reports
  uint8_t tankNumber;      // Numeric tank reference for legacy formatting
  SensorType sensorType;   // Digital, analog, or current loop
  int16_t primaryPin;      // Digital pin or analog channel
  int16_t secondaryPin;    // Optional secondary pin (unused by default)
  int16_t currentLoopChannel; // 4-20mA channel index (-1 if unused)
  float heightInches;      // Tank height for conversions
  float highAlarmInches;   // High threshold for triggering alarm
  float lowAlarmInches;    // Low threshold for triggering alarm
  float hysteresisInches;  // Hysteresis band (default 2.0 inches)
  bool enableDailyReport;  // Include in daily summary
  bool enableAlarmSms;     // Escalate SMS when alarms trigger
  bool enableServerUpload; // Send telemetry to server
  char relayTargetClient[48]; // Client UID to trigger relays on (empty = none)
  uint8_t relayMask;       // Bitmask of relays to trigger (bit 0=relay 1, etc.)
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
};

struct TankRuntime {
  float currentInches;
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

// Forward declarations
static void initializeStorage();
static void ensureConfigLoaded();
static void createDefaultConfig(ClientConfig &cfg);
static bool loadConfigFromFlash(ClientConfig &cfg);
static bool saveConfigToFlash(const ClientConfig &cfg);
static void printHardwareRequirements(const ClientConfig &cfg);
static void initializeNotecard();
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

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 2000) {
    delay(10);
  }

  Serial.println();
  Serial.println(F("Tank Alarm Client 112025 starting"));

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
  IWatchdog.begin(WATCHDOG_TIMEOUT_SECONDS * 1000000UL);  // Timeout in microseconds
  Serial.print(F("Watchdog timer enabled: "));
  Serial.print(WATCHDOG_TIMEOUT_SECONDS);
  Serial.println(F(" seconds"));
#else
  Serial.println(F("Warning: Watchdog timer not available on this platform"));
#endif

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

  Serial.println(F("Client setup complete"));
}

void loop() {
  unsigned long now = millis();

#ifdef WATCHDOG_AVAILABLE
  // Reset watchdog timer to prevent system reset
  IWatchdog.reload();
#endif

  // Check notecard health periodically
  static unsigned long lastHealthCheck = 0;
  if (now - lastHealthCheck > 30000UL) {  // Check every 30 seconds
    lastHealthCheck = now;
    if (!gNotecardAvailable) {
      checkNotecardHealth();
    }
  }

  if (now - gLastTelemetryMillis >= (unsigned long)gConfig.sampleSeconds * 1000UL) {
    gLastTelemetryMillis = now;
    sampleTanks();
  }

  if (now - gLastConfigCheckMillis >= 15000UL) {
    gLastConfigCheckMillis = now;
    pollForConfigUpdates();
  }

  if (now - gLastRelayCheckMillis >= 3600000UL) {  // Check every 1 hour
    gLastRelayCheckMillis = now;
    pollForRelayCommands();
  }

  persistConfigIfDirty();
  ensureTimeSync();
  updateDailyScheduleIfNeeded();

  if (gNextDailyReportEpoch > 0.0 && currentEpoch() >= gNextDailyReportEpoch) {
    sendDailyReport();
    scheduleNextDailyReport();
  }
}

static void initializeStorage() {
#ifdef FILESYSTEM_AVAILABLE
  if (!LittleFS.begin()) {
    Serial.println(F("LittleFS init failed; halting"));
    while (true) {
      delay(1000);
    }
  }
#else
  Serial.println(F("Warning: Filesystem not available on this platform - configuration will not persist"));
#endif
}

static void ensureConfigLoaded() {
  if (!loadConfigFromFlash(gConfig)) {
    createDefaultConfig(gConfig);
    gConfigDirty = true;
    persistConfigIfDirty();
    Serial.println(F("Default configuration written to flash"));
  }
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
  cfg.tanks[0].heightInches = 120.0f;
  cfg.tanks[0].highAlarmInches = 100.0f;
  cfg.tanks[0].lowAlarmInches = 20.0f;
  cfg.tanks[0].hysteresisInches = 2.0f; // 2 inch hysteresis band
  cfg.tanks[0].enableDailyReport = true;
  cfg.tanks[0].enableAlarmSms = true;
  cfg.tanks[0].enableServerUpload = true;
  cfg.tanks[0].relayTargetClient[0] = '\0'; // No relay target by default
  cfg.tanks[0].relayMask = 0; // No relays triggered by default
}

static bool loadConfigFromFlash(ClientConfig &cfg) {
#ifdef FILESYSTEM_AVAILABLE
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
    } else {
      cfg.tanks[i].sensorType = SENSOR_ANALOG;
    }
    cfg.tanks[i].primaryPin = t["primaryPin"].is<int>() ? t["primaryPin"].as<int>() : (cfg.tanks[i].sensorType == SENSOR_DIGITAL ? 2 : 0);
    cfg.tanks[i].secondaryPin = t["secondaryPin"].is<int>() ? t["secondaryPin"].as<int>() : -1;
    cfg.tanks[i].currentLoopChannel = t["loopChannel"].is<int>() ? t["loopChannel"].as<int>() : -1;
    cfg.tanks[i].heightInches = t["heightInches"].is<float>() ? t["heightInches"].as<float>() : 120.0f;
    cfg.tanks[i].highAlarmInches = t["highAlarm"].is<float>() ? t["highAlarm"].as<float>() : 100.0f;
    cfg.tanks[i].lowAlarmInches = t["lowAlarm"].is<float>() ? t["lowAlarm"].as<float>() : 20.0f;
    cfg.tanks[i].hysteresisInches = t["hysteresis"].is<float>() ? t["hysteresis"].as<float>() : 2.0f;
    cfg.tanks[i].enableDailyReport = t["daily"].is<bool>() ? t["daily"].as<bool>() : true;
    cfg.tanks[i].enableAlarmSms = t["alarmSms"].is<bool>() ? t["alarmSms"].as<bool>() : true;
    cfg.tanks[i].enableServerUpload = t["upload"].is<bool>() ? t["upload"].as<bool>() : true;
  }

  return true;
#else
  return false; // Filesystem not available
#endif
}

static bool saveConfigToFlash(const ClientConfig &cfg) {
#ifdef FILESYSTEM_AVAILABLE
  DynamicJsonDocument doc(4096);
  doc["site"] = cfg.siteName;
  doc["deviceLabel"] = cfg.deviceLabel;
  doc["serverFleet"] = cfg.serverFleet;
  doc["sampleSeconds"] = cfg.sampleSeconds;
  doc["levelChangeThreshold"] = cfg.minLevelChangeInches;
  doc["reportHour"] = cfg.reportHour;
  doc["reportMinute"] = cfg.reportMinute;
  doc["dailyEmail"] = cfg.dailyEmail;

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
      default: t["sensor"] = "analog"; break;
    }
    t["primaryPin"] = cfg.tanks[i].primaryPin;
    t["secondaryPin"] = cfg.tanks[i].secondaryPin;
    t["loopChannel"] = cfg.tanks[i].currentLoopChannel;
    t["heightInches"] = cfg.tanks[i].heightInches;
    t["highAlarm"] = cfg.tanks[i].highAlarmInches;
    t["lowAlarm"] = cfg.tanks[i].lowAlarmInches;
    t["hysteresis"] = cfg.tanks[i].hysteresisInches;
    t["daily"] = cfg.tanks[i].enableDailyReport;
    t["alarmSms"] = cfg.tanks[i].enableAlarmSms;
    t["upload"] = cfg.tanks[i].enableServerUpload;
  }

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

  for (uint8_t i = 0; i < cfg.tankCount; ++i) {
    if (cfg.tanks[i].sensorType == SENSOR_ANALOG) {
      needsAnalogExpansion = true;
    }
    if (cfg.tanks[i].sensorType == SENSOR_CURRENT_LOOP) {
      needsCurrentLoop = true;
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
  }
  if (needsRelayOutput) {
    Serial.println(F("Relay outputs wired for audible/visual alarm"));
  }
  Serial.println(F("-----------------------------"));
}

static void initializeNotecard() {
  notecard.setDebugOutputStream(Serial);
  notecard.begin(NOTECARD_I2C_ADDRESS);

  J *req = notecard.newRequest("card.wire");
  if (req) {
    JAddIntToObject(req, "speed", (int)NOTECARD_I2C_FREQUENCY);
    notecard.sendRequest(req);
  }

  req = notecard.newRequest("hub.set");
  if (req) {
    JAddStringToObject(req, "product", PRODUCT_UID);
    JAddStringToObject(req, "mode", "continuous");
    // No route needed - using fleet-based targeting
    notecard.sendRequest(req);
  }

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
      } else {
        gConfig.tanks[i].sensorType = SENSOR_ANALOG;
      }
      gConfig.tanks[i].primaryPin = t["primaryPin"].is<int>() ? t["primaryPin"].as<int>() : gConfig.tanks[i].primaryPin;
      gConfig.tanks[i].secondaryPin = t["secondaryPin"].is<int>() ? t["secondaryPin"].as<int>() : gConfig.tanks[i].secondaryPin;
      gConfig.tanks[i].currentLoopChannel = t["loopChannel"].is<int>() ? t["loopChannel"].as<int>() : gConfig.tanks[i].currentLoopChannel;
      gConfig.tanks[i].heightInches = t["heightInches"].is<float>() ? t["heightInches"].as<float>() : gConfig.tanks[i].heightInches;
      gConfig.tanks[i].highAlarmInches = t["highAlarm"].is<float>() ? t["highAlarm"].as<float>() : gConfig.tanks[i].highAlarmInches;
      gConfig.tanks[i].lowAlarmInches = t["lowAlarm"].is<float>() ? t["lowAlarm"].as<float>() : gConfig.tanks[i].lowAlarmInches;
      gConfig.tanks[i].hysteresisInches = t["hysteresis"].is<float>() ? t["hysteresis"].as<float>() : gConfig.tanks[i].hysteresisInches;
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
        strlcpy(gConfig.tanks[i].relayTargetClient, 
                t["relayTargetClient"].as<const char *>() ?: "", 
                sizeof(gConfig.tanks[i].relayTargetClient));
      }
      if (t.containsKey("relayMask")) {
        gConfig.tanks[i].relayMask = t["relayMask"].as<uint8_t>();
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

  // Check for out-of-range values (allow 10% margin)
  float minValid = -cfg.heightInches * 0.1f;
  float maxValid = cfg.heightInches * 1.1f;
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
      // Use explicit bounds check for pin
      int pin = (cfg.primaryPin >= 0 && cfg.primaryPin < 255) ? cfg.primaryPin : (2 + idx);
      pinMode(pin, INPUT_PULLUP);
      int level = digitalRead(pin);
      return level == HIGH ? cfg.heightInches : 0.0f;
    }
    case SENSOR_ANALOG: {
      // Use explicit bounds check for channel (A0602 has channels 0-7)
      int channel = (cfg.primaryPin >= 0 && cfg.primaryPin < 8) ? cfg.primaryPin : 0;
      // Opta analog channels use analogRead with index 0..7 for extension A0602
      if (cfg.heightInches < 0.1f) {
        return 0.0f;
      }
      float total = 0.0f;
      const uint8_t samples = 8;
      for (uint8_t i = 0; i < samples; ++i) {
        int raw = analogRead(channel);
        total += (float)raw / 4095.0f; // 12-bit resolution
        delay(2);
      }
      float avg = total / samples;
      return linearMap(avg, 0.05f, 0.95f, 0.0f, cfg.heightInches);
    }
    case SENSOR_CURRENT_LOOP: {
      // Use explicit bounds check for current loop channel
      int16_t channel = (cfg.currentLoopChannel >= 0 && cfg.currentLoopChannel < 8) ? cfg.currentLoopChannel : 0;
      if (cfg.heightInches < 0.1f) {
        return 0.0f;
      }
      float milliamps = readCurrentLoopMilliamps(channel);
      if (milliamps < 0.0f) {
        return gTankState[idx].currentInches; // keep previous on failure
      }
      return linearMap(milliamps, 4.0f, 20.0f, 0.0f, cfg.heightInches);
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

  // Apply hysteresis: use different thresholds for triggering vs clearing
  float highTrigger = cfg.highAlarmInches;
  float highClear = cfg.highAlarmInches - cfg.hysteresisInches;
  float lowTrigger = cfg.lowAlarmInches;
  float lowClear = cfg.lowAlarmInches + cfg.hysteresisInches;

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
  doc["heightInches"] = cfg.heightInches;
  doc["levelInches"] = state.currentInches;
  doc["percent"] = (cfg.heightInches > 0.1f) ? (state.currentInches / cfg.heightInches * 100.0f) : 0.0f;
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
    doc["highThreshold"] = cfg.highAlarmInches;
    doc["lowThreshold"] = cfg.lowAlarmInches;
    doc["smsEnabled"] = allowSmsEscalation;
    doc["time"] = currentEpoch();

    publishNote(ALARM_FILE, doc, true);
    Serial.print(F("Alarm sent for tank "));
    Serial.print(cfg.name);
    Serial.print(F(" type "));
    Serial.println(alarmType);
    
    // Trigger remote relays if configured
    if (isAlarm && cfg.relayTargetClient[0] != '\0' && cfg.relayMask != 0) {
      triggerRemoteRelays(cfg.relayTargetClient, cfg.relayMask, isAlarm);
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

  while (tankCursor < eligibleCount) {
    DynamicJsonDocument doc(1024);
    doc["client"] = gDeviceUID;
    doc["site"] = gConfig.siteName;
    doc["email"] = gConfig.dailyEmail;
    doc["time"] = reportEpoch;
    doc["part"] = static_cast<uint8_t>(part + 1);

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
  t["percent"] = (cfg.heightInches > 0.1f) ? (state.currentInches / cfg.heightInches * 100.0f) : 0.0f;
  t["high"] = cfg.highAlarmInches;
  t["low"] = cfg.lowAlarmInches;

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
    notecard.deleteResponse(req);
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
      notecard.deleteRequest(req);
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
}

static void pruneNoteBufferIfNeeded() {
#ifdef FILESYSTEM_AVAILABLE
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
  // Command format:
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
  // TODO: Implement timer tracking for automatic relay shutoff
  // This feature is documented in the API but not yet implemented
  if (doc.containsKey("duration") && state) {
    uint16_t duration = doc["duration"].as<uint16_t>();
    if (duration > 0) {
      Serial.print(F("Note: Auto-off duration requested ("));
      Serial.print(duration);
      Serial.println(F(" sec) but not yet implemented"));
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
