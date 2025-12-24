/*
  Tank Alarm Server 112025 - Arduino Opta + Blues Notecard
  Version: 1.0.0

  Hardware:
  - Arduino Opta Lite (built-in Ethernet)
  - Blues Wireless Notecard for Opta adapter

  Features:
  - Aggregates telemetry from client nodes via Blues Notecard
  - Dispatches SMS alerts for alarm events
  - Sends daily email summary of tank levels
  - Hosts lightweight intranet dashboard and REST API
  - Persists configuration to internal flash (LittleFS)
  - Allows remote client configuration updates via web UI

  Created: November 2025
  Using GitHub Copilot for code generation
*/

#include <Arduino.h>
#include <Wire.h>
#include <ArduinoJson.h>
#include <Notecard.h>
#include <memory>
#if defined(ARDUINO_OPTA) || defined(ARDUINO_PORTENTA_H7_M7) || defined(ARDUINO_PORTENTA_H7_M4)
  #include <PortentaEthernet.h>
  #include <Ethernet.h>
#else
  #include <Ethernet.h>
#endif
#include <math.h>
#include <string.h>
#include <ctype.h>

// Firmware version for production tracking
#define FIRMWARE_VERSION "1.0.0"
#define FIRMWARE_BUILD_DATE __DATE__

// Debug mode - controls Serial output and Notecard debug logging
// For PRODUCTION: Leave commented out (default) to save power consumption
// For DEVELOPMENT: Uncomment the line below for troubleshooting and monitoring
//#define DEBUG_MODE
#if defined(ARDUINO_ARCH_AVR)
  #include <avr/pgmspace.h>
#else
  #ifndef PROGMEM
    #define PROGMEM
  #endif
  #ifndef pgm_read_byte_near
    #define pgm_read_byte_near(addr) (*(const uint8_t *)(addr))
  #endif
#endif

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
  #define FILESYSTEM_AVAILABLE
  #define WATCHDOG_AVAILABLE
  #define WATCHDOG_TIMEOUT_SECONDS 30
  
  // Mbed OS filesystem instance
  static LittleFileSystem *mbedFS = nullptr;
  static BlockDevice *mbedBD = nullptr;
  static Watchdog &mbedWatchdog = Watchdog::get_instance();
#endif

#ifndef SERVER_PRODUCT_UID
#define SERVER_PRODUCT_UID "com.senax.tankalarm112025:server"
#endif

#ifndef SERVER_CONFIG_PATH
#define SERVER_CONFIG_PATH "/server_config.json"
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

#ifndef CONFIG_OUTBOX_FILE
#define CONFIG_OUTBOX_FILE "config.qo"
#endif

#ifndef MAX_TANK_RECORDS
#define MAX_TANK_RECORDS 32
#endif

// Email buffer must accommodate all tanks. Per-tank JSON: ~230 bytes worst-case
// (48 clientUid + 32 site + 24 label + 24 alarmType + keys/floats ~100)
// 32 tanks * 230 = 7360 + overhead. Using 16KB for ~70 tanks capacity with margin.
#ifndef MAX_EMAIL_BUFFER
#define MAX_EMAIL_BUFFER 16384
#endif

#ifndef NOTECARD_I2C_ADDRESS
#define NOTECARD_I2C_ADDRESS 0x17
#endif

#ifndef NOTECARD_I2C_FREQUENCY
#define NOTECARD_I2C_FREQUENCY 400000UL
#endif

#ifndef ETHERNET_PORT
#define ETHERNET_PORT 80
#endif

#ifndef DAILY_EMAIL_HOUR_DEFAULT
#define DAILY_EMAIL_HOUR_DEFAULT 6
#endif

#ifndef DAILY_EMAIL_MINUTE_DEFAULT
#define DAILY_EMAIL_MINUTE_DEFAULT 0
#endif

#ifndef MAX_SMS_ALERTS_PER_HOUR
#define MAX_SMS_ALERTS_PER_HOUR 2  // Maximum SMS alerts per tank per hour
#endif

#ifndef MIN_SMS_ALERT_INTERVAL_SECONDS
#define MIN_SMS_ALERT_INTERVAL_SECONDS 300  // Minimum 5 minutes between SMS for same tank
#endif

#ifndef CLIENT_CONFIG_CACHE_PATH
#define CLIENT_CONFIG_CACHE_PATH "/client_config_cache.txt"
#endif

#ifndef FTP_PORT_DEFAULT
#define FTP_PORT_DEFAULT 21
#endif

#ifndef FTP_PATH_DEFAULT
#define FTP_PATH_DEFAULT "/tankalarm/server"
#endif

#ifndef FTP_TIMEOUT_MS
#define FTP_TIMEOUT_MS 8000UL
#endif

#ifndef FTP_MAX_FILE_BYTES
#define FTP_MAX_FILE_BYTES 24576UL
#endif

#ifndef MAX_CLIENT_CONFIG_SNAPSHOTS
#define MAX_CLIENT_CONFIG_SNAPSHOTS 20
#endif

#ifndef VIEWER_SUMMARY_FILE
#define VIEWER_SUMMARY_FILE "viewer_summary.qo"
#endif

#ifndef VIEWER_SUMMARY_INTERVAL_SECONDS
#define VIEWER_SUMMARY_INTERVAL_SECONDS 21600UL  // 6 hours
#endif

#ifndef VIEWER_SUMMARY_BASE_HOUR
#define VIEWER_SUMMARY_BASE_HOUR 6
#endif

#ifndef MAX_RELAYS
#define MAX_RELAYS 4  // Arduino Opta has 4 relay outputs (D0-D3)
#endif

#ifndef SERVER_SERIAL_BUFFER_SIZE
#define SERVER_SERIAL_BUFFER_SIZE 100  // Keep last 100 server serial messages
#endif

#ifndef CLIENT_SERIAL_BUFFER_SIZE
#define CLIENT_SERIAL_BUFFER_SIZE 50  // Keep last 50 messages per client
#endif

#ifndef MAX_CLIENT_SERIAL_LOGS
#define MAX_CLIENT_SERIAL_LOGS 10  // Track serial logs for up to 10 clients
#endif

#ifndef SERIAL_LOG_FILE
#define SERIAL_LOG_FILE "serial_log.qi"  // Client serial logs sent to server
#endif

#ifndef SERIAL_REQUEST_FILE
#define SERIAL_REQUEST_FILE "serial_request.qi"  // Server requests for client logs
#endif

#ifndef SERIAL_ACK_FILE
#define SERIAL_ACK_FILE "serial_ack.qi"  // Client acknowledgments for log requests
#endif

#ifndef SERIAL_DEFAULT_MAX_ENTRIES
#define SERIAL_DEFAULT_MAX_ENTRIES 50
#endif

#ifndef SERIAL_STALE_SECONDS
#define SERIAL_STALE_SECONDS 1800  // 30 minutes
#endif

#ifndef MAX_HTTP_BODY_BYTES
#define MAX_HTTP_BODY_BYTES 16384  // Global cap on incoming HTTP body size (16KB)
#endif

#ifndef MAX_NOTES_PER_FILE_PER_POLL
#define MAX_NOTES_PER_FILE_PER_POLL 10  // Prevent long blocking notefile drains
#endif

// Calibration learning system constants
#ifndef CALIBRATION_LOG_PATH
#define CALIBRATION_LOG_PATH "/calibration_log.txt"
#endif

#ifndef MAX_CALIBRATION_ENTRIES
#define MAX_CALIBRATION_ENTRIES 100  // Max calibration entries per tank
#endif

#ifndef MAX_CALIBRATION_TANKS
#define MAX_CALIBRATION_TANKS 20  // Max tanks to track calibration for
#endif

// Tank JSON: each tank object has up to 13 fields (c,s,n,k,l,ma,st,d,pe,a,at,u + nested)
// Formula: array overhead + (count * object_size) + string buffer
static const size_t TANK_JSON_CAPACITY = JSON_ARRAY_SIZE(MAX_TANK_RECORDS) + (MAX_TANK_RECORDS * JSON_OBJECT_SIZE(16)) + 1024;
static const size_t CLIENT_JSON_CAPACITY = 32768;  // 32KB for full fleet data

static byte gMacAddress[6] = { 0x02, 0x00, 0x01, 0x12, 0x20, 0x25 };
static IPAddress gStaticIp(192, 168, 1, 200);
static IPAddress gStaticGateway(192, 168, 1, 1);
static IPAddress gStaticSubnet(255, 255, 255, 0);
static IPAddress gStaticDns(8, 8, 8, 8);

struct ServerConfig {
  char serverName[32];
  char clientFleet[32];  // Target fleet for client devices (e.g., "tankalarm-clients")
  char smsPrimary[20];
  char smsSecondary[20];
  char dailyEmail[64];
  char configPin[8];
  uint8_t dailyHour;
  uint8_t dailyMinute;
  uint16_t webRefreshSeconds;
  bool useStaticIp;
  bool smsOnHigh;
  bool smsOnLow;
  bool smsOnClear;
  // Optional LAN FTP backup/restore
  bool ftpEnabled;
  bool ftpPassive;
  bool ftpBackupOnChange;
  bool ftpRestoreOnBoot;
  uint16_t ftpPort;
  char ftpHost[64];
  char ftpUser[32];
  char ftpPass[32];
  char ftpPath[64];
};

struct TankRecord {
  char clientUid[48];
  char site[32];
  char label[24];
  uint8_t tankNumber;
  float levelInches;
  float sensorMa;             // Raw 4-20mA sensor reading (0 if not available)
  float sensorVoltage;        // Raw voltage sensor reading (0 if not available)
  char objectType[16];        // Object type: "tank", "engine", "pump", "gas", "flow"
  char sensorType[16];        // Sensor interface: "analog", "digital", "currentLoop", "pulse"
  char measurementUnit[8];    // Unit for display: "inches", "rpm", "psi", "gpm", etc.
  bool alarmActive;
  char alarmType[24];
  double lastUpdateEpoch;
  // 24-hour change tracking (computed server-side)
  float previousLevelInches;  // Level reading from ~24h ago
  double previousLevelEpoch;  // When the previous level was recorded
  // Rate limiting for SMS alerts
  double lastSmsAlertEpoch;
  uint8_t smsAlertsInLastHour;
  double smsAlertTimestamps[10];  // Track last 10 SMS alerts per tank
};

// Per-client metadata (VIN voltage, etc.)
#ifndef MAX_CLIENT_METADATA
#define MAX_CLIENT_METADATA 20
#endif

struct ClientMetadata {
  char clientUid[48];
  float vinVoltage;          // Blues Notecard VIN voltage from daily report
  double vinVoltageEpoch;    // When the VIN voltage was last updated
};

struct SerialLogEntry {
  double timestamp;          // Epoch timestamp
  char message[160];         // Log message
  char level[8];             // Log level (info, warn, error)
  char source[16];           // Source module identifier
};

struct ServerSerialBuffer {
  SerialLogEntry entries[SERVER_SERIAL_BUFFER_SIZE];
  uint8_t writeIndex;
  uint8_t count;
};

struct ClientSerialBuffer {
  char clientUid[48];
  SerialLogEntry entries[CLIENT_SERIAL_BUFFER_SIZE];
  uint8_t writeIndex;
  uint8_t count;
  double lastRequestEpoch;
  double lastAckEpoch;
  double lastLogEpoch;
  bool awaitingLogs;
  char lastAckStatus[24];
};

static ClientMetadata gClientMetadata[MAX_CLIENT_METADATA];
static uint8_t gClientMetadataCount = 0;

static ServerSerialBuffer gServerSerial;
static ClientSerialBuffer gClientSerialBuffers[MAX_CLIENT_SERIAL_LOGS];
static uint8_t gClientSerialBufferCount = 0;

// Calibration learning system data structures
// Stores manual level readings paired with sensor readings for learning
struct CalibrationEntry {
  double timestamp;           // Epoch time of the reading
  float sensorReading;        // Raw sensor value (mA for 4-20mA sensors)
  float verifiedLevelInches;  // Manually verified tank level in inches
  char notes[64];             // Optional notes about the reading
};

// Per-tank calibration data with learned parameters
struct TankCalibration {
  char clientUid[48];
  uint8_t tankNumber;
  // Learned linear regression parameters: level = slope * sensorReading + offset
  float learnedSlope;         // Learned inches per mA (replaces maxValue calculation)
  float learnedOffset;        // Learned offset in inches
  bool hasLearnedCalibration; // True if we have enough data points for calibration
  uint8_t entryCount;         // Number of calibration entries
  float rSquared;             // Goodness of fit (0-1, higher is better)
  double lastCalibrationEpoch; // When calibration was last updated
  // Original sensor configuration for reference
  float originalMaxValue;     // Original maxValue from config
  char sensorType[16];        // "pressure" or "ultrasonic"
  float sensorMountHeight;    // Sensor mount height in inches
};

static TankCalibration gTankCalibrations[MAX_CALIBRATION_TANKS];
static uint8_t gTankCalibrationCount = 0;

static ServerConfig gConfig;
static bool gConfigDirty = false;
static bool gPendingFtpBackup = false;
static bool gPaused = false;  // When true, pause Notecard processing for maintenance

static TankRecord gTankRecords[MAX_TANK_RECORDS];
static uint8_t gTankRecordCount = 0;

struct ClientConfigSnapshot {
  char uid[48];
  char site[32];
  char payload[1536];
};

static ClientConfigSnapshot gClientConfigs[MAX_CLIENT_CONFIG_SNAPSHOTS];
static uint8_t gClientConfigCount = 0;

static Notecard notecard;
static EthernetServer gWebServer(ETHERNET_PORT);
static char gServerUid[48] = {0};

static double gLastSyncedEpoch = 0.0;
static unsigned long gLastSyncMillis = 0;
static double gNextDailyEmailEpoch = 0.0;
static double gNextViewerSummaryEpoch = 0.0;
static double gLastViewerSummaryEpoch = 0.0;

static unsigned long gLastPollMillis = 0;
static unsigned long gLastLinkCheckMillis = 0;
static bool gLastLinkState = false;

// Email rate limiting
static double gLastDailyEmailSentEpoch = 0.0;
#define MIN_DAILY_EMAIL_INTERVAL_SECONDS 3600  // Minimum 1 hour between daily emails

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

// Helper to round float to N decimal places for data savings
static float roundTo(float val, int decimals) {
  float multiplier = pow(10, decimals);
  return round(val * multiplier) / multiplier;
}

static bool isValidPin(const char *pin) {
  if (!pin) {
    return false;
  }
  size_t len = strlen(pin);
  if (len != 4) {
    return false;
  }
  for (size_t i = 0; i < len; ++i) {
    if (!isdigit(pin[i])) {
      return false;
    }
  }
  return true;
}

static bool pinMatches(const char *pin) {
  if (!pin || gConfig.configPin[0] == '\0') {
    return false;
  }
  return strncmp(pin, gConfig.configPin, sizeof(gConfig.configPin)) == 0;
}

// Forward declaration for respondStatus (defined later in the web server section)
static void respondStatus(EthernetClient &client, int status, const char *message);
static void respondStatus(EthernetClient &client, int status, const String &message);

// Forward declarations for FTP backup/restore functions (defined in FTP section)
static bool performFtpBackup(char *errorOut = nullptr, size_t errorSize = 0);
static bool performFtpRestore(char *errorOut = nullptr, size_t errorSize = 0);

// Require that a valid admin PIN is configured and provided; respond with 403/400 on failure.
static bool requireValidPin(EthernetClient &client, const char *pinValue) {
  if (gConfig.configPin[0] == '\0') {
    respondStatus(client, 403, "Configure admin PIN before making changes");
    return false;
  }
  if (!pinMatches(pinValue)) {
    respondStatus(client, 403, "Invalid PIN");
    return false;
  }
  return true;
}

static const char SERVER_SETTINGS_HTML[] PROGMEM = R"HTML(<!DOCTYPE html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1"><title>Server Settings - Tank Alarm</title><style>:root{font-family:"Segoe UI",Arial,sans-serif;color-scheme:light dark}*{box-sizing:border-box}body{margin:0;min-height:100vh;background:var(--bg);color:var(--text);transition:background 0.2s ease,color 0.2s ease}body[data-theme="light"]{--bg:#f8fafc;--surface:#ffffff;--text:#1f2933;--muted:#475569;--header-bg:#e2e8f0;--card-border:rgba(15,23,42,0.08);--card-shadow:rgba(15,23,42,0.08);--accent:#2563eb;--accent-strong:#1d4ed8;--accent-contrast:#f8fafc;--chip:#f8fafc;--input-border:#cbd5e1;--danger:#ef4444;--pill-bg:rgba(37,99,235,0.12)}body[data-theme="dark"]{--bg:#0f172a;--surface:#1e293b;--text:#e2e8f0;--muted:#94a3b8;--header-bg:#16213d;--card-border:rgba(15,23,42,0.55);--card-shadow:rgba(0,0,0,0.55);--accent:#38bdf8;--accent-strong:#22d3ee;--accent-contrast:#0f172a;--chip:rgba(148,163,184,0.15);--input-border:rgba(148,163,184,0.4);--danger:#f87171;--pill-bg:rgba(56,189,248,0.18)}header{background:var(--header-bg);padding:28px 24px;box-shadow:0 20px 45px var(--card-shadow)}header .bar{display:flex;justify-content:space-between;gap:16px;flex-wrap:wrap;align-items:flex-start}header h1{margin:0;font-size:1.9rem}header p{margin:8px 0 0;color:var(--muted);max-width:640px;line-height:1.4}.header-actions{display:flex;gap:12px;flex-wrap:wrap;align-items:center}.pill{padding:10px 20px;text-decoration:none;font-weight:600;background:var(--pill-bg);color:var(--accent);border:1px solid transparent;transition:transform 0.15s ease}.pill:hover{transform:translateY(-1px)}.icon-button{width:42px;height:42px;border:1px solid var(--card-border);background:var(--surface);color:var(--text);font-size:1.2rem;cursor:pointer;transition:transform 0.15s ease}.icon-button:hover{transform:translateY(-1px)}main{padding:24px;max-width:1000px;margin:0 auto;width:100%}.card{background:var(--surface);border:1px solid var(--card-border);padding:20px;box-shadow:0 25px 55px var(--card-shadow)}h2{margin-top:0;font-size:1.3rem}h3{margin:20px 0 10px;font-size:1.1rem;border-bottom:1px solid var(--card-border);padding-bottom:6px;color:var(--text)}.field{display:flex;flex-direction:column;margin-bottom:12px}.field span{font-size:0.9rem;color:var(--muted);margin-bottom:4px}.field input,.field select{padding:10px 12px;border:1px solid var(--input-border);font-size:0.95rem;background:var(--bg);color:var(--text)}.form-grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(200px,1fr));gap:12px}.toggle-group{display:grid;grid-template-columns:repeat(auto-fit,minmax(220px,1fr));gap:12px;margin:16px 0}.toggle{display:flex;align-items:center;justify-content:space-between;border:1px solid var(--card-border);padding:10px 14px;background:var(--chip)}.toggle span{font-size:0.9rem;color:var(--muted)}.actions{margin-top:24px;display:flex;gap:12px;flex-wrap:wrap}button{border:none;padding:10px 16px;font-size:0.95rem;font-weight:600;cursor:pointer;background:var(--accent);color:var(--accent-contrast);transition:transform 0.15s ease}button.secondary{background:transparent;border:1px solid var(--card-border);color:var(--text)}button:hover{transform:translateY(-1px)}button:disabled{opacity:0.5;cursor:not-allowed;transform:none}#toast{position:fixed;left:50%;bottom:24px;transform:translateX(-50%);background:#0284c7;color:#fff;padding:12px 18px;box-shadow:0 10px 30px rgba(15,23,42,0.25);opacity:0;pointer-events:none;transition:opacity 0.3s ease;font-weight:600}#toast.show{opacity:1}.modal{position:fixed;inset:0;background:rgba(15,23,42,0.4);display:flex;align-items:center;justify-content:center;z-index:999;backdrop-filter:blur(4px)}.modal.hidden{opacity:0;pointer-events:none}.modal-card{background:var(--surface);padding:28px 26px 24px;width:min(420px,90%);border:1px solid var(--card-border);box-shadow:0 24px 50px rgba(15,23,42,0.35)}.modal-card h2{margin-top:0}.modal-card .field + .actions{margin-top:12px}</style></head><body data-theme="light"><header><div class="bar"><div><h1>Server Settings</h1><p>Configure server-wide settings including SMS alerts, FTP backup, and daily email reports.</p></div><div class="header-actions"><button class="icon-button" id="themeToggle" aria-label="Switch to dark mode">&#9789;</button><a class="pill" href="/">Dashboard</a><a class="pill secondary" href="/client-console">Client Console</a><a class="pill secondary" href="/contacts">Contacts</a></div></div></header><main><div class="card"><h2>Server Configuration</h2><form id="settingsForm"><h3>Server SMS Alerts</h3><div class="form-grid"><label class="field"><span>SMS Primary</span><input id="smsPrimaryInput" type="text" placeholder="+1234567890"></label><label class="field"><span>SMS Secondary</span><input id="smsSecondaryInput" type="text" placeholder="+1234567890"></label></div><div class="toggle-group"><label class="toggle"><span>Send SMS on High Alarm</span><input type="checkbox" id="smsHighToggle"></label><label class="toggle"><span>Send SMS on Low Alarm</span><input type="checkbox" id="smsLowToggle"></label><label class="toggle"><span>Send SMS on Clear Alarm</span><input type="checkbox" id="smsClearToggle"></label></div><h3>Daily Email Report</h3><div class="form-grid"><label class="field"><span>Daily Email Hour (0-23)</span><input id="dailyHourInput" type="number" min="0" max="23" value="5"></label><label class="field"><span>Daily Email Minute (0-59)</span><input id="dailyMinuteInput" type="number" min="0" max="59" value="0"></label><label class="field"><span>Daily Report Email</span><input id="dailyEmailInput" type="email" placeholder="reports@example.com"></label></div><h3>FTP Backup & Restore</h3><div class="form-grid"><label class="field"><span>FTP Host</span><input id="ftpHost" type="text" placeholder="192.168.1.50"></label><label class="field"><span>FTP Port</span><input id="ftpPort" type="number" min="1" max="65535" value="21"></label><label class="field"><span>FTP User</span><input id="ftpUser" type="text" placeholder="user"></label><label class="field"><span>FTP Password <small style="color:var(--muted);font-weight:400;">(leave blank to keep)</small></span><input id="ftpPass" type="password" autocomplete="off"></label><label class="field"><span>FTP Path</span><input id="ftpPath" type="text" placeholder="/tankalarm/server"></label></div><div class="toggle-group"><label class="toggle"><span>Enable FTP</span><input type="checkbox" id="ftpEnabled"></label><label class="toggle"><span>Passive Mode</span><input type="checkbox" id="ftpPassive" checked></label><label class="toggle"><span>Auto-backup on save</span><input type="checkbox" id="ftpBackupOnChange"></label><label class="toggle"><span>Restore on boot</span><input type="checkbox" id="ftpRestoreOnBoot"></label></div><div class="actions"><button type="button" id="ftpBackupNow">Backup Now</button><button type="button" class="secondary" id="ftpRestoreNow">Restore Now</button></div><div class="actions"><button type="submit">Save Settings</button></div></form></div></main><div id="toast"></div><div id="pinModal" class="modal hidden"><div class="modal-card"><h2>Enter Admin PIN</h2><p style="color:var(--muted);margin-bottom:16px;">Server settings require authentication</p><label class="field"><span>PIN</span><input id="pinInput" type="password" autocomplete="off"></label><div class="actions"><button type="button" id="pinCancel" class="secondary">Cancel</button><button type="button" id="pinSubmit">Continue</button></div></div></div><script>const THEME_KEY='tankalarmTheme';const themeToggle=document.getElementById('themeToggle');function applyTheme(next){const theme=next==='dark'?'dark':'light';document.body.dataset.theme=theme;themeToggle.textContent=theme==='dark'?'☀':'☾';themeToggle.setAttribute('aria-label',theme==='dark'?'Switch to light mode':'Switch to dark mode');localStorage.setItem(THEME_KEY,theme);}applyTheme(localStorage.getItem(THEME_KEY)||'light');themeToggle.addEventListener('click',()=>{const next=document.body.dataset.theme==='dark'?'light':'dark';applyTheme(next);});const state={pin:null,pinConfigured:false,pendingAction:null};const els={smsPrimary:document.getElementById('smsPrimaryInput'),smsSecondary:document.getElementById('smsSecondaryInput'),smsHighToggle:document.getElementById('smsHighToggle'),smsLowToggle:document.getElementById('smsLowToggle'),smsClearToggle:document.getElementById('smsClearToggle'),dailyHour:document.getElementById('dailyHourInput'),dailyMinute:document.getElementById('dailyMinuteInput'),dailyEmail:document.getElementById('dailyEmailInput'),ftpEnabled:document.getElementById('ftpEnabled'),ftpPassive:document.getElementById('ftpPassive'),ftpBackupOnChange:document.getElementById('ftpBackupOnChange'),ftpRestoreOnBoot:document.getElementById('ftpRestoreOnBoot'),ftpHost:document.getElementById('ftpHost'),ftpPort:document.getElementById('ftpPort'),ftpUser:document.getElementById('ftpUser'),ftpPass:document.getElementById('ftpPass'),ftpPath:document.getElementById('ftpPath'),ftpBackupNow:document.getElementById('ftpBackupNow'),ftpRestoreNow:document.getElementById('ftpRestoreNow'),form:document.getElementById('settingsForm'),toast:document.getElementById('toast'),pinModal:document.getElementById('pinModal'),pinInput:document.getElementById('pinInput'),pinSubmit:document.getElementById('pinSubmit'),pinCancel:document.getElementById('pinCancel')};function showToast(message,isError){els.toast.textContent=message;els.toast.style.background=isError?'#dc2626':'#0284c7';els.toast.classList.add('show');setTimeout(()=>els.toast.classList.remove('show'),2500);}function showPinModal(callback){state.pendingAction=callback;els.pinInput.value='';els.pinModal.classList.remove('hidden');els.pinInput.focus();}function hidePinModal(){els.pinModal.classList.add('hidden');state.pendingAction=null;}els.pinSubmit.addEventListener('click',()=>{const pin=els.pinInput.value.trim();if(pin){state.pin=pin;hidePinModal();if(state.pendingAction){state.pendingAction();}}else{showToast('PIN required',true);}});els.pinCancel.addEventListener('click',hidePinModal);els.pinInput.addEventListener('keyup',e=>{if(e.key==='Enter'){els.pinSubmit.click();}});async function loadSettings(){try{const res=await fetch('/api/clients');if(!res.ok)throw new Error('Failed to fetch server data');const data=await res.json();let serverInfo={};if(data&&data.srv){const s=data.srv||{};serverInfo={smsPrimary:s.sp||'',smsSecondary:s.ss||'',smsOnHigh:!!s.sh,smsOnLow:!!s.sl,smsOnClear:!!s.sc,dailyHour:typeof s.dh==='number'?s.dh:5,dailyMinute:typeof s.dm==='number'?s.dm:0,dailyEmail:s.de||''};state.pinConfigured=!!s.pc;}els.smsPrimary.value=serverInfo.smsPrimary||'';els.smsSecondary.value=serverInfo.smsSecondary||'';els.smsHighToggle.checked=!!serverInfo.smsOnHigh;els.smsLowToggle.checked=!!serverInfo.smsOnLow;els.smsClearToggle.checked=!!serverInfo.smsOnClear;els.dailyHour.value=serverInfo.dailyHour||5;els.dailyMinute.value=serverInfo.dailyMinute||0;els.dailyEmail.value=serverInfo.dailyEmail||'';const ftp=serverInfo.ftp||{};els.ftpEnabled.checked=!!ftp.enabled;els.ftpPassive.checked=ftp.passive!==false;els.ftpBackupOnChange.checked=!!ftp.backupOnChange;els.ftpRestoreOnBoot.checked=!!ftp.restoreOnBoot;els.ftpHost.value=ftp.host||'';els.ftpPort.value=ftp.port||21;els.ftpUser.value=ftp.user||'';els.ftpPath.value=ftp.path||'/tankalarm/server';els.ftpPass.value='';}catch(err){showToast(err.message||'Failed to load settings',true);}}async function saveSettingsImpl(){const ftpSettings={enabled:!!els.ftpEnabled.checked,passive:!!els.ftpPassive.checked,backupOnChange:!!els.ftpBackupOnChange.checked,restoreOnBoot:!!els.ftpRestoreOnBoot.checked,host:els.ftpHost.value.trim(),port:parseInt(els.ftpPort.value,10)||21,user:els.ftpUser.value.trim(),path:els.ftpPath.value.trim()||'/tankalarm/server'};const ftpPass=els.ftpPass.value.trim();if(ftpPass){ftpSettings.pass=ftpPass;}const payload={pin:state.pin||'',server:{smsPrimary:els.smsPrimary.value.trim(),smsSecondary:els.smsSecondary.value.trim(),smsOnHigh:!!els.smsHighToggle.checked,smsOnLow:!!els.smsLowToggle.checked,smsOnClear:!!els.smsClearToggle.checked,dailyHour:parseInt(els.dailyHour.value,10)||5,dailyMinute:parseInt(els.dailyMinute.value,10)||0,dailyEmail:els.dailyEmail.value.trim(),ftp:ftpSettings}};try{const res=await fetch('/api/server-settings',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(payload)});if(!res.ok){if(res.status===403){state.pin=null;throw new Error('PIN required or invalid');}const text=await res.text();throw new Error(text||'Server rejected settings');}showToast('Settings saved successfully');await loadSettings();}catch(err){showToast(err.message||'Failed to save settings',true);}}async function saveSettings(event){event.preventDefault();if(state.pinConfigured&&!state.pin){showPinModal(saveSettingsImpl);}else{await saveSettingsImpl();}}async function performFtpAction(kind){const payload={};const endpoint=kind==='restore'?'/api/ftp-restore':'/api/ftp-backup';try{const res=await fetch(endpoint,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(payload)});if(!res.ok){const text=await res.text();throw new Error(text||`${kind} failed`);}const data=await res.json();if(data&&data.message){showToast(data.message,false);}else{showToast(kind==='restore'?'FTP restore completed':'FTP backup completed');}}catch(err){showToast(err.message||`FTP ${kind} failed`,true);}}els.form.addEventListener('submit',saveSettings);els.ftpBackupNow.addEventListener('click',()=>performFtpAction('backup'));els.ftpRestoreNow.addEventListener('click',()=>performFtpAction('restore'));loadSettings();</script></body></html>)HTML";

static const char CONFIG_GENERATOR_HTML[] PROGMEM = R"HTML(<!DOCTYPE html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1"><title>Config Generator</title><style>:root{font-family:"Segoe UI",Arial,sans-serif;color-scheme:light dark}*{box-sizing:border-box}body{margin:0;min-height:100vh;background:var(--bg);color:var(--text);transition:background 0.2s ease,color 0.2s ease}body[data-theme="light"]{--bg:#f8fafc;--surface:#ffffff;--text:#1f2933;--muted:#475569;--header-bg:#e2e8f0;--card-border:rgba(15,23,42,0.08);--card-shadow:rgba(15,23,42,0.08);--accent:#2563eb;--accent-strong:#1d4ed8;--accent-contrast:#f8fafc;--chip:#f8fafc;--input-border:#cbd5e1;--danger:#ef4444;--pill-bg:rgba(37,99,235,0.12)}body[data-theme="dark"]{--bg:#0f172a;--surface:#1e293b;--text:#e2e8f0;--muted:#94a3b8;--header-bg:#16213d;--card-border:rgba(15,23,42,0.55);--card-shadow:rgba(0,0,0,0.55);--accent:#38bdf8;--accent-strong:#22d3ee;--accent-contrast:#0f172a;--chip:rgba(148,163,184,0.15);--input-border:rgba(148,163,184,0.4);--danger:#f87171;--pill-bg:rgba(56,189,248,0.18)}header{background:var(--header-bg);padding:28px 24px;box-shadow:0 20px 45px var(--card-shadow)}header .bar{display:flex;justify-content:space-between;gap:16px;flex-wrap:wrap;align-items:flex-start}header h1{margin:0;font-size:1.9rem}header p{margin:8px 0 0;color:var(--muted);max-width:640px;line-height:1.4}.header-actions{display:flex;gap:12px;flex-wrap:wrap;align-items:center}.pill{padding:10px 20px;text-decoration:none;font-weight:600;background:var(--pill-bg);color:var(--accent);border:1px solid transparent;transition:transform 0.15s ease}.pill:hover{transform:translateY(-1px)}.icon-button{width:42px;height:42px;border:1px solid var(--card-border);background:var(--surface);color:var(--text);font-size:1.2rem;cursor:pointer;transition:transform 0.15s ease}.icon-button:hover{transform:translateY(-1px)}main{padding:24px;max-width:1000px;margin:0 auto;width:100%}.card{background:var(--surface);border:1px solid var(--card-border);padding:20px;box-shadow:0 25px 55px var(--card-shadow)}h2{margin-top:0;font-size:1.3rem}h3{margin:20px 0 10px;font-size:1.1rem;border-bottom:1px solid var(--card-border);padding-bottom:6px;color:var(--text)}.field{display:flex;flex-direction:column;margin-bottom:12px}.field span{font-size:0.9rem;color:var(--muted);margin-bottom:4px}.field input,.field select{padding:10px 12px;border:1px solid var(--input-border);font-size:0.95rem;background:var(--bg);color:var(--text)}.form-grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(200px,1fr));gap:12px}.sensor-card{background:var(--chip);border:1px solid var(--card-border);padding:16px;margin-bottom:16px;position:relative}.sensor-header{display:flex;justify-content:space-between;margin-bottom:12px}.sensor-title{font-weight:600;color:var(--text)}.remove-btn{color:var(--danger);cursor:pointer;font-size:0.9rem;border:none;background:none;padding:0;font-weight:600}.remove-btn:hover{opacity:0.8}.actions{margin-top:24px;display:flex;gap:12px;flex-wrap:wrap}.collapsible-section{display:none}.collapsible-section.visible{display:block}.add-section-btn{background:transparent;border:1px dashed var(--input-border);color:var(--accent);padding:8px 14px;font-size:0.85rem;cursor:pointer;margin-top:8px}.add-section-btn:hover{background:var(--pill-bg)}.add-section-btn.hidden{display:none}.tooltip-icon{display:inline-flex;align-items:center;justify-content:center;width:16px;height:16px;background:var(--muted);color:var(--surface);font-size:0.7rem;font-weight:700;cursor:help;margin-left:4px;position:relative}.tooltip-icon:hover::after,.tooltip-icon:focus::after{content:attr(data-tooltip);position:absolute;bottom:100%;left:50%;transform:translateX(-50%);background:var(--text);color:var(--bg);padding:6px 10px;font-size:0.75rem;font-weight:400;max-width:280px;white-space:normal;z-index:100;margin-bottom:4px;box-shadow:0 4px 12px var(--card-shadow)}button{border:none;padding:10px 16px;font-size:0.95rem;font-weight:600;cursor:pointer;background:var(--accent);color:var(--accent-contrast);transition:transform 0.15s ease}button.secondary{background:transparent;border:1px solid var(--card-border);color:var(--text)}button:hover{transform:translateY(-1px)}button:disabled{opacity:0.5;cursor:not-allowed;transform:none}</style></head><body data-theme="light"><header><div class="bar"><div><h1>Config Generator</h1><p> Create new client configurations with sensor definitions and upload settings for Tank Alarm field units. </p></div><div class="header-actions"><button class="icon-button" id="themeToggle" aria-label="Switch to dark mode">&#9789;</button><a class="pill" href="/">Dashboard</a><a class="pill secondary" href="/contacts">Contacts</a></div></div></header><main><div class="card"><h2>New Client Configuration</h2><form id="generatorForm"><div class="form-grid"><label class="field"><span>Site Name</span><input id="siteName" type="text" placeholder="Site Name" required></label><label class="field"><span>Device Label</span><input id="deviceLabel" type="text" placeholder="Device Label" required></label><label class="field"><span>Server Fleet</span><input id="serverFleet" type="text" value="tankalarm-server"></label><label class="field"><span>Sample Minutes</span><input id="sampleMinutes" type="number" value="30" min="1" max="1440"></label><label class="field"><span>Level Change Threshold (in)<span class="tooltip-icon" tabindex="0" data-tooltip="Minimum level change in inches required before sending telemetry. Set to 0 to send all readings. Useful to reduce data usage by only reporting significant changes.">?</span></span><input id="levelChangeThreshold" type="number" step="0.1" value="0" placeholder="0 = disabled"></label><label class="field"><span>Report Time</span><input id="reportTime" type="time" value="05:00"></label><label class="field"><span>Daily Report Email Recipient</span><input id="dailyEmail" type="email"></label></div><h3>Power Configuration</h3><div class="form-grid"><label class="field" style="display: flex; align-items: center; gap: 8px; grid-column: 1 / -1;"><input type="checkbox" id="solarPowered" style="width: auto;"><span>Solar Powered<span class="tooltip-icon" tabindex="0" data-tooltip="Enable power saving features for solar-powered installations. Uses periodic mode with 60-minute inbound check intervals and deep sleep routines. When disabled (grid-tied), uses continuous mode for faster response times.">?</span></span></label></div><h3>Sensors</h3><div id="sensorsContainer"></div><div class="actions" style="margin-bottom: 24px;"><button type="button" id="addSensorBtn" class="secondary">+ Add Sensor</button></div><h3>Inputs (Buttons &amp; Switches)</h3><p style="color: var(--muted); font-size: 0.9rem; margin-bottom: 12px;">Configure physical inputs for actions like clearing relay alarms. More input actions will be available in future updates.</p><div id="inputsContainer"></div><div class="actions" style="margin-bottom: 24px;"><button type="button" id="addInputBtn" class="secondary">+ Add Input</button></div><div class="actions"><button type="button" id="downloadBtn">Download Config</button></div></form></div></main><script>const THEME_KEY = 'tankalarmTheme';const themeToggle = document.getElementById('themeToggle');function applyTheme(next){const theme = next === 'dark' ? 'dark':'light';document.body.dataset.theme = theme;themeToggle.textContent = theme === 'dark' ? '☀':'☾';themeToggle.setAttribute('aria-label',theme === 'dark' ? 'Switch to light mode':'Switch to dark mode');localStorage.setItem(THEME_KEY,theme);}applyTheme(localStorage.getItem(THEME_KEY)|| 'light');themeToggle.addEventListener('click',()=>{const next = document.body.dataset.theme === 'dark' ? 'light':'dark';applyTheme(next);});const sensorTypes = [{value:0,label:'Digital Input(Float Switch)'},{value:1,label:'Analog Input(0-10V)'},{value:2,label:'Current Loop(4-20mA)'},{value:3,label:'Hall Effect RPM'}];const currentLoopTypes = [{value:'pressure',label:'Pressure Sensor(Bottom-Mounted)',tooltip:'Pressure sensor mounted near tank bottom(e.g.,Dwyer 626-06-CB-P1-E5-S1). 4mA = empty,20mA = full.'},{value:'ultrasonic',label:'Ultrasonic Sensor(Top-Mounted)',tooltip:'Ultrasonic level sensor mounted on tank top(e.g.,Siemens Sitrans LU240). 4mA = full,20mA = empty.'}];const monitorTypes = [{value:'tank',label:'Tank Level'},{value:'gas',label:'Gas Pressure'},{value:'rpm',label:'RPM Sensor'}];const optaPins = [{value:0,label:'Opta I1'},{value:1,label:'Opta I2'},{value:2,label:'Opta I3'},{value:3,label:'Opta I4'},{value:4,label:'Opta I5'},{value:5,label:'Opta I6'},{value:6,label:'Opta I7'},{value:7,label:'Opta I8'}];const expansionChannels = [{value:0,label:'A0602 Ch1'},{value:1,label:'A0602 Ch2'},{value:2,label:'A0602 Ch3'},{value:3,label:'A0602 Ch4'},{value:4,label:'A0602 Ch5'},{value:5,label:'A0602 Ch6'}];let sensorCount = 0;function createSensorHtml(id){return ` <div class="sensor-card" id="sensor-${id}"><div class="sensor-header"><span class="sensor-title">Sensor #${id + 1}</span><button type="button" class="remove-btn" onclick="removeSensor(${id})">Remove</button></div><div class="form-grid"><label class="field"><span>Monitor Type</span><select class="monitor-type" onchange="updateMonitorFields(${id})"> ${monitorTypes.map(t => `<option value="${t.value}">${t.label}</option>`).join('')}</select></label><label class="field tank-num-field"><span>Tank Number</span><input type="number" class="tank-num" value="${id + 1}"></label><label class="field"><span><span class="name-label">Tank Name</span></span><input type="text" class="tank-name" placeholder="Name"></label><label class="field"><span>Sensor Type</span><select class="sensor-type" onchange="updatePinOptions(${id})"> ${sensorTypes.map(t => `<option value="${t.value}">${t.label}</option>`).join('')}</select></label><label class="field"><span>Pin / Channel</span><select class="sensor-pin"> ${optaPins.map(p => `<option value="${p.value}">${p.label}</option>`).join('')}</select></label><label class="field switch-mode-field" style="display:none;"><span>Switch Mode<span class="tooltip-icon" tabindex="0" data-tooltip="NO(Normally-Open):Switch is open by default,closes when fluid is present. NC(Normally-Closed):Switch is closed by default,opens when fluid is present. The wiring is the same - only the software interpretation changes.">?</span></span><select class="switch-mode"><option value="NO">Normally-Open(NO)</option><option value="NC">Normally-Closed(NC)</option></select></label><label class="field pulses-per-rev-field" style="display:none;"><span>Pulses/Rev</span><input type="number" class="pulses-per-rev" value="1" min="1" max="255"></label><label class="field current-loop-type-field" style="display:none;"><span>4-20mA Sensor Type<span class="tooltip-icon" tabindex="0" data-tooltip="Select the type of 4-20mA sensor:Pressure sensors are mounted near the tank bottom and measure liquid pressure. Ultrasonic sensors are mounted on top of the tank and measure distance to the liquid surface.">?</span></span><select class="current-loop-type" onchange="updateCurrentLoopFields(${id})"><option value="pressure">Pressure Sensor(Bottom-Mounted)</option><option value="ultrasonic">Ultrasonic Sensor(Top-Mounted)</option></select></label><label class="field sensor-range-field" style="display:none;"><span><span class="sensor-range-label">Sensor Range</span><span class="tooltip-icon sensor-range-tooltip" tabindex="0" data-tooltip="Native measurement range of the sensor(e.g.,0-5 PSI for pressure,0-10m for ultrasonic). This is the range that corresponds to 4-20mA output.">?</span></span><div style="display:flex;gap:8px;align-items:center;"><input type="number" class="sensor-range-min" value="0" step="0.1" style="width:70px;" placeholder="Min"><span>to</span><input type="number" class="sensor-range-max" value="5" step="0.1" style="width:70px;" placeholder="Max"><select class="sensor-range-unit" style="width:70px;"><option value="PSI">PSI</option><option value="bar">bar</option><option value="m">m</option><option value="ft">ft</option><option value="in">in</option><option value="cm">cm</option></select></div></label><label class="field sensor-mount-height-field" style="display:none;"><span><span class="mount-height-label">Sensor Mount Height(in)</span><span class="tooltip-icon mount-height-tooltip" tabindex="0" data-tooltip="For pressure sensors:height of sensor above tank bottom(usually 0-2 inches). For ultrasonic sensors:distance from sensor to tank bottom when empty.">?</span></span><input type="number" class="sensor-mount-height" value="0" step="0.1" min="0"></label><label class="field height-field"><span><span class="height-label">Height(in)</span><span class="tooltip-icon height-tooltip" tabindex="0" data-tooltip="Maximum height or capacity of the tank in inches. Used to set alarm thresholds relative to tank size.">?</span></span><input type="number" class="tank-height" value="120"></label></div><div class="digital-sensor-info" style="display:none;background:var(--chip);border:1px solid var(--card-border);padding:12px;margin-top:8px;font-size:0.9rem;color:var(--muted);"><strong>Float Switch Mode:</strong> This sensor only detects whether fluid has reached the switch position. It does not measure actual fluid level. The alarm will trigger when the switch is activated(fluid present)or not activated(fluid absent).<br><br><strong>Wiring Note:</strong> For both NO and NC switches,connect the switch between the input pin and GND. The software uses an internal pull-up resistor and interprets the signal based on your selected switch mode. </div><div class="current-loop-sensor-info" style="display:none;background:var(--chip);border:1px solid var(--card-border);padding:12px;margin-top:8px;font-size:0.9rem;color:var(--muted);"><div class="pressure-sensor-info"><strong>Pressure Sensor(Bottom-Mounted):</strong> Installed near the bottom of the tank,this sensor measures the pressure of the liquid column above it. Examples:Dwyer 626-06-CB-P1-E5-S1(0-5 PSI).<br> • 4mA = Empty tank(0 pressure)<br> • 20mA = Full tank(max pressure)<br> • Sensor Range:The native pressure range(e.g.,0-5 PSI,0-2 bar)<br> • Mount Height:Distance from sensor to tank bottom(usually 0-2 inches)</div><div class="ultrasonic-sensor-info" style="display:none;"><strong>Ultrasonic Sensor(Top-Mounted):</strong> Mounted on top of the tank,this sensor measures the distance from the sensor to the liquid surface. Examples:Siemens Sitrans LU240.<br> • 4mA = Full tank(liquid close to sensor)<br> • 20mA = Empty tank(liquid far from sensor)<br> • Sensor Range:The native distance range(e.g.,0-10m,0-30ft)<br> • Sensor Mount Height:Distance from sensor to tank bottom when empty </div></div><button type="button" class="add-section-btn add-alarm-btn" onclick="toggleAlarmSection(${id})">+ Add Alarm</button><div class="collapsible-section alarm-section"><h4 style="margin:16px 0 8px;font-size:0.95rem;border-top:1px solid var(--card-border);padding-top:12px;"><span class="alarm-section-title">Alarm Thresholds</span><button type="button" class="remove-btn" onclick="removeAlarmSection(${id})" style="float:right;">Remove Alarm</button></h4><div class="form-grid alarm-thresholds-grid"><div class="field"><span><label style="display:flex;align-items:center;gap:6px;"><input type="checkbox" class="high-alarm-enabled" checked> High Alarm </label></span><input type="number" class="high-alarm" value="100"></div><div class="field"><span><label style="display:flex;align-items:center;gap:6px;"><input type="checkbox" class="low-alarm-enabled" checked> Low Alarm </label></span><input type="number" class="low-alarm" value="20"></div></div><div class="form-grid digital-alarm-grid" style="display:none;"><div class="field" style="grid-column:1 / -1;"><span>Trigger Condition<span class="tooltip-icon" tabindex="0" data-tooltip="Select when the alarm should trigger based on the float switch state.">?</span></span><select class="digital-trigger-state"><option value="activated">When Switch is Activated(fluid detected)</option><option value="not_activated">When Switch is NOT Activated(no fluid)</option></select></div></div></div><button type="button" class="add-section-btn add-relay-btn hidden" onclick="toggleRelaySection(${id})">+ Add Relay Control</button><div class="collapsible-section relay-section"><h4 style="margin:16px 0 8px;font-size:0.95rem;border-top:1px solid var(--card-border);padding-top:12px;">Relay Switch Control(Triggered by This Sensor's Alarm)<button type="button" class="remove-btn" onclick="removeRelaySection(${id})" style="float:right;">Remove Relay</button></h4><div class="form-grid"><label class="field"><span>Target Client UID</span><input type="text" class="relay-target" placeholder="dev:IMEI(optional)"></label><label class="field"><span>Trigger On</span><select class="relay-trigger"><option value="any">Any Alarm(High or Low)</option><option value="high">High Alarm Only</option><option value="low">Low Alarm Only</option></select></label><label class="field"><span>Relay Mode</span><select class="relay-mode" onchange="toggleRelayDurations(${id})"><option value="momentary">Momentary(configurable duration)</option><option value="until_clear">Stay On Until Alarm Clears</option><option value="manual_reset">Stay On Until Manual Server Reset</option></select></label><div class="field"><span>Relay Outputs</span><div style="display:flex;gap:12px;padding:8px 0;"><label style="display:flex;align-items:center;gap:4px;"><input type="checkbox" class="relay-1" value="1"> R1</label><label style="display:flex;align-items:center;gap:4px;"><input type="checkbox" class="relay-2" value="2"> R2</label><label style="display:flex;align-items:center;gap:4px;"><input type="checkbox" class="relay-3" value="4"> R3</label><label style="display:flex;align-items:center;gap:4px;"><input type="checkbox" class="relay-4" value="8"> R4</label></div></div><div class="relay-durations-section" style="grid-column:1 / -1;display:block;"><span style="font-size:0.85rem;color:var(--text-secondary);margin-bottom:8px;display:block;">Momentary Duration per Relay(seconds,0 = default 30 min):</span><div style="display:flex;gap:12px;flex-wrap:wrap;"><label style="display:flex;align-items:center;gap:4px;font-size:0.85rem;">R1:<input type="number" class="relay-duration-1" value="0" min="0" max="86400" style="width:70px;"></label><label style="display:flex;align-items:center;gap:4px;font-size:0.85rem;">R2:<input type="number" class="relay-duration-2" value="0" min="0" max="86400" style="width:70px;"></label><label style="display:flex;align-items:center;gap:4px;font-size:0.85rem;">R3:<input type="number" class="relay-duration-3" value="0" min="0" max="86400" style="width:70px;"></label><label style="display:flex;align-items:center;gap:4px;font-size:0.85rem;">R4:<input type="number" class="relay-duration-4" value="0" min="0" max="86400" style="width:70px;"></label></div></div></div></div><button type="button" class="add-section-btn add-sms-btn hidden" onclick="toggleSmsSection(${id})">+ Add SMS Alert</button><div class="collapsible-section sms-section"><h4 style="margin:16px 0 8px;font-size:0.95rem;border-top:1px solid var(--card-border);padding-top:12px;">SMS Alert Notifications <button type="button" class="remove-btn" onclick="removeSmsSection(${id})" style="float:right;">Remove SMS Alert</button></h4><div class="form-grid"><label class="field" style="grid-column:1 / -1;"><span>Phone Numbers<span class="tooltip-icon" tabindex="0" data-tooltip="Enter phone numbers with country code(e.g.,+15551234567). Separate multiple numbers with commas.">?</span></span><input type="text" class="sms-phones" placeholder="+15551234567,+15559876543"></label><label class="field"><span>Trigger On</span><select class="sms-trigger"><option value="any">Any Alarm(High or Low)</option><option value="high">High Alarm Only</option><option value="low">Low Alarm Only</option></select></label><label class="field" style="grid-column:span 2;"><span>Custom Message(optional)</span><input type="text" class="sms-message" placeholder="Tank alarm triggered"></label></div></div></div> `;}function addSensor(){const container = document.getElementById('sensorsContainer');const div = document.createElement('div');div.innerHTML = createSensorHtml(sensorCount);container.appendChild(div.firstElementChild);updateSensorTypeFields(sensorCount);sensorCount++;}window.removeSensor = function(id){const el = document.getElementById(`sensor-${id}`);if(el)el.remove();};window.toggleAlarmSection = function(id){const card = document.getElementById(`sensor-${id}`);const alarmSection = card.querySelector('.alarm-section');const addAlarmBtn = card.querySelector('.add-alarm-btn');const addRelayBtn = card.querySelector('.add-relay-btn');const addSmsBtn = card.querySelector('.add-sms-btn');alarmSection.classList.add('visible');addAlarmBtn.classList.add('hidden');addRelayBtn.classList.remove('hidden');addSmsBtn.classList.remove('hidden');};window.removeAlarmSection = function(id){const card = document.getElementById(`sensor-${id}`);const alarmSection = card.querySelector('.alarm-section');const addAlarmBtn = card.querySelector('.add-alarm-btn');const addRelayBtn = card.querySelector('.add-relay-btn');const addSmsBtn = card.querySelector('.add-sms-btn');const relaySection = card.querySelector('.relay-section');const smsSection = card.querySelector('.sms-section');alarmSection.classList.remove('visible');addAlarmBtn.classList.remove('hidden');addRelayBtn.classList.add('hidden');addSmsBtn.classList.add('hidden');relaySection.classList.remove('visible');smsSection.classList.remove('visible');card.querySelector('.high-alarm').value = '100';card.querySelector('.low-alarm').value = '20';card.querySelector('.high-alarm-enabled').checked = true;card.querySelector('.low-alarm-enabled').checked = true;card.querySelector('.relay-target').value = '';card.querySelector('.relay-trigger').value = 'any';card.querySelector('.relay-mode').value = 'momentary';['relay-1','relay-2','relay-3','relay-4'].forEach(cls =>{card.querySelector('.' + cls).checked = false;});card.querySelector('.sms-phones').value = '';card.querySelector('.sms-trigger').value = 'any';card.querySelector('.sms-message').value = '';};window.toggleRelaySection = function(id){const card = document.getElementById(`sensor-${id}`);const relaySection = card.querySelector('.relay-section');const addBtn = card.querySelector('.add-relay-btn');relaySection.classList.add('visible');addBtn.classList.add('hidden');};window.removeRelaySection = function(id){const card = document.getElementById(`sensor-${id}`);const relaySection = card.querySelector('.relay-section');const addBtn = card.querySelector('.add-relay-btn');relaySection.classList.remove('visible');addBtn.classList.remove('hidden');card.querySelector('.relay-target').value = '';card.querySelector('.relay-trigger').value = 'any';card.querySelector('.relay-mode').value = 'momentary';['relay-1','relay-2','relay-3','relay-4'].forEach(cls =>{card.querySelector('.' + cls).checked = false;});['relay-duration-1','relay-duration-2','relay-duration-3','relay-duration-4'].forEach(cls =>{card.querySelector('.' + cls).value = '0';});card.querySelector('.relay-durations-section').style.display = 'block';};window.toggleSmsSection = function(id){const card = document.getElementById(`sensor-${id}`);const smsSection = card.querySelector('.sms-section');const addBtn = card.querySelector('.add-sms-btn');smsSection.classList.add('visible');addBtn.classList.add('hidden');};window.removeSmsSection = function(id){const card = document.getElementById(`sensor-${id}`);const smsSection = card.querySelector('.sms-section');const addBtn = card.querySelector('.add-sms-btn');smsSection.classList.remove('visible');addBtn.classList.remove('hidden');card.querySelector('.sms-phones').value = '';card.querySelector('.sms-trigger').value = 'any';card.querySelector('.sms-message').value = '';};window.toggleRelayDurations = function(id){const card = document.getElementById(`sensor-${id}`);const relayMode = card.querySelector('.relay-mode').value;const durationsSection = card.querySelector('.relay-durations-section');if(relayMode === 'momentary'){durationsSection.style.display = 'block';}else{durationsSection.style.display = 'none';}};window.updateMonitorFields = function(id){const card = document.getElementById(`sensor-${id}`);const type = card.querySelector('.monitor-type').value;const numField = card.querySelector('.tank-num-field');const numFieldLabel = numField.querySelector('span');const nameLabel = card.querySelector('.name-label');const heightLabel = card.querySelector('.height-label');const sensorTypeSelect = card.querySelector('.sensor-type');const pulsesPerRevField = card.querySelector('.pulses-per-rev-field');if(type === 'gas'){numField.style.display = 'none';nameLabel.textContent = 'System Name';heightLabel.textContent = 'Max Pressure';pulsesPerRevField.style.display = 'none';}else if(type === 'rpm'){numField.style.display = 'flex';numFieldLabel.textContent = 'Engine Number';nameLabel.textContent = 'Engine Name';heightLabel.textContent = 'Max RPM';pulsesPerRevField.style.display = 'flex';sensorTypeSelect.value = '3';updatePinOptions(id);}else{numField.style.display = 'flex';numFieldLabel.textContent = 'Tank Number';nameLabel.textContent = 'Tank Name';heightLabel.textContent = 'Height(in)';pulsesPerRevField.style.display = 'none';}};window.updatePinOptions = function(id){const card = document.getElementById(`sensor-${id}`);const typeSelect = card.querySelector('.sensor-type');const pinSelect = card.querySelector('.sensor-pin');const type = parseInt(typeSelect.value);pinSelect.innerHTML = '';let options = [];if(type === 2){options = expansionChannels;}else{options = optaPins;}options.forEach(opt =>{const option = document.createElement('option');option.value = opt.value;option.textContent = opt.label;pinSelect.appendChild(option);});updateSensorTypeFields(id);};window.updateSensorTypeFields = function(id){const card = document.getElementById(`sensor-${id}`);const type = parseInt(card.querySelector('.sensor-type').value);const heightField = card.querySelector('.height-field');const heightLabel = card.querySelector('.height-label');const heightTooltip = card.querySelector('.height-tooltip');const digitalInfoBox = card.querySelector('.digital-sensor-info');const currentLoopInfoBox = card.querySelector('.current-loop-sensor-info');const alarmThresholdsGrid = card.querySelector('.alarm-thresholds-grid');const digitalAlarmGrid = card.querySelector('.digital-alarm-grid');const alarmSectionTitle = card.querySelector('.alarm-section-title');const pulsesPerRevField = card.querySelector('.pulses-per-rev-field');const switchModeField = card.querySelector('.switch-mode-field');const currentLoopTypeField = card.querySelector('.current-loop-type-field');const sensorMountHeightField = card.querySelector('.sensor-mount-height-field');const sensorRangeField = card.querySelector('.sensor-range-field');if(type === 0){heightField.style.display = 'none';digitalInfoBox.style.display = 'block';currentLoopInfoBox.style.display = 'none';switchModeField.style.display = 'flex';currentLoopTypeField.style.display = 'none';sensorMountHeightField.style.display = 'none';sensorRangeField.style.display = 'none';alarmThresholdsGrid.style.display = 'none';digitalAlarmGrid.style.display = 'grid';alarmSectionTitle.textContent = 'Float Switch Alarm';pulsesPerRevField.style.display = 'none';}else if(type === 2){heightField.style.display = 'flex';heightLabel.textContent = 'Tank Height(in)';heightTooltip.setAttribute('data-tooltip','Maximum height of liquid in the tank in inches. Meaning varies based on sensor subtype.');digitalInfoBox.style.display = 'none';currentLoopInfoBox.style.display = 'block';switchModeField.style.display = 'none';currentLoopTypeField.style.display = 'flex';sensorRangeField.style.display = 'flex';sensorMountHeightField.style.display = 'flex';alarmThresholdsGrid.style.display = 'grid';digitalAlarmGrid.style.display = 'none';alarmSectionTitle.textContent = 'Alarm Thresholds';pulsesPerRevField.style.display = 'none';updateCurrentLoopFields(id);}else if(type === 3){heightField.style.display = 'flex';heightLabel.textContent = 'Max RPM';heightTooltip.setAttribute('data-tooltip','Maximum expected RPM value. Used for alarm threshold reference.');digitalInfoBox.style.display = 'none';currentLoopInfoBox.style.display = 'none';switchModeField.style.display = 'none';currentLoopTypeField.style.display = 'none';sensorMountHeightField.style.display = 'none';sensorRangeField.style.display = 'none';alarmThresholdsGrid.style.display = 'grid';digitalAlarmGrid.style.display = 'none';alarmSectionTitle.textContent = 'Alarm Thresholds';pulsesPerRevField.style.display = 'flex';}else{heightField.style.display = 'flex';heightLabel.textContent = 'Height(in)';heightTooltip.setAttribute('data-tooltip','Maximum height or capacity of the tank in inches. Used to set alarm thresholds relative to tank size.');digitalInfoBox.style.display = 'none';currentLoopInfoBox.style.display = 'none';switchModeField.style.display = 'none';currentLoopTypeField.style.display = 'none';sensorMountHeightField.style.display = 'none';sensorRangeField.style.display = 'none';alarmThresholdsGrid.style.display = 'grid';digitalAlarmGrid.style.display = 'none';alarmSectionTitle.textContent = 'Alarm Thresholds';pulsesPerRevField.style.display = 'none';}};window.updateCurrentLoopFields = function(id){const card = document.getElementById(`sensor-${id}`);const currentLoopType = card.querySelector('.current-loop-type').value;const currentLoopInfoBox = card.querySelector('.current-loop-sensor-info');const pressureInfo = currentLoopInfoBox.querySelector('.pressure-sensor-info');const ultrasonicInfo = currentLoopInfoBox.querySelector('.ultrasonic-sensor-info');const mountHeightLabel = card.querySelector('.mount-height-label');const mountHeightTooltip = card.querySelector('.mount-height-tooltip');const heightLabel = card.querySelector('.height-label');const heightTooltip = card.querySelector('.height-tooltip');const sensorRangeLabel = card.querySelector('.sensor-range-label');const sensorRangeTooltip = card.querySelector('.sensor-range-tooltip');const sensorRangeUnit = card.querySelector('.sensor-range-unit');const sensorRangeMax = card.querySelector('.sensor-range-max');if(currentLoopType === 'ultrasonic'){pressureInfo.style.display = 'none';ultrasonicInfo.style.display = 'block';mountHeightLabel.textContent = 'Sensor Mount Height(in)';mountHeightTooltip.setAttribute('data-tooltip','Distance from the ultrasonic sensor to the tank bottom when empty. This is used to calculate the actual liquid level.');heightLabel.textContent = 'Tank Height(in)';heightTooltip.setAttribute('data-tooltip','Maximum liquid height in the tank. When the tank is full,the liquid level equals this value.');sensorRangeLabel.textContent = 'Sensor Range';sensorRangeTooltip.setAttribute('data-tooltip','Native distance range of the ultrasonic sensor(e.g.,0-10m). This is the measurement range that corresponds to the 4-20mA output.');sensorRangeUnit.value = 'm';sensorRangeMax.value = '10';}else{pressureInfo.style.display = 'block';ultrasonicInfo.style.display = 'none';mountHeightLabel.textContent = 'Sensor Mount Height(in)';mountHeightTooltip.setAttribute('data-tooltip','Height of the pressure sensor above the tank bottom(usually 0-2 inches). This offset is added to the measured level.');heightLabel.textContent = 'Max Measured Height(in)';heightTooltip.setAttribute('data-tooltip','Maximum liquid height the sensor can measure(corresponds to 20mA / full sensor scale). Does not include the sensor mount height offset.');sensorRangeLabel.textContent = 'Sensor Range';sensorRangeTooltip.setAttribute('data-tooltip','Native pressure range of the sensor(e.g.,0-5 PSI,0-2 bar). This is the measurement range that corresponds to the 4-20mA output.');sensorRangeUnit.value = 'PSI';sensorRangeMax.value = '5';}};document.getElementById('addSensorBtn').addEventListener('click',addSensor);const inputActions = [{value:'clear_relays',label:'Clear All Relay Alarms',tooltip:'When pressed,turns off all active relay outputs and resets alarm states.'},{value:'none',label:'Disabled(No Action)',tooltip:'Input is configured but does not trigger any action.'}];const inputModes = [{value:'active_low',label:'Active LOW(Button to GND,internal pullup)'},{value:'active_high',label:'Active HIGH(Button to VCC,external pull-down)'}];let inputIdCounter = 0;function addInput(){const id = inputIdCounter++;const container = document.getElementById('inputsContainer');const card = document.createElement('div');card.className = 'sensor-card';card.id = `input-${id}`;card.innerHTML = ` <div class="card-header" style="display:flex;justify-content:space-between;align-items:center;margin-bottom:12px;"><h4 style="margin:0;font-size:1rem;">Input ${id + 1}</h4><button type="button" class="remove-btn" onclick="removeInput(${id})">Remove</button></div><div class="form-grid"><label class="field"><span>Input Name</span><input type="text" class="input-name" placeholder="Clear Button" value="Clear Button"></label><label class="field"><span>Pin Number<span class="tooltip-icon" tabindex="0" data-tooltip="Arduino Opta pin number for the input. A0=0,A1=1,etc. Use a digital-capable pin.">?</span></span><input type="number" class="input-pin" value="0" min="0" max="99"></label><label class="field"><span>Input Mode<span class="tooltip-icon" tabindex="0" data-tooltip="Active LOW:Button connects pin to GND,uses internal pull-up resistor. Active HIGH:Button connects pin to VCC,requires external pull-down resistor.">?</span></span><select class="input-mode"> ${inputModes.map(m => `<option value="${m.value}">${m.label}</option>`).join('')}</select></label><label class="field"><span>Action<span class="tooltip-icon" tabindex="0" data-tooltip="What happens when this input is activated(button pressed for 500ms).">?</span></span><select class="input-action"> ${inputActions.map(a => `<option value="${a.value}">${a.label}</option>`).join('')}</select></label></div> `;container.appendChild(card);}window.removeInput = function(id){const card = document.getElementById(`input-${id}`);if(card){card.remove();}};document.getElementById('addInputBtn').addEventListener('click',addInput);function sensorKeyFromValue(value){switch(value){case 0:return 'digital';case 2:return 'current';case 3:return 'rpm';default:return 'analog';}}document.getElementById('downloadBtn').addEventListener('click',()=>{const levelChange = parseFloat(document.getElementById('levelChangeThreshold').value);const sampleMinutes = Math.max(1,Math.min(1440,parseInt(document.getElementById('sampleMinutes').value,10)|| 30));const reportTimeValue = document.getElementById('reportTime').value || '05:00';const timeParts = reportTimeValue.split(':');const reportHour = timeParts.length === 2 ?(isNaN(parseInt(timeParts[0],10))? 5:parseInt(timeParts[0],10)):5;const reportMinute = timeParts.length === 2 ?(isNaN(parseInt(timeParts[1],10))? 0:parseInt(timeParts[1],10)):0;const config ={site:document.getElementById('siteName').value.trim(),deviceLabel:document.getElementById('deviceLabel').value.trim()|| 'Client-112025',serverFleet:document.getElementById('serverFleet').value.trim()|| 'tankalarm-server',sampleSeconds:sampleMinutes * 60,levelChangeThreshold:Math.max(0,isNaN(levelChange)? 0:levelChange),reportHour:reportHour,reportMinute:reportMinute,dailyEmail:document.getElementById('dailyEmail').value.trim(),solarPowered:document.getElementById('solarPowered').checked,tanks:[]};const sensorCards = document.querySelectorAll('.sensor-card');if(!sensorCards.length){alert('Add at least one sensor before downloading a configuration.');return;}sensorCards.forEach((card,index)=>{const monitorType = card.querySelector('.monitor-type').value;const type = parseInt(card.querySelector('.sensor-type').value);const pin = parseInt(card.querySelector('.sensor-pin').value);let tankNum = parseInt(card.querySelector('.tank-num').value)||(index + 1);let name = card.querySelector('.tank-name').value;if(monitorType === 'gas'){if(!name)name = `Gas System ${index + 1}`;}else if(monitorType === 'rpm'){if(!name)name = `Engine ${tankNum}`;}else{if(!name)name = `Tank ${index + 1}`;}const sensor = sensorKeyFromValue(type);const pulsesPerRev = Math.max(1,Math.min(255,parseInt(card.querySelector('.pulses-per-rev').value)|| 1));const switchMode = card.querySelector('.switch-mode').value;const alarmSectionVisible = card.querySelector('.alarm-section').classList.contains('visible');const highAlarmEnabled = card.querySelector('.high-alarm-enabled').checked;const lowAlarmEnabled = card.querySelector('.low-alarm-enabled').checked;const highAlarmValue = card.querySelector('.high-alarm').value;const lowAlarmValue = card.querySelector('.low-alarm').value;const tank ={id:String.fromCharCode(65 + index),name:name,number:tankNum,sensor:sensor,primaryPin:sensor === 'current' ? 0:pin,secondaryPin:-1,loopChannel:sensor === 'current' ? pin:-1,rpmPin:sensor === 'rpm' ? pin:-1,maxValue:sensor === 'digital' ? 1:(parseFloat(card.querySelector('.tank-height').value)|| 120),hysteresis:sensor === 'digital' ? 0:2.0,daily:true,upload:true};if(sensor === 'digital'){tank.digitalSwitchMode = switchMode;}if(sensor === 'current'){const currentLoopType = card.querySelector('.current-loop-type').value;const sensorMountHeight = parseFloat(card.querySelector('.sensor-mount-height').value)|| 0;const sensorRangeMin = parseFloat(card.querySelector('.sensor-range-min').value)|| 0;const sensorRangeMax = parseFloat(card.querySelector('.sensor-range-max').value)|| 5;const sensorRangeUnit = card.querySelector('.sensor-range-unit').value || 'PSI';tank.currentLoopType = currentLoopType;tank.sensorMountHeight = sensorMountHeight;tank.sensorRangeMin = sensorRangeMin;tank.sensorRangeMax = sensorRangeMax;tank.sensorRangeUnit = sensorRangeUnit;}if(alarmSectionVisible){if(sensor === 'digital'){const digitalTriggerState = card.querySelector('.digital-trigger-state').value;tank.digitalTrigger = digitalTriggerState;if(digitalTriggerState === 'activated'){tank.highAlarm = 1;}else{tank.lowAlarm = 0;}tank.alarmSms = true;}else if(highAlarmEnabled || lowAlarmEnabled){if(highAlarmEnabled && highAlarmValue !== ''){const highAlarmFloat = parseFloat(highAlarmValue);if(!isNaN(highAlarmFloat)){tank.highAlarm = highAlarmFloat;}}if(lowAlarmEnabled && lowAlarmValue !== ''){const lowAlarmFloat = parseFloat(lowAlarmValue);if(!isNaN(lowAlarmFloat)){tank.lowAlarm = lowAlarmFloat;}}tank.alarmSms = true;}else{tank.alarmSms = false;}}else{tank.alarmSms = false;}if(sensor === 'rpm'){tank.pulsesPerRev = pulsesPerRev;}const relaySectionVisible = card.querySelector('.relay-section').classList.contains('visible');if(relaySectionVisible){let relayMask = 0;['relay-1','relay-2','relay-3','relay-4'].forEach(cls =>{const checkbox = card.querySelector('.' + cls);if(checkbox.checked)relayMask |= parseInt(checkbox.value);});const relayTarget = card.querySelector('.relay-target').value.trim();const relayTrigger = card.querySelector('.relay-trigger').value;const relayMode = card.querySelector('.relay-mode').value;if(relayTarget){tank.relayTargetClient = relayTarget;tank.relayMask = relayMask;tank.relayTrigger = relayTrigger;tank.relayMode = relayMode;if(relayMode === 'momentary'){const durations = [ parseInt(card.querySelector('.relay-duration-1').value)|| 0,parseInt(card.querySelector('.relay-duration-2').value)|| 0,parseInt(card.querySelector('.relay-duration-3').value)|| 0,parseInt(card.querySelector('.relay-duration-4').value)|| 0 ];tank.relayMomentaryDurations = durations;}if(relayMask === 0){alert("You have set a relay target but have not selected any relay outputs for " + name + ". The configuration will be incomplete.");}if((relayTrigger === 'high' &&(!highAlarmEnabled || highAlarmValue === ''))||(relayTrigger === 'low' &&(!lowAlarmEnabled || lowAlarmValue === ''))){alert(`Warning:Relay for ${name}is set to trigger on "${relayTrigger}" alarm,but that alarm type is not fully configured(either not enabled or value is missing).`);}}}const smsSectionVisible = card.querySelector('.sms-section').classList.contains('visible');if(smsSectionVisible){const smsPhones = card.querySelector('.sms-phones').value.trim();const smsTrigger = card.querySelector('.sms-trigger').value;const smsMessage = card.querySelector('.sms-message').value.trim();if(smsPhones){const phoneArray = smsPhones.split(',').map(p => p.trim()).filter(p => p.length > 0);if(phoneArray.length > 0){tank.smsAlert ={phones:phoneArray,trigger:smsTrigger,message:smsMessage || 'Tank alarm triggered'};if((smsTrigger === 'high' &&(!highAlarmEnabled || highAlarmValue === ''))||(smsTrigger === 'low' &&(!lowAlarmEnabled || lowAlarmValue === ''))){alert(`Warning:SMS Alert for ${name}is set to trigger on "${smsTrigger}" alarm,but that alarm type is not fully configured(either not enabled or value is missing).`);}}}}config.tanks.push(tank);});const inputCards = document.querySelectorAll('#inputsContainer .sensor-card');let clearButtonConfigured = false;inputCards.forEach((card)=>{const inputName = card.querySelector('.input-name').value.trim()|| 'Input';const inputPin = parseInt(card.querySelector('.input-pin').value,10);const inputMode = card.querySelector('.input-mode').value;const inputAction = card.querySelector('.input-action').value;if(inputAction === 'clear_relays' && !clearButtonConfigured){config.clearButtonPin = isNaN(inputPin)? -1:inputPin;config.clearButtonActiveHigh =(inputMode === 'active_high');clearButtonConfigured = true;}});if(!clearButtonConfigured){config.clearButtonPin = -1;config.clearButtonActiveHigh = false;}const blob = new Blob([JSON.stringify(config,null,2)],{type:'application/json'});const url = URL.createObjectURL(blob);const a = document.createElement('a');a.href = url;a.download = 'client_config.json';document.body.appendChild(a);a.click();document.body.removeChild(a);URL.revokeObjectURL(url);});addSensor();</script></body></html>)HTML";

static const char SERIAL_MONITOR_HTML[] PROGMEM = R"HTML(<!DOCTYPE html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1"><title>Serial Monitor - Tank Alarm Server</title><style>:root{font-family:"Segoe UI",Arial,sans-serif;color-scheme:light dark}*{box-sizing:border-box}body{margin:0;min-height:100vh;background:var(--bg);color:var(--text);transition:background 0.2s ease,color 0.2s ease}body[data-theme="light"]{--bg:#f8fafc;--surface:#ffffff;--text:#1f2933;--muted:#475569;--header-bg:#e2e8f0;--card-border:rgba(15,23,42,0.08);--card-shadow:rgba(15,23,42,0.08);--accent:#2563eb;--accent-strong:#1d4ed8;--accent-contrast:#f8fafc;--chip:#eceff7;--pill-bg:rgba(37,99,235,0.12);--log-bg:#f1f5f9;--log-border:#cbd5e1}body[data-theme="dark"]{--bg:#0f172a;--surface:#1e293b;--text:#e2e8f0;--muted:#94a3b8;--header-bg:#16213d;--card-border:rgba(15,23,42,0.55);--card-shadow:rgba(0,0,0,0.55);--accent:#38bdf8;--accent-strong:#22d3ee;--accent-contrast:#0f172a;--chip:rgba(148,163,184,0.15);--pill-bg:rgba(56,189,248,0.18);--log-bg:#0f172a;--log-border:#334155}header{background:var(--header-bg);padding:28px 24px;box-shadow:0 20px 45px var(--card-shadow)}header .bar{display:flex;justify-content:space-between;gap:16px;flex-wrap:wrap;align-items:flex-start}header h1{margin:0;font-size:1.9rem}header p{margin:8px 0 0;color:var(--muted);max-width:640px;line-height:1.4}.header-actions{display:flex;gap:12px;flex-wrap:wrap;align-items:center}.pill{padding:10px 20px;text-decoration:none;font-weight:600;background:var(--pill-bg);color:var(--accent);border:1px solid transparent;transition:transform 0.15s ease}.pill:hover{transform:translateY(-1px)}.icon-button{width:42px;height:42px;border:1px solid var(--card-border);background:var(--surface);color:var(--text);font-size:1.2rem;cursor:pointer;transition:transform 0.15s ease}.icon-button:hover{transform:translateY(-1px)}main{padding:24px;max-width:1400px;margin:0 auto;width:100%}.card{background:var(--surface);border:1px solid var(--card-border);padding:20px;box-shadow:0 25px 55px var(--card-shadow);margin-bottom:20px}h2{margin-top:0;font-size:1.3rem}.controls{display:flex;gap:12px;margin-bottom:16px;flex-wrap:wrap;align-items:center}.controls-grid{display:flex;flex-wrap:wrap;gap:16px;align-items:flex-end;margin-bottom:12px}.control-group{display:flex;flex-direction:column;gap:6px;min-width:200px;color:var(--muted);font-size:0.85rem}.control-group select,.control-group button{width:100%}.control-group.compact{flex-direction:row;align-items:stretch;gap:10px}select,button{padding:10px 16px;font-size:0.95rem;border:1px solid var(--card-border);background:var(--surface);color:var(--text);cursor:pointer}button{background:var(--accent);color:var(--accent-contrast);border-color:var(--accent);font-weight:600}button:disabled{opacity:0.5;cursor:not-allowed}button.secondary{background:transparent;color:var(--text);border-color:var(--card-border)}button.ghost{background:transparent;border:1px solid transparent;color:var(--muted);padding:4px 8px;font-size:1.1rem;min-width:32px}.log-container{background:var(--log-bg);border:1px solid var(--log-border);padding:16px;font-family:"Courier New",monospace;font-size:0.85rem;line-height:1.6;max-height:600px;overflow-y:auto;white-space:pre-wrap;word-break:break-word}.log-container.slim{max-height:360px}.log-entry{margin-bottom:8px;padding:4px 0;border-bottom:1px solid var(--card-border)}.log-entry:last-child{border-bottom:none}.log-time{color:var(--muted);font-size:0.8rem;margin-right:8px}.log-meta{display:inline-block;font-size:0.75rem;color:var(--muted);margin-right:8px}.log-message{color:var(--text)}.empty-state{text-align:center;color:var(--muted);padding:40px}.panel-grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(320px,1fr));gap:16px}.client-panel{border:1px solid var(--card-border);padding:16px;background:var(--surface);box-shadow:0 15px 40px var(--card-shadow);display:flex;flex-direction:column;gap:12px}.panel-head{display:flex;justify-content:space-between;gap:12px;align-items:flex-start}.panel-title{font-size:1rem;font-weight:600}.panel-subtitle{color:var(--muted);font-size:0.9rem}.panel-meta{font-size:0.75rem;color:var(--muted)}.panel-actions{display:flex;gap:6px;flex-wrap:wrap;justify-content:flex-end}.chip-row{display:flex;flex-wrap:wrap;gap:8px}.chip{background:var(--chip);color:var(--muted);padding:4px 12px;font-size:0.75rem;font-weight:600}#toast{position:fixed;left:50%;bottom:24px;transform:translateX(-50%);background:#0284c7;color:#fff;padding:12px 18px;box-shadow:0 10px 30px rgba(15,23,42,0.25);opacity:0;pointer-events:none;transition:opacity 0.3s ease;font-weight:600}#toast.show{opacity:1}</style></head><body data-theme="light"><header><div class="bar"><div><h1>Serial Monitor</h1><p> View serial debug output from the server and remote client devices. Server logs update automatically, client logs can be pinned side-by-side for easier comparisons. </p></div><div class="header-actions"><button class="icon-button" id="themeToggle" aria-label="Switch to dark mode">&#9789;</button><a class="pill" href="/">Dashboard</a><a class="pill secondary" href="/contacts">Contacts</a></div></div></header><main><div class="card"><h2>Server Serial Output</h2><div class="controls"><button id="refreshServerBtn">Refresh Server Logs</button><button class="secondary" id="clearServerBtn">Clear Display</button><span style="color: var(--muted); font-size: 0.9rem;">Auto-refreshes every 5 seconds</span></div><div class="log-container" id="serverLogs"><div class="empty-state">Loading server logs...</div></div></div><div class="card"><h2>Client Serial Output</h2><div class="controls-grid"><label class="control-group"><span>Site Filter</span><select id="siteSelect"><option value="all">All sites</option></select></label><label class="control-group"><span>Client</span><select id="clientSelect"><option value="">-- Select a client --</option></select></label><div class="control-group compact" style="flex: 1 1 340px;"><button id="pinClientBtn" disabled>Add Client Panel</button><button id="pinSiteBtn" class="secondary" disabled>Add Site Clients</button><button id="refreshAllBtn" class="secondary">Refresh All</button><button id="clearClientBtn" class="secondary">Clear Panels</button></div></div><div class="panel-grid" id="clientPanels"><div class="empty-state">Pin one or more clients to view their serial output alongside the server.</div></div></div></main><div id="toast"></div><script>(()=>{function escapeHtml(str){if(!str)return'';const div=document.createElement('div');div.textContent=str;return div.innerHTML;}const THEME_KEY = 'tankalarmTheme';const state ={siteFilter:'all',clients:[],clientsByUid:new Map(),clientsBySite:new Map()};const clientPanels = new Map();const themeToggle = document.getElementById('themeToggle');const els ={serverLogs:document.getElementById('serverLogs'),refreshServerBtn:document.getElementById('refreshServerBtn'),clearServerBtn:document.getElementById('clearServerBtn'),siteSelect:document.getElementById('siteSelect'),clientSelect:document.getElementById('clientSelect'),pinClientBtn:document.getElementById('pinClientBtn'),pinSiteBtn:document.getElementById('pinSiteBtn'),refreshAllBtn:document.getElementById('refreshAllBtn'),clearClientBtn:document.getElementById('clearClientBtn'),clientPanels:document.getElementById('clientPanels'),toast:document.getElementById('toast')};function applyTheme(next){const theme = next === 'dark' ? 'dark':'light';document.body.dataset.theme = theme;themeToggle.textContent = theme === 'dark' ? '☀':'☾';themeToggle.setAttribute('aria-label',theme === 'dark' ? 'Switch to light mode':'Switch to dark mode');localStorage.setItem(THEME_KEY,theme);}applyTheme(localStorage.getItem(THEME_KEY)|| 'light');themeToggle.addEventListener('click',()=>{applyTheme(document.body.dataset.theme === 'dark' ? 'light':'dark');});let serverRefreshTimer = null;function showToast(message,isError){els.toast.textContent = message;els.toast.style.background = isError ? '#dc2626':'#0284c7';els.toast.classList.add('show');setTimeout(()=> els.toast.classList.remove('show'),2500);}function formatTime(epoch){if(!epoch)return '--';const date = new Date(epoch * 1000);return date.toLocaleTimeString(undefined,{hour:'2-digit',minute:'2-digit',second:'2-digit'});}function formatRelative(epoch){if(!epoch)return '';const diff = Math.max(0,(Date.now()/ 1000)- epoch);if(diff < 60)return `${Math.floor(diff)}s ago`;if(diff < 3600)return `${Math.floor(diff / 60)}m ago`;if(diff < 86400)return `${Math.floor(diff / 3600)}h ago`;return `${Math.floor(diff / 86400)}d ago`;}function renderLogs(container,logs){if(!logs || logs.length === 0){container.innerHTML = '<div class="empty-state">No logs available</div>';return;}container.innerHTML = '';logs.forEach(entry =>{const div = document.createElement('div');div.className = 'log-entry';const time = document.createElement('span');time.className = 'log-time';time.textContent = formatTime(entry.timestamp);const meta = document.createElement('span');meta.className = 'log-meta';const level =(entry.level || 'info').toUpperCase();const source = entry.source ? ` · ${entry.source}`:'';meta.textContent = `[${level}${source}]`;const msg = document.createElement('span');msg.className = 'log-message';msg.textContent = entry.message;div.appendChild(time);div.appendChild(meta);div.appendChild(msg);container.appendChild(div);});container.scrollTop = container.scrollHeight;}async function refreshServerLogs(){try{const res = await fetch('/api/serial-logs?source=server');if(!res.ok)throw new Error('Failed to fetch server logs');const data = await res.json();renderLogs(els.serverLogs,data.logs || []);}catch(err){console.error('Server logs error:',err);}}function ensureClientPanelPlaceholder(){if(clientPanels.size === 0){els.clientPanels.innerHTML = '<div class="empty-state">Pin one or more clients to view their serial output alongside the server.</div>';}}function clearClientPanelPlaceholder(){if(clientPanels.size === 0){els.clientPanels.innerHTML = '';}}function setClients(list){state.clients = list;state.clientsByUid = new Map(list.map(c => [c.uid,c]));state.clientsBySite = new Map();list.forEach(c =>{const site = c.site || 'Unassigned';if(!state.clientsBySite.has(site)){state.clientsBySite.set(site,[]);}state.clientsBySite.get(site).push(c);});}function renderSiteOptions(){const current = state.siteFilter;const sites = Array.from(state.clientsBySite.keys()).sort((a,b)=> a.localeCompare(b));els.siteSelect.innerHTML = '';const allOption = document.createElement('option');allOption.value = 'all';allOption.textContent = 'All sites';els.siteSelect.appendChild(allOption);sites.forEach(site =>{const opt = document.createElement('option');opt.value = site;opt.textContent = site;els.siteSelect.appendChild(opt);});if(current !== 'all' && sites.includes(current)){els.siteSelect.value = current;}else{state.siteFilter = 'all';els.siteSelect.value = 'all';}updateSiteButtons();}function renderClientOptions(){const previous = els.clientSelect.value;const candidates = state.siteFilter === 'all' ? state.clients:(state.clientsBySite.get(state.siteFilter)|| []);els.clientSelect.innerHTML = '';const placeholder = document.createElement('option');placeholder.value = '';placeholder.textContent = '-- Select a client --';els.clientSelect.appendChild(placeholder);candidates.forEach(c =>{const opt = document.createElement('option');opt.value = c.uid;const label = c.label ? `${c.label}(${c.uid})`:c.uid;opt.textContent = `${c.site || 'Unassigned'}· ${label}`;els.clientSelect.appendChild(opt);});if(previous && candidates.some(c => c.uid === previous)){els.clientSelect.value = previous;els.pinClientBtn.disabled = false;}else{els.clientSelect.value = '';els.pinClientBtn.disabled = true;}}function getClientInfo(uid){return state.clientsByUid.get(uid);}function updatePanelHeader(uid){const info = getClientInfo(uid);const panelState = clientPanels.get(uid);if(!info || !panelState){return;}panelState.title.textContent = info.site || 'Unknown Site';panelState.subtitle.textContent = info.label || uid;panelState.meta.textContent = info.uid || uid;}function updateSiteButtons(){if(state.siteFilter === 'all'){els.pinSiteBtn.disabled = true;return;}const count = state.clientsBySite.get(state.siteFilter)?.length || 0;els.pinSiteBtn.disabled = count === 0;}function pinClient(uid){if(!uid){showToast('Select a client first',true);return;}if(clientPanels.has(uid)){showToast('Client already pinned');refreshClientLogs(uid);return;}createClientPanel(uid);}async function pinSiteClients(){if(state.siteFilter === 'all'){showToast('Select a specific site first',true);return;}const clients = state.clientsBySite.get(state.siteFilter)|| [];if(!clients.length){showToast('No clients found for that site',true);return;}els.pinSiteBtn.disabled = true;for(const c of clients){if(!clientPanels.has(c.uid)){createClientPanel(c.uid);await new Promise(r => setTimeout(r,500));}}els.pinSiteBtn.disabled = false;}async function refreshAllClients(){const uids = Array.from(clientPanels.keys());if(uids.length === 0)return;els.refreshAllBtn.disabled = true;for(const uid of uids){await refreshClientLogs(uid);await new Promise(r => setTimeout(r,200));}els.refreshAllBtn.disabled = false;}function clearClientPanels(){clientPanels.forEach(panel => panel.root.remove());clientPanels.clear();ensureClientPanelPlaceholder();}function createClientPanel(uid){const info = getClientInfo(uid);if(!info){showToast('Client metadata not available yet',true);return;}if(clientPanels.size === 0){clearClientPanelPlaceholder();}const panel = document.createElement('div');panel.className = 'client-panel';panel.dataset.client = uid;panel.innerHTML = ` <div class="panel-head"><div><div class="panel-title">${escapeHtml(info.site || 'Unknown Site')}</div><div class="panel-subtitle">${escapeHtml(info.label || uid)}</div><div class="panel-meta">${escapeHtml(uid)}</div></div><div class="panel-actions"><button class="secondary" data-action="refresh">Refresh</button><button data-action="request">Request Logs</button><button class="ghost" data-action="close" title="Remove panel">&times;</button></div></div><div class="chip-row"><span class="chip" data-role="lastLog">Last log:--</span><span class="chip" data-role="lastAck">Ack:--</span><span class="chip" data-role="status">Status:idle</span></div><div class="log-container slim"><div class="empty-state">No logs yet</div></div> `;els.clientPanels.appendChild(panel);const panelState ={root:panel,logs:panel.querySelector('.log-container'),title:panel.querySelector('.panel-title'),subtitle:panel.querySelector('.panel-subtitle'),meta:panel.querySelector('.panel-meta'),requestBtn:panel.querySelector('[data-action="request"]'),refreshBtn:panel.querySelector('[data-action="refresh"]'),chips:{lastLog:panel.querySelector('[data-role="lastLog"]'),lastAck:panel.querySelector('[data-role="lastAck"]'),status:panel.querySelector('[data-role="status"]')}};panelState.refreshBtn.addEventListener('click',()=> refreshClientLogs(uid));panelState.requestBtn.addEventListener('click',()=> requestClientLogs(uid,panelState));panel.querySelector('[data-action="close"]').addEventListener('click',()=> removeClientPanel(uid));clientPanels.set(uid,panelState);refreshClientLogs(uid);}function removeClientPanel(uid){const panelState = clientPanels.get(uid);if(!panelState){return;}panelState.root.remove();clientPanels.delete(uid);ensureClientPanelPlaceholder();}function updatePanelMeta(panelState,meta ={}){const lastLog = meta.lastLogEpoch || meta.lastLog;const lastAck = meta.lastAckEpoch;const ackStatus = meta.lastAckStatus || 'n/a';panelState.chips.lastLog.textContent = lastLog ? `Last log:${formatTime(lastLog)}(${formatRelative(lastLog)})`:'Last log:--';panelState.chips.lastAck.textContent = lastAck ? `Ack:${formatTime(lastAck)}(${ackStatus})`:'Ack:--';panelState.chips.status.textContent = meta.awaitingLogs ? 'Status:awaiting client response':'Status:ready';}async function refreshClientLogs(clientUid){const panelState = clientPanels.get(clientUid);if(!panelState){return;}panelState.refreshBtn.disabled = true;panelState.chips.status.textContent = 'Status:refreshing…';try{const res = await fetch(`/api/serial-logs?source=client&client=${encodeURIComponent(clientUid)}`);if(!res.ok)throw new Error('Failed to fetch client logs');const data = await res.json();renderLogs(panelState.logs,data.logs || []);updatePanelMeta(panelState,data.meta ||{});}catch(err){console.error('Client logs error:',err);panelState.logs.innerHTML = '<div class="empty-state">Error loading client logs</div>';panelState.chips.status.textContent = 'Status:error fetching logs';}finally{panelState.refreshBtn.disabled = false;}}async function requestClientLogs(clientUid,panelState){if(!clientUid){showToast('Select a client first',true);return;}if(panelState){panelState.requestBtn.disabled = true;panelState.chips.status.textContent = 'Status:requesting logs…';}try{const res = await fetch('/api/serial-request',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({client:clientUid})});if(!res.ok){const text = await res.text();throw new Error(text || 'Request failed');}showToast(`Log request sent to ${clientUid}`);if(panelState){panelState.chips.status.textContent = 'Status:awaiting client response';setTimeout(()=> refreshClientLogs(clientUid),3500);}}catch(err){console.error('Client request error:',err);showToast(err.message || 'Request failed',true);if(panelState){panelState.chips.status.textContent = 'Status:request failed';}}finally{if(panelState){setTimeout(()=>{panelState.requestBtn.disabled = false;},4000);}}}async function loadClients(){try{const res = await fetch('/api/clients');if(!res.ok)throw new Error('Failed to fetch clients');const data = await res.json();const rawClients = data.clients || [];const unique = new Map();rawClients.forEach(c =>{if(c.client){unique.set(c.client,{uid:c.client,site:c.site || 'Unassigned',label:c.label || c.client});}});setClients(Array.from(unique.values()));renderSiteOptions();renderClientOptions();clientPanels.forEach((_,uid)=> updatePanelHeader(uid));}catch(err){console.error('Failed to load clients:',err);}}els.refreshServerBtn.addEventListener('click',refreshServerLogs);els.clearServerBtn.addEventListener('click',()=>{els.serverLogs.innerHTML = '<div class="empty-state">Logs cleared</div>';});els.siteSelect.addEventListener('change',()=>{state.siteFilter = els.siteSelect.value;renderClientOptions();updateSiteButtons();});els.clientSelect.addEventListener('change',()=>{els.pinClientBtn.disabled = !els.clientSelect.value;});els.pinClientBtn.addEventListener('click',()=> pinClient(els.clientSelect.value));els.pinSiteBtn.addEventListener('click',pinSiteClients);els.refreshAllBtn.addEventListener('click',refreshAllClients);els.clearClientBtn.addEventListener('click',clearClientPanels);themeToggle.addEventListener('keyup',e =>{if(e.key === 'Enter' || e.key === ' '){themeToggle.click();}});serverRefreshTimer = setInterval(refreshServerLogs,5000);refreshServerLogs();ensureClientPanelPlaceholder();loadClients();})();</script></body></html>)HTML";

static const char CALIBRATION_HTML[] PROGMEM = R"HTML(<!DOCTYPE html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1"><title>Calibration Learning - Tank Alarm Server</title><style>:root{font-family:"Segoe UI",Arial,sans-serif;color-scheme:light dark}*{box-sizing:border-box}body{margin:0;min-height:100vh;background:var(--bg);color:var(--text);transition:background 0.2s ease,color 0.2s ease}body[data-theme="light"]{--bg:#f8fafc;--surface:#ffffff;--text:#1f2933;--muted:#475569;--header-bg:#e2e8f0;--card-border:rgba(15,23,42,0.08);--card-shadow:rgba(15,23,42,0.08);--accent:#2563eb;--accent-strong:#1d4ed8;--accent-contrast:#f8fafc;--chip:#f8fafc;--input-border:#cbd5e1;--danger:#ef4444;--success:#10b981;--pill-bg:rgba(37,99,235,0.12);--table-border:rgba(15,23,42,0.08)}body[data-theme="dark"]{--bg:#0f172a;--surface:#1e293b;--text:#e2e8f0;--muted:#94a3b8;--header-bg:#16213d;--card-border:rgba(15,23,42,0.55);--card-shadow:rgba(0,0,0,0.55);--accent:#38bdf8;--accent-strong:#22d3ee;--accent-contrast:#0f172a;--chip:rgba(148,163,184,0.15);--input-border:rgba(148,163,184,0.4);--danger:#f87171;--success:#34d399;--pill-bg:rgba(56,189,248,0.18);--table-border:rgba(255,255,255,0.12)}header{background:var(--header-bg);padding:28px 24px;box-shadow:0 20px 45px var(--card-shadow)}header .bar{display:flex;justify-content:space-between;gap:16px;flex-wrap:wrap;align-items:flex-start}header h1{margin:0;font-size:1.9rem}header p{margin:8px 0 0;color:var(--muted);max-width:640px;line-height:1.4}.header-actions{display:flex;gap:12px;flex-wrap:wrap;align-items:center}.pill{padding:10px 20px;text-decoration:none;font-weight:600;background:var(--pill-bg);color:var(--accent);border:1px solid transparent;transition:transform 0.15s ease}.pill:hover{transform:translateY(-1px)}.icon-button{width:42px;height:42px;border:1px solid var(--card-border);background:var(--surface);color:var(--text);font-size:1.2rem;cursor:pointer;transition:transform 0.15s ease}.icon-button:hover{transform:translateY(-1px)}main{padding:24px;max-width:1200px;margin:0 auto;width:100%}.card{background:var(--surface);border:1px solid var(--card-border);padding:20px;box-shadow:0 25px 55px var(--card-shadow);margin-bottom:24px}h2{margin-top:0;font-size:1.3rem}h3{margin:20px 0 10px;font-size:1.1rem;color:var(--text)}.field{display:flex;flex-direction:column;margin-bottom:12px}.field span{font-size:0.9rem;color:var(--muted);margin-bottom:4px}.field input,.field select,.field textarea{padding:10px 12px;border:1px solid var(--input-border);font-size:0.95rem;background:var(--bg);color:var(--text)}.form-grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(180px,1fr));gap:12px}.level-input-group{display:flex;gap:8px;align-items:center}.level-input-group input{width:70px}button{border:none;padding:10px 16px;font-size:0.95rem;font-weight:600;cursor:pointer;background:var(--accent);color:var(--accent-contrast);transition:transform 0.15s ease}button.secondary{background:transparent;border:1px solid var(--card-border);color:var(--text)}button:hover{transform:translateY(-1px)}button:disabled{opacity:0.5;cursor:not-allowed;transform:none}table{width:100%;border-collapse:collapse;margin-top:12px}th,td{text-align:left;padding:10px 8px;border-bottom:1px solid var(--table-border);font-size:0.9rem}th{text-transform:uppercase;letter-spacing:0.05em;font-size:0.75rem;color:var(--muted)}tr:last-child td{border-bottom:none}.calibration-status{display:inline-flex;align-items:center;gap:6px;padding:4px 10px;font-size:0.8rem;font-weight:600}.calibration-status.calibrated{background:rgba(16,185,129,0.15);color:var(--success)}.calibration-status.uncalibrated{background:rgba(239,68,68,0.15);color:var(--danger)}.calibration-status.learning{background:rgba(37,99,235,0.15);color:var(--accent)}.drift-indicator{display:inline-block;padding:2px 8px;font-size:0.8rem;font-weight:600}.drift-indicator.low{background:rgba(16,185,129,0.15);color:var(--success)}.drift-indicator.medium{background:rgba(245,158,11,0.15);color:#f59e0b}.drift-indicator.high{background:rgba(239,68,68,0.15);color:var(--danger)}.info-box{background:var(--chip);border:1px solid var(--card-border);padding:12px;margin-top:12px;font-size:0.9rem;color:var(--muted)}.stats-row{display:flex;gap:24px;flex-wrap:wrap;margin-bottom:16px}.stat-item{display:flex;flex-direction:column}.stat-item span{font-size:0.8rem;color:var(--muted);text-transform:uppercase;letter-spacing:0.05em}.stat-item strong{font-size:1.4rem;margin-top:4px}#toast{position:fixed;left:50%;bottom:24px;transform:translateX(-50%);background:var(--accent);color:var(--accent-contrast);padding:12px 18px;box-shadow:0 10px 30px rgba(15,23,42,0.25);opacity:0;pointer-events:none;transition:opacity 0.3s ease;font-weight:600}#toast.show{opacity:1}</style></head><body data-theme="light"><header><div class="bar"><div><h1>Calibration Learning System</h1><p> Enter manual tank level readings to improve sensor accuracy over time. The system learns the relationship between sensor readings and actual tank levels using linear regression. </p></div><div class="header-actions"><button class="icon-button" id="themeToggle" aria-label="Switch to dark mode">&#9789;</button><a class="pill" href="/">Dashboard</a><a class="pill secondary" href="/contacts">Contacts</a></div></div></header><main><div class="card"><h2>Add Calibration Reading</h2><p style="color: var(--muted); margin-bottom: 16px;"> Enter a manually verified tank level reading. For best results, take readings at different fill levels (e.g., 25%, 50%, 75%, full). </p><form id="calibrationForm"><div class="form-grid"><label class="field"><span>Select Tank</span><select id="tankSelect" required><option value="">-- Select a tank --</option></select></label><label class="field"><span>Verified Level</span><div class="level-input-group"><input type="number" id="levelFeet" min="0" max="100" placeholder="Feet" required><span>'</span><input type="number" id="levelInches" min="0" max="11.9" step="0.1" placeholder="Inches" required><span>"</span></div></label><label class="field"><span>Reading Timestamp</span><input type="datetime-local" id="readingTimestamp"></label></div><label class="field" style="margin-top: 12px;"><span>Notes (optional)</span><textarea id="notes" rows="2" placeholder="e.g., Measured with stick gauge at front of tank"></textarea></label><div style="margin-top: 16px;"><button type="submit">Submit Calibration Reading</button><button type="button" class="secondary" onclick="document.getElementById('calibrationForm').reset();">Clear Form</button></div></form><div class="info-box"><strong>How it works:</strong> Each calibration reading pairs a verified tank level (measured with a stick gauge, sight glass, or other method) with the raw 4-20mA sensor reading from telemetry. With at least 2 data points at different levels, the system calculates a linear regression to determine the actual relationship between sensor output and tank level. This learned calibration replaces the theoretical maxValue-based calculation. </div></div><div class="card"><h2>Calibration Status</h2><div id="calibrationStats" class="stats-row"><div class="stat-item"><span>Total Tanks</span><strong id="statTotalTanks">0</strong></div><div class="stat-item"><span>Calibrated</span><strong id="statCalibrated">0</strong></div><div class="stat-item"><span>Learning (1 point)</span><strong id="statLearning">0</strong></div><div class="stat-item"><span>Uncalibrated</span><strong id="statUncalibrated">0</strong></div></div><table><thead><tr><th>Tank</th><th>Site</th><th>Status</th><th>Data Points</th><th>R² Fit</th><th>Learned Slope</th><th>Drift from Original</th><th>Last Calibration</th></tr></thead><tbody id="calibrationTableBody"></tbody></table></div><div class="card"><h2>Calibration Log</h2><div class="form-grid" style="margin-bottom: 12px;"><label class="field"><span>Filter by Tank</span><select id="logTankFilter"><option value="">All Tanks</option></select></label></div><table><thead><tr><th>Timestamp</th><th>Tank</th><th>Sensor (mA)</th><th>Verified Level</th><th>Notes</th></tr></thead><tbody id="logTableBody"></tbody></table></div><div class="card"><h2>Drift Analysis</h2><p style="color: var(--muted); margin-bottom: 16px;"> Shows how sensor accuracy has changed over time. High drift may indicate sensor degradation, tank modifications, or environmental factors. </p><div id="driftAnalysis"><p style="color: var(--muted);">Select a tank with calibration data to view drift analysis.</p></div></div></main><div id="toast"></div><script>(()=>{function escapeHtml(str){if(!str)return'';const div=document.createElement('div');div.textContent=str;return div.innerHTML;}const THEME_KEY = 'tankalarmTheme';const themeToggle = document.getElementById('themeToggle');function applyTheme(next){const theme = next === 'dark' ? 'dark':'light';document.body.dataset.theme = theme;themeToggle.textContent = theme === 'dark' ? '☀':'☾';themeToggle.setAttribute('aria-label',theme === 'dark' ? 'Switch to light mode':'Switch to dark mode');localStorage.setItem(THEME_KEY,theme);}applyTheme(localStorage.getItem(THEME_KEY)|| 'light');themeToggle.addEventListener('click',()=>{const next = document.body.dataset.theme === 'dark' ? 'light':'dark';applyTheme(next);});function showToast(message,isError){const toast = document.getElementById('toast');toast.textContent = message;toast.style.background = isError ? '#dc2626':'#0284c7';toast.classList.add('show');setTimeout(()=> toast.classList.remove('show'),2500);}function formatEpoch(epoch){if(!epoch)return '--';const date = new Date(epoch * 1000);if(isNaN(date.getTime()))return '--';return date.toLocaleString(undefined,{year:'numeric',month:'short',day:'numeric',hour:'numeric',minute:'2-digit'});}function formatLevel(inches){if(typeof inches !== 'number' || !isFinite(inches)|| inches < 0){return '--';}const feet = Math.floor(inches / 12);const remainingInches = inches % 12;if(feet === 0){return `${remainingInches.toFixed(1)}"`;}return `${feet}' ${remainingInches.toFixed(1)}"`;}let tanks = [];let calibrations = [];let calibrationLogs = [];async function loadTanks(){try{const response = await fetch('/api/tanks');if(!response.ok)throw new Error('Failed to load tanks');const data = await response.json();tanks = data.tanks || [];populateTankDropdowns();}catch(err){console.error('Error loading tanks:',err);showToast('Failed to load tank list',true);}}function populateTankDropdowns(){const tankSelect = document.getElementById('tankSelect');const logTankFilter = document.getElementById('logTankFilter');tankSelect.innerHTML = '<option value="">-- Select a tank --</option>';logTankFilter.innerHTML = '<option value="">All Tanks</option>';const uniqueTanks = new Map();tanks.forEach(t =>{const key = `${t.client}:${t.tank}`;if(!uniqueTanks.has(key)){uniqueTanks.set(key,{client:t.client,tank:t.tank,site:t.site,label:t.label || `Tank ${t.tank}`,heightInches:t.heightInches || 0,levelInches:t.levelInches || 0,sensorMa:t.sensorMa || 0,lastUpdate:t.lastUpdate || 0});}});uniqueTanks.forEach((tank,key)=>{const option = document.createElement('option');option.value = key;option.textContent = `${tank.site}- ${tank.label}(#${tank.tank})`;tankSelect.appendChild(option.cloneNode(true));logTankFilter.appendChild(option);});}async function loadCalibrationData(){try{const response = await fetch('/api/calibration');if(!response.ok)throw new Error('Failed to load calibration data');const data = await response.json();calibrations = data.calibrations || [];calibrationLogs = data.logs || [];updateCalibrationStats();updateCalibrationTable();updateCalibrationLog();}catch(err){console.error('Error loading calibration data:',err);}}function updateCalibrationStats(){const total = tanks.length > 0 ? new Set(tanks.map(t => `${t.client}:${t.tank}`)).size:0;const calibrated = calibrations.filter(c => c.hasLearnedCalibration).length;const learning = calibrations.filter(c => !c.hasLearnedCalibration && c.entryCount > 0).length;const uncalibrated = total - calibrated - learning;document.getElementById('statTotalTanks').textContent = total;document.getElementById('statCalibrated').textContent = calibrated;document.getElementById('statLearning').textContent = learning;document.getElementById('statUncalibrated').textContent = Math.max(0,uncalibrated);}function updateCalibrationTable(){const tbody = document.getElementById('calibrationTableBody');tbody.innerHTML = '';if(calibrations.length === 0){tbody.innerHTML = '<tr><td colspan="8" style="text-align:center;color:var(--muted);">No calibration data yet. Add readings to start learning.</td></tr>';return;}calibrations.forEach(cal =>{const tr = document.createElement('tr');const tankInfo = tanks.find(t => t.client === cal.clientUid && t.tank === cal.tankNumber);const tankName = tankInfo ? `${tankInfo.label || 'Tank ' + cal.tankNumber}`:`Tank ${cal.tankNumber}`;const site = tankInfo ? tankInfo.site:'--';let statusClass = 'uncalibrated';let statusText = 'Uncalibrated';if(cal.hasLearnedCalibration){statusClass = 'calibrated';statusText = 'Calibrated';}else if(cal.entryCount > 0){statusClass = 'learning';statusText = 'Learning';}let driftText = '--';let driftClass = 'low';if(cal.hasLearnedCalibration && cal.originalMaxValue > 0){const originalSlope = cal.originalMaxValue / 16.0;const drift = Math.abs((cal.learnedSlope - originalSlope)/ originalSlope * 100);driftText = drift.toFixed(1)+ '%';if(drift > 10)driftClass = 'high';else if(drift > 5)driftClass = 'medium';}tr.innerHTML = ` <td>${escapeHtml(tankName)}</td><td>${escapeHtml(site)}</td><td><span class="calibration-status ${statusClass}">${statusText}</span></td><td>${cal.entryCount}</td><td>${cal.hasLearnedCalibration ?(cal.rSquared * 100).toFixed(1)+ '%':'--'}</td><td>${cal.hasLearnedCalibration ? cal.learnedSlope.toFixed(3)+ ' in/mA':'--'}</td><td><span class="drift-indicator ${driftClass}">${driftText}</span></td><td>${formatEpoch(cal.lastCalibrationEpoch)}</td> `;tbody.appendChild(tr);});}function updateCalibrationLog(){const tbody = document.getElementById('logTableBody');const filter = document.getElementById('logTankFilter').value;tbody.innerHTML = '';let filtered = calibrationLogs;if(filter){const [clientUid,tankNum] = filter.split(':');filtered = calibrationLogs.filter(log => log.clientUid === clientUid && log.tankNumber === parseInt(tankNum));}if(filtered.length === 0){tbody.innerHTML = '<tr><td colspan="5" style="text-align:center;color:var(--muted);">No calibration entries found.</td></tr>';return;}filtered.sort((a,b)=> b.timestamp - a.timestamp);filtered.forEach(log =>{const tr = document.createElement('tr');const tankInfo = tanks.find(t => t.client === log.clientUid && t.tank === log.tankNumber);const tankName = tankInfo ? `${tankInfo.site}- ${tankInfo.label || 'Tank ' + log.tankNumber}`:`Tank ${log.tankNumber}`;const isValidReading = log.sensorReading >= 4 && log.sensorReading <= 20;const sensorDisplay = isValidReading ? log.sensorReading.toFixed(2)+ ' mA':(log.sensorReading ? `${log.sensorReading.toFixed(2)}mA ⚠️`:'-- ⚠️');tr.innerHTML = ` <td>${formatEpoch(log.timestamp)}</td><td>${escapeHtml(tankName)}</td><td title="${isValidReading ? '':'Not used for calibration(outside 4-20mA range)'}">${sensorDisplay}</td><td>${formatLevel(log.verifiedLevelInches)}</td><td>${escapeHtml(log.notes || '--')}</td> `;if(!isValidReading){tr.style.opacity = '0.6';}tbody.appendChild(tr);});}document.getElementById('calibrationForm').addEventListener('submit',async(e)=>{e.preventDefault();const tankKey = document.getElementById('tankSelect').value;if(!tankKey){showToast('Please select a tank',true);return;}const [clientUid,tankNumber] = tankKey.split(':');const levelFeet = parseInt(document.getElementById('levelFeet').value)|| 0;const levelInches = parseFloat(document.getElementById('levelInches').value)|| 0;const totalInches = levelFeet * 12 + levelInches;const timestampInput = document.getElementById('readingTimestamp').value;const notes = document.getElementById('notes').value.trim();if(totalInches < 0){showToast('Invalid level value',true);return;}const tank = tanks.find(t => `${t.client}:${t.tank}` === tankKey);const payload ={clientUid:clientUid,tankNumber:parseInt(tankNumber),verifiedLevelInches:totalInches,notes:notes};if(tank && tank.sensorMa && tank.sensorMa >= 4 && tank.sensorMa <= 20){payload.sensorReading = tank.sensorMa;}if(timestampInput){payload.timestamp = Math.floor(new Date(timestampInput).getTime()/ 1000);}try{const response = await fetch('/api/calibration',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(payload)});if(!response.ok){const text = await response.text();throw new Error(text || 'Failed to submit calibration');}showToast('Calibration reading submitted successfully');document.getElementById('calibrationForm').reset();loadCalibrationData();}catch(err){console.error('Error submitting calibration:',err);showToast(err.message || 'Failed to submit calibration',true);}});document.getElementById('logTankFilter').addEventListener('change',updateCalibrationLog);const now = new Date();now.setMinutes(now.getMinutes()- now.getTimezoneOffset());document.getElementById('readingTimestamp').value = now.toISOString().slice(0,16);loadTanks();loadCalibrationData();setInterval(loadCalibrationData,30000);})();</script></body></html>)HTML";

static const char CONTACTS_MANAGER_HTML[] PROGMEM = R"HTML(<!DOCTYPE html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1"><title>Contacts Manager - Tank Alarm Server</title><style>:root{font-family:"Segoe UI",Arial,sans-serif;color-scheme:light dark}*{box-sizing:border-box}body{margin:0;min-height:100vh;background:var(--bg);color:var(--text);transition:background 0.2s ease,color 0.2s ease}body[data-theme="light"]{--bg:#f8fafc;--surface:#ffffff;--text:#0f172a;--muted:#475569;--header-bg:#e2e8f0;--card-border:rgba(15,23,42,0.08);--card-shadow:rgba(15,23,42,0.08);--accent:#2563eb;--accent-strong:#1d4ed8;--accent-contrast:#f8fafc;--button-hover:#3b82f6;--danger:#dc2626;--danger-hover:#b91c1c;--input-bg:#ffffff;--input-border:#cbd5e1}body[data-theme="dark"]{--bg:#0f172a;--surface:#1e293b;--text:#f8fafc;--muted:#94a3b8;--header-bg:#16213d;--card-border:rgba(15,23,42,0.55);--card-shadow:rgba(0,0,0,0.55);--accent:#38bdf8;--accent-strong:#22d3ee;--accent-contrast:#0f172a;--button-hover:#0ea5e9;--danger:#f87171;--danger-hover:#ef4444;--input-bg:#334155;--input-border:#475569}header{background:var(--header-bg);padding:24px 32px;border-bottom:1px solid var(--card-border)}.header-content{max-width:1400px;margin:0 auto;display:flex;justify-content:space-between;align-items:flex-start;gap:24px}h1{margin:8px 0 12px;font-size:2rem;font-weight:600}.timestamp{font-size:0.9rem;color:var(--muted)}.header-actions{display:flex;gap:12px;flex-wrap:wrap;align-items:center}.pill{display:inline-flex;align-items:center;padding:10px 20px;background:var(--accent);color:var(--accent-contrast);text-decoration:none;font-weight:500;font-size:0.9rem;transition:background 0.15s ease}.pill:hover{background:var(--button-hover)}.pill.secondary{background:var(--surface);color:var(--accent);border:1px solid var(--accent)}.pill.secondary:hover{background:var(--accent);color:var(--accent-contrast)}.icon-button{background:transparent;border:none;font-size:1.5rem;cursor:pointer;padding:8px;color:var(--text);transition:background 0.15s ease}.icon-button:hover{background:rgba(0,0,0,0.05)}body[data-theme="dark"] .icon-button:hover{background:rgba(255,255,255,0.1)}main{max-width:1400px;margin:32px auto;padding:0 32px}.card{background:var(--surface);border:1px solid var(--card-border);padding:24px;box-shadow:0 4px 12px var(--card-shadow);margin-bottom:24px}.card h2{margin:0 0 20px;font-size:1.5rem;font-weight:600}.card h3{margin:20px 0 16px;font-size:1.1rem;font-weight:600;border-top:1px solid var(--card-border);padding-top:20px}.card h3:first-of-type{border-top:none;padding-top:0;margin-top:0}.filter-section{display:flex;gap:12px;margin-bottom:20px;flex-wrap:wrap}.filter-group{display:flex;flex-direction:column;gap:4px}.filter-group label{font-size:0.85rem;color:var(--muted);font-weight:500}select,input[type="text"],input[type="email"],input[type="tel"]{padding:10px 14px;border:1px solid var(--input-border);background:var(--input-bg);color:var(--text);font-size:0.95rem;font-family:inherit}select{min-width:200px}.contact-list{display:flex;flex-direction:column;gap:16px}.contact-card{background:var(--bg);border:1px solid var(--card-border);padding:16px}.contact-header{display:flex;justify-content:space-between;align-items:flex-start;gap:12px;margin-bottom:12px}.contact-info{flex:1}.contact-name{font-size:1.1rem;font-weight:600;margin-bottom:6px}.contact-details{font-size:0.9rem;color:var(--muted)}.contact-details div{margin:4px 0}.contact-actions{display:flex;gap:8px}.btn{padding:8px 16px;border:none;font-size:0.9rem;font-weight:500;cursor:pointer;transition:all 0.15s ease;font-family:inherit}.btn-primary{background:var(--accent);color:var(--accent-contrast)}.btn-primary:hover{background:var(--button-hover)}.btn-danger{background:var(--danger);color:white}.btn-danger:hover{background:var(--danger-hover)}.btn-secondary{background:transparent;color:var(--accent);border:1px solid var(--accent)}.btn-secondary:hover{background:var(--accent);color:var(--accent-contrast)}.btn-small{padding:6px 12px;font-size:0.85rem}.associations{margin-top:12px}.association-section{margin-bottom:12px}.association-section h4{font-size:0.9rem;font-weight:600;margin:0 0 8px;color:var(--muted)}.association-list{display:flex;flex-wrap:wrap;gap:8px}.association-tag{display:inline-flex;align-items:center;gap:6px;padding:6px 12px;background:var(--accent);color:var(--accent-contrast);font-size:0.85rem;font-weight:500}.association-tag .remove-tag{background:transparent;border:none;color:inherit;cursor:pointer;font-size:1rem;padding:0;width:16px;height:16px;display:flex;align-items:center;justify-content:center;transition:background 0.15s ease}.association-tag .remove-tag:hover{background:rgba(0,0,0,0.2)}.add-association{display:flex;gap:8px;margin-top:8px;flex-wrap:wrap}.empty-state{text-align:center;padding:40px 20px;color:var(--muted);font-style:italic}.form-grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(250px,1fr));gap:16px;margin-bottom:16px}.form-field{display:flex;flex-direction:column;gap:6px}.form-field label{font-size:0.9rem;font-weight:500;color:var(--muted)}.daily-report-section{background:var(--bg);border:1px solid var(--card-border);padding:16px}.daily-report-list{display:flex;flex-direction:column;gap:8px;margin-bottom:12px}.daily-report-item{display:flex;justify-content:space-between;align-items:center;padding:10px;background:var(--surface);border:1px solid var(--card-border);}.modal{display:none;position:fixed;top:0;left:0;width:100%;height:100%;background:rgba(0,0,0,0.5);z-index:1000;align-items:center;justify-content:center}.modal.active{display:flex}.modal-content{background:var(--surface);padding:24px;max-width:600px;width:90%;max-height:90vh;overflow-y:auto}.modal-header{display:flex;justify-content:space-between;align-items:center;margin-bottom:20px}.modal-header h2{margin:0}.modal-close{background:transparent;border:none;font-size:1.5rem;cursor:pointer;color:var(--text);padding:0;width:32px;height:32px;display:flex;align-items:center;justify-content:center;}.modal-close:hover{background:rgba(0,0,0,0.1)}#toast{position:fixed;left:50%;bottom:24px;transform:translateX(-50%) translateY(100px);background:var(--surface);padding:16px 24px;box-shadow:0 4px 20px rgba(0,0,0,0.3);border:1px solid var(--card-border);opacity:0;transition:all 0.3s ease;z-index:2000;max-width:90%}#toast.show{transform:translateX(-50%) translateY(0);opacity:1}</style></head><body data-theme="light"><header><div class="header-content"><div><p class="timestamp">Tank Alarm Fleet · Contacts Manager</p><h1>Contacts Manager</h1><p>Manage email and SMS contacts for alarm notifications and daily reports.</p></div><div class="header-actions"><button class="icon-button" id="themeToggle" aria-label="Switch to dark mode">&#9789;</button><a class="pill secondary" href="/">Dashboard</a><a class="pill secondary" href="/client-console">Client Console</a><a class="pill secondary" href="/config-generator">Config Generator</a></div></div></header><main><div class="card"><h2>Contacts</h2><div style="display: flex; justify-content: space-between; align-items: center; margin-bottom: 20px;"><div class="filter-section"><div class="filter-group"><label>Filter by View</label><select id="viewFilter"><option value="all">All Contacts</option><option value="site">By Site</option><option value="alarm">By Alarm</option></select></div><div class="filter-group" id="siteFilterGroup" style="display: none;"><label>Select Site</label><select id="siteSelect"><option value="">All Sites</option></select></div><div class="filter-group" id="alarmFilterGroup" style="display: none;"><label>Select Alarm</label><select id="alarmSelect"><option value="">All Alarms</option></select></div></div><button class="btn btn-primary" onclick="openAddContactModal()">+ Add Contact</button></div><div id="contactsList" class="contact-list"><div class="empty-state">No contacts configured. Click "+ Add Contact" to get started.</div></div></div><div class="card"><h2>Daily Report Recipients</h2><p style="color: var(--muted); margin-bottom: 16px;">Contacts who receive the daily tank level summary email.</p><div class="daily-report-section"><div id="dailyReportList" class="daily-report-list"><div class="empty-state">No daily report recipients configured.</div></div><button class="btn btn-secondary" onclick="openAddDailyReportModal()">+ Add Recipient</button></div></div></main><div id="contactModal" class="modal"><div class="modal-content"><div class="modal-header"><h2 id="modalTitle">Add Contact</h2><button class="modal-close" onclick="closeContactModal()">&times;</button></div><form id="contactForm"><div class="form-grid"><div class="form-field"><label>Name *</label><input type="text" id="contactName" required></div><div class="form-field"><label>Phone Number</label><input type="tel" id="contactPhone" placeholder="+15551234567"></div><div class="form-field"><label>Email Address</label><input type="email" id="contactEmail" placeholder="contact@example.com"></div></div><h3>Alarm Associations</h3><p style="color: var(--muted); font-size: 0.9rem; margin-bottom: 12px;">Select which alarms should trigger notifications to this contact.</p><div id="alarmAssociations" class="form-grid"></div><div style="display: flex; gap: 12px; justify-content: flex-end; margin-top: 24px;"><button type="button" class="btn btn-secondary" onclick="closeContactModal()">Cancel</button><button type="submit" class="btn btn-primary">Save Contact</button></div></form></div></div><div id="dailyReportModal" class="modal"><div class="modal-content"><div class="modal-header"><h2>Add Daily Report Recipient</h2><button class="modal-close" onclick="closeDailyReportModal()">&times;</button></div><form id="dailyReportForm"><div class="form-grid"><div class="form-field"><label>Select Contact</label><select id="dailyReportContactSelect" required><option value="">Choose a contact...</option></select></div></div><div style="display: flex; gap: 12px; justify-content: flex-end; margin-top: 24px;"><button type="button" class="btn btn-secondary" onclick="closeDailyReportModal()">Cancel</button><button type="submit" class="btn btn-primary">Add Recipient</button></div></form></div></div><div id="toast"></div><script>(()=>{const THEME_KEY = 'tankalarmTheme';let contacts = [];let dailyReportRecipients = [];let sites = [];let alarms = [];let editingContactId = null;const themeToggle = document.getElementById('themeToggle');const savedTheme = localStorage.getItem(THEME_KEY)|| 'light';document.body.dataset.theme = savedTheme;updateThemeIcon();themeToggle.addEventListener('click',()=>{const current = document.body.dataset.theme;const next = current === 'light' ? 'dark':'light';document.body.dataset.theme = next;localStorage.setItem(THEME_KEY,next);updateThemeIcon();});function updateThemeIcon(){const isDark = document.body.dataset.theme === 'dark';themeToggle.textContent = isDark ? '☀':'☽';themeToggle.setAttribute('aria-label',isDark ? 'Switch to light mode':'Switch to dark mode');}function showToast(message){const toast = document.getElementById('toast');toast.textContent = message;toast.classList.add('show');setTimeout(()=>{toast.classList.remove('show');},3000);}function loadData(){fetch('/api/contacts').then(response => response.json()).then(data =>{contacts = data.contacts || [];dailyReportRecipients = data.dailyReportRecipients || [];sites = data.sites || [];alarms = data.alarms || [];renderContacts();renderDailyReportRecipients();updateFilters();}).catch(err =>{console.error('Failed to load contacts:',err);showToast('Failed to load contacts data:' +(err && err.message ? err.message:err)+ '. Please check your network connection and try again.');});}function saveData(){fetch('/api/contacts',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({contacts:contacts,dailyReportRecipients:dailyReportRecipients})}).then(response => response.json()).then(data =>{if(data.success){showToast('Changes saved successfully');loadData();}else{showToast('Failed to save changes:' +(data.error || 'Unknown error'));}}).catch(err =>{console.error('Failed to save contacts:',err);showToast('Failed to save changes:' +(err && err.message ? err.message:err));});}document.getElementById('viewFilter').addEventListener('change',(e)=>{const view = e.target.value;document.getElementById('siteFilterGroup').style.display = view === 'site' ? 'block':'none';document.getElementById('alarmFilterGroup').style.display = view === 'alarm' ? 'block':'none';renderContacts();});document.getElementById('siteSelect').addEventListener('change',()=> renderContacts());document.getElementById('alarmSelect').addEventListener('change',()=> renderContacts());function updateFilters(){const siteSelect = document.getElementById('siteSelect');const alarmSelect = document.getElementById('alarmSelect');siteSelect.innerHTML = '<option value="">All Sites</option>';sites.forEach(site =>{const option = document.createElement('option');option.value = site;option.textContent = site;siteSelect.appendChild(option);});alarmSelect.innerHTML = '<option value="">All Alarms</option>';alarms.forEach(alarm =>{const option = document.createElement('option');option.value = alarm.id;option.textContent = `${alarm.site}- ${alarm.tank}(${alarm.type})`;alarmSelect.appendChild(option);});}function renderContacts(){const container = document.getElementById('contactsList');const viewFilter = document.getElementById('viewFilter').value;const siteFilter = document.getElementById('siteSelect').value;const alarmFilter = document.getElementById('alarmSelect').value;let filteredContacts = contacts;if(viewFilter === 'site' && siteFilter){filteredContacts = contacts.filter(c => c.alarmAssociations && c.alarmAssociations.some(a =>{const alarm = alarms.find(al => al.id === a);return alarm && alarm.site === siteFilter;}));}else if(viewFilter === 'alarm' && alarmFilter){filteredContacts = contacts.filter(c => c.alarmAssociations && c.alarmAssociations.includes(alarmFilter));}if(filteredContacts.length === 0){container.innerHTML = '<div class="empty-state">No contacts match the current filter.</div>';return;}container.innerHTML = filteredContacts.map(contact =>{const associatedAlarms =(contact.alarmAssociations || []).map(alarmId => alarms.find(a => a.id === alarmId)).filter(a => a);const groupedBySite ={};associatedAlarms.forEach(alarm =>{if(!groupedBySite[alarm.site]){groupedBySite[alarm.site] = [];}groupedBySite[alarm.site].push(alarm);});return ` <div class="contact-card"><div class="contact-header"><div class="contact-info"><div class="contact-name">${escapeHtml(contact.name)}</div><div class="contact-details"> ${contact.phone ? `<div>📱 ${escapeHtml(contact.phone)}</div>`:''}${contact.email ? `<div>✉️ ${escapeHtml(contact.email)}</div>`:''}</div></div><div class="contact-actions"><button class="btn btn-small btn-secondary" data-contact-id="${escapeHtml(contact.id)}" data-action="edit">Edit</button><button class="btn btn-small btn-danger" data-contact-id="${escapeHtml(contact.id)}" data-action="delete">Delete</button></div></div> ${associatedAlarms.length > 0 ? ` <div class="associations"> ${Object.keys(groupedBySite).map(site => ` <div class="association-section"><h4>${escapeHtml(site)}</h4><div class="association-list"> ${groupedBySite[site].map(alarm => ` <div class="association-tag"> ${escapeHtml(alarm.tank)}(${escapeHtml(alarm.type)})<button class="remove-tag" data-contact-id="${escapeHtml(contact.id)}" data-alarm-id="${escapeHtml(alarm.id)}">&times;</button></div> `).join('')}</div></div> `).join('')}</div> `:''}</div> `;}).join('');container.querySelectorAll('[data-action="edit"]').forEach(btn =>{btn.addEventListener('click',()=> editContact(btn.dataset.contactId));});container.querySelectorAll('[data-action="delete"]').forEach(btn =>{btn.addEventListener('click',()=> deleteContact(btn.dataset.contactId));});container.querySelectorAll('.remove-tag').forEach(btn =>{btn.addEventListener('click',function(){removeAlarmAssociation(this.dataset.contactId,this.dataset.alarmId);});});}function renderDailyReportRecipients(){const container = document.getElementById('dailyReportList');if(dailyReportRecipients.length === 0){container.innerHTML = '<div class="empty-state">No daily report recipients configured.</div>';return;}container.innerHTML = dailyReportRecipients.map(recipientId =>{const contact = contacts.find(c => c.id === recipientId);if(!contact)return '';return ` <div class="daily-report-item"><div><strong>${escapeHtml(contact.name)}</strong> ${contact.email ? ` - ${escapeHtml(contact.email)}`:''}</div><button class="btn btn-small btn-danger" data-recipient-id="${escapeHtml(recipientId)}" data-action="remove-recipient">Remove</button></div> `;}).filter(Boolean).join('');container.querySelectorAll('[data-action="remove-recipient"]').forEach(btn =>{btn.addEventListener('click',()=> removeDailyReportRecipient(btn.dataset.recipientId));});}window.openAddContactModal = function(){editingContactId = null;document.getElementById('modalTitle').textContent = 'Add Contact';document.getElementById('contactName').value = '';document.getElementById('contactPhone').value = '';document.getElementById('contactEmail').value = '';renderAlarmAssociations([]);document.getElementById('contactModal').classList.add('active');};window.editContact = function(contactId){const contact = contacts.find(c => c.id === contactId);if(!contact)return;editingContactId = contactId;document.getElementById('modalTitle').textContent = 'Edit Contact';document.getElementById('contactName').value = contact.name;document.getElementById('contactPhone').value = contact.phone || '';document.getElementById('contactEmail').value = contact.email || '';renderAlarmAssociations(contact.alarmAssociations || []);document.getElementById('contactModal').classList.add('active');};window.closeContactModal = function(){document.getElementById('contactModal').classList.remove('active');};function renderAlarmAssociations(selectedAlarms){const container = document.getElementById('alarmAssociations');if(alarms.length === 0){container.innerHTML = '<p style="color:var(--muted);font-style:italic;">No alarms configured in the system.</p>';return;}const groupedBySite ={};alarms.forEach(alarm =>{if(!groupedBySite[alarm.site]){groupedBySite[alarm.site] = [];}groupedBySite[alarm.site].push(alarm);});container.innerHTML = Object.keys(groupedBySite).map(site => ` <div style="grid-column:1 / -1;"><strong style="display:block;margin-bottom:8px;">${escapeHtml(site)}</strong> ${groupedBySite[site].map((alarm,idx)=>{const checkboxId = 'alarm_' + escapeHtml(alarm.id)+ '_' + idx;return ` <label for="${checkboxId}" style="display:flex;align-items:center;gap:8px;margin-bottom:6px;"><input type="checkbox" id="${checkboxId}" name="alarmAssoc" value="${escapeHtml(alarm.id)}" ${selectedAlarms.includes(alarm.id)? 'checked':''}><span>${escapeHtml(alarm.tank)}(${escapeHtml(alarm.type)})</span></label> `;}).join('')}</div> `).join('');}document.getElementById('contactForm').addEventListener('submit',(e)=>{e.preventDefault();const name = document.getElementById('contactName').value.trim();const phone = document.getElementById('contactPhone').value.trim();const email = document.getElementById('contactEmail').value.trim();if(!name){showToast('Contact name is required');return;}if(!phone && !email){showToast('Either phone or email is required');return;}const alarmAssociations = Array.from(document.querySelectorAll('input[name="alarmAssoc"]:checked')).map(cb => cb.value);if(editingContactId){const contact = contacts.find(c => c.id === editingContactId);if(contact){contact.name = name;contact.phone = phone;contact.email = email;contact.alarmAssociations = alarmAssociations;}}else{const newContact ={id:'contact_' + Date.now()+ '_' + Math.random().toString(36).substr(2,9),name:name,phone:phone,email:email,alarmAssociations:alarmAssociations};contacts.push(newContact);}saveData();closeContactModal();});window.deleteContact = function(contactId){if(!confirm('Are you sure you want to delete this contact?'))return;contacts = contacts.filter(c => c.id !== contactId);dailyReportRecipients = dailyReportRecipients.filter(r => r !== contactId);saveData();};window.removeAlarmAssociation = function(contactId,alarmId){const contact = contacts.find(c => c.id === contactId);if(!contact)return;contact.alarmAssociations =(contact.alarmAssociations || []).filter(a => a !== alarmId);saveData();};window.openAddDailyReportModal = function(){const select = document.getElementById('dailyReportContactSelect');select.innerHTML = '<option value="">Choose a contact...</option>';contacts.forEach(contact =>{if(contact.email && !dailyReportRecipients.includes(contact.id)){const option = document.createElement('option');option.value = contact.id;option.textContent = `${contact.name}(${contact.email})`;select.appendChild(option);}});if(select.options.length === 1){showToast('No contacts with email addresses available');return;}document.getElementById('dailyReportModal').classList.add('active');};window.closeDailyReportModal = function(){document.getElementById('dailyReportModal').classList.remove('active');};document.getElementById('dailyReportForm').addEventListener('submit',(e)=>{e.preventDefault();const contactId = document.getElementById('dailyReportContactSelect').value;if(!contactId){showToast('Please select a contact');return;}if(!dailyReportRecipients.includes(contactId)){dailyReportRecipients.push(contactId);saveData();loadData();}closeDailyReportModal();});window.removeDailyReportRecipient = function(recipientId){dailyReportRecipients = dailyReportRecipients.filter(r => r !== recipientId);saveData();};function escapeHtml(text){const div = document.createElement('div');div.textContent = text;return div.innerHTML;}loadData();})();</script></body></html>)HTML";

static const char DASHBOARD_HTML[] PROGMEM = R"HTML(<!DOCTYPE html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1"><title>Tank Alarm Server</title><style> :root {font-family:"Segoe UI", Arial, sans-serif;--bg:#f8fafc;--surface:#ffffff;--muted:#475569;--header-bg:#e2e8f0;--card-border:#cbd5e1;--accent:#2563eb;--accent-strong:#1d4ed8;--accent-contrast:#f8fafc;--chip:#eceff7;--table-border:#e2e8f0;--pill-bg:#dbeafe;--alarm:#b91c1c;--ok:#0f766e;} * {box-sizing:border-box;} body {margin:0;min-height:100vh;background:var(--bg);color:var(--text);} header {background:var(--header-bg);padding:28px 24px;} header .bar {display:flex;justify-content:space-between;gap:16px;flex-wrap:wrap;align-items:flex-start;} header h1 {margin:0;font-size:1.9rem;} header p {margin:8px 0 0;color:var(--muted);max-width:640px;line-height:1.4;} .header-actions {display:flex;gap:12px;flex-wrap:wrap;align-items:center;} .pill {padding:10px 20px;text-decoration:none;font-weight:600;background:var(--pill-bg);color:var(--accent);border:1px solid transparent;} .pill.secondary {background:transparent;border-color:var(--card-border);color:var(--muted);} .pill:hover {transform:translateY(-1px);} .icon-button {width:42px;height:42px;border:1px solid var(--card-border);background:var(--surface);color:var(--text);font-size:1.2rem;cursor:pointer;} .icon-button:hover {transform:translateY(-1px);} .meta-grid {display:grid;grid-template-columns:repeat(auto-fit, minmax(220px, 1fr));gap:12px;margin-top:20px;} .meta-card {background:var(--surface);border:1px solid var(--card-border);padding:16px;} .meta-card span {display:block;font-size:0.8rem;letter-spacing:0.08em;text-transform:uppercase;color:var(--muted);} .meta-card strong {display:block;margin-top:6px;font-size:1.05rem;word-break:break-all;} main {padding:24px;max-width:1400px;margin:0 auto;width:100%;} .stats-grid {display:grid;grid-template-columns:repeat(auto-fit, minmax(180px, 1fr));gap:16px;margin-bottom:20px;} .stat-card {background:var(--surface);padding:18px;border:1px solid var(--card-border);} .stat-card span {font-size:0.85rem;color:var(--muted);text-transform:uppercase;letter-spacing:0.08em;} .stat-card strong {display:block;margin-top:8px;font-size:1.8rem;} .btn {border:none;padding:10px 20px;font-weight:600;cursor:pointer;background:var(--accent);color:var(--accent-contrast);} .btn.secondary {background:transparent;border:1px solid var(--card-border);color:var(--text);} .btn:disabled {opacity:0.5;cursor:not-allowed;transform:none;} .btn:not(:disabled):hover {transform:translateY(-1px);} .pause-btn {border:1px solid var(--card-border);padding:10px 16px;background:transparent;color:var(--text);font-weight:700;cursor:pointer;} .pause-btn:hover {transform:translateY(-1px);} .pause-btn.paused {background:#b91c1c;color:#fff;border-color:#b91c1c;} .pause-btn.paused:hover {background:#991b1b;border-color:#991b1b;} .card {background:var(--surface);border:1px solid var(--card-border);padding:20px;} .card-head {display:flex;align-items:baseline;justify-content:space-between;gap:12px;flex-wrap:wrap;} .badge {display:inline-flex;align-items:center;gap:6px;padding:6px 12px;background:var(--chip);color:var(--muted);font-size:0.8rem;font-weight:600;} table {width:100%;border-collapse:collapse;margin-top:18px;} th, td {text-align:left;padding:12px 10px;border-bottom:1px solid var(--table-border);font-size:0.9rem;} th {text-transform:uppercase;letter-spacing:0.05em;font-size:0.75rem;color:var(--muted);} tr:last-child td {border-bottom:none;} tr.alarm {background:rgba(220,38,38,0.08);} body[data-theme="dark"] tr.alarm {background:rgba(248,113,113,0.08);} .status-pill {display:inline-flex;align-items:center;gap:6px;padding:4px 10px;font-size:0.8rem;font-weight:600;} .status-pill.ok {background:rgba(16,185,129,0.15);color:var(--ok);} .status-pill.alarm {background:rgba(220,38,38,0.15);color:var(--alarm);} .timestamp {font-size:0.9rem;color:var(--muted);} #toast {position:fixed;left:50%;bottom:24px;transform:translateX(-50%);background:#0284c7;color:#fff;padding:12px 18px;opacity:0;pointer-events:none;font-weight:600;} #toast.show {opacity:1;} </style></head><body data-theme="light"><header><div class="bar"><div><p class="timestamp">Tank Alarm Fleet · Live server telemetry</p><h1 id="serverName">Tank Alarm Server</h1><p> Monitor every field unit in one place. Filter by site, highlight alarms, and jump into the client console when you need to push configuration updates. </p></div><div class="header-actions"><button class="pause-btn" id="pauseBtn" aria-label="Pause data flow">Pause</button><a class="pill" href="/client-console">Client Console</a><a class="pill secondary" href="/config-generator">Config Generator</a><a class="pill secondary" href="/contacts">Contacts</a><a class="pill secondary" href="/serial-monitor">Serial Monitor</a><a class="pill secondary" href="/calibration">Calibration</a></div></div><div class="meta-grid"><div class="meta-card"><span>Server UID</span><strong id="serverUid">--</strong></div><div class="meta-card"><span>Client Fleet</span><strong id="fleetName">--</strong></div><div class="meta-card"><span>Next Daily Email</span><strong id="nextEmail">--</strong></div><div class="meta-card"><span>Last Time Sync</span><strong id="lastSync">--</strong></div><div class="meta-card"><span>Last Dashboard Refresh</span><strong id="lastRefresh">--</strong></div></div></header><main><div class="stats-grid"><div class="stat-card"><span>Total Clients</span><strong id="statClients">0</strong></div><div class="stat-card"><span>Active Tanks</span><strong id="statTanks">0</strong></div><div class="stat-card"><span>Active Alarms</span><strong id="statAlarms">0</strong></div><div class="stat-card"><span>Stale Tanks (&gt;60m)</span><strong id="statStale">0</strong></div></div><section class="card"><div class="card-head"><h2 style="margin:0;">Fleet Telemetry</h2><span class="timestamp">Rows update automatically while this page remains open.</span></div><table><thead><tr><th>Client</th><th>Site</th><th>Tank</th><th>Level</th><th>VIN Voltage</th><th>Status</th><th>Updated</th><th>Relay Control</th><th>Refresh</th></tr></thead><tbody id="tankBody"></tbody></table></section></main><div id="toast"></div><script> (() => {const DEFAULT_REFRESH_SECONDS = 60;const STALE_MINUTES = 60;const els = {pauseBtn:document.getElementById('pauseBtn'), serverName:document.getElementById('serverName'), serverUid:document.getElementById('serverUid'), fleetName:document.getElementById('fleetName'), nextEmail:document.getElementById('nextEmail'), lastSync:document.getElementById('lastSync'), lastRefresh:document.getElementById('lastRefresh'), tankBody:document.getElementById('tankBody'), statClients:document.getElementById('statClients'), statTanks:document.getElementById('statTanks'), statAlarms:document.getElementById('statAlarms'), statStale:document.getElementById('statStale'), toast:document.getElementById('toast')};const state = {clients:[], tanks:[], refreshing:false, timer:null, uiRefreshSeconds:DEFAULT_REFRESH_SECONDS, paused:false, pin:null, pinConfigured:false};function applyTheme(next) {const theme = next === 'dark' ? 'dark' :'light';document.body.dataset.theme = theme;els.themeToggle.textContent = theme === 'dark' ? '☀' :'☾';els.themeToggle.setAttribute('aria-label', theme === 'dark' ? 'Switch to light mode' :'Switch to dark mode');localStorage.setItem(THEME_KEY, theme);} applyTheme(localStorage.getItem(THEME_KEY) || 'light');els.themeToggle.addEventListener('click', () => {const next = document.body.dataset.theme === 'dark' ? 'light' :'dark';applyTheme(next);});els.pauseBtn.addEventListener('click', togglePause);els.pauseBtn.addEventListener('mouseenter', () => {if (state.paused) {els.pauseBtn.textContent = 'Resume';}});els.pauseBtn.addEventListener('mouseleave', () => {renderPauseButton();});function showToast(message, isError) {els.toast.textContent = message;els.toast.style.background = isError ? '#dc2626' :'#0284c7';els.toast.classList.add('show');setTimeout(() => els.toast.classList.remove('show'), 2500);} function formatNumber(value) {return (typeof value === 'number' && isFinite(value)) ? value.toFixed(1) :'--';} function formatLevel(inches) {if (typeof inches !== 'number' || !isFinite(inches) || inches <= 0) {return '';} const feet = Math.floor(inches / 12);const remainingInches = inches % 12;if (feet === 0) {return `${remainingInches.toFixed(1)}"`;} return `${feet}' ${remainingInches.toFixed(1)}"`;} function formatEpoch(epoch) {if (!epoch) return '--';const date = new Date(epoch * 1000);if (isNaN(date.getTime())) return '--';return date.toLocaleString(undefined, {year:'numeric', month:'numeric', day:'numeric', hour:'numeric', minute:'2-digit', hour12:true});} function renderPauseButton() {const btn = els.pauseBtn;if (!btn) return;if (state.paused) {btn.classList.add('paused');btn.textContent = 'Paused';btn.title = 'Paused – hover to resume';} else {btn.classList.remove('paused');btn.textContent = 'Pause';btn.title = 'Pause data flow';}} function describeCadence(seconds) {if (!seconds) return '6 h';if (seconds < 3600) {return `${Math.round(seconds / 60)} m`;} const hours = (seconds / 3600).toFixed(1).replace(/\.0$/, '');return `${hours} h`;} function flattenTanks(clients) {const rows = [];clients.forEach(client => {const tanks = Array.isArray(client.ts) ? client.ts :[];if (!tanks.length) {rows.push({client:client.c, site:client.s, label:client.n || 'Tank', tank:client.k || '--', tankIdx:0, levelInches:client.l, alarm:client.a, alarmType:client.at, lastUpdate:client.u, vinVoltage:client.v});return;} tanks.forEach((tank, idx) => {rows.push({client:client.c, site:client.s, label:tank.n || client.n || 'Tank', tank:tank.k || '--', tankIdx:idx, levelInches:tank.l, alarm:tank.a, alarmType:tank.at || client.at, lastUpdate:tank.u, vinVoltage:idx === 0 ? client.v :null});});});return rows;} function formatVoltage(voltage) {if (typeof voltage !== 'number' || !isFinite(voltage) || voltage <= 0) {return '--';} return voltage.toFixed(2) + ' V';} function renderTankRows() {const tbody = els.tankBody;tbody.innerHTML = '';const rows = state.tanks;if (!rows.length) {const tr = document.createElement('tr');tr.innerHTML = '<td colspan="9">No telemetry available</td>';tbody.appendChild(tr);return;} rows.forEach(row => {const tr = document.createElement('tr');if (row.alarm) tr.classList.add('alarm');tr.innerHTML = ` <td><code>${row.client || '--'}</code></td><td>${row.site || '--'}</td><td>${row.label || 'Tank'} #${row.tank || '?'}</td><td>${formatLevel(row.levelInches)}</td><td>${formatVoltage(row.vinVoltage)}</td><td>${statusBadge(row)}</td><td>${formatEpoch(row.lastUpdate)}</td><td>${relayButtons(row)}</td><td>${refreshButton(row)}</td>`;tbody.appendChild(tr);});} function statusBadge(row) {if (!row.alarm) {return '<span class="status-pill ok">Normal</span>';} return '<span class="status-pill alarm">ALARM</span>';} function relayButtons(row) {if (!row.client || row.client === '--') return '--';const escapedClient = escapeHtml(row.client);const tankIdx = row.tankIdx !== undefined ? row.tankIdx :0;const disabled = state.refreshing ? 'disabled' :'';const btnStyle = 'padding:4px 8px;font-size:0.75rem;border:1px solid var(--card-border);background:var(--card-bg);cursor:pointer;margin:2px;';return `<button style="${btnStyle}" onclick="clearRelays('${escapedClient}', ${tankIdx})" title="Clear all relays for this tank" ${disabled}>🔕 Clear</button>`;} function refreshButton(row) {if (!row.client || row.client === '--') return '--';const escapedClient = escapeHtml(row.client);const disabled = state.refreshing ? 'disabled' :'';const opacity = state.refreshing ? 'opacity:0.4;' :'';return `<button class="icon-button refresh-btn" onclick="refreshTank('${escapedClient}')" title="Refresh Tank" style="width:32px;height:32px;font-size:1rem;${opacity}" ${disabled}>🔄</button>`;} function escapeHtml(unsafe) {if (!unsafe) return '';return String(unsafe) .replace(/&/g, '&amp;') .replace(/</g, '&lt;') .replace(/>/g, '&gt;') .replace(/"/g, '&quot;') .replace(/'/g, '&#039;');} async function refreshTank(clientUid) {if (state.refreshing) return;state.refreshing = true;renderTankRows();try {const res = await fetch('/api/refresh', {method:'POST', headers:{'Content-Type':'application/json'}, body:JSON.stringify({client:clientUid})});if (!res.ok) {const text = await res.text();throw new Error(text || 'Refresh failed');} const data = await res.json();applyServerData(data);showToast('Tank refreshed');} catch (err) {showToast(err.message || 'Refresh failed', true);} finally {state.refreshing = false;renderTankRows();}} window.refreshTank = refreshTank;async function clearRelays(clientUid, tankIdx) {if (state.refreshing) return;if (state.pinConfigured && !state.pin) {const pinInput = prompt('Enter admin PIN to control relays');if (!pinInput) return;state.pin = pinInput.trim();} state.refreshing = true;renderTankRows();try {const res = await fetch('/api/relay/clear', {method:'POST', headers:{'Content-Type':'application/json'}, body:JSON.stringify({clientUid:clientUid, tankIdx:tankIdx, pin:state.pin || ''})});if (!res.ok) {if (res.status === 403) {state.pin = null;throw new Error('PIN required or invalid');} const text = await res.text();throw new Error(text || 'Clear relay failed');} showToast('Relay clear command sent');setTimeout(() => refreshData(), 1000);} catch (err) {showToast(err.message || 'Clear relay failed', true);} finally {state.refreshing = false;renderTankRows();}} window.clearRelays = clearRelays;function updateStats() {const clientIds = new Set();state.tanks.forEach(t => {if (t.client) {clientIds.add(t.client);}});els.statClients.textContent = clientIds.size;els.statTanks.textContent = state.tanks.length;els.statAlarms.textContent = state.tanks.filter(t => t.alarm).length;const cutoff = Date.now() - STALE_MINUTES * 60 * 1000;const stale = state.tanks.filter(t => !t.lastUpdate || (t.lastUpdate * 1000) < cutoff).length;els.statStale.textContent = stale;} function scheduleUiRefresh() {if (state.timer) {clearInterval(state.timer);} state.timer = setInterval(() => {refreshData();}, state.uiRefreshSeconds * 1000);} async function togglePause() {if (!state.pinConfigured) {} else if (!state.pin) {const pinInput = prompt('Enter admin PIN to toggle pause');if (!pinInput) {return;} state.pin = pinInput.trim();} const targetPaused = !state.paused;try {const res = await fetch('/api/pause', {method:'POST', headers:{'Content-Type':'application/json'}, body:JSON.stringify({paused:targetPaused, pin:state.pin || ''})});if (!res.ok) {if (res.status === 403) {state.pin = null;throw new Error('PIN required or invalid');} const text = await res.text();throw new Error(text || 'Pause toggle failed');} const data = await res.json();state.paused = !!data.paused;renderPauseButton();showToast(state.paused ? 'Paused for maintenance' :'Resumed');} catch (err) {showToast(err.message || 'Pause toggle failed', true);}} function applyServerData(data) {state.clients = data.cs || [];state.tanks = flattenTanks(state.clients);const serverInfo = data.srv || {};els.serverName.textContent = serverInfo.n || 'Tank Alarm Server';els.serverUid.textContent = data.si || '--';els.fleetName.textContent = serverInfo.cf || 'tankalarm-clients';els.nextEmail.textContent = formatEpoch(data.nde);els.lastSync.textContent = formatEpoch(data.lse);els.lastRefresh.textContent = new Date().toLocaleString(undefined, {year:'numeric', month:'numeric', day:'numeric', hour:'numeric', minute:'2-digit', hour12:true});state.paused = !!serverInfo.ps;state.pinConfigured = !!serverInfo.pc;state.uiRefreshSeconds = DEFAULT_REFRESH_SECONDS;renderTankRows();renderPauseButton();updateStats();scheduleUiRefresh();} async function refreshData() {try {const res = await fetch('/api/clients');if (!res.ok) {throw new Error('Failed to fetch fleet data');} const data = await res.json();applyServerData(data);} catch (err) {showToast(err.message || 'Fleet refresh failed', true);}} refreshData();})();</script></body></html>)HTML";

static const char CLIENT_CONSOLE_HTML[] PROGMEM = R"HTML(<!DOCTYPE html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1"><title>Tank Alarm Client Console</title><style>:root{font-family:"Segoe UI",Arial,sans-serif;color-scheme:light dark}*{box-sizing:border-box}body{margin:0;min-height:100vh;background:var(--bg);color:var(--text);transition:background 0.2s ease,color 0.2s ease}body[data-theme="light"]{--bg:#f8fafc;--surface:#ffffff;--muted:#475569;--header-bg:#eef2ff;--card-border:rgba(15,23,42,0.08);--card-shadow:rgba(15,23,42,0.08);--accent:#2563eb;--accent-strong:#1d4ed8;--accent-contrast:#f8fafc;--chip:#e2e8f0;--danger:#dc2626}body[data-theme="dark"]{--bg:#0f172a;--surface:#1e293b;--muted:#94a3b8;--header-bg:#16213d;--card-border:rgba(15,23,42,0.55);--card-shadow:rgba(0,0,0,0.55);--accent:#38bdf8;--accent-strong:#22d3ee;--accent-contrast:#0f172a;--chip:rgba(148,163,184,0.2);--danger:#f87171}header{background:var(--header-bg);padding:28px 24px;box-shadow:0 18px 40px var(--card-shadow)}header .bar{display:flex;justify-content:space-between;gap:16px;flex-wrap:wrap;align-items:flex-start}h1{margin:0 0 8px}p{margin:0;color:var(--muted)}.header-actions{display:flex;gap:12px;flex-wrap:wrap;align-items:center}.pill{padding:10px 20px;text-decoration:none;font-weight:600;background:rgba(37,99,235,0.12);color:var(--accent);border:1px solid transparent}.pill.secondary{background:transparent;border-color:var(--card-border);color:var(--muted)}.icon-button{width:42px;height:42px;border:1px solid var(--card-border);background:var(--surface);color:var(--text);font-size:1.2rem;cursor:pointer}main{padding:24px;max-width:1400px;margin:0 auto;width:100%;display:flex;flex-direction:column;gap:20px}.card{background:var(--surface);border:1px solid var(--card-border);padding:20px;box-shadow:0 18px 40px var(--card-shadow)}.console-layout{display:grid;grid-template-columns:repeat(auto-fit,minmax(280px,1fr));gap:20px}label.field{display:flex;flex-direction:column;margin-bottom:12px;font-size:0.9rem;color:var(--muted)}.field span{margin-bottom:4px}input,select{border:1px solid var(--card-border);padding:10px 12px;font-size:0.95rem;background:var(--bg);color:var(--text)}select{width:100%}.form-grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(200px,1fr));gap:12px}.actions{display:flex;gap:12px;flex-wrap:wrap;margin-top:16px}button{border:none;padding:10px 16px;font-weight:600;cursor:pointer;background:var(--accent);color:var(--accent-contrast)}button.secondary{background:transparent;border:1px solid var(--card-border);color:var(--text)}button.destructive{background:var(--danger);color:#fff}button:disabled{opacity:0.5;cursor:not-allowed}table{width:100%;border-collapse:collapse;margin-top:12px}th,td{border-bottom:1px solid var(--card-border);padding:8px 8px;text-align:left;font-size:0.85rem}th{text-transform:uppercase;letter-spacing:0.05em;font-size:0.75rem;color:var(--muted)}tr.alarm{background:rgba(220,38,38,0.08)}.toggle-group{display:grid;grid-template-columns:repeat(auto-fit,minmax(220px,1fr));gap:12px;margin:16px 0}.toggle{display:flex;align-items:center;justify-content:space-between;border:1px solid var(--card-border);padding:10px 14px;background:var(--chip)}.toggle span{font-size:0.9rem;color:var(--muted)}.pin-actions,.refresh-actions{display:flex;gap:12px;flex-wrap:wrap;margin:12px 0}.details{margin:12px 0;color:var(--muted);font-size:0.95rem}.details code{background:var(--chip);padding:2px 6px;}.checkbox-cell{text-align:center}.checkbox-cell input{width:16px;height:16px}#toast{position:fixed;left:50%;bottom:24px;transform:translateX(-50%);background:#0284c7;color:#fff;padding:12px 18px;box-shadow:0 10px 30px rgba(15,23,42,0.25);opacity:0;pointer-events:none;transition:opacity 0.3s ease;font-weight:600}#toast.show{opacity:1}.modal{position:fixed;inset:0;background:#f8fafc;display:flex;align-items:center;justify-content:center;z-index:999;backdrop-filter:blur(4px)}.modal.hidden{opacity:0;pointer-events:none}.modal-card{background:var(--surface);padding:28px 26px 24px;width:min(420px,90%);border:1px solid var(--card-border);box-shadow:0 24px 50px rgba(15,23,42,0.35);position:relative}.modal-card h2{margin-top:0}.modal-card .field + .field,.modal-card .field + .actions{margin-top:12px}.pin-hint{display:block;margin-top:6px;color:var(--muted);font-size:0.9rem}.modal-badge{position:absolute;top:14px;right:16px;padding:6px 10px;font-size:0.75rem;background:var(--chip);color:var(--muted);border:1px solid var(--card-border)}.hidden{display:none !important}
.site-list{display:flex;flex-direction:column;gap:16px}
.site-card{border:1px solid var(--card-border);background:var(--bg);border-radius:4px;overflow:hidden}
.site-header{padding:12px 16px;background:var(--chip);border-bottom:1px solid var(--card-border);display:flex;justify-content:space-between;align-items:center;font-weight:600}
.site-tanks{display:grid;grid-template-columns:repeat(auto-fill,minmax(240px,1fr));gap:1px;background:var(--card-border)}
.site-tank{background:var(--surface);padding:12px;display:flex;flex-direction:column;gap:4px}
.site-tank.alarm{background:rgba(220,38,38,0.05)}
.tank-meta{font-size:0.85rem;color:var(--muted);display:flex;justify-content:space-between}
.tank-value{font-size:1.2rem;font-weight:600;margin:4px 0}
.tank-footer{font-size:0.75rem;color:var(--muted);display:flex;justify-content:space-between;align-items:center}
.tank-container{display:flex;flex-direction:column;gap:16px}
.tank-card{background:var(--bg);border:1px solid var(--card-border);padding:16px;border-radius:4px}
.tank-header{display:flex;justify-content:space-between;align-items:center;margin-bottom:12px;border-bottom:1px solid var(--card-border);padding-bottom:8px}
.tank-header-inputs{display:flex;gap:12px;align-items:center;flex:1}
.tank-grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(140px,1fr));gap:12px;margin-bottom:16px}
.alarm-section{background:var(--chip);padding:12px;border-radius:4px}
.alarm-header{font-size:0.9rem;font-weight:600;margin-bottom:8px;color:var(--muted)}
.alarm-grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(120px,1fr));gap:12px;align-items:center}
.checkbox-label{display:flex;align-items:center;gap:6px;font-size:0.9rem}
</style></head><body data-theme="light"><header><div class="bar"><div><p>Queue configuration changes for remote Tank Alarm clients.</p><h1>Client Console</h1><p> Select a client, review the cached configuration, and dispatch updates with PIN-protected controls. </p><div class="details" style="margin-top:12px;"><span>Server: <strong id="serverName">Tank Alarm Server</strong></span> &nbsp;•&nbsp; <span>Server UID: <code id="serverUid">--</code></span> &nbsp;•&nbsp; <span>Next Email: <span id="nextEmail">--</span></span></div></div><div class="header-actions"><button class="icon-button" id="themeToggle" aria-label="Switch to dark mode">&#9789;</button><a class="pill" href="/">Dashboard</a><a class="pill secondary" href="/config-generator">Config Generator</a><a class="pill secondary" href="/contacts">Contacts</a></div></div></header><main><div class="console-layout"><section class="card"><label class="field"><span>Select Client</span><select id="clientSelect"></select></label><div id="clientDetails" class="details">Select a client to review configuration.</div><div class="refresh-actions"><button type="button" id="refreshSelectedBtn">Refresh Selected Site</button><button type="button" class="secondary" id="refreshAllBtn">Refresh All Sites</button></div><div class="pin-actions"><button type="button" class="secondary" id="changePinBtn" data-pin-control="true">Change PIN</button><button type="button" class="secondary" id="lockPinBtn" data-pin-control="true">Lock Console</button></div></section><section class="card"><h2 style="margin-top:0;">Active Sites</h2><div id="telemetryContainer" class="site-list"></div></section></div><section class="card"><h2 style="margin-top:0;">Client Configuration</h2><form id="configForm"><div class="form-grid"><label class="field"><span>Site Name</span><input id="siteInput" type="text" placeholder="Site name"></label><label class="field"><span>Device Label</span><input id="deviceLabelInput" type="text" placeholder="Device label"></label><label class="field"><span>Server Fleet</span><input id="routeInput" type="text" placeholder="tankalarm-server"></label><label class="field"><span>Sample Seconds</span><input id="sampleSecondsInput" type="number" min="30" step="30"></label><label class="field"><span>Level Change Threshold (in)</span><input id="levelChangeThresholdInput" type="number" min="0" step="0.1" placeholder="0 = disabled"></label><label class="field"><span>Report Hour (0-23)</span><input id="reportHourInput" type="number" min="0" max="23"></label><label class="field"><span>Report Minute (0-59)</span><input id="reportMinuteInput" type="number" min="0" max="59"></label><label class="field"><span>SMS Primary</span><input id="smsPrimaryInput" type="text" placeholder="+1234567890"></label><label class="field"><span>SMS Secondary</span><input id="smsSecondaryInput" type="text" placeholder="+1234567890"></label><label class="field"><span>Daily Report Email</span><input id="dailyEmailInput" type="email" placeholder="reports@example.com"></label></div><h3 style="margin-top:24px;">Clear Button (Physical Button to Clear Relays)</h3><div class="form-grid"><label class="field"><span>Clear Button Pin (-1 = disabled)<span class="tooltip-icon" tabindex="0" data-tooltip="Hardware Setting: Defined in firmware/config file">?</span></span><input id="clearButtonPinInput" type="number" min="-1" max="99" value="-1" disabled style="cursor:not-allowed;opacity:0.7;background:var(--chip);"></label><label class="field"><span>Button Active State</span><select id="clearButtonActiveHighInput" disabled style="cursor:not-allowed;opacity:0.7;background:var(--chip);"><option value="false">Active LOW (pullup, button to GND)</option><option value="true">Active HIGH (external pull-down)</option></select></label></div><h3 style="margin-top:24px;">Server SMS Alerts</h3><div class="toggle-group"><label class="toggle"><span>Send SMS on High Alarm</span><input type="checkbox" id="smsHighToggle"></label><label class="toggle"><span>Send SMS on Low Alarm</span><input type="checkbox" id="smsLowToggle"></label><label class="toggle"><span>Send SMS on Clear Alarm</span><input type="checkbox" id="smsClearToggle"></label></div><h3>FTP Backup &amp; Restore</h3><div class="form-grid"><label class="field"><span>FTP Host</span><input id="ftpHost" type="text" placeholder="192.168.1.50"></label><label class="field"><span>FTP Port</span><input id="ftpPort" type="number" min="1" max="65535" value="21"></label><label class="field"><span>FTP User</span><input id="ftpUser" type="text" placeholder="user"></label><label class="field"><span>FTP Password <small style="color:var(--muted);font-weight:400;">(leave blank to keep)</small></span><input id="ftpPass" type="password" autocomplete="off"></label><label class="field"><span>FTP Path</span><input id="ftpPath" type="text" placeholder="/tankalarm/server"></label></div><div class="toggle-group"><label class="toggle"><span>Enable FTP</span><input type="checkbox" id="ftpEnabled"></label><label class="toggle"><span>Passive Mode</span><input type="checkbox" id="ftpPassive" checked></label><label class="toggle"><span>Auto-backup on save</span><input type="checkbox" id="ftpBackupOnChange"></label><label class="toggle"><span>Restore on boot (best for replacements)</span><input type="checkbox" id="ftpRestoreOnBoot"></label></div><div class="actions" style="margin-top:12px;"><button type="button" id="ftpBackupNow">Backup Now</button><button type="button" class="secondary" id="ftpRestoreNow">Restore Now</button></div><h3>Tanks</h3><div id="tankContainer" class="tank-container"></div><div class="actions"><button type="button" class="secondary" id="addTank">Add Tank</button><button type="submit">Send Configuration</button></div></form></section></main><div id="toast"></div><div id="pinModal" class="modal hidden"><div class="modal-card"><div class="modal-badge" id="pinSessionBadge">Session</div><h2 id="pinModalTitle">Set Admin PIN</h2><p id="pinModalDescription">Enter a 4-digit PIN to unlock configuration changes.</p><form id="pinForm"><label class="field hidden" id="pinCurrentGroup"><span>Current PIN</span><input type="password" id="pinCurrentInput" inputmode="numeric" pattern="\d*" maxlength="4" autocomplete="off"></label><label class="field" id="pinPrimaryGroup"><span id="pinPrimaryLabel">PIN</span><input type="password" id="pinInput" inputmode="numeric" pattern="\d*" maxlength="4" autocomplete="off" required placeholder="4 digits" aria-describedby="pinHint" title="Enter exactly four digits (0-9)"><small class="pin-hint" id="pinHint">Use exactly 4 digits (0-9). The PIN is kept locally in this browser for 90 days.</small></label><label class="field hidden" id="pinConfirmGroup"><span>Confirm PIN</span><input type="password" id="pinConfirmInput" inputmode="numeric" pattern="\d*" maxlength="4" autocomplete="off"></label><div class="actions"><button type="submit" id="pinSubmit">Save PIN</button><button type="button" class="secondary" id="pinCancel">Cancel</button></div></form></div></div><script>(function(){function escapeHtml(str){if(!str)return'';const div=document.createElement('div');div.textContent=str;return div.innerHTML;}const THEME_KEY = 'tankalarmTheme';const themeToggle = document.getElementById('themeToggle');function applyTheme(next){const theme = next === 'dark' ? 'dark':'light';document.body.dataset.theme = theme;themeToggle.textContent = theme === 'dark' ? '☀':'☾';themeToggle.setAttribute('aria-label',theme === 'dark' ? 'Switch to light mode':'Switch to dark mode');localStorage.setItem(THEME_KEY,theme);}applyTheme(localStorage.getItem(THEME_KEY)|| 'light');themeToggle.addEventListener('click',()=>{const next = document.body.dataset.theme === 'dark' ? 'light':'dark';applyTheme(next);});const PIN_STORAGE_KEY = 'tankalarmPin';const PIN_SESSION_TTL_MS = 90 * 24 * 60 * 60 * 1000;function loadStoredPin(){try{const raw = localStorage.getItem(PIN_STORAGE_KEY);if(!raw)return null;const parsed = JSON.parse(raw);if(!parsed || !parsed.pin || !parsed.expiresAt){localStorage.removeItem(PIN_STORAGE_KEY);return null;}if(Date.now()> parsed.expiresAt){localStorage.removeItem(PIN_STORAGE_KEY);return null;}return parsed.pin;}catch(err){localStorage.removeItem(PIN_STORAGE_KEY);return null;}}function storePin(pin){const payload ={pin,expiresAt:Date.now()+ PIN_SESSION_TTL_MS};localStorage.setItem(PIN_STORAGE_KEY,JSON.stringify(payload));}function clearStoredPin(){localStorage.removeItem(PIN_STORAGE_KEY);}const state ={data:null,selected:null};const pinState ={value:loadStoredPin()|| null,configured:false,mode:'unlock'};const els ={serverName:document.getElementById('serverName'),serverUid:document.getElementById('serverUid'),nextEmail:document.getElementById('nextEmail'),telemetryContainer:document.getElementById('telemetryContainer'),clientSelect:document.getElementById('clientSelect'),clientDetails:document.getElementById('clientDetails'),site:document.getElementById('siteInput'),deviceLabel:document.getElementById('deviceLabelInput'),route:document.getElementById('routeInput'),sampleSeconds:document.getElementById('sampleSecondsInput'),levelChangeThreshold:document.getElementById('levelChangeThresholdInput'),reportHour:document.getElementById('reportHourInput'),reportMinute:document.getElementById('reportMinuteInput'),smsPrimary:document.getElementById('smsPrimaryInput'),smsSecondary:document.getElementById('smsSecondaryInput'),dailyEmail:document.getElementById('dailyEmailInput'),clearButtonPin:document.getElementById('clearButtonPinInput'),clearButtonActiveHigh:document.getElementById('clearButtonActiveHighInput'),smsHighToggle:document.getElementById('smsHighToggle'),smsLowToggle:document.getElementById('smsLowToggle'),smsClearToggle:document.getElementById('smsClearToggle'),ftpEnabled:document.getElementById('ftpEnabled'),ftpPassive:document.getElementById('ftpPassive'),ftpBackupOnChange:document.getElementById('ftpBackupOnChange'),ftpRestoreOnBoot:document.getElementById('ftpRestoreOnBoot'),ftpHost:document.getElementById('ftpHost'),ftpPort:document.getElementById('ftpPort'),ftpUser:document.getElementById('ftpUser'),ftpPass:document.getElementById('ftpPass'),ftpPath:document.getElementById('ftpPath'),ftpBackupNow:document.getElementById('ftpBackupNow'),ftpRestoreNow:document.getElementById('ftpRestoreNow'),tankContainer:document.getElementById('tankContainer'),toast:document.getElementById('toast'),addTank:document.getElementById('addTank'),form:document.getElementById('configForm'),changePinBtn:document.getElementById('changePinBtn'),lockPinBtn:document.getElementById('lockPinBtn'),refreshSelectedBtn:document.getElementById('refreshSelectedBtn'),refreshAllBtn:document.getElementById('refreshAllBtn')};const pinEls ={modal:document.getElementById('pinModal'),title:document.getElementById('pinModalTitle'),description:document.getElementById('pinModalDescription'),currentGroup:document.getElementById('pinCurrentGroup'),current:document.getElementById('pinCurrentInput'),pin:document.getElementById('pinInput'),confirmGroup:document.getElementById('pinConfirmGroup'),confirm:document.getElementById('pinConfirmInput'),primaryLabel:document.getElementById('pinPrimaryLabel'),form:document.getElementById('pinForm'),submit:document.getElementById('pinSubmit'),cancel:document.getElementById('pinCancel')};els.smsHighToggle.checked = true;els.smsLowToggle.checked = true;els.smsClearToggle.checked = false;function setFormDisabled(disabled){const controls = els.form.querySelectorAll('input,select,button');controls.forEach(control =>{if(control.dataset && control.dataset.pinControl === 'true'){return;}control.disabled = disabled;});els.addTank.disabled = disabled;}function invalidatePin(){pinState.value = null;clearStoredPin();setFormDisabled(true);}function isPinModalVisible(){return !pinEls.modal.classList.contains('hidden');}function resetPinForm(){pinEls.form.reset();pinEls.currentGroup.classList.add('hidden');pinEls.confirmGroup.classList.add('hidden');pinEls.primaryLabel.textContent = 'PIN';}function showPinModal(mode){pinState.mode = mode;resetPinForm();if(mode === 'setup'){pinEls.title.textContent = 'Set Admin PIN';pinEls.description.textContent = 'Choose a 4-digit PIN to secure configuration changes.';pinEls.confirmGroup.classList.remove('hidden');pinEls.cancel.classList.add('hidden');document.getElementById('pinSessionBadge').textContent = 'Required';}else if(mode === 'change'){pinEls.title.textContent = 'Change Admin PIN';pinEls.description.textContent = 'Enter your current PIN and the new PIN you would like to use.';pinEls.currentGroup.classList.remove('hidden');pinEls.confirmGroup.classList.remove('hidden');pinEls.primaryLabel.textContent = 'New PIN';pinEls.cancel.classList.remove('hidden');document.getElementById('pinSessionBadge').textContent = 'Secured';}else{pinEls.title.textContent = 'Enter Admin PIN';pinEls.description.textContent = 'Enter the admin PIN to unlock configuration controls. We keep it in this browser for 90 days.';pinEls.cancel.classList.remove('hidden');document.getElementById('pinSessionBadge').textContent = 'Locked';}pinEls.modal.classList.remove('hidden');setFormDisabled(true);setTimeout(()=> pinEls.pin.focus(),50);}function hidePinModal(){if(isPinModalVisible()){pinEls.modal.classList.add('hidden');resetPinForm();}}async function requestPin(payload){const res = await fetch('/api/pin',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(payload)});if(!res.ok){const text = await res.text();const error = new Error(text || 'PIN request failed');error.serverRejected = true;throw error;}const data = await res.json();pinState.configured = !!data.pinConfigured;return data;}async function handlePinSubmit(event){event.preventDefault();let payload;let pinToStore = null;try{if(pinState.mode === 'setup'){const newPin = pinEls.pin.value.trim();const confirmPin = pinEls.confirm.value.trim();if(!isFourDigits(newPin)){throw new Error('PIN must be exactly 4 digits.');}if(newPin !== confirmPin){throw new Error('PIN confirmation does not match.');}payload ={newPin};pinToStore = newPin;}else if(pinState.mode === 'change'){const currentPin = pinEls.current.value.trim();const newPin = pinEls.pin.value.trim();const confirmPin = pinEls.confirm.value.trim();if(!isFourDigits(currentPin)|| !isFourDigits(newPin)){throw new Error('PINs must be exactly 4 digits.');}if(newPin !== confirmPin){throw new Error('PIN confirmation does not match.');}payload ={pin:currentPin,newPin};pinToStore = newPin;}else{const pin = pinEls.pin.value.trim();if(!isFourDigits(pin)){throw new Error('PIN must be exactly 4 digits.');}payload ={pin};pinToStore = pin;}const result = await requestPin(payload);pinState.value = pinToStore;storePin(pinToStore);hidePinModal();setFormDisabled(false);showToast((result && result.message)|| 'PIN updated');updatePinLock();}catch(err){if(err.serverRejected){invalidatePin();}showToast(err.message || 'PIN action failed',true);}}function isFourDigits(value){return /^\d{4}$/.test(value || '');}function updatePinLock(){const configured = !!(state.data && state.data.server && state.data.server.pinConfigured);pinState.configured = configured;if(!configured){if(!isPinModalVisible()|| pinState.mode !== 'setup'){invalidatePin();showPinModal('setup');}return;}if(!pinState.value){if(!isPinModalVisible()){showPinModal('unlock');}}else{storePin(pinState.value);setFormDisabled(false);hidePinModal();}}setFormDisabled(true);function showToast(message,isError){els.toast.textContent = message;els.toast.style.background = isError ? '#dc2626':'#0284c7';els.toast.classList.add('show');setTimeout(()=> els.toast.classList.remove('show'),2500);}function formatNumber(value){return(typeof value === 'number' && isFinite(value))? value.toFixed(1):'--';}function valueOr(value,fallback){return(value === undefined || value === null)? fallback:value;}function syncServerSettings(serverInfo){els.smsPrimary.value = valueOr(serverInfo && serverInfo.smsPrimary,'');els.smsSecondary.value = valueOr(serverInfo && serverInfo.smsSecondary,'');els.smsHighToggle.checked = !!valueOr(serverInfo && serverInfo.smsOnHigh,true);els.smsLowToggle.checked = !!valueOr(serverInfo && serverInfo.smsOnLow,true);els.smsClearToggle.checked = !!valueOr(serverInfo && serverInfo.smsOnClear,false);const ftp = serverInfo && serverInfo.ftp ? serverInfo.ftp:{};els.ftpEnabled.checked = !!valueOr(ftp.enabled,false);els.ftpPassive.checked = ftp.passive !== false;els.ftpBackupOnChange.checked = !!valueOr(ftp.backupOnChange,false);els.ftpRestoreOnBoot.checked = !!valueOr(ftp.restoreOnBoot,false);els.ftpHost.value = valueOr(ftp.host,'');els.ftpPort.value = valueOr(ftp.port,21);els.ftpUser.value = valueOr(ftp.user,'');els.ftpPath.value = valueOr(ftp.path,'/tankalarm/server');els.ftpPass.value = '';}function formatEpoch(epoch){if(!epoch)return '--';const date = new Date(epoch * 1000);if(isNaN(date.getTime()))return '--';return date.toLocaleString(undefined,{year:'numeric',month:'numeric',day:'numeric',hour:'numeric',minute:'2-digit',hour12:true});}function rowHtml(value){return(value === undefined || value === null || value === '')? '--':value;}function renderTelemetry(){if(!els.telemetryContainer)return;els.telemetryContainer.innerHTML = '';if(!state.data || !state.data.clients)return;const sites ={};state.data.clients.forEach(client =>{const siteName = client.site || 'Unknown Site';if(!sites[siteName])sites[siteName]=[];sites[siteName].push(client);});Object.keys(sites).sort().forEach(siteName =>{const tanks = sites[siteName];const siteCard = document.createElement('div');siteCard.className = 'site-card';const header = document.createElement('div');header.className = 'site-header';header.innerHTML = `<span>${escapeHtml(siteName)}</span><span style="font-size:0.8rem;font-weight:400;color:var(--muted)">${tanks.length} Tank${tanks.length===1?'':'s'}</span>`;siteCard.appendChild(header);const grid = document.createElement('div');grid.className = 'site-tanks';tanks.forEach(tank =>{const tankDiv = document.createElement('div');tankDiv.className = 'site-tank';if(tank.alarm)tankDiv.classList.add('alarm');tankDiv.innerHTML = `<div class="tank-meta"><strong>${escapeHtml(tank.label || 'Tank')} #${tank.tank}</strong><code>${escapeHtml(tank.client).substring(0,8)}...</code></div><div class="tank-value">${formatNumber(tank.levelInches)} <small style="font-size:0.7em;color:var(--muted)">in</small></div><div class="tank-footer">${tank.alarm ? `<span style="color:var(--danger);font-weight:600">ALARM: ${escapeHtml(tank.alarmType)}</span>`:'Normal'}<span style="float:right">${formatEpoch(tank.lastUpdate)}</span></div>`;grid.appendChild(tankDiv);});siteCard.appendChild(grid);els.telemetryContainer.appendChild(siteCard);});}function ensureOption(uid,label){let option = Array.from(els.clientSelect.options).find(opt => opt.value === uid);if(!option){option = document.createElement('option');option.value = uid;els.clientSelect.appendChild(option);}option.textContent = label;}function populateClientSelect(){els.clientSelect.innerHTML = '';if(!state.data || !state.data.clients)return;state.data.clients.forEach(client =>{const label = `${escapeHtml(client.site || 'Site')}– ${escapeHtml(client.label || 'Tank')}(#${client.tank || '?'})`;ensureOption(client.client,label);});if(state.data.configs){state.data.configs.forEach(entry =>{ensureOption(entry.client,`${entry.site || 'Site'}– Stored config`);});}if(!state.selected && els.clientSelect.options.length){state.selected = els.clientSelect.options[0].value;}if(state.selected){els.clientSelect.value = state.selected;}}function lookupClient(uid){if(!state.data || !state.data.clients)return null;return state.data.clients.find(c => c.client === uid)|| null;}function lookupConfig(uid){if(!state.data || !state.data.configs)return null;const entry = state.data.configs.find(c => c.client === uid);if(!entry)return null;if(entry.config){return entry.config;}if(entry.configJson){try{entry.config = JSON.parse(entry.configJson);return entry.config;}catch(err){console.warn('Stored config failed to parse',err);}}return null;}function buildDefaultConfig(uid){const client = lookupClient(uid);const serverDefaults =(state.data && state.data.server)? state.data.server:{};const tankList =(client && Array.isArray(client.tanks))? client.tanks:[];const firstTank = tankList.length ? tankList[0]:null;const tankId = firstTank && firstTank.tank ? firstTank.tank:'A';const parsedNumber = firstTank && firstTank.tank ? parseInt(firstTank.tank,10):NaN;const defaultTank ={id:tankId || 'A',name:firstTank ?(firstTank.label || `Tank ${tankId || 'A'}`):'Tank A',number:isFinite(parsedNumber)&& parsedNumber > 0 ? parsedNumber:1,sensor:'analog',primaryPin:0,secondaryPin:-1,loopChannel:-1,heightInches:firstTank && typeof firstTank.heightInches === 'number' ? firstTank.heightInches:120,highAlarm:100,lowAlarm:20,daily:true,alarmSms:true,upload:true};return{site:client ?(client.site || ''):'',deviceLabel:client ? `${((client.site || 'Client')).replace(/\s+/g,'-')}-${escapeHtml(client.tank || tankId || 'A')}`:'Client-112025',serverFleet:'tankalarm-server',sampleSeconds:1800,levelChangeThreshold:0,reportHour:5,reportMinute:0,dailyEmail:serverDefaults.dailyEmail || '',tanks:tankList.length ? tankList.map(t =>({id:t.tank || 'A',name:t.label ||(t.tank ? `Tank ${t.tank}`:'Tank'),number:t.tank ?(parseInt(t.tank,10)|| 1):1,sensor:'analog',primaryPin:0,secondaryPin:-1,loopChannel:-1,heightInches:typeof t.heightInches === 'number' ? t.heightInches:120,highAlarm:100,lowAlarm:20,daily:true,alarmSms:true,upload:true})):[defaultTank]};}function populateTankRows(tanks){els.tankContainer.innerHTML = '';(tanks || []).forEach(tank => addTankCard(tank));if(!tanks || !tanks.length){addTankCard();}}function addTankCard(tank){let defaults=tank;if(!defaults){const cards=els.tankContainer.querySelectorAll('.tank-card');let maxNum=0;const usedIds=new Set();cards.forEach(c=>{const n=parseInt(c.querySelector('.tank-number').value,10)||0;if(n>maxNum)maxNum=n;const i=c.querySelector('.tank-id').value.trim().toUpperCase();if(i)usedIds.add(i);});let nextChar=65;while(usedIds.has(String.fromCharCode(nextChar))&&nextChar<90){nextChar++;}defaults={id:String.fromCharCode(nextChar),name:`Tank ${String.fromCharCode(nextChar)}`,number:maxNum+1,sensor:'analog',primaryPin:0,secondaryPin:-1,loopChannel:-1,heightInches:120,highAlarm:100,lowAlarm:20,daily:true,alarmSms:true,upload:true};}const card=document.createElement('div');card.className='tank-card';card.innerHTML=` <div class="tank-header"><div class="tank-header-inputs"><label class="field" style="margin:0;width:60px;"><span>ID</span><input type="text" class="tank-id" maxlength="1" value="${defaults.id||''}"></label><label class="field" style="margin:0;flex:1;"><span>Name</span><input type="text" class="tank-name" value="${defaults.name||''}"></label></div><button type="button" class="destructive remove">Remove</button></div><div class="tank-grid"><label class="field"><span>Number</span><input type="number" class="tank-number" min="1" value="${valueOr(defaults.number,1)}"></label><label class="field"><span>Sensor <small>(Hardware)</small></span><select class="tank-sensor" disabled style="background:var(--chip);"><option value="analog" ${defaults.sensor==='analog'?'selected':''}>Analog</option><option value="digital" ${defaults.sensor==='digital'?'selected':''}>Digital</option><option value="current" ${defaults.sensor==='current'?'selected':''}>Current Loop</option></select></label><label class="field"><span>Primary Pin <small>(Hardware)</small></span><input type="number" class="tank-primary" value="${valueOr(defaults.primaryPin,0)}" disabled style="background:var(--chip);"></label><label class="field"><span>Secondary Pin <small>(Hardware)</small></span><input type="number" class="tank-secondary" value="${valueOr(defaults.secondaryPin,-1)}" disabled style="background:var(--chip);"></label><label class="field"><span>Loop Ch <small>(Hardware)</small></span><input type="number" class="tank-loop" value="${valueOr(defaults.loopChannel,-1)}" disabled style="background:var(--chip);"></label><label class="field"><span>Height (in)</span><input type="number" class="tank-height" step="0.1" value="${valueOr(defaults.heightInches,120)}"></label></div><div class="alarm-section"><div class="alarm-header">Alarms & Reporting</div><div class="alarm-grid"><label class="field"><span>High Alarm</span><input type="number" class="tank-high" step="0.1" value="${valueOr(defaults.highAlarm,100)}"></label><label class="field"><span>Low Alarm</span><input type="number" class="tank-low" step="0.1" value="${valueOr(defaults.lowAlarm,20)}"></label><label class="checkbox-label"><input type="checkbox" class="tank-daily" ${defaults.daily?'checked':''}> Daily Report</label><label class="checkbox-label"><input type="checkbox" class="tank-alarm" ${defaults.alarmSms?'checked':''}> Alarm SMS</label><label class="checkbox-label"><input type="checkbox" class="tank-upload" ${defaults.upload?'checked':''}> Upload</label></div></div> `;card.querySelector('.remove').addEventListener('click',()=>{if(confirm('Remove this tank?'))card.remove();});els.tankContainer.appendChild(card);}function loadConfigIntoForm(uid){if(!uid)return;const client = lookupClient(uid);const stored = lookupConfig(uid);const config = stored ? JSON.parse(JSON.stringify(stored)):buildDefaultConfig(uid);state.selected = uid;els.site.value = config.site || '';els.deviceLabel.value = config.deviceLabel || '';els.route.value = config.serverFleet || '';els.sampleSeconds.value = valueOr(config.sampleSeconds,1800);els.levelChangeThreshold.value = valueOr(config.levelChangeThreshold,0);els.reportHour.value = valueOr(config.reportHour,5);els.reportMinute.value = valueOr(config.reportMinute,0);els.dailyEmail.value = config.dailyEmail || '';els.clearButtonPin.value = valueOr(config.clearButtonPin,-1);els.clearButtonActiveHigh.value = config.clearButtonActiveHigh ? 'true':'false';populateTankRows(config.tanks);const detailParts = [];if(client){detailParts.push(`<strong>Site:</strong> ${escapeHtml(client.site || 'Unknown')}`);detailParts.push(`<strong>Latest Tank:</strong> ${escapeHtml(client.label || 'Tank')}#${escapeHtml(client.tank || '?')}at ${formatNumber(client.levelInches)}in`);}detailParts.push(`<strong>Target UID:</strong><code>${uid}</code>`);els.clientDetails.innerHTML = detailParts.join(' · ');}function collectConfig(){const tanks=[];const seenIds=new Set();const seenNumbers=new Set();els.tankContainer.querySelectorAll('.tank-card').forEach(card=>{const idVal=card.querySelector('.tank-id').value.trim().toUpperCase();const id=idVal.substring(0,1)||'A';if(seenIds.has(id)){throw new Error(`Duplicate Tank ID: ${id}`);}seenIds.add(id);const numVal=parseInt(card.querySelector('.tank-number').value,10);const number=isFinite(numVal)&&numVal>0?numVal:1;if(seenNumbers.has(number)){throw new Error(`Duplicate Tank Number: ${number}`);}seenNumbers.add(number);const tank={id:id,name:card.querySelector('.tank-name').value.trim()||'Tank',number:number,sensor:card.querySelector('.tank-sensor').value||'analog',primaryPin:parseInt(card.querySelector('.tank-primary').value,10)||0,secondaryPin:parseInt(card.querySelector('.tank-secondary').value,10)|| -1,loopChannel:parseInt(card.querySelector('.tank-loop').value,10)|| -1,heightInches:parseFloat(card.querySelector('.tank-height').value)||120,highAlarm:parseFloat(card.querySelector('.tank-high').value)||100,lowAlarm:parseFloat(card.querySelector('.tank-low').value)||20,daily:card.querySelector('.tank-daily').checked,alarmSms:card.querySelector('.tank-alarm').checked,upload:card.querySelector('.tank-upload').checked};tanks.push(tank);});if(!tanks.length){throw new Error('Add at least one tank before sending.');}const config ={site:els.site.value.trim(),deviceLabel:els.deviceLabel.value.trim(),serverFleet:els.route.value.trim()|| 'tankalarm-server',sampleSeconds:parseInt(els.sampleSeconds.value,10)|| 1800,levelChangeThreshold:Math.max(0,parseFloat(els.levelChangeThreshold.value)|| 0),reportHour:parseInt(els.reportHour.value,10)|| 5,reportMinute:parseInt(els.reportMinute.value,10)|| 0,dailyEmail:els.dailyEmail.value.trim(),clearButtonPin:parseInt(els.clearButtonPin.value,10),clearButtonActiveHigh:els.clearButtonActiveHigh.value === 'true',tanks};return config;}function collectServerSettings(){const ftpSettings ={enabled:!!els.ftpEnabled.checked,passive:!!els.ftpPassive.checked,backupOnChange:!!els.ftpBackupOnChange.checked,restoreOnBoot:!!els.ftpRestoreOnBoot.checked,host:els.ftpHost.value.trim(),port:parseInt(els.ftpPort.value,10)|| 21,user:els.ftpUser.value.trim(),path:els.ftpPath.value.trim()|| '/tankalarm/server'};const ftpPass = els.ftpPass.value.trim();if(ftpPass){ftpSettings.pass = ftpPass;}return{smsPrimary:els.smsPrimary.value.trim(),smsSecondary:els.smsSecondary.value.trim(),smsOnHigh:!!els.smsHighToggle.checked,smsOnLow:!!els.smsLowToggle.checked,smsOnClear:!!els.smsClearToggle.checked,ftp:ftpSettings};}async function performFtpAction(kind){if(pinState.configured && !pinState.value){showPinModal('unlock');showToast('Enter the admin PIN first.',true);return;}const payload ={pin:pinState.value || ''};const endpoint = kind === 'restore' ? '/api/ftp-restore':'/api/ftp-backup';try{const res = await fetch(endpoint,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(payload)});if(!res.ok){if(res.status === 403){invalidatePin();showPinModal('unlock');}const text = await res.text();throw new Error(text || `${kind}failed`);}const data = await res.json();if(data && data.message){showToast(data.message,false);}else{showToast(kind === 'restore' ? 'FTP restore completed':'FTP backup completed');}if(kind === 'restore'){await refreshData(state.selected);}}catch(err){showToast(err.message || `FTP ${kind}failed`,true);}}async function submitConfig(event){event.preventDefault();const uid = state.selected;if(!uid){showToast('Select a client first.',true);return;}let config;try{config = collectConfig();}catch(err){showToast(err.message,true);return;}if(pinState.configured && !pinState.value){showPinModal('unlock');showToast('Enter the admin PIN to send configurations.',true);return;}const payload ={client:uid,config,server:collectServerSettings()};if(pinState.value){payload.pin = pinState.value;}try{const res = await fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(payload)});if(!res.ok){if(res.status === 403){invalidatePin();showPinModal(pinState.configured ? 'unlock':'setup');throw new Error('PIN required or invalid.');}const text = await res.text();throw new Error(text || 'Server rejected configuration');}showToast('Configuration queued for delivery');await refreshData(state.selected);}catch(err){showToast(err.message || 'Failed to send config',true);}}function applyServerData(data,preferredUid){state.data = data;const serverInfo =(state.data && state.data.server)? state.data.server:{};els.serverName.textContent = serverInfo.name || 'Tank Alarm Server';syncServerSettings(serverInfo);els.serverUid.textContent = state.data.serverUid || '--';els.nextEmail.textContent = formatEpoch(state.data.nextDailyEmailEpoch);if(preferredUid){state.selected = preferredUid;}renderTelemetry();populateClientSelect();if(state.selected){loadConfigIntoForm(state.selected);}else if(els.clientSelect.options.length){loadConfigIntoForm(els.clientSelect.value);}updatePinLock();}async function refreshData(preferredUid){try{const query = preferredUid ? `?client=${encodeURIComponent(preferredUid)}`:'';const res = await fetch(`/api/clients${query}`);if(!res.ok){throw new Error('Failed to fetch server data');}const data = await res.json();applyServerData(data,preferredUid || state.selected);}catch(err){showToast(err.message || 'Initialization failed',true);}}async function triggerManualRefresh(targetUid){const payload = targetUid ?{client:targetUid}:{};try{const res = await fetch('/api/refresh',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(payload)});if(!res.ok){const text = await res.text();throw new Error(text || 'Refresh failed');}const data = await res.json();applyServerData(data,targetUid || state.selected);showToast(targetUid ? 'Selected site updated':'All sites updated');}catch(err){showToast(err.message || 'Refresh failed',true);}}pinEls.form.addEventListener('submit',handlePinSubmit);pinEls.cancel.addEventListener('click',()=>{hidePinModal();});els.changePinBtn.addEventListener('click',()=>{if(!pinState.configured){showPinModal('setup');}else if(!pinState.value){showPinModal('unlock');}else{showPinModal('change');}});els.lockPinBtn.addEventListener('click',()=>{invalidatePin();showToast('Console locked',false);if(pinState.configured){showPinModal('unlock');}else{showPinModal('setup');}});els.addTank.addEventListener('click',()=> addTankCard());els.clientSelect.addEventListener('change',event =>{loadConfigIntoForm(event.target.value);});els.form.addEventListener('submit',submitConfig);els.refreshSelectedBtn.addEventListener('click',()=>{const target = state.selected ||(els.clientSelect.value || '');if(!target){showToast('Select a client first.',true);return;}triggerManualRefresh(target);});els.refreshAllBtn.addEventListener('click',()=>{triggerManualRefresh(null);});els.ftpBackupNow.addEventListener('click',()=> performFtpAction('backup'));els.ftpRestoreNow.addEventListener('click',()=> performFtpAction('restore'));refreshData();})();</script></body></html>)HTML";

static void initializeStorage();
static void ensureConfigLoaded();
static void createDefaultConfig(ServerConfig &cfg);
static bool loadConfig(ServerConfig &cfg);
static bool saveConfig(const ServerConfig &cfg);
static void printHardwareRequirements();
static void initializeNotecard();
static void ensureTimeSync();
static double currentEpoch();
static void scheduleNextDailyEmail();
static void scheduleNextViewerSummary();
static void initializeEthernet();
static void handleWebRequests();
static bool readHttpRequest(EthernetClient &client, String &method, String &path, String &body, size_t &contentLength, bool &bodyTooLarge);
static void respondHtml(EthernetClient &client, const String &body);
static void respondJson(EthernetClient &client, const String &body, int status = 200);
// Note: respondStatus is forward-declared earlier in the file (before requireValidPin)
static void sendDashboard(EthernetClient &client);
static void sendClientConsole(EthernetClient &client);
static void sendConfigGenerator(EthernetClient &client);
static void sendTankJson(EthernetClient &client);
static void sendClientDataJson(EthernetClient &client);
static void handleConfigPost(EthernetClient &client, const String &body);
static void handlePausePost(EthernetClient &client, const String &body);
static void handleFtpBackupPost(EthernetClient &client, const String &body);
static void handleFtpRestorePost(EthernetClient &client, const String &body);
// Enum definitions
enum class ConfigDispatchStatus : uint8_t {
  Ok = 0,
  PayloadTooLarge,
  NotecardFailure
};

enum class SerialRequestResult : uint8_t {
  Sent = 0,
  Throttled,
  NotecardFailure
};

// Forward declarations
static void handlePinPost(EthernetClient &client, const String &body);
static void handleRefreshPost(EthernetClient &client, const String &body);
static void handleRelayPost(EthernetClient &client, const String &body);
static void handleRelayClearPost(EthernetClient &client, const String &body);
static void handleSerialLogsGet(EthernetClient &client, const String &queryString);
static void handleSerialLogsDownload(EthernetClient &client, const String &queryString);
static void handleSerialRequestPost(EthernetClient &client, const String &body);
static void sendSerialMonitor(EthernetClient &client);
static void sendCalibrationPage(EthernetClient &client);
static void handleCalibrationGet(EthernetClient &client);
static void handleCalibrationPost(EthernetClient &client, const String &body);
static void sendContactsManager(EthernetClient &client);
static void sendServerSettings(EthernetClient &client);
static void handleServerSettingsPost(EthernetClient &client, const String &body);
static void handleContactsGet(EthernetClient &client);
static void handleContactsPost(EthernetClient &client, const String &body);
static TankCalibration *findOrCreateTankCalibration(const char *clientUid, uint8_t tankNumber);
static void recalculateCalibration(TankCalibration *cal);
static void loadCalibrationData();
static void saveCalibrationData();
static void saveCalibrationEntry(const char *clientUid, uint8_t tankNumber, double timestamp, float sensorReading, float verifiedLevelInches, const char *notes);
static ConfigDispatchStatus dispatchClientConfig(const char *clientUid, JsonVariantConst cfgObj);
static bool sendRelayCommand(const char *clientUid, uint8_t relayNum, bool state, const char *source);
static void pollNotecard();
static void processNotefile(const char *fileName, void (*handler)(JsonDocument &, double));
static void handleTelemetry(JsonDocument &doc, double epoch);
static void handleAlarm(JsonDocument &doc, double epoch);
static void handleDaily(JsonDocument &doc, double epoch);
static void handleSerialLog(JsonDocument &doc, double epoch);
static void handleSerialAck(JsonDocument &doc, double epoch);
static void addServerSerialLog(const char *message, const char *level = "info", const char *source = "server");
static ClientSerialBuffer *findOrCreateClientSerialBuffer(const char *clientUid);
static void addClientSerialLog(const char *clientUid, const char *message, double timestamp, const char *level = "info", const char *source = "client");
static SerialRequestResult requestClientSerialLogs(const char *clientUid, String &errorMessage);
static TankRecord *upsertTankRecord(const char *clientUid, uint8_t tankNumber);
static void sendSmsAlert(const char *message);
static void sendDailyEmail();
static void loadClientConfigSnapshots();
static void saveClientConfigSnapshots();
static void cacheClientConfigFromBuffer(const char *clientUid, const char *buffer);
static ClientConfigSnapshot *findClientConfigSnapshot(const char *clientUid);
static float convertMaToLevel(const char *clientUid, uint8_t tankNumber, float mA);
static float convertVoltageToLevel(const char *clientUid, uint8_t tankNumber, float voltage);
static ClientMetadata *findClientMetadata(const char *clientUid);
static ClientMetadata *findOrCreateClientMetadata(const char *clientUid);
static bool checkSmsRateLimit(TankRecord *rec);
static void publishViewerSummary();
static double computeNextAlignedEpoch(double epoch, uint8_t baseHour, uint32_t intervalSeconds);
static String getQueryParam(const String &query, const char *key);
static bool requireValidPin(EthernetClient &client, const char *pinValue);

static void handleRefreshPost(EthernetClient &client, const String &body) {
  char clientUid[64] = {0};
  const char *pinValue = nullptr;
  if (body.length() > 0) {
    DynamicJsonDocument doc(192);
    if (deserializeJson(doc, body) == DeserializationError::Ok) {
      const char *uid = doc["client"] | "";
      if (uid && *uid) {
        strlcpy(clientUid, uid, sizeof(clientUid));
      }
      pinValue = doc["pin"].as<const char *>();
    }
  }

  if (!requireValidPin(client, pinValue)) {
    return;
  }

  if (clientUid[0]) {
    Serial.print(F("Manual refresh requested for client " ));
    Serial.println(clientUid);
  } else {
    Serial.println(F("Manual refresh requested for all clients"));
  }

  pollNotecard();
  sendClientDataJson(client);
}

static void handleSerialLogsGet(EthernetClient &client, const String &queryString) {
  String source = getQueryParam(queryString, "source");
  source.toLowerCase();
  if (source.length() == 0) {
    source = "server";
  }

  String clientUid = getQueryParam(queryString, "client");
  int maxEntries = SERIAL_DEFAULT_MAX_ENTRIES;
  String maxParam = getQueryParam(queryString, "max");
  if (maxParam.length()) {
    int requested = maxParam.toInt();
    if (requested > 0) {
      int cap = (source == "server") ? SERVER_SERIAL_BUFFER_SIZE : CLIENT_SERIAL_BUFFER_SIZE;
      maxEntries = min(requested, cap);
    }
  }

  double sinceEpoch = 0.0;
  String sinceParam = getQueryParam(queryString, "since");
  if (sinceParam.length()) {
    sinceEpoch = sinceParam.toDouble();
  }

  DynamicJsonDocument doc(8192);
  JsonArray logsArray = doc.createNestedArray("logs");
  JsonObject meta = doc.createNestedObject("meta");
  meta["source"] = source;
  meta["requestedClient"] = clientUid;
  meta["staleSeconds"] = SERIAL_STALE_SECONDS;
  meta["max"] = maxEntries;
  meta["since"] = sinceEpoch;

  int added = 0;

  if (source == "server") {
    uint8_t startIdx = (gServerSerial.count < SERVER_SERIAL_BUFFER_SIZE) ? 0 : gServerSerial.writeIndex;
    for (uint8_t i = 0; i < gServerSerial.count; ++i) {
      uint8_t idx = (startIdx + i) % SERVER_SERIAL_BUFFER_SIZE;
      SerialLogEntry &entry = gServerSerial.entries[idx];
      if (entry.message[0] == '\0') {
        continue;
      }
      if (sinceEpoch > 0.0 && entry.timestamp <= sinceEpoch) {
        continue;
      }
      JsonObject row = logsArray.createNestedObject();
      row["timestamp"] = entry.timestamp;
      row["message"] = entry.message;
      row["level"] = entry.level;
      row["source"] = entry.source;
      row["client"] = "server";
      added++;
      if (maxEntries > 0 && added >= maxEntries) {
        break;
      }
    }
    meta["total"] = gServerSerial.count;
  } else if (source == "client" && clientUid.length() > 0) {
    ClientSerialBuffer *buf = nullptr;
    for (uint8_t b = 0; b < gClientSerialBufferCount; ++b) {
      if (strcmp(gClientSerialBuffers[b].clientUid, clientUid.c_str()) == 0) {
        buf = &gClientSerialBuffers[b];
        break;
      }
    }

    if (buf) {
      meta["lastAckEpoch"] = buf->lastAckEpoch;
      meta["lastAckStatus"] = buf->lastAckStatus;
      meta["lastRequestEpoch"] = buf->lastRequestEpoch;
      meta["lastLogEpoch"] = buf->lastLogEpoch;
      meta["awaitingLogs"] = buf->awaitingLogs;
      uint8_t startIdx = (buf->count < CLIENT_SERIAL_BUFFER_SIZE) ? 0 : buf->writeIndex;
      for (uint8_t i = 0; i < buf->count; ++i) {
        uint8_t idx = (startIdx + i) % CLIENT_SERIAL_BUFFER_SIZE;
        SerialLogEntry &entry = buf->entries[idx];
        if (entry.message[0] == '\0') {
          continue;
        }
        if (sinceEpoch > 0.0 && entry.timestamp <= sinceEpoch) {
          continue;
        }
        JsonObject row = logsArray.createNestedObject();
        row["timestamp"] = entry.timestamp;
        row["message"] = entry.message;
        row["level"] = entry.level;
        row["source"] = entry.source;
        row["client"] = buf->clientUid;
        added++;
        if (maxEntries > 0 && added >= maxEntries) {
          break;
        }
      }
    } else {
      meta["lastAckEpoch"] = 0;
      meta["awaitingLogs"] = false;
    }
  }

  String json;
  serializeJson(doc, json);
  respondJson(client, json);
}

static void handleSerialLogsDownload(EthernetClient &client, const String &queryString) {
  String source = getQueryParam(queryString, "source");
  source.toLowerCase();
  if (source.length() == 0) {
    source = "server";
  }
  String clientUid = getQueryParam(queryString, "client");
  int maxEntries = SERIAL_DEFAULT_MAX_ENTRIES;
  String maxParam = getQueryParam(queryString, "max");
  if (maxParam.length()) {
    int requested = maxParam.toInt();
    if (requested > 0) {
      int cap = (source == "server") ? SERVER_SERIAL_BUFFER_SIZE : CLIENT_SERIAL_BUFFER_SIZE;
      maxEntries = min(requested, cap);
    }
  }
  double sinceEpoch = 0.0;
  String sinceParam = getQueryParam(queryString, "since");
  if (sinceParam.length()) {
    sinceEpoch = sinceParam.toDouble();
  }

  String csv = "timestamp,level,source,client,message\n";
  int added = 0;

  auto appendCsvLine = [&](const SerialLogEntry &entry, const char *clientLabel) {
    if (entry.message[0] == '\0') {
      return;
    }
    csv += String(entry.timestamp, 3);
    csv += ',';
    csv += entry.level;
    csv += ',';
    csv += entry.source;
    csv += ',';
    csv += clientLabel;
    csv += ",\"";
    String msg(entry.message);
    msg.replace("\"", "\"\"");
    csv += msg;
    csv += "\"\n";
  };

  if (source == "server") {
    uint8_t startIdx = (gServerSerial.count < SERVER_SERIAL_BUFFER_SIZE) ? 0 : gServerSerial.writeIndex;
    for (uint8_t i = 0; i < gServerSerial.count; ++i) {
      uint8_t idx = (startIdx + i) % SERVER_SERIAL_BUFFER_SIZE;
      SerialLogEntry &entry = gServerSerial.entries[idx];
      if (sinceEpoch > 0.0 && entry.timestamp <= sinceEpoch) {
        continue;
      }
      appendCsvLine(entry, "server");
      added++;
      if (maxEntries > 0 && added >= maxEntries) {
        break;
      }
    }
  } else if (source == "client" && clientUid.length() > 0) {
    for (uint8_t b = 0; b < gClientSerialBufferCount; ++b) {
      if (strcmp(gClientSerialBuffers[b].clientUid, clientUid.c_str()) == 0) {
        ClientSerialBuffer &buf = gClientSerialBuffers[b];
        uint8_t startIdx = (buf.count < CLIENT_SERIAL_BUFFER_SIZE) ? 0 : buf.writeIndex;
        for (uint8_t i = 0; i < buf.count; ++i) {
          uint8_t idx = (startIdx + i) % CLIENT_SERIAL_BUFFER_SIZE;
          SerialLogEntry &entry = buf.entries[idx];
          if (sinceEpoch > 0.0 && entry.timestamp <= sinceEpoch) {
            continue;
          }
          appendCsvLine(entry, buf.clientUid);
          added++;
          if (maxEntries > 0 && added >= maxEntries) {
            break;
          }
        }
        break;
      }
    }
  }

  client.println(F("HTTP/1.1 200 OK"));
  client.println(F("Content-Type: text/csv"));
  client.print(F("Content-Disposition: attachment; filename=\""));
  client.print(source == "client" && clientUid.length() ? clientUid : source);
  client.println(F("-serial.csv\""));
  client.print(F("Content-Length: "));
  client.println(csv.length());
  client.println();
  client.print(csv);
}

static void handleSerialRequestPost(EthernetClient &client, const String &body) {
  DynamicJsonDocument doc(256);
  if (deserializeJson(doc, body)) {
    respondStatus(client, 400, "Invalid JSON");
    return;
  }

  const char *clientUid = doc["client"].as<const char *>();
  if (!clientUid || strlen(clientUid) == 0) {
    respondStatus(client, 400, "Missing client UID");
    return;
  }

  String error;
  SerialRequestResult result = requestClientSerialLogs(clientUid, error);
  if (result == SerialRequestResult::Sent) {
    respondStatus(client, 200, "OK");
    return;
  }

  if (result == SerialRequestResult::Throttled) {
    respondStatus(client, 429, error.length() ? error : F("Request throttled"));
  } else {
    respondStatus(client, 500, error.length() ? error : F("Failed to send request"));
  }
}

static void addServerSerialLog(const char *message, const char *level, const char *source) {
  if (!message || strlen(message) == 0) {
    return;
  }

  SerialLogEntry &entry = gServerSerial.entries[gServerSerial.writeIndex];
  entry.timestamp = currentEpoch();
  strlcpy(entry.message, message, sizeof(entry.message));
  strlcpy(entry.level, level ? level : "info", sizeof(entry.level));
  strlcpy(entry.source, source ? source : "server", sizeof(entry.source));

  gServerSerial.writeIndex = (gServerSerial.writeIndex + 1) % SERVER_SERIAL_BUFFER_SIZE;
  if (gServerSerial.count < SERVER_SERIAL_BUFFER_SIZE) {
    gServerSerial.count++;
  }
}

static ClientSerialBuffer *findOrCreateClientSerialBuffer(const char *clientUid) {
  if (!clientUid || strlen(clientUid) == 0) {
    return nullptr;
  }

  // Search for existing buffer
  for (uint8_t i = 0; i < gClientSerialBufferCount; ++i) {
    if (strcmp(gClientSerialBuffers[i].clientUid, clientUid) == 0) {
      return &gClientSerialBuffers[i];
    }
  }

  // Create new buffer if space available
  if (gClientSerialBufferCount < MAX_CLIENT_SERIAL_LOGS) {
    ClientSerialBuffer &buf = gClientSerialBuffers[gClientSerialBufferCount++];
    memset(&buf, 0, sizeof(ClientSerialBuffer));
    strlcpy(buf.clientUid, clientUid, sizeof(buf.clientUid));
    return &buf;
  }

  return nullptr;
}

static void addClientSerialLog(const char *clientUid, const char *message, double timestamp, const char *level, const char *source) {
  if (!message || strlen(message) == 0) {
    return;
  }

  ClientSerialBuffer *buf = findOrCreateClientSerialBuffer(clientUid);
  if (!buf) {
    return;
  }

  double serverEpoch = currentEpoch();
  SerialLogEntry &entry = buf->entries[buf->writeIndex];
  entry.timestamp = timestamp;
  strlcpy(entry.message, message, sizeof(entry.message));
  strlcpy(entry.level, level ? level : "info", sizeof(entry.level));
  strlcpy(entry.source, source ? source : clientUid, sizeof(entry.source));

  buf->writeIndex = (buf->writeIndex + 1) % CLIENT_SERIAL_BUFFER_SIZE;
  if (buf->count < CLIENT_SERIAL_BUFFER_SIZE) {
    buf->count++;
  }
  buf->lastLogEpoch = timestamp;
  buf->lastAckEpoch = serverEpoch;
  buf->awaitingLogs = false;
  strlcpy(buf->lastAckStatus, "logs_received", sizeof(buf->lastAckStatus));
}

static void handleSerialLog(JsonDocument &doc, double epoch) {
  const char *clientUid = doc["client"] | "";
  if (!clientUid || strlen(clientUid) == 0) {
    return;
  }

  const char *defaultLevel = doc["level"] | "info";
  const char *defaultSource = doc["source"] | clientUid;

  // Handle single log entry or array of entries
  if (doc.containsKey("message")) {
    const char *message = doc["message"] | "";
    if (strlen(message) > 0) {
      const char *level = doc["level"] | defaultLevel;
      const char *source = doc["source"] | defaultSource;
      addClientSerialLog(clientUid, message, epoch, level, source);
    }
  } else if (doc.containsKey("logs")) {
    JsonArray logs = doc["logs"].as<JsonArray>();
    for (JsonVariant v : logs) {
      JsonObject logObj = v.as<JsonObject>();
      const char *message = logObj["message"] | "";
      double ts = logObj["timestamp"] | epoch;
      if (strlen(message) > 0) {
        const char *level = logObj["level"] | defaultLevel;
        const char *source = logObj["source"] | defaultSource;
        addClientSerialLog(clientUid, message, ts, level, source);
      }
    }
  }
}

static void handleSerialAck(JsonDocument &doc, double epoch) {
  const char *clientUid = doc["client"] | "";
  if (!clientUid || strlen(clientUid) == 0) {
    return;
  }

  ClientSerialBuffer *buf = findOrCreateClientSerialBuffer(clientUid);
  if (!buf) {
    return;
  }

  const char *status = doc["status"] | "ack";
  strlcpy(buf->lastAckStatus, status, sizeof(buf->lastAckStatus));
  buf->lastAckEpoch = (epoch > 0.0) ? epoch : currentEpoch();
  
  // If we received an explicit ack, we can clear the awaiting flag
  // unless the status indicates otherwise (e.g. "processing")
  if (strcmp(status, "processing") != 0) {
    buf->awaitingLogs = false;
  }
}

static SerialRequestResult requestClientSerialLogs(const char *clientUid, String &errorMessage) {
  errorMessage = "";

  if (!clientUid || strlen(clientUid) == 0) {
    errorMessage = F("Missing client UID");
    return SerialRequestResult::NotecardFailure;
  }

  double now = currentEpoch();
  ClientSerialBuffer *buf = findOrCreateClientSerialBuffer(clientUid);
  if (buf && buf->awaitingLogs) {
    double sinceLastRequest = (buf->lastRequestEpoch > 0.0) ? (now - buf->lastRequestEpoch) : 0.0;
    if (sinceLastRequest < SERIAL_STALE_SECONDS) {
      errorMessage = F("Client log request already pending");
      return SerialRequestResult::Throttled;
    }
    buf->awaitingLogs = false;
  }

  J *req = notecard.newRequest("note.add");
  if (!req) {
    errorMessage = F("Unable to allocate Notecard request");
    return SerialRequestResult::NotecardFailure;
  }

  char targetFile[80];
  snprintf(targetFile, sizeof(targetFile), "device:%s:%s", clientUid, SERIAL_REQUEST_FILE);
  JAddStringToObject(req, "file", targetFile);
  JAddBoolToObject(req, "sync", true);

  J *body = JCreateObject();
  if (!body) {
    errorMessage = F("Unable to allocate Notecard body");
    return SerialRequestResult::NotecardFailure;
  }

  JAddStringToObject(body, "request", "send_logs");
  JAddNumberToObject(body, "timestamp", now);
  JAddItemToObject(req, "body", body);

  if (!notecard.sendRequest(req)) {
    errorMessage = F("Failed to queue Notecard request");
    return SerialRequestResult::NotecardFailure;
  }

  Serial.print(F("Serial log request sent to client: "));
  Serial.println(clientUid);
  addServerSerialLog("Serial log request sent", "info", "serial");

  if (buf) {
    buf->lastRequestEpoch = now;
    buf->awaitingLogs = true;
    strlcpy(buf->lastAckStatus, "request_sent", sizeof(buf->lastAckStatus));
  }

  return SerialRequestResult::Sent;
}

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 2000) {
    delay(10);
  }

  Serial.println();
  Serial.print(F("Tank Alarm Server 112025 v"));
  Serial.print(F(FIRMWARE_VERSION));
  Serial.print(F(" ("));
  Serial.print(F(FIRMWARE_BUILD_DATE));
  Serial.println(F(")"));

  // Initialize serial log buffers
  memset(&gServerSerial, 0, sizeof(ServerSerialBuffer));
  gClientSerialBufferCount = 0;

  initializeStorage();
  ensureConfigLoaded();
  loadCalibrationData();  // Load calibration learning data
  printHardwareRequirements();

  Wire.begin();
  Wire.setClock(NOTECARD_I2C_FREQUENCY);

  initializeNotecard();
  ensureTimeSync();
  scheduleNextDailyEmail();
  scheduleNextViewerSummary();

  initializeEthernet();
  gWebServer.begin();

  if (gConfig.ftpEnabled && gConfig.ftpRestoreOnBoot) {
    char err[128];
    if (performFtpRestore(err, sizeof(err))) {
      Serial.println(F("FTP restore on boot completed"));
      ensureConfigLoaded();
      loadClientConfigSnapshots();
      loadCalibrationData();
      scheduleNextDailyEmail();
      scheduleNextViewerSummary();
    } else {
      Serial.print(F("FTP restore on boot skipped/failed: "));
      Serial.println(err);
    }
  }

#ifdef WATCHDOG_AVAILABLE
  // Initialize watchdog timer
  #if defined(ARDUINO_OPTA) || defined(ARDUINO_ARCH_MBED)
    uint32_t timeoutMs = WATCHDOG_TIMEOUT_SECONDS * 1000;
    if (mbedWatchdog.start(timeoutMs)) {
      Serial.print(F("Mbed Watchdog enabled: "));
      Serial.print(WATCHDOG_TIMEOUT_SECONDS);
      Serial.println(F(" seconds"));
    } else {
      Serial.println(F("Warning: Watchdog initialization failed"));
    }
  #else
    IWatchdog.begin(WATCHDOG_TIMEOUT_SECONDS * 1000000UL);
    Serial.print(F("Watchdog timer enabled: "));
    Serial.print(WATCHDOG_TIMEOUT_SECONDS);
    Serial.println(F(" seconds"));
  #endif
#else
  Serial.println(F("Warning: Watchdog timer not available on this platform"));
#endif

  Serial.println(F("Server setup complete"));
  Serial.println(F("----------------------------------"));
  Serial.print(F("Local IP Address: "));
  Serial.println(Ethernet.localIP());
  Serial.print(F("Gateway: "));
  Serial.println(Ethernet.gatewayIP());
  Serial.print(F("Subnet Mask: "));
  Serial.println(Ethernet.subnetMask());
  Serial.println(F("----------------------------------"));
  
  addServerSerialLog("Server started", "info", "lifecycle");
}

void loop() {
#ifdef WATCHDOG_AVAILABLE
  // Reset watchdog timer to prevent system reset
  #if defined(ARDUINO_OPTA) || defined(ARDUINO_ARCH_MBED)
    mbedWatchdog.kick();
  #else
    IWatchdog.reload();
  #endif
#endif

  // Maintain DHCP lease and check link status
  Ethernet.maintain();

  // Check for link state changes and display IP when link comes up
  unsigned long now = millis();
  if (now - gLastLinkCheckMillis > 5000UL) {
    gLastLinkCheckMillis = now;
    bool linkUp = (Ethernet.linkStatus() == LinkON);
    if (linkUp && !gLastLinkState) {
      // Link just came up
      Serial.println(F("----------------------------------"));
      Serial.println(F("Network link established!"));
      Serial.print(F("Local IP Address: "));
      Serial.println(Ethernet.localIP());
      Serial.print(F("Gateway: "));
      Serial.println(Ethernet.gatewayIP());
      Serial.print(F("Subnet Mask: "));
      Serial.println(Ethernet.subnetMask());
      Serial.println(F("----------------------------------"));
    } else if (!linkUp && gLastLinkState) {
      // Link just went down
      Serial.println(F("WARNING: Network link lost!"));
    }
    gLastLinkState = linkUp;
  }

  handleWebRequests();

  now = millis();
  if (now - gLastPollMillis > 5000UL) {
    gLastPollMillis = now;
    if (!gPaused) {
      pollNotecard();
    }
  }

  ensureTimeSync();

  if (gNextDailyEmailEpoch > 0.0 && currentEpoch() >= gNextDailyEmailEpoch) {
    sendDailyEmail();
    scheduleNextDailyEmail();
  }

  if (gNextViewerSummaryEpoch > 0.0 && currentEpoch() >= gNextViewerSummaryEpoch) {
    publishViewerSummary();
    scheduleNextViewerSummary();
  }

  if (gConfigDirty) {
    if (saveConfig(gConfig)) {
      gConfigDirty = false;
      if (gConfig.ftpEnabled && gConfig.ftpBackupOnChange) {
        gPendingFtpBackup = true;
      }
    }
  }

  if (gPendingFtpBackup) {
    char error[128];
    bool ok = performFtpBackup(error, sizeof(error));
    gPendingFtpBackup = false;
    if (!ok) {
      Serial.print(F("FTP auto-backup failed: "));
      Serial.println(error);
    }
  }
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
  if (!loadConfig(gConfig)) {
    createDefaultConfig(gConfig);
    saveConfig(gConfig);
    Serial.println(F("Default server configuration created"));
    gPaused = true;  // Start paused on fresh install to allow safe setup/restore
  }

  loadClientConfigSnapshots();
}

static void createDefaultConfig(ServerConfig &cfg) {
  memset(&cfg, 0, sizeof(ServerConfig));
  strlcpy(cfg.serverName, "Tank Alarm Server", sizeof(cfg.serverName));
  strlcpy(cfg.clientFleet, "tankalarm-clients", sizeof(cfg.clientFleet));
  strlcpy(cfg.smsPrimary, "+12223334444", sizeof(cfg.smsPrimary));
  strlcpy(cfg.smsSecondary, "+15556667777", sizeof(cfg.smsSecondary));
  strlcpy(cfg.dailyEmail, "reports@example.com", sizeof(cfg.dailyEmail));
  cfg.configPin[0] = '\0';
  cfg.dailyHour = DAILY_EMAIL_HOUR_DEFAULT;
  cfg.dailyMinute = DAILY_EMAIL_MINUTE_DEFAULT;
  cfg.webRefreshSeconds = 21600;
  cfg.useStaticIp = false;  // Use DHCP by default for easier deployment
  cfg.smsOnHigh = true;
  cfg.smsOnLow = true;
  cfg.smsOnClear = false;
  cfg.ftpEnabled = false;
  cfg.ftpPassive = true;
  cfg.ftpBackupOnChange = false;
  cfg.ftpRestoreOnBoot = false;
  cfg.ftpPort = FTP_PORT_DEFAULT;
  strlcpy(cfg.ftpHost, "", sizeof(cfg.ftpHost));
  strlcpy(cfg.ftpUser, "", sizeof(cfg.ftpUser));
  strlcpy(cfg.ftpPass, "", sizeof(cfg.ftpPass));
  strlcpy(cfg.ftpPath, FTP_PATH_DEFAULT, sizeof(cfg.ftpPath));
}

static bool loadConfig(ServerConfig &cfg) {
#ifdef FILESYSTEM_AVAILABLE
  #if defined(ARDUINO_OPTA) || defined(ARDUINO_ARCH_MBED)
    if (!mbedFS) return false;
    
    FILE *file = fopen("/fs/server_config.json", "r");
    if (!file) {
      return false;
    }
    
    // Read file into buffer
    fseek(file, 0, SEEK_END);
    long fileSize = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    if (fileSize <= 0 || fileSize > 4096) {
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
    
    DynamicJsonDocument doc(2048);
    DeserializationError err = deserializeJson(doc, buffer);
    free(buffer);
  #else
    if (!LittleFS.exists(SERVER_CONFIG_PATH)) {
      return false;
    }

    File file = LittleFS.open(SERVER_CONFIG_PATH, "r");
    if (!file) {
      return false;
    }

    DynamicJsonDocument doc(2048);
    DeserializationError err = deserializeJson(doc, file);
    file.close();
  #endif

  if (err) {
    Serial.println(F("Server config parse failed"));
    return false;
  }

  memset(&cfg, 0, sizeof(ServerConfig));

  strlcpy(cfg.serverName, doc["serverName"].as<const char *>() ? doc["serverName"].as<const char *>() : "Tank Alarm Server", sizeof(cfg.serverName));
  strlcpy(cfg.clientFleet, doc["clientFleet"].as<const char *>() ? doc["clientFleet"].as<const char *>() : "tankalarm-clients", sizeof(cfg.clientFleet));
  strlcpy(cfg.smsPrimary, doc["smsPrimary"].as<const char *>() ? doc["smsPrimary"].as<const char *>() : "+12223334444", sizeof(cfg.smsPrimary));
  strlcpy(cfg.smsSecondary, doc["smsSecondary"].as<const char *>() ? doc["smsSecondary"].as<const char *>() : "+15556667777", sizeof(cfg.smsSecondary));
  strlcpy(cfg.dailyEmail, doc["dailyEmail"].as<const char *>() ? doc["dailyEmail"].as<const char *>() : "reports@example.com", sizeof(cfg.dailyEmail));
  if (doc["configPin"].as<const char *>() && isValidPin(doc["configPin"].as<const char *>())) {
    strlcpy(cfg.configPin, doc["configPin"].as<const char *>(), sizeof(cfg.configPin));
  } else {
    cfg.configPin[0] = '\0';
  }
  cfg.dailyHour = doc["dailyHour"].is<uint8_t>() ? doc["dailyHour"].as<uint8_t>() : DAILY_EMAIL_HOUR_DEFAULT;
  cfg.dailyMinute = doc["dailyMinute"].is<uint8_t>() ? doc["dailyMinute"].as<uint8_t>() : DAILY_EMAIL_MINUTE_DEFAULT;
  if (doc["webRefreshSeconds"].is<uint16_t>() || doc["webRefreshSeconds"].is<uint32_t>()) {
    cfg.webRefreshSeconds = doc["webRefreshSeconds"].as<uint16_t>();
  } else {
    cfg.webRefreshSeconds = 21600;
  }
  cfg.useStaticIp = doc["useStaticIp"].is<bool>() ? doc["useStaticIp"].as<bool>() : false;
  cfg.smsOnHigh = doc["smsOnHigh"].is<bool>() ? doc["smsOnHigh"].as<bool>() : true;
  cfg.smsOnLow = doc["smsOnLow"].is<bool>() ? doc["smsOnLow"].as<bool>() : true;
  cfg.smsOnClear = doc["smsOnClear"].is<bool>() ? doc["smsOnClear"].as<bool>() : false;

  JsonObject ftpObj = doc["ftp"].as<JsonObject>();
  cfg.ftpEnabled = ftpObj ? (ftpObj["enabled"].is<bool>() ? ftpObj["enabled"].as<bool>() : false) : (doc["ftpEnabled"].is<bool>() ? doc["ftpEnabled"].as<bool>() : false);
  cfg.ftpPassive = ftpObj ? (ftpObj["passive"].is<bool>() ? ftpObj["passive"].as<bool>() : true) : true;
  cfg.ftpBackupOnChange = ftpObj ? (ftpObj["backupOnChange"].is<bool>() ? ftpObj["backupOnChange"].as<bool>() : false) : (doc["ftpBackupOnChange"].is<bool>() ? doc["ftpBackupOnChange"].as<bool>() : false);
  cfg.ftpRestoreOnBoot = ftpObj ? (ftpObj["restoreOnBoot"].is<bool>() ? ftpObj["restoreOnBoot"].as<bool>() : false) : (doc["ftpRestoreOnBoot"].is<bool>() ? doc["ftpRestoreOnBoot"].as<bool>() : false);
  cfg.ftpPort = ftpObj ? (ftpObj["port"].is<uint16_t>() ? ftpObj["port"].as<uint16_t>() : FTP_PORT_DEFAULT) : (doc["ftpPort"].is<uint16_t>() ? doc["ftpPort"].as<uint16_t>() : FTP_PORT_DEFAULT);
  if (ftpObj && ftpObj.containsKey("host")) {
    strlcpy(cfg.ftpHost, ftpObj["host"], sizeof(cfg.ftpHost));
  }
  if (ftpObj && ftpObj.containsKey("user")) {
    strlcpy(cfg.ftpUser, ftpObj["user"], sizeof(cfg.ftpUser));
  }
  if (ftpObj && ftpObj.containsKey("pass")) {
    strlcpy(cfg.ftpPass, ftpObj["pass"], sizeof(cfg.ftpPass));
  }
  if (ftpObj && ftpObj.containsKey("path")) {
    strlcpy(cfg.ftpPath, ftpObj["path"], sizeof(cfg.ftpPath));
  }
  if ((!ftpObj || !ftpObj.containsKey("host")) && doc["ftpHost"].as<const char *>()) {
    strlcpy(cfg.ftpHost, doc["ftpHost"], sizeof(cfg.ftpHost));
  }
  if ((!ftpObj || !ftpObj.containsKey("user")) && doc["ftpUser"].as<const char *>()) {
    strlcpy(cfg.ftpUser, doc["ftpUser"], sizeof(cfg.ftpUser));
  }
  if ((!ftpObj || !ftpObj.containsKey("pass")) && doc["ftpPass"].as<const char *>()) {
    strlcpy(cfg.ftpPass, doc["ftpPass"], sizeof(cfg.ftpPass));
  }
  if ((!ftpObj || !ftpObj.containsKey("path")) && doc["ftpPath"].as<const char *>()) {
    strlcpy(cfg.ftpPath, doc["ftpPath"], sizeof(cfg.ftpPath));
  }

  if (doc.containsKey("staticIp")) {
    JsonArrayConst ip = doc["staticIp"].as<JsonArrayConst>();
    if (ip.size() == 4) {
      gStaticIp = IPAddress(ip[0], ip[1], ip[2], ip[3]);
    }
  }
  if (doc.containsKey("gateway")) {
    JsonArrayConst gw = doc["gateway"].as<JsonArrayConst>();
    if (gw.size() == 4) {
      gStaticGateway = IPAddress(gw[0], gw[1], gw[2], gw[3]);
    }
  }
  if (doc.containsKey("subnet")) {
    JsonArrayConst sn = doc["subnet"].as<JsonArrayConst>();
    if (sn.size() == 4) {
      gStaticSubnet = IPAddress(sn[0], sn[1], sn[2], sn[3]);
    }
  }
  if (doc.containsKey("dns")) {
    JsonArrayConst dns = doc["dns"].as<JsonArrayConst>();
    if (dns.size() == 4) {
      gStaticDns = IPAddress(dns[0], dns[1], dns[2], dns[3]);
    }
  }

  return true;
#else
  return false; // Filesystem not available
#endif
}

static bool saveConfig(const ServerConfig &cfg) {
#ifdef FILESYSTEM_AVAILABLE
  #if defined(ARDUINO_OPTA) || defined(ARDUINO_ARCH_MBED)
    if (!mbedFS) return false;
  #endif
  
  DynamicJsonDocument doc(2048);
  doc["serverName"] = cfg.serverName;
  doc["clientFleet"] = cfg.clientFleet;
  doc["smsPrimary"] = cfg.smsPrimary;
  doc["smsSecondary"] = cfg.smsSecondary;
  doc["dailyEmail"] = cfg.dailyEmail;
  doc["configPin"] = cfg.configPin;
  doc["dailyHour"] = cfg.dailyHour;
  doc["dailyMinute"] = cfg.dailyMinute;
  doc["webRefreshSeconds"] = cfg.webRefreshSeconds;
  doc["useStaticIp"] = cfg.useStaticIp;
  doc["smsOnHigh"] = cfg.smsOnHigh;
  doc["smsOnLow"] = cfg.smsOnLow;
  doc["smsOnClear"] = cfg.smsOnClear;

  JsonObject ftp = doc.createNestedObject("ftp");
  ftp["enabled"] = cfg.ftpEnabled;
  ftp["passive"] = cfg.ftpPassive;
  ftp["backupOnChange"] = cfg.ftpBackupOnChange;
  ftp["restoreOnBoot"] = cfg.ftpRestoreOnBoot;
  ftp["port"] = cfg.ftpPort;
  ftp["host"] = cfg.ftpHost;
  ftp["user"] = cfg.ftpUser;
  ftp["pass"] = cfg.ftpPass;
  ftp["path"] = cfg.ftpPath;

  JsonArray ip = doc.createNestedArray("staticIp");
  ip.add(gStaticIp[0]);
  ip.add(gStaticIp[1]);
  ip.add(gStaticIp[2]);
  ip.add(gStaticIp[3]);

  JsonArray gw = doc.createNestedArray("gateway");
  gw.add(gStaticGateway[0]);
  gw.add(gStaticGateway[1]);
  gw.add(gStaticGateway[2]);
  gw.add(gStaticGateway[3]);

  JsonArray sn = doc.createNestedArray("subnet");
  sn.add(gStaticSubnet[0]);
  sn.add(gStaticSubnet[1]);
  sn.add(gStaticSubnet[2]);
  sn.add(gStaticSubnet[3]);

  JsonArray dns = doc.createNestedArray("dns");
  dns.add(gStaticDns[0]);
  dns.add(gStaticDns[1]);
  dns.add(gStaticDns[2]);
  dns.add(gStaticDns[3]);

  #if defined(ARDUINO_OPTA) || defined(ARDUINO_ARCH_MBED)
    // Mbed OS file operations
    FILE *file = fopen("/fs/server_config.json", "w");
    if (!file) {
      Serial.println(F("Failed to open server config for write"));
      return false;
    }
    
    // Serialize to buffer first, then write
    String jsonStr;
    size_t len = serializeJson(doc, jsonStr);
    if (len == 0) {
      fclose(file);
      Serial.println(F("Failed to serialize server config"));
      return false;
    }
    
    size_t written = fwrite(jsonStr.c_str(), 1, jsonStr.length(), file);
    fclose(file);
    if (written != jsonStr.length()) {
      Serial.println(F("Failed to write server config (incomplete)"));
      return false;
    }
    return true;
  #else
    File file = LittleFS.open(SERVER_CONFIG_PATH, "w");
    if (!file) {
      Serial.println(F("Failed to open server config for write"));
      return false;
    }

    if (serializeJson(doc, file) == 0) {
      file.close();
      Serial.println(F("Failed to serialize server config"));
      return false;
    }

    file.close();
    return true;
  #endif
#else
  return false; // Filesystem not available
#endif
}

// ---------------------------------------------------------------------------
// FTP backup/restore helpers
// ---------------------------------------------------------------------------

struct FtpSession {
  EthernetClient ctrl;
};

struct BackupFileEntry {
  const char *localPath;
  const char *remoteName;
};

static const BackupFileEntry kBackupFiles[] = {
  { SERVER_CONFIG_PATH, "server_config.json" },
  { CLIENT_CONFIG_CACHE_PATH, "client_config_cache.txt" },
  { CALIBRATION_LOG_PATH, "calibration_log.txt" },
  { "/calibration_data.txt", "calibration_data.txt" }
};

static void buildLocalPath(const char *relativePath, char *out, size_t outSize) {
  if (!relativePath || !out || outSize == 0) {
    return;
  }
#if defined(ARDUINO_OPTA) || defined(ARDUINO_ARCH_MBED)
  if (strncmp(relativePath, "/fs", 3) == 0) {
    strlcpy(out, relativePath, outSize);
  } else {
    snprintf(out, outSize, "/fs%s", (relativePath[0] == '/') ? relativePath : "/");
  }
#else
  strlcpy(out, relativePath, outSize);
#endif
}

static bool readFileToBuffer(const char *relativePath, char *out, size_t outMax, size_t &outLen) {
#ifndef FILESYSTEM_AVAILABLE
  return false;
#else
  char fullPath[96];
  buildLocalPath(relativePath, fullPath, sizeof(fullPath));

#if defined(ARDUINO_OPTA) || defined(ARDUINO_ARCH_MBED)
  FILE *file = fopen(fullPath, "rb");
  if (!file) {
    return false;
  }

  fseek(file, 0, SEEK_END);
  long fileSize = ftell(file);
  fseek(file, 0, SEEK_SET);

  if (fileSize < 0 || fileSize >= (long)outMax) {
    fclose(file);
    return false;
  }

  outLen = fread(out, 1, fileSize, file);
  out[outLen] = 0;
  fclose(file);
  return true;
#else
  if (!LittleFS.exists(fullPath)) {
    return false;
  }

  File file = LittleFS.open(fullPath, "r");
  if (!file) {
    return false;
  }

  size_t fileSize = file.size();
  if (fileSize >= outMax) {
    file.close();
    return false;
  }

  outLen = file.read((uint8_t*)out, fileSize);
  out[outLen] = 0;
  file.close();
  return true;
#endif
#endif
}

static bool writeBufferToFile(const char *relativePath, const uint8_t *data, size_t len) {
#ifndef FILESYSTEM_AVAILABLE
  (void)relativePath;
  (void)data;
  (void)len;
  return false;
#else
  if (!data || len > FTP_MAX_FILE_BYTES) {
    return false;
  }

  char fullPath[96];
  buildLocalPath(relativePath, fullPath, sizeof(fullPath));

#if defined(ARDUINO_OPTA) || defined(ARDUINO_ARCH_MBED)
  FILE *file = fopen(fullPath, "wb");
  if (!file) {
    return false;
  }
  size_t written = fwrite(data, 1, len, file);
  fclose(file);
  return written == len;
#else
  File file = LittleFS.open(fullPath, "w");
  if (!file) {
    return false;
  }
  size_t written = file.write(data, len);
  file.close();
  return written == len;
#endif
#endif
}

static bool ftpReadResponse(EthernetClient &client, int &code, char *message, size_t maxLen, uint32_t timeoutMs = FTP_TIMEOUT_MS) {
  if (maxLen > 0) message[0] = '\0';
  char line[128];
  size_t linePos = 0;
  unsigned long start = millis();
  int multilineCode = -1;

  while (millis() - start < timeoutMs) {
    while (client.available()) {
      char c = client.read();
      if (c == '\r') {
        continue;
      }
      if (c == '\n') {
        line[linePos] = '\0';
        
        if (linePos >= 3 && isdigit(line[0]) && isdigit(line[1]) && isdigit(line[2])) {
          char codeStr[4] = {line[0], line[1], line[2], '\0'};
          int thisCode = atoi(codeStr);
          
          if (linePos > 3 && line[3] == '-') {
            multilineCode = thisCode;
            // Append to message if space allows
            size_t currentLen = strlen(message);
            if (currentLen + linePos + 2 < maxLen) {
              strcat(message, line);
              strcat(message, "\n");
            }
          } else if (multilineCode == -1 || thisCode == multilineCode) {
            code = thisCode;
            // Append last line
            size_t currentLen = strlen(message);
            if (currentLen + linePos + 1 < maxLen) {
              strcat(message, line);
            }
            return true;
          }
        }
        linePos = 0;
      } else {
        if (linePos < sizeof(line) - 1) {
          line[linePos++] = c;
        }
      }
    }
    delay(5);
  }
  return false;
}

static bool ftpSendCommand(FtpSession &session, const char *command, int &code, char *message, size_t maxLen) {
  session.ctrl.print(command);
  session.ctrl.print("\r\n");
  return ftpReadResponse(session.ctrl, code, message, maxLen);
}

static bool ftpBackupClientConfigs(FtpSession &session, char *error, size_t errorSize, uint8_t &uploadedFiles);
static bool ftpRestoreClientConfigs(FtpSession &session, char *error, size_t errorSize, uint8_t &restoredFiles);
static bool ftpConnectAndLogin(FtpSession &session, char *error, size_t errorSize) {
  if (!gConfig.ftpEnabled || strlen(gConfig.ftpHost) == 0) {
    snprintf(error, errorSize, "FTP disabled or host missing");
    return false;
  }

  if (!session.ctrl.connect(gConfig.ftpHost, gConfig.ftpPort)) {
    snprintf(error, errorSize, "FTP connect failed");
    return false;
  }

  int code = 0;
  char msg[128];
  if (!ftpReadResponse(session.ctrl, code, msg, sizeof(msg)) || code >= 400) {
    snprintf(error, errorSize, "No welcome banner");
    return false;
  }

  char cmdBuffer[96];
  snprintf(cmdBuffer, sizeof(cmdBuffer), "USER %s", (strlen(gConfig.ftpUser) > 0) ? gConfig.ftpUser : "anonymous");
  if (!ftpSendCommand(session, cmdBuffer, code, msg, sizeof(msg)) || (code != 230 && code != 331)) {
    snprintf(error, errorSize, "USER rejected");
    return false;
  }
  if (code == 331) {
    snprintf(cmdBuffer, sizeof(cmdBuffer), "PASS %s", (strlen(gConfig.ftpPass) > 0) ? gConfig.ftpPass : "guest");
    if (!ftpSendCommand(session, cmdBuffer, code, msg, sizeof(msg)) || code != 230) {
      snprintf(error, errorSize, "PASS rejected");
      return false;
    }
  }

  if (!ftpSendCommand(session, "TYPE I", code, msg, sizeof(msg)) || code >= 400) {
    snprintf(error, errorSize, "TYPE failed");
    return false;
  }

  return true;
}

static bool ftpEnterPassive(FtpSession &session, IPAddress &dataHost, uint16_t &dataPort, char *error, size_t errorSize) {
  if (!gConfig.ftpPassive) {
    snprintf(error, errorSize, "Only passive FTP supported");
    return false;
  }
  int code = 0;
  char msg[128];
  if (!ftpSendCommand(session, "PASV", code, msg, sizeof(msg)) || code >= 400) {
    snprintf(error, errorSize, "PASV failed");
    return false;
  }

  int parts[6] = {0};
  int idx = 0;
  size_t len = strlen(msg);
  for (size_t i = 0; i < len && idx < 6; ++i) {
    if (isdigit(msg[i])) {
      parts[idx] = parts[idx] * 10 + (msg[i] - '0');
    } else if (msg[i] == ',' || msg[i] == ')') {
      idx++;
    }
  }

  if (idx < 6) {
    snprintf(error, errorSize, "PASV parse error");
    return false;
  }

  dataHost = IPAddress(parts[0], parts[1], parts[2], parts[3]);
  dataPort = (parts[4] << 8) | parts[5];
  return true;
}

static void buildRemotePath(char *out, size_t outLen, const char *fileName) {
  const char *base = (strlen(gConfig.ftpPath) > 0) ? gConfig.ftpPath : FTP_PATH_DEFAULT;
  const char *uid = (strlen(gServerUid) > 0) ? gServerUid : "server";
  bool baseHasSlash = base[strlen(base) - 1] == '/';
  snprintf(out, outLen, "%s%s%s/%s", base, baseHasSlash ? "" : "/", uid, fileName);
}

static bool ftpStoreBuffer(FtpSession &session, const char *remoteFile, const uint8_t *data, size_t len, char *error, size_t errorSize) {
  if (!data || len == 0) {
    snprintf(error, errorSize, "No data to upload");
    return false;
  }

  IPAddress dataHost;
  uint16_t dataPort = 0;
  if (!ftpEnterPassive(session, dataHost, dataPort, error, errorSize)) {
    return false;
  }

  EthernetClient dataClient;
  if (!dataClient.connect(dataHost, dataPort)) {
    snprintf(error, errorSize, "Data connect failed");
    return false;
  }

  char cmd[160];
  snprintf(cmd, sizeof(cmd), "STOR %s", remoteFile);
  int code = 0;
  char msg[128];
  if (!ftpSendCommand(session, cmd, code, msg, sizeof(msg)) || code >= 400) {
    snprintf(error, errorSize, "STOR rejected");
    dataClient.stop();
    return false;
  }

  size_t written = dataClient.write(data, len);
  dataClient.stop();
  if (written != len) {
    snprintf(error, errorSize, "Short write");
    return false;
  }

  if (!ftpReadResponse(session.ctrl, code, msg, sizeof(msg)) || code >= 400) {
    snprintf(error, errorSize, "STOR completion failed");
    return false;
  }

  return true;
}

static bool ftpRetrieveBuffer(FtpSession &session, const char *remoteFile, char *out, size_t outMax, size_t &outLen, char *error, size_t errorSize) {
  IPAddress dataHost;
  uint16_t dataPort = 0;
  if (!ftpEnterPassive(session, dataHost, dataPort, error, errorSize)) {
    return false;
  }

  EthernetClient dataClient;
  if (!dataClient.connect(dataHost, dataPort)) {
    snprintf(error, errorSize, "Data connect failed");
    return false;
  }

  char cmd[160];
  snprintf(cmd, sizeof(cmd), "RETR %s", remoteFile);
  int code = 0;
  char msg[128];
  if (!ftpSendCommand(session, cmd, code, msg, sizeof(msg)) || code >= 400) {
    snprintf(error, errorSize, "RETR rejected");
    dataClient.stop();
    return false;
  }

  outLen = 0;
  unsigned long start = millis();
  while (millis() - start < FTP_TIMEOUT_MS) {
    while (dataClient.available()) {
      int c = dataClient.read();
      if (c != -1) {
        if (outLen < outMax - 1) {
          out[outLen++] = (char)c;
        } else {
          snprintf(error, errorSize, "File too large");
          dataClient.stop();
          return false;
        }
      }
    }
    if (!dataClient.connected()) {
      break;
    }
    delay(2);
  }
  out[outLen] = 0;
  dataClient.stop();

  if (!ftpReadResponse(session.ctrl, code, msg, sizeof(msg)) || code >= 400) {
    snprintf(error, errorSize, "RETR completion failed");
    return false;
  }

  return true;
}

static void ftpQuit(FtpSession &session) {
  if (session.ctrl.connected()) {
    int code = 0;
    char msg[64];
    ftpSendCommand(session, "QUIT", code, msg, sizeof(msg));
    session.ctrl.stop();
  }
}

// Upload per-client configs (from in-memory snapshots) as individual files plus a manifest.
static bool ftpBackupClientConfigs(FtpSession &session, char *error, size_t errorSize, uint8_t &uploadedFiles) {
  uploadedFiles = 0;
  if (gClientConfigCount == 0) {
    return true;  // Nothing to do
  }

  // Build and upload manifest listing client UIDs (and optional site for readability)
  char manifest[2048];
  manifest[0] = 0;
  
  for (uint8_t i = 0; i < gClientConfigCount; ++i) {
    strncat(manifest, gClientConfigs[i].uid, sizeof(manifest) - strlen(manifest) - 1);
    strncat(manifest, "\t", sizeof(manifest) - strlen(manifest) - 1);
    strncat(manifest, gClientConfigs[i].site, sizeof(manifest) - strlen(manifest) - 1);
    strncat(manifest, "\n", sizeof(manifest) - strlen(manifest) - 1);
  }

  char manifestPath[192];
  buildRemotePath(manifestPath, sizeof(manifestPath), "clients_manifest.txt");
  if (!ftpStoreBuffer(session, manifestPath, (const uint8_t *)manifest, strlen(manifest), error, errorSize)) {
    return false;
  }
  uploadedFiles++;
  Serial.println(F("FTP backup: clients_manifest.txt"));

  // Upload each cached client config as its own file
  for (uint8_t i = 0; i < gClientConfigCount; ++i) {
    ClientConfigSnapshot &snap = gClientConfigs[i];
    size_t len = strlen(snap.payload);
    if (len == 0 || len > FTP_MAX_FILE_BYTES) {
      continue;  // Skip empty/oversized
    }

    char remotePath[192];
    char fileName[96];
    snprintf(fileName, sizeof(fileName), "clients/%s.json", snap.uid);
    buildRemotePath(remotePath, sizeof(remotePath), fileName);

    if (ftpStoreBuffer(session, remotePath, (const uint8_t *)snap.payload, len, error, errorSize)) {
      uploadedFiles++;
      Serial.print(F("FTP backup client config: "));
      Serial.println(remotePath);
    }
  }

  return true;
}

// Download per-client configs if present and rebuild the cache file locally.
static bool ftpRestoreClientConfigs(FtpSession &session, char *error, size_t errorSize, uint8_t &restoredFiles) {
  restoredFiles = 0;

  char manifestPath[192];
  buildRemotePath(manifestPath, sizeof(manifestPath), "clients_manifest.txt");

  char manifest[2048];
  size_t manifestLen = 0;
  if (!ftpRetrieveBuffer(session, manifestPath, manifest, sizeof(manifest), manifestLen, error, errorSize)) {
    // Manifest not found; treat as optional and succeed silently.
    return true;
  }

  // Open cache file for writing (truncate)
#if defined(ARDUINO_OPTA) || defined(ARDUINO_ARCH_MBED)
  FILE *cacheFile = fopen("/fs/client_config_cache.txt", "w");
  if (!cacheFile) {
      snprintf(error, errorSize, "Failed to open cache file");
      return false;
  }
#else
  if (LittleFS.exists(CLIENT_CONFIG_CACHE_PATH)) {
      LittleFS.remove(CLIENT_CONFIG_CACHE_PATH);
  }
  File cacheFile = LittleFS.open(CLIENT_CONFIG_CACHE_PATH, "w");
  if (!cacheFile) {
      snprintf(error, errorSize, "Failed to open cache file");
      return false;
  }
#endif

  // Parse manifest lines: uid \t site
  char *lineStart = manifest;
  char *manifestEnd = manifest + manifestLen;
  
  while (lineStart < manifestEnd) {
    char *lineEnd = strchr(lineStart, '\n');
    if (!lineEnd) lineEnd = manifestEnd;
    
    char savedChar = *lineEnd;
    *lineEnd = 0;
    
    char *line = lineStart;
    while(isspace(*line)) line++;
    
    if (*line == 0) {
        *lineEnd = savedChar;
        lineStart = lineEnd + 1;
        continue;
    }

    char *sep = strchr(line, '\t');
    if (sep) *sep = 0; 
    
    char *uid = line;
    // Trim UID
    char *p = uid + strlen(uid) - 1;
    while(p >= uid && isspace(*p)) *p-- = 0;

    if (strlen(uid) > 0) {
        char remotePath[192];
        char fileName[96];
        snprintf(fileName, sizeof(fileName), "clients/%s.json", uid);
        buildRemotePath(remotePath, sizeof(remotePath), fileName);
        
        // Use 4KB buffer to accommodate larger client configs (was 1KB, risked truncation)
        char cfg[4096];
        size_t cfgLen = 0;
        if (ftpRetrieveBuffer(session, remotePath, cfg, sizeof(cfg), cfgLen, error, errorSize)) {
            // Trim cfg
            char *cStart = cfg;
            while(*cStart && isspace(*cStart)) cStart++;
            
            char *cEnd = cfg + cfgLen - 1;
            while(cEnd >= cStart && isspace(*cEnd)) *cEnd-- = 0;
            
            if (strlen(cStart) > 0) {
#if defined(ARDUINO_OPTA) || defined(ARDUINO_ARCH_MBED)
                fprintf(cacheFile, "%s\t%s\n", uid, cStart);
#else
                cacheFile.print(uid);
                cacheFile.print('\t');
                cacheFile.print(cStart);
                cacheFile.print('\n');
#endif
                restoredFiles++;
                Serial.print(F("FTP restore client config: "));
                Serial.println(remotePath);
            }
        }
    }
    
    if (sep) *sep = '\t';
    *lineEnd = savedChar;
    lineStart = lineEnd + 1;
  }
  
#if defined(ARDUINO_OPTA) || defined(ARDUINO_ARCH_MBED)
  fclose(cacheFile);
#else
  cacheFile.close();
#endif
  return true;
}

static bool performFtpBackup(char *errorOut, size_t errorSize) {
  if (!gConfig.ftpEnabled) {
    if (errorOut) snprintf(errorOut, errorSize, "FTP disabled");
    return false;
  }

  FtpSession session;
  char err[128];
  if (!ftpConnectAndLogin(session, err, sizeof(err))) {
    if (errorOut) snprintf(errorOut, errorSize, "%s", err);
    return false;
  }

  uint8_t uploaded = 0;
  for (size_t i = 0; i < sizeof(kBackupFiles) / sizeof(kBackupFiles[0]); ++i) {
    const BackupFileEntry &entry = kBackupFiles[i];
    char contents[2048];
    size_t len = 0;
    if (!readFileToBuffer(entry.localPath, contents, sizeof(contents), len)) {
      continue;  // Missing or too large; skip quietly
    }

    char remotePath[192];
    buildRemotePath(remotePath, sizeof(remotePath), entry.remoteName);
    if (ftpStoreBuffer(session, remotePath, (const uint8_t *)contents, len, err, sizeof(err))) {
      uploaded++;
      Serial.print(F("FTP backup: "));
      Serial.println(remotePath);
    } else {
      Serial.print(F("FTP upload failed for "));
      Serial.println(remotePath);
    }
  }

  // Also back up per-client cached configs (manifest + per-uid JSON)
  uint8_t clientUploaded = 0;
  ftpBackupClientConfigs(session, err, sizeof(err), clientUploaded);
  uploaded += clientUploaded;

  ftpQuit(session);

  if (uploaded == 0) {
    if (errorOut) snprintf(errorOut, errorSize, "No files uploaded");
    return false;
  }

  return true;
}

static bool performFtpRestore(char *errorOut, size_t errorSize) {
  if (!gConfig.ftpEnabled) {
    if (errorOut) snprintf(errorOut, errorSize, "FTP disabled");
    return false;
  }

  FtpSession session;
  char err[128];
  if (!ftpConnectAndLogin(session, err, sizeof(err))) {
    if (errorOut) snprintf(errorOut, errorSize, "%s", err);
    return false;
  }

  uint8_t restored = 0;
  for (size_t i = 0; i < sizeof(kBackupFiles) / sizeof(kBackupFiles[0]); ++i) {
    const BackupFileEntry &entry = kBackupFiles[i];
    char contents[2048];
    size_t len = 0;

    char remotePath[192];
    buildRemotePath(remotePath, sizeof(remotePath), entry.remoteName);
    if (!ftpRetrieveBuffer(session, remotePath, contents, sizeof(contents), len, err, sizeof(err))) {
      continue;
    }

    if (writeBufferToFile(entry.localPath, (const uint8_t *)contents, len)) {
      restored++;
      Serial.print(F("FTP restore: "));
      Serial.println(entry.localPath);
    }
  }

  // Attempt to restore per-client cached configs (optional)
  uint8_t clientRestored = 0;
  ftpRestoreClientConfigs(session, err, sizeof(err), clientRestored);
  restored += clientRestored;

  ftpQuit(session);

  if (restored == 0) {
    if (errorOut) snprintf(errorOut, errorSize, "No files restored");
    return false;
  }

  return true;
}

static void printHardwareRequirements() {
  Serial.println(F("--- Hardware Requirements ---"));
  Serial.println(F("Base: Arduino Opta Lite"));
  Serial.println(F("Networking: Integrated Ethernet (static or DHCP)"));
  Serial.println(F("Cellular relay: Blues Notecard (I2C 0x17)"));
  Serial.println(F("Storage: LittleFS internal flash for configuration"));
  Serial.println(F("-----------------------------"));
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

  req = notecard.newRequest("hub.set");
  if (req) {
    JAddStringToObject(req, "product", SERVER_PRODUCT_UID);
    JAddStringToObject(req, "mode", "continuous");
    notecard.sendRequest(req);
  }

  req = notecard.newRequest("card.uuid");
  J *rsp = notecard.requestAndResponse(req);
  if (rsp) {
    const char *uid = JGetString(rsp, "uuid");
    if (uid) {
      strlcpy(gServerUid, uid, sizeof(gServerUid));
    }
    notecard.deleteResponse(rsp);
  }

  Serial.print(F("Server Notecard UID: " ));
  Serial.println(gServerUid);
}

static void ensureTimeSync() {
  if (gLastSyncedEpoch <= 0.0 || (uint32_t)(millis() - gLastSyncMillis) > 6UL * 60UL * 60UL * 1000UL) {
    J *req = notecard.newRequest("card.time");
    if (!req) {
      return;
    }
    JAddStringToObject(req, "mode", "auto");
    J *rsp = notecard.requestAndResponse(req);
    if (!rsp) {
      return;
    }
    // Check for error response (e.g., "time is not yet set {no-time}")
    // This is normal during startup before Notecard syncs with cloud
    const char *err = JGetString(rsp, "err");
    if (err && strlen(err) > 0) {
      // Time not yet available - this is expected during startup
      // Will retry on next call
      notecard.deleteResponse(rsp);
      return;
    }
    double time = JGetNumber(rsp, "time");
    if (time > 0) {
      gLastSyncedEpoch = time;
      gLastSyncMillis = millis();
    }
    notecard.deleteResponse(rsp);
  }
}

static double currentEpoch() {
  if (gLastSyncedEpoch <= 0.0) {
    return 0.0;
  }
  uint32_t delta = (uint32_t)(millis() - gLastSyncMillis);  // Handles millis() rollover
  return gLastSyncedEpoch + (double)delta / 1000.0;
}

static void scheduleNextDailyEmail() {
  double epoch = currentEpoch();
  if (epoch <= 0.0) {
    gNextDailyEmailEpoch = 0.0;
    return;
  }
  double dayStart = floor(epoch / 86400.0) * 86400.0;
  double scheduled = dayStart + (double)gConfig.dailyHour * 3600.0 + (double)gConfig.dailyMinute * 60.0;
  if (scheduled <= epoch) {
    scheduled += 86400.0;
  }
  gNextDailyEmailEpoch = scheduled;
}

static double computeNextAlignedEpoch(double epoch, uint8_t baseHour, uint32_t intervalSeconds) {
  if (epoch <= 0.0 || intervalSeconds == 0) {
    return 0.0;
  }
  double aligned = floor(epoch / 86400.0) * 86400.0 + (double)baseHour * 3600.0;
  while (aligned <= epoch) {
    aligned += (double)intervalSeconds;
  }
  return aligned;
}

static void scheduleNextViewerSummary() {
  double epoch = currentEpoch();
  gNextViewerSummaryEpoch = computeNextAlignedEpoch(epoch, VIEWER_SUMMARY_BASE_HOUR, VIEWER_SUMMARY_INTERVAL_SECONDS);
}

static void initializeEthernet() {
  Serial.print(F("Initializing Ethernet..."));
  
  // Check if Ethernet hardware is present
  if (Ethernet.hardwareStatus() == EthernetNoHardware) {
    Serial.println(F(" FAILED - No Ethernet hardware detected!"));
    Serial.println(F("ERROR: Cannot continue without Ethernet. Please check hardware."));
    while (true) {
      delay(1000);
    }
  }
  
  int status;
  if (gConfig.useStaticIp) {
    status = Ethernet.begin(gMacAddress, gStaticIp, gStaticDns, gStaticGateway, gStaticSubnet);
  } else {
    status = Ethernet.begin(gMacAddress);
  }

  if (status == 0) {
    Serial.println(F(" FAILED - Could not configure Ethernet!"));
    if (!gConfig.useStaticIp) {
      Serial.println(F("ERROR: DHCP failed. Check network cable and DHCP server."));
    } else {
      Serial.println(F("ERROR: Static IP configuration failed."));
    }
    while (true) {
      delay(1000);
    }
  }
  
  // Check link status
  if (Ethernet.linkStatus() == LinkOFF) {
    Serial.println(F(" WARNING - No network cable connected!"));
    Serial.println(F("Continuing, but web server will not be accessible."));
  } else {
    Serial.println(F(" ok"));
    Serial.print(F("IP Address: "));
    Serial.println(Ethernet.localIP());
    Serial.print(F("Gateway: "));
    Serial.println(Ethernet.gatewayIP());
    Serial.print(F("Subnet: "));
    Serial.println(Ethernet.subnetMask());
  }
}

static void handleWebRequests() {
  EthernetClient client = gWebServer.available();
  if (!client) {
    return;
  }

  String method;
  String path;
  String body;
  size_t contentLength = 0;
  bool bodyTooLarge = false;

  if (!readHttpRequest(client, method, path, body, contentLength, bodyTooLarge)) {
    respondStatus(client, 400, "Bad Request");
    client.stop();
    return;
  }

  if (bodyTooLarge) {
    respondStatus(client, 413, "Payload Too Large");
    client.stop();
    return;
  }

  if (method == "GET" && path == "/") {
    sendDashboard(client);
  } else if (method == "GET" && path == "/client-console") {
    sendClientConsole(client);
  } else if (method == "GET" && path == "/config-generator") {
    sendConfigGenerator(client);
  } else if (method == "GET" && path == "/serial-monitor") {
    sendSerialMonitor(client);
  } else if (method == "GET" && path == "/calibration") {
    sendCalibrationPage(client);
  } else if (method == "GET" && path == "/contacts") {
    sendContactsManager(client);
  } else if (method == "GET" && path == "/server-settings") {
    sendServerSettings(client);
  } else if (method == "GET" && path == "/api/tanks") {
    sendTankJson(client);
  } else if (method == "GET" && path == "/api/clients") {
    sendClientDataJson(client);
  } else if (method == "GET" && path == "/api/calibration") {
    handleCalibrationGet(client);
  } else if (method == "GET" && path == "/api/contacts") {
    handleContactsGet(client);
  } else if (method == "GET" && path.startsWith("/api/serial-logs")) {
    String queryString = "";
    int queryStart = path.indexOf('?');
    if (queryStart >= 0) {
      queryString = path.substring(queryStart + 1);
    }
    handleSerialLogsGet(client, queryString);
  } else if (method == "GET" && path.startsWith("/api/serial-export")) {
    String queryString = "";
    int queryStart = path.indexOf('?');
    if (queryStart >= 0) {
      queryString = path.substring(queryStart + 1);
    }
    handleSerialLogsDownload(client, queryString);
  } else if (method == "POST" && path == "/api/serial-request") {
    if (contentLength > 256) {
      respondStatus(client, 413, "Payload Too Large");
    } else {
      handleSerialRequestPost(client, body);
    }
  } else if (method == "POST" && path == "/api/calibration") {
    if (contentLength > 512) {
      respondStatus(client, 413, "Payload Too Large");
    } else {
      handleCalibrationPost(client, body);
    }
  } else if (method == "POST" && path == "/api/contacts") {
    if (contentLength > 8192) {
      respondStatus(client, 413, "Payload Too Large");
    } else {
      handleContactsPost(client, body);
    }
  } else if (method == "POST" && path == "/api/server-settings") {
    if (contentLength > 2048) {
      respondStatus(client, 413, "Payload Too Large");
    } else {
      handleServerSettingsPost(client, body);
    }
  } else if (method == "POST" && path == "/api/config") {
    if (contentLength > 8192) {
      respondStatus(client, 413, "Payload Too Large");
    } else {
      handleConfigPost(client, body);
    }
  } else if (method == "POST" && path == "/api/pin") {
    if (contentLength > 256) {
      respondStatus(client, 413, "Payload Too Large");
    } else {
      handlePinPost(client, body);
    }
  } else if (method == "POST" && path == "/api/refresh") {
    if (contentLength > 512) {
      respondStatus(client, 413, "Payload Too Large");
    } else {
      handleRefreshPost(client, body);
    }
  } else if (method == "POST" && path == "/api/relay") {
    if (contentLength > 512) {
      respondStatus(client, 413, "Payload Too Large");
    } else {
      handleRelayPost(client, body);
    }
  } else if (method == "POST" && path == "/api/relay/clear") {
    if (contentLength > 512) {
      respondStatus(client, 413, "Payload Too Large");
    } else {
      handleRelayClearPost(client, body);
    }
  } else if (method == "POST" && path == "/api/pause") {
    if (contentLength > 256) {
      respondStatus(client, 413, "Payload Too Large");
    } else {
      handlePausePost(client, body);
    }
  } else if (method == "POST" && path == "/api/ftp-backup") {
    if (contentLength > 512) {
      respondStatus(client, 413, "Payload Too Large");
    } else {
      handleFtpBackupPost(client, body);
    }
  } else if (method == "POST" && path == "/api/ftp-restore") {
    if (contentLength > 512) {
      respondStatus(client, 413, "Payload Too Large");
    } else {
      handleFtpRestorePost(client, body);
    }
  } else {
    respondStatus(client, 404, F("Not Found"));
  }

  delay(1);
  client.stop();
}

static bool readHttpRequest(EthernetClient &client, String &method, String &path, String &body, size_t &contentLength, bool &bodyTooLarge) {
  method = "";
  path = "";
  contentLength = 0;
  body = "";
  bodyTooLarge = false;

  String line;
  bool firstLine = true;

  unsigned long start = millis();
  while (client.connected() && millis() - start < 5000UL) {
    if (!client.available()) {
      delay(1);
      continue;
    }

    char c = client.read();
    if (c == '\r') {
      continue;
    }
    if (c == '\n') {
      if (line.length() == 0) {
        break; // end of headers
      }
      if (firstLine) {
        int space = line.indexOf(' ');
        if (space < 0) {
          return false;
        }
        method = line.substring(0, space);
        int nextSpace = line.indexOf(' ', space + 1);
        if (nextSpace < 0) {
          return false;
        }
        path = line.substring(space + 1, nextSpace);
        firstLine = false;
      } else {
        int colonPos = line.indexOf(':');
        if (colonPos > 0) {
          String headerKey = line.substring(0, colonPos);
          headerKey.trim();
          String headerValue = line.substring(colonPos + 1);
          headerValue.trim();
          if (headerKey.equalsIgnoreCase("Content-Length")) {
            contentLength = headerValue.toInt();
            if (contentLength > MAX_HTTP_BODY_BYTES) {
              bodyTooLarge = true;
              contentLength = MAX_HTTP_BODY_BYTES;
            }
          }
        }
      }
      line = "";
    } else {
      line += c;
      if (line.length() > 512) {
        return false;
      }
    }
  }

  // If the body is already too large, don't read it into RAM.
  // We'll respond with 413 and close the connection.
  if (bodyTooLarge) {
    return true;
  }

  if (contentLength > 0) {
    size_t readBytes = 0;
    while (readBytes < contentLength && client.connected()) {
      while (client.available() && readBytes < contentLength) {
        char c = client.read();
        body += c;
        readBytes++;
      }
      if (readBytes >= MAX_HTTP_BODY_BYTES) {
        bodyTooLarge = true;
        break;
      }
    }
  }

  return true;
}

static String urlDecode(const String &value) {
  String decoded;
  decoded.reserve(value.length());
  for (size_t i = 0; i < value.length(); ++i) {
    char c = value.charAt(i);
    if (c == '+') {
      decoded += ' ';
    } else if (c == '%' && i + 2 < value.length()) {
      char hi = value.charAt(i + 1);
      char lo = value.charAt(i + 2);
      char hex[3] = {hi, lo, '\0'};
      decoded += (char)strtol(hex, nullptr, 16);
      i += 2;
    } else {
      decoded += c;
    }
  }
  return decoded;
}

static String getQueryParam(const String &query, const char *key) {
  size_t keyLen = strlen(key);
  size_t start = 0;
  while (start < query.length()) {
    size_t end = query.indexOf('&', start);
    if (end == (size_t)-1) {
      end = query.length();
    }
    String pair = query.substring(start, end);
    size_t eq = pair.indexOf('=');
    if (eq != (size_t)-1) {
      String k = pair.substring(0, eq);
      if (k == key) {
        String value = pair.substring(eq + 1);
        return urlDecode(value);
      }
    } else if (pair == key) {
      return String("true");
    }
    if (end == query.length()) {
      break;
    }
    start = end + 1;
  }
  return String();
}

static void respondHtml(EthernetClient &client, const String &body) {
  client.println(F("HTTP/1.1 200 OK"));
  client.println(F("Content-Type: text/html"));
  client.print(F("Content-Length: "));
  client.println(body.length());
  client.println();
  client.print(body);
}

static void respondJson(EthernetClient &client, const String &body, int status) {
  client.print(F("HTTP/1.1 "));
  client.print(status);
  if (status == 200) {
    client.println(F(" OK"));
  } else {
    client.println();
  }
  client.println(F("Content-Type: application/json"));
  client.print(F("Content-Length: "));
  client.println(body.length());
  client.println();
  client.print(body);
}

static void respondStatus(EthernetClient &client, int status, const char *message) {
  client.print(F("HTTP/1.1 "));
  client.print(status);
  client.print(F(" "));
  if (status == 200) {
    client.println(F("OK"));
  } else if (status == 400) {
    client.println(F("Bad Request"));
  } else if (status == 404) {
    client.println(F("Not Found"));
  } else if (status == 500) {
    client.println(F("Internal Server Error"));
  } else {
    client.println(F("Error"));
  }
  client.println(F("Content-Type: text/plain"));
  client.print(F("Content-Length: "));
  client.println(strlen(message));
  client.println();
  client.print(message);
}

static void respondStatus(EthernetClient &client, int status, const String &message) {
  respondStatus(client, status, message.c_str());
}

static void sendDashboard(EthernetClient &client) {
  size_t htmlLen = strlen_P(DASHBOARD_HTML);
  client.println(F("HTTP/1.1 200 OK"));
  client.println(F("Content-Type: text/html; charset=utf-8"));
  client.print(F("Content-Length: "));
  client.println(htmlLen);
  client.println(F("Cache-Control: no-cache, no-store, must-revalidate"));
  client.println();

  for (size_t i = 0; i < htmlLen; ++i) {
    char c = pgm_read_byte_near(DASHBOARD_HTML + i);
    client.write(c);
  }
}

static void sendClientConsole(EthernetClient &client) {
  size_t htmlLen = strlen_P(CLIENT_CONSOLE_HTML);
  client.println(F("HTTP/1.1 200 OK"));
  client.println(F("Content-Type: text/html; charset=utf-8"));
  client.print(F("Content-Length: "));
  client.println(htmlLen);
  client.println(F("Cache-Control: no-cache, no-store, must-revalidate"));
  client.println();

  for (size_t i = 0; i < htmlLen; ++i) {
    char c = pgm_read_byte_near(CLIENT_CONSOLE_HTML + i);
    client.write(c);
  }
}

static void sendConfigGenerator(EthernetClient &client) {
  size_t htmlLen = strlen_P(CONFIG_GENERATOR_HTML);
  client.println(F("HTTP/1.1 200 OK"));
  client.println(F("Content-Type: text/html; charset=utf-8"));
  client.print(F("Content-Length: "));
  client.println(htmlLen);
  client.println(F("Cache-Control: no-cache, no-store, must-revalidate"));
  client.println();

  for (size_t i = 0; i < htmlLen; ++i) {
    char c = pgm_read_byte_near(CONFIG_GENERATOR_HTML + i);
    client.write(c);
  }
}

static void sendSerialMonitor(EthernetClient &client) {
  size_t htmlLen = strlen_P(SERIAL_MONITOR_HTML);
  client.println(F("HTTP/1.1 200 OK"));
  client.println(F("Content-Type: text/html; charset=utf-8"));
  client.print(F("Content-Length: "));
  client.println(htmlLen);
  client.println(F("Cache-Control: no-cache, no-store, must-revalidate"));
  client.println();

  for (size_t i = 0; i < htmlLen; ++i) {
    char c = pgm_read_byte_near(SERIAL_MONITOR_HTML + i);
    client.write(c);
  }
}

static void sendTankJson(EthernetClient &client) {
  DynamicJsonDocument doc(TANK_JSON_CAPACITY);
  JsonArray arr = doc.createNestedArray("tanks");
  for (uint8_t i = 0; i < gTankRecordCount; ++i) {
    JsonObject obj = arr.createNestedObject();
    obj["c"] = gTankRecords[i].clientUid;
    obj["s"] = gTankRecords[i].site;
    obj["n"] = gTankRecords[i].label;
    obj["k"] = gTankRecords[i].tankNumber;
    obj["l"] = gTankRecords[i].levelInches;
    // Only include sensorMa for current-loop sensors (valid range is 4-20mA)
    if (gTankRecords[i].sensorMa >= 4.0f) {
      obj["ma"] = gTankRecords[i].sensorMa;
    }
    // Include sensor type if known
    if (gTankRecords[i].sensorType[0] != '\0') {
      obj["st"] = gTankRecords[i].sensorType;
    }
    // Calculate and include 24hr change if we have previous data
    if (gTankRecords[i].previousLevelEpoch > 0.0) {
      float delta = gTankRecords[i].levelInches - gTankRecords[i].previousLevelInches;
      obj["d"] = delta;  // 24hr delta in inches
      obj["pe"] = gTankRecords[i].previousLevelEpoch;  // when previous reading was taken
    }
    obj["a"] = gTankRecords[i].alarmActive;
    obj["at"] = gTankRecords[i].alarmType;
    obj["u"] = gTankRecords[i].lastUpdateEpoch;
  }

  // Detect if we ran out of memory while building the document
  if (doc.overflowed()) {
    Serial.println(F("[ERROR] Tank JSON document overflowed - increase TANK_JSON_CAPACITY"));
    respondStatus(client, 500, F("Tank data too large"));
    return;
  }

  String json;
  if (serializeJson(doc, json) == 0) {
    respondStatus(client, 500, F("Failed to encode tank data"));
    return;
  }
  respondJson(client, json);
}

static void sendClientDataJson(EthernetClient &client) {
  // Allocate large JSON document on heap instead of stack to prevent overflow
  std::unique_ptr<DynamicJsonDocument> docPtr(new DynamicJsonDocument(CLIENT_JSON_CAPACITY));
  if (!docPtr) {
    respondStatus(client, 500, F("Server Out of Memory"));
    return;
  }
  DynamicJsonDocument &doc = *docPtr;

  JsonObject serverObj = doc.createNestedObject("srv");
  serverObj["n"] = gConfig.serverName;
  serverObj["cf"] = gConfig.clientFleet;
  serverObj["sp"] = gConfig.smsPrimary;
  serverObj["ss"] = gConfig.smsSecondary;
  serverObj["de"] = gConfig.dailyEmail;
  serverObj["dh"] = gConfig.dailyHour;
  serverObj["dm"] = gConfig.dailyMinute;
  serverObj["wrs"] = gConfig.webRefreshSeconds;
  serverObj["soh"] = gConfig.smsOnHigh;
  serverObj["sol"] = gConfig.smsOnLow;
  serverObj["soc"] = gConfig.smsOnClear;
  serverObj["pc"] = (gConfig.configPin[0] != '\0');
  serverObj["ps"] = gPaused;

  JsonObject ftpObj = serverObj.createNestedObject("ftp");
  ftpObj["en"] = gConfig.ftpEnabled;
  ftpObj["pas"] = gConfig.ftpPassive;
  ftpObj["boc"] = gConfig.ftpBackupOnChange;
  ftpObj["rob"] = gConfig.ftpRestoreOnBoot;
  ftpObj["pt"] = gConfig.ftpPort;
  ftpObj["hst"] = gConfig.ftpHost;
  ftpObj["usr"] = gConfig.ftpUser;
  ftpObj["pth"] = gConfig.ftpPath;
  ftpObj["pset"] = (gConfig.ftpPass[0] != '\0');

  doc["si"] = gServerUid;
  doc["nde"] = gNextDailyEmailEpoch;
  doc["lse"] = gLastSyncedEpoch;

  JsonArray clientsArr = doc.createNestedArray("cs");
  for (uint8_t i = 0; i < gTankRecordCount; ++i) {
    const TankRecord &rec = gTankRecords[i];

    JsonObject clientObj;
    for (JsonObject existing : clientsArr) {
      const char *uid = existing["c"];
      if (uid && strcmp(uid, rec.clientUid) == 0) {
        clientObj = existing;
        break;
      }
    }

    if (!clientObj) {
      clientObj = clientsArr.createNestedObject();
      clientObj["c"] = rec.clientUid;
      clientObj["s"] = rec.site;
      clientObj["a"] = false;
      clientObj["u"] = 0.0;
      
      // Add VIN voltage from client metadata if available
      ClientMetadata *meta = findClientMetadata(rec.clientUid);
      if (meta && meta->vinVoltage > 0.0f) {
        clientObj["v"] = meta->vinVoltage;
        clientObj["ve"] = meta->vinVoltageEpoch;
      }
    }

    const char *existingSite = clientObj.containsKey("s") ? clientObj["s"].as<const char *>() : nullptr;
    if (!existingSite || strlen(existingSite) == 0) {
      clientObj["s"] = rec.site;
    }

    if (rec.alarmActive) {
      clientObj["a"] = true;
      clientObj["at"] = rec.alarmType;
    }

    double previousUpdate = clientObj["u"].is<double>() ? clientObj["u"].as<double>() : 0.0;
    if (rec.lastUpdateEpoch > previousUpdate) {
      clientObj["n"] = rec.label;
      clientObj["k"] = rec.tankNumber;
      clientObj["l"] = rec.levelInches;
      // Only include sensorMa for current-loop sensors (valid range is 4-20mA)
      if (rec.sensorMa >= 4.0f) {
        clientObj["ma"] = rec.sensorMa;
      }
      clientObj["u"] = rec.lastUpdateEpoch;
      clientObj["at"] = rec.alarmType;
    }

    JsonArray tankList;
    if (!clientObj.containsKey("ts")) {
      tankList = clientObj.createNestedArray("ts");
    } else {
      tankList = clientObj["ts"].as<JsonArray>();
    }
    JsonObject tankObj = tankList.createNestedObject();
    tankObj["n"] = rec.label;
    tankObj["k"] = rec.tankNumber;
    tankObj["l"] = rec.levelInches;
    // Only include sensorMa for current-loop sensors (valid range is 4-20mA)
    if (rec.sensorMa >= 4.0f) {
      tankObj["ma"] = rec.sensorMa;
    }
    // Include sensor type if known
    if (rec.sensorType[0] != '\0') {
      tankObj["st"] = rec.sensorType;
    }
    // Include 24hr change if available
    if (rec.previousLevelEpoch > 0.0) {
      tankObj["d"] = rec.levelInches - rec.previousLevelInches;
    }
    tankObj["a"] = rec.alarmActive;
    tankObj["at"] = rec.alarmType;
    tankObj["u"] = rec.lastUpdateEpoch;

    clientObj["tc"] = tankList.size();
  }

  JsonArray configsArr = doc.createNestedArray("cfgs");
  for (uint8_t i = 0; i < gClientConfigCount; ++i) {
    ClientConfigSnapshot &snap = gClientConfigs[i];
    JsonObject cfgEntry = configsArr.createNestedObject();
    cfgEntry["c"] = snap.uid;
    cfgEntry["s"] = snap.site;
    cfgEntry["cj"] = snap.payload;
  }

  // Detect if we ran out of memory while building the document
  if (doc.overflowed()) {
    Serial.println(F("[ERROR] Client data JSON document overflowed - increase CLIENT_JSON_CAPACITY"));
    respondStatus(client, 500, F("Client data too large"));
    return;
  }

  String json;
  if (serializeJson(doc, json) == 0) {
    respondStatus(client, 500, F("Failed to encode client data"));
    return;
  }
  respondJson(client, json);
}

static void handleConfigPost(EthernetClient &client, const String &body) {
  // Use larger buffer to match MAX_HTTP_BODY_BYTES (16KB) for complex configs
  std::unique_ptr<DynamicJsonDocument> docPtr(new DynamicJsonDocument(MAX_HTTP_BODY_BYTES));
  if (!docPtr) {
    respondStatus(client, 500, F("Server Out of Memory"));
    return;
  }
  DynamicJsonDocument &doc = *docPtr;

  DeserializationError err = deserializeJson(doc, body);
  if (err) {
    respondStatus(client, 400, F("Invalid JSON"));
    return;
  }

  if (gConfig.configPin[0] == '\0') {
    respondStatus(client, 403, F("Configure admin PIN before making changes"));
    return;
  }

  const char *pinValue = doc["pin"].as<const char *>();
  if (!pinMatches(pinValue)) {
    respondStatus(client, 403, F("Invalid PIN"));
    return;
  }

  if (doc.containsKey("server")) {
    JsonObject serverObj = doc["server"].as<JsonObject>();
    if (serverObj.containsKey("smsPrimary")) {
      strlcpy(gConfig.smsPrimary, serverObj["smsPrimary"], sizeof(gConfig.smsPrimary));
    }
    if (serverObj.containsKey("smsSecondary")) {
      strlcpy(gConfig.smsSecondary, serverObj["smsSecondary"], sizeof(gConfig.smsSecondary));
    }
    if (serverObj.containsKey("dailyEmail")) {
      strlcpy(gConfig.dailyEmail, serverObj["dailyEmail"], sizeof(gConfig.dailyEmail));
    }
    if (serverObj.containsKey("dailyHour")) {
      gConfig.dailyHour = serverObj["dailyHour"].as<uint8_t>();
    }
    if (serverObj.containsKey("dailyMinute")) {
      gConfig.dailyMinute = serverObj["dailyMinute"].as<uint8_t>();
    }
    if (serverObj.containsKey("webRefreshSeconds")) {
      gConfig.webRefreshSeconds = serverObj["webRefreshSeconds"].as<uint16_t>();
    }
    if (serverObj.containsKey("smsOnHigh")) {
      gConfig.smsOnHigh = serverObj["smsOnHigh"].as<bool>();
    }
    if (serverObj.containsKey("smsOnLow")) {
      gConfig.smsOnLow = serverObj["smsOnLow"].as<bool>();
    }
    if (serverObj.containsKey("smsOnClear")) {
      gConfig.smsOnClear = serverObj["smsOnClear"].as<bool>();
    }

    if (serverObj.containsKey("ftp")) {
      JsonObject ftpObj = serverObj["ftp"].as<JsonObject>();
      if (ftpObj.containsKey("enabled")) {
        gConfig.ftpEnabled = ftpObj["enabled"].as<bool>();
      }
      if (ftpObj.containsKey("passive")) {
        gConfig.ftpPassive = ftpObj["passive"].as<bool>();
      }
      if (ftpObj.containsKey("backupOnChange")) {
        gConfig.ftpBackupOnChange = ftpObj["backupOnChange"].as<bool>();
      }
      if (ftpObj.containsKey("restoreOnBoot")) {
        gConfig.ftpRestoreOnBoot = ftpObj["restoreOnBoot"].as<bool>();
      }
      if (ftpObj.containsKey("port")) {
        gConfig.ftpPort = ftpObj["port"].as<uint16_t>();
      }
      if (ftpObj.containsKey("host")) {
        strlcpy(gConfig.ftpHost, ftpObj["host"], sizeof(gConfig.ftpHost));
      }
      if (ftpObj.containsKey("user")) {
        strlcpy(gConfig.ftpUser, ftpObj["user"], sizeof(gConfig.ftpUser));
      }
      if (ftpObj.containsKey("pass")) {
        const char *passVal = ftpObj["pass"].as<const char *>();
        if (passVal && strlen(passVal) > 0) {
          strlcpy(gConfig.ftpPass, passVal, sizeof(gConfig.ftpPass));
        }
      }
      if (ftpObj.containsKey("path")) {
        strlcpy(gConfig.ftpPath, ftpObj["path"], sizeof(gConfig.ftpPath));
      }
    }
    gConfigDirty = true;
    scheduleNextDailyEmail();
  }

  if (doc.containsKey("client") && doc.containsKey("config")) {
    const char *clientUid = doc["client"].as<const char *>();
    if (clientUid && strlen(clientUid) > 0) {
      ConfigDispatchStatus status = dispatchClientConfig(clientUid, doc["config"]);
      if (status == ConfigDispatchStatus::PayloadTooLarge) {
        respondStatus(client, 413, F("Config payload too large"));
        return;
      }
      if (status == ConfigDispatchStatus::NotecardFailure) {
        respondStatus(client, 500, F("Failed to queue config"));
        return;
      }
    }
  }

  respondStatus(client, 200, F("OK"));
}

static void sendPinResponse(EthernetClient &client, const __FlashStringHelper *message) {
  DynamicJsonDocument resp(128);
  resp["pinConfigured"] = (gConfig.configPin[0] != '\0');
  String msg(message);
  resp["message"] = msg;
  String json;
  serializeJson(resp, json);
  respondJson(client, json);
}

static void handlePinPost(EthernetClient &client, const String &body) {
  DynamicJsonDocument doc(256);
  if (deserializeJson(doc, body)) {
    respondStatus(client, 400, F("Invalid JSON"));
    return;
  }

  const char *currentPin = doc["pin"].as<const char *>();
  const char *newPin = doc["newPin"].as<const char *>();
  bool configured = (gConfig.configPin[0] != '\0');

  if (!configured) {
    if (!isValidPin(newPin)) {
      respondStatus(client, 400, F("PIN must be 4 digits"));
      return;
    }
    strlcpy(gConfig.configPin, newPin, sizeof(gConfig.configPin));
    gConfigDirty = true;
    sendPinResponse(client, F("PIN set"));
    return;
  }

  if (!pinMatches(currentPin)) {
    respondStatus(client, 403, F("Invalid PIN"));
    return;
  }

  if (newPin) {
    if (!isValidPin(newPin)) {
      respondStatus(client, 400, F("PIN must be 4 digits"));
      return;
    }
    strlcpy(gConfig.configPin, newPin, sizeof(gConfig.configPin));
    gConfigDirty = true;
    sendPinResponse(client, F("PIN updated"));
  } else {
    sendPinResponse(client, F("PIN verified"));
  }
}

static void handleRelayPost(EthernetClient &client, const String &body) {
  DynamicJsonDocument doc(640);
  if (deserializeJson(doc, body)) {
    respondStatus(client, 400, F("Invalid JSON"));
    return;
  }

  const char *pinValue = doc["pin"].as<const char *>();
  if (!requireValidPin(client, pinValue)) {
    return;
  }

  const char *clientUid = doc["clientUid"].as<const char *>();
  if (!clientUid || strlen(clientUid) == 0) {
    respondStatus(client, 400, F("Missing clientUid"));
    return;
  }

  uint8_t relayNum = doc["relay"].as<uint8_t>();
  if (relayNum < 1 || relayNum > MAX_RELAYS) {
    char errMsg[32];
    snprintf(errMsg, sizeof(errMsg), "relay must be 1-%d", MAX_RELAYS);
    respondStatus(client, 400, errMsg);
    return;
  }

  bool state = doc["state"].as<bool>();

  if (sendRelayCommand(clientUid, relayNum, state, "server")) {
    respondStatus(client, 200, F("OK"));
  } else {
    respondStatus(client, 500, F("Failed to send relay command"));
  }
}

static bool sendRelayCommand(const char *clientUid, uint8_t relayNum, bool state, const char *source) {
  if (!clientUid || relayNum < 1 || relayNum > MAX_RELAYS) {
    return false;
  }

  J *req = notecard.newRequest("note.add");
  if (!req) {
    return false;
  }

  // Use device-specific targeting: send directly to client's relay.qi inbox
  char targetFile[80];
  snprintf(targetFile, sizeof(targetFile), "device:%s:relay.qi", clientUid);
  JAddStringToObject(req, "file", targetFile);
  JAddBoolToObject(req, "sync", true);

  J *body = JCreateObject();
  if (!body) {
    return false;
  }

  JAddNumberToObject(body, "relay", relayNum);
  JAddBoolToObject(body, "state", state);
  JAddStringToObject(body, "source", source);
  
  JAddItemToObject(req, "body", body);
  
  bool queued = notecard.sendRequest(req);
  if (!queued) {
    return false;
  }

  Serial.print(F("Queued relay command for client "));
  Serial.print(clientUid);
  Serial.print(F(": Relay "));
  Serial.print(relayNum);
  Serial.print(F(" -> "));
  Serial.println(state ? "ON" : "OFF");

  return true;
}

// Send a command to clear/reset relay alarms for a specific tank on a client
static bool sendRelayClearCommand(const char *clientUid, uint8_t tankIdx) {
  if (!clientUid) {
    return false;
  }

  J *req = notecard.newRequest("note.add");
  if (!req) {
    return false;
  }

  // Use device-specific targeting: send directly to client's relay.qi inbox
  char targetFile[80];
  snprintf(targetFile, sizeof(targetFile), "device:%s:relay.qi", clientUid);
  JAddStringToObject(req, "file", targetFile);
  JAddBoolToObject(req, "sync", true);

  J *body = JCreateObject();
  if (!body) {
    return false;
  }

  // Use relay_reset_tank command format that the client already supports
  JAddNumberToObject(body, "relay_reset_tank", tankIdx);
  JAddStringToObject(body, "source", "server-dashboard");
  
  JAddItemToObject(req, "body", body);
  
  bool queued = notecard.sendRequest(req);
  if (!queued) {
    return false;
  }

  Serial.print(F("Queued relay clear command for client "));
  Serial.print(clientUid);
  Serial.print(F(", tank "));
  Serial.println(tankIdx);

  return true;
}

static void handleRelayClearPost(EthernetClient &client, const String &body) {
  DynamicJsonDocument doc(256);
  if (deserializeJson(doc, body)) {
    respondStatus(client, 400, F("Invalid JSON"));
    return;
  }

  const char *pinValue = doc["pin"].as<const char *>();
  if (gConfig.configPin[0] != '\0') {
    if (!requireValidPin(client, pinValue)) {
      return;
    }
  }

  const char *clientUid = doc["clientUid"].as<const char *>();
  if (!clientUid || strlen(clientUid) == 0) {
    respondStatus(client, 400, F("Missing clientUid"));
    return;
  }

  uint8_t tankIdx = doc["tankIdx"].as<uint8_t>();
  // Note: tankIdx validation is handled by the client device based on its actual tank configuration

  if (sendRelayClearCommand(clientUid, tankIdx)) {
    respondStatus(client, 200, F("OK"));
  } else {
    respondStatus(client, 500, F("Failed to send relay clear command"));
  }
}

static void handlePausePost(EthernetClient &client, const String &body) {
  DynamicJsonDocument doc(256);
  if (deserializeJson(doc, body)) {
    respondStatus(client, 400, F("Invalid JSON"));
    return;
  }

  const char *pinValue = doc["pin"].as<const char *>();
  if (gConfig.configPin[0] != '\0') {
    if (!requireValidPin(client, pinValue)) {
      return;
    }
  }

  bool paused = doc["paused"].is<bool>() ? doc["paused"].as<bool>() : true;
  gPaused = paused;

  DynamicJsonDocument resp(128);
  resp["paused"] = gPaused;
  String json;
  serializeJson(resp, json);
  respondJson(client, json, 200);

  Serial.print(F("Server pause state: "));
  Serial.println(gPaused ? F("paused") : F("running"));
}

static void handleFtpBackupPost(EthernetClient &client, const String &body) {
  DynamicJsonDocument doc(256);
  if (deserializeJson(doc, body)) {
    respondStatus(client, 400, F("Invalid JSON"));
    return;
  }

  const char *pinValue = doc["pin"].as<const char *>();
  if (!requireValidPin(client, pinValue)) {
    return;
  }

  char error[128];
  bool ok = performFtpBackup(error, sizeof(error));

  DynamicJsonDocument resp(192);
  resp["ok"] = ok;
  if (ok) {
    resp["message"] = F("Backup uploaded to FTP");
  } else {
    resp["error"] = (strlen(error) > 0) ? error : "Backup failed";
  }
  String json;
  serializeJson(resp, json);
  respondJson(client, json, ok ? 200 : 500);
}

static void handleFtpRestorePost(EthernetClient &client, const String &body) {
  DynamicJsonDocument doc(256);
  if (deserializeJson(doc, body)) {
    respondStatus(client, 400, F("Invalid JSON"));
    return;
  }

  const char *pinValue = doc["pin"].as<const char *>();
  if (!requireValidPin(client, pinValue)) {
    return;
  }

  char error[128];
  bool ok = performFtpRestore(error, sizeof(error));

  if (ok) {
    // Reload in-memory views now that on-disk state changed
    ensureConfigLoaded();
    loadClientConfigSnapshots();
    loadCalibrationData();
    scheduleNextDailyEmail();
    scheduleNextViewerSummary();
  }

  DynamicJsonDocument resp(192);
  resp["ok"] = ok;
  if (ok) {
    resp["message"] = "Restore completed from FTP";
  } else {
    resp["error"] = strlen(error) ? error : "Restore failed";
  }
  String json;
  serializeJson(resp, json);
  respondJson(client, json, ok ? 200 : 500);
}

static ConfigDispatchStatus dispatchClientConfig(const char *clientUid, JsonVariantConst cfgObj) {
  char buffer[4096];
  size_t len = serializeJson(cfgObj, buffer, sizeof(buffer));
  if (len == 0 || len >= sizeof(buffer)) {
    Serial.println(F("Client config payload too large"));
    return ConfigDispatchStatus::PayloadTooLarge;
  }
  buffer[len] = '\0';

  J *req = notecard.newRequest("note.add");
  if (!req) {
    return ConfigDispatchStatus::NotecardFailure;
  }
  // Use device-specific targeting: send directly to client's config.qi inbox
  char targetFile[80];
  snprintf(targetFile, sizeof(targetFile), "device:%s:config.qi", clientUid);
  JAddStringToObject(req, "file", targetFile);
  JAddBoolToObject(req, "sync", true);

  J *body = JParse(buffer);
  if (!body) {
    return ConfigDispatchStatus::PayloadTooLarge;
  }
  JAddItemToObject(req, "body", body);
  bool queued = notecard.sendRequest(req);
  if (!queued) {
    return ConfigDispatchStatus::NotecardFailure;
  }

  cacheClientConfigFromBuffer(clientUid, buffer);

  Serial.print(F("Queued config update for client " ));
  Serial.println(clientUid);

  return ConfigDispatchStatus::Ok;
}

static void pollNotecard() {
  processNotefile(TELEMETRY_FILE, handleTelemetry);
  processNotefile(ALARM_FILE, handleAlarm);
  processNotefile(DAILY_FILE, handleDaily);
  processNotefile(SERIAL_LOG_FILE, handleSerialLog);
  processNotefile(SERIAL_ACK_FILE, handleSerialAck);
}

static void processNotefile(const char *fileName, void (*handler)(JsonDocument &, double)) {
  uint8_t processed = 0;
  while (processed < MAX_NOTES_PER_FILE_PER_POLL) {
    J *req = notecard.newRequest("note.get");
    if (!req) {
      return;
    }
    JAddStringToObject(req, "file", fileName);
    JAddBoolToObject(req, "delete", true);
    J *rsp = notecard.requestAndResponse(req);
    if (!rsp) {
      // Note: notecard.requestAndResponse() internally cleans up the request
      // so we don't need to explicitly delete it here
      return;
    }

    J *body = JGetObject(rsp, "body");
    if (!body) {
      notecard.deleteResponse(rsp);
      break;
    }

    char *json = JConvertToJSONString(body);
    double epoch = JGetNumber(rsp, "time");
    if (json) {
      DynamicJsonDocument doc(4096);
      DeserializationError err = deserializeJson(doc, json);
      NoteFree(json);
      if (!err) {
        handler(doc, epoch);
      }
    }

    notecard.deleteResponse(rsp);
    processed++;

    // Yield to keep the loop responsive and allow watchdog kicks elsewhere
    delay(1);
  }
}

static void handleTelemetry(JsonDocument &doc, double epoch) {
  const char *clientUid = doc["c"] | doc["client"] | "";
  uint8_t tankNumber = doc["k"] | doc["tank"].as<uint8_t>();
  TankRecord *rec = upsertTankRecord(clientUid, tankNumber);
  if (!rec) {
    return;
  }

  strlcpy(rec->site, doc["s"] | doc["site"] | "", sizeof(rec->site));
  strlcpy(rec->label, doc["n"] | doc["label"] | "Tank", sizeof(rec->label));
  
  // Store object type if provided (tank, engine, pump, gas, flow)
  const char *objectType = doc["ot"] | doc["objectType"] | "";
  if (objectType && strlen(objectType) > 0) {
    strlcpy(rec->objectType, objectType, sizeof(rec->objectType));
  } else if (rec->objectType[0] == '\0') {
    strlcpy(rec->objectType, "tank", sizeof(rec->objectType)); // Default
  }
  
  // Store sensor interface type if provided (digital, analog, currentLoop, pulse)
  // Support both old "st"/"sensorType" and new names
  const char *sensorType = doc["si"] | doc["sensorInterface"] | doc["st"] | doc["sensorType"] | "";
  if (sensorType && strlen(sensorType) > 0) {
    // Normalize "rpm" to "pulse" for consistency
    if (strcmp(sensorType, "rpm") == 0) {
      strlcpy(rec->sensorType, "pulse", sizeof(rec->sensorType));
    } else {
      strlcpy(rec->sensorType, sensorType, sizeof(rec->sensorType));
    }
  }
  
  // Store measurement unit if provided
  const char *unit = doc["mu"] | doc["measurementUnit"] | "";
  if (unit && strlen(unit) > 0) {
    strlcpy(rec->measurementUnit, unit, sizeof(rec->measurementUnit));
  }
  
  // Handle raw sensor readings
  float mA = 0.0f;
  float voltage = 0.0f;
  
  // Raw mA for current-loop sensors
  if (doc.containsKey("ma")) {
    mA = doc["ma"].as<float>();
    rec->sensorMa = (mA >= 4.0f) ? mA : 0.0f;
  } else if (doc.containsKey("sensorMa")) {
    mA = doc["sensorMa"].as<float>();
    rec->sensorMa = (mA >= 4.0f) ? mA : 0.0f;
  }
  
  // Raw voltage for analog sensors
  if (doc.containsKey("vt")) {
    voltage = doc["vt"].as<float>();
    rec->sensorVoltage = (voltage > 0.0f) ? voltage : 0.0f;
  }
  
  // Get level/value reading based on sensor type
  float newLevel = 0.0f;
  bool isCurrentLoop = (strcmp(rec->sensorType, "currentLoop") == 0);
  bool isAnalog = (strcmp(rec->sensorType, "analog") == 0);
  bool isDigital = (strcmp(rec->sensorType, "digital") == 0);
  bool isPulse = (strcmp(rec->sensorType, "pulse") == 0);
  
  if (isCurrentLoop && mA >= 4.0f) {
    // Current-loop sensor: convert raw mA to level using config
    newLevel = convertMaToLevel(clientUid, tankNumber, mA);
  } else if (isAnalog && voltage > 0.0f) {
    // Analog voltage sensor: convert raw voltage to level using config
    newLevel = convertVoltageToLevel(clientUid, tankNumber, voltage);
  } else if (isDigital && doc.containsKey("fl")) {
    // Digital float switch: use fl field
    newLevel = doc["fl"].as<float>();
  } else if (isPulse && doc.containsKey("rm")) {
    // Pulse/RPM sensor: use rm field
    newLevel = doc["rm"].as<float>();
  }
  
  double now = (epoch > 0.0) ? epoch : currentEpoch();
  
  // Update 24-hour tracking: if current level is >22 hours old, roll it to previous
  const double HOURS_22 = 22.0 * 3600.0;  // 22 hours in seconds
  if (rec->lastUpdateEpoch > 0.0 && (now - rec->lastUpdateEpoch) >= HOURS_22) {
    rec->previousLevelInches = rec->levelInches;
    rec->previousLevelEpoch = rec->lastUpdateEpoch;
  } else if (rec->previousLevelEpoch == 0.0 && rec->lastUpdateEpoch > 0.0) {
    // Initialize previous level on first update
    rec->previousLevelInches = rec->levelInches;
    rec->previousLevelEpoch = rec->lastUpdateEpoch;
  }
  
  rec->levelInches = newLevel;
  rec->lastUpdateEpoch = now;
}

static void handleAlarm(JsonDocument &doc, double epoch) {
  const char *clientUid = doc["c"] | doc["client"] | "";
  uint8_t tankNumber = doc["k"] | doc["tank"].as<uint8_t>();
  TankRecord *rec = upsertTankRecord(clientUid, tankNumber);
  if (!rec) {
    return;
  }

  const char *type = doc["y"] | doc["type"] | "";
  
  // Store object type if provided
  const char *objectType = doc["ot"] | doc["objectType"] | "";
  if (objectType && strlen(objectType) > 0) {
    strlcpy(rec->objectType, objectType, sizeof(rec->objectType));
  }
  
  // Store measurement unit if provided
  const char *unit = doc["mu"] | doc["measurementUnit"] | "";
  if (unit && strlen(unit) > 0) {
    strlcpy(rec->measurementUnit, unit, sizeof(rec->measurementUnit));
  }
  
  // Handle raw sensor readings
  float mA = 0.0f;
  float voltage = 0.0f;
  
  if (doc.containsKey("ma")) {
    mA = doc["ma"].as<float>();
    rec->sensorMa = (mA >= 4.0f) ? mA : 0.0f;
  }
  if (doc.containsKey("vt")) {
    voltage = doc["vt"].as<float>();
    rec->sensorVoltage = (voltage > 0.0f) ? voltage : 0.0f;
  }
  
  // Get level - convert from raw sensor data if no level provided
  float level = 0.0f;
  bool isCurrentLoop = (strcmp(rec->sensorType, "currentLoop") == 0);
  bool isAnalog = (strcmp(rec->sensorType, "analog") == 0);
  bool isDigital = (strcmp(rec->sensorType, "digital") == 0);
  bool isPulse = (strcmp(rec->sensorType, "pulse") == 0);
  
  if (isCurrentLoop && mA >= 4.0f) {
    level = convertMaToLevel(clientUid, tankNumber, mA);
  } else if (isAnalog && voltage > 0.0f) {
    level = convertVoltageToLevel(clientUid, tankNumber, voltage);
  } else if (isDigital && doc.containsKey("fl")) {
    level = doc["fl"].as<float>();
  } else if (isPulse && doc.containsKey("rm")) {
    level = doc["rm"].as<float>();
  }
  
  bool isDiagnostic = (strcmp(type, "sensor-fault") == 0) ||
                      (strcmp(type, "sensor-stuck") == 0) ||
                      (strcmp(type, "sensor-recovered") == 0);
  bool isRecovery = (strcmp(type, "sensor-recovered") == 0);
  // Digital sensor (float switch) alarm types
  bool isDigitalAlarm = (strcmp(type, "triggered") == 0) ||
                        (strcmp(type, "not_triggered") == 0);

  if (strcmp(type, "clear") == 0 || isRecovery) {
    rec->alarmActive = false;
    strlcpy(rec->alarmType, "clear", sizeof(rec->alarmType));
    if (isRecovery) {
      strlcpy(rec->alarmType, type, sizeof(rec->alarmType));
    }
  } else {
    rec->alarmActive = true;
    strlcpy(rec->alarmType, type, sizeof(rec->alarmType));
  }
  rec->levelInches = level;
  rec->lastUpdateEpoch = (epoch > 0.0) ? epoch : currentEpoch();

  // Check rate limit before sending SMS
  bool smsEnabled = true;
  if (doc.containsKey("se")) {
    smsEnabled = doc["se"].as<bool>();
  } else if (doc.containsKey("smsEnabled")) {
    smsEnabled = doc["smsEnabled"].as<bool>();
  }
  bool smsAllowedByServer = true;
  if (strcmp(type, "high") == 0) {
    smsAllowedByServer = gConfig.smsOnHigh;
  } else if (strcmp(type, "low") == 0) {
    smsAllowedByServer = gConfig.smsOnLow;
  } else if (strcmp(type, "clear") == 0) {
    smsAllowedByServer = gConfig.smsOnClear;
  } else if (isDigitalAlarm) {
    // Digital sensor alarms are treated like high alarms for SMS purposes
    smsAllowedByServer = gConfig.smsOnHigh;
  }

  if (!isDiagnostic && smsEnabled && smsAllowedByServer && checkSmsRateLimit(rec)) {
    char message[160];
    // Format message differently for digital sensors
    if (isDigitalAlarm) {
      const char *stateDesc = (strcmp(type, "triggered") == 0) ? "ACTIVATED" : "NOT ACTIVATED";
      snprintf(message, sizeof(message), "%s #%d Float Switch %s", rec->site, rec->tankNumber, stateDesc);
    } else {
      snprintf(message, sizeof(message), "%s #%d %s alarm %.1f in", rec->site, rec->tankNumber, rec->alarmType, level);
    }
    sendSmsAlert(message);
  }
}

// Helper function to find client metadata entry (read-only, does not create)
static ClientMetadata *findClientMetadata(const char *clientUid) {
  if (!clientUid || strlen(clientUid) == 0) {
    return nullptr;
  }
  
  for (uint8_t i = 0; i < gClientMetadataCount; ++i) {
    if (strcmp(gClientMetadata[i].clientUid, clientUid) == 0) {
      return &gClientMetadata[i];
    }
  }
  
  return nullptr;
}

// Helper function to find or create client metadata entry
static ClientMetadata *findOrCreateClientMetadata(const char *clientUid) {
  if (!clientUid || strlen(clientUid) == 0) {
    return nullptr;
  }
  
  // Search for existing entry
  ClientMetadata *existing = findClientMetadata(clientUid);
  if (existing) {
    return existing;
  }
  
  // Create new entry if space available
  if (gClientMetadataCount < MAX_CLIENT_METADATA) {
    ClientMetadata *meta = &gClientMetadata[gClientMetadataCount++];
    memset(meta, 0, sizeof(ClientMetadata));
    strlcpy(meta->clientUid, clientUid, sizeof(meta->clientUid));
    return meta;
  }
  
  // Maximum client metadata capacity reached
  Serial.print(F("Warning: Cannot create client metadata for "));
  Serial.print(clientUid);
  Serial.print(F(" - max capacity ("));
  Serial.print(MAX_CLIENT_METADATA);
  Serial.println(F(") reached"));
  return nullptr;
}

static void handleDaily(JsonDocument &doc, double epoch) {
  // Support both short (c) and long (client) field names for compatibility
  const char *clientUid = doc["c"] | doc["client"] | "";
  if (!clientUid || strlen(clientUid) == 0) {
    return;
  }

  // Extract VIN voltage from daily report if present
  // Check if this is part 0 (new) or part 1 (legacy) of the daily report
  uint8_t part = doc["p"] | doc["part"].as<uint8_t>();
  bool isFirstPart = (part == 0 || part == 1);
  float vinVoltage = doc["v"] | doc["vinVoltage"].as<float>();
  if (isFirstPart && vinVoltage > 0.0f) {
    ClientMetadata *meta = findOrCreateClientMetadata(clientUid);
    if (meta) {
      meta->vinVoltage = vinVoltage;
      meta->vinVoltageEpoch = (epoch > 0.0) ? epoch : currentEpoch();
      Serial.print(F("VIN voltage received from "));
      Serial.print(clientUid);
      Serial.print(F(": "));
      Serial.print(vinVoltage);
      Serial.println(F(" V"));
    }
  }

  // Get site name (supports both short and long field names)
  const char *siteName = doc["s"] | doc["site"] | "";

  // Process tank records in the daily report
  JsonArray tanks = doc["tanks"];
  for (JsonObject t : tanks) {
    // Support both short (k) and long (tank) field names
    uint8_t tankNumber = t["k"] | t["tank"].as<uint8_t>();
    TankRecord *rec = upsertTankRecord(clientUid, tankNumber);
    if (!rec) {
      continue;
    }

    strlcpy(rec->site, siteName, sizeof(rec->site));
    // Support both short (n) and long (label) field names
    strlcpy(rec->label, t["n"] | t["label"] | "Tank", sizeof(rec->label));
    
    // Store object type if provided (tank, engine, pump, gas, flow)
    const char *objectType = t["ot"] | t["objectType"] | "";
    if (objectType && strlen(objectType) > 0) {
      strlcpy(rec->objectType, objectType, sizeof(rec->objectType));
    } else if (rec->objectType[0] == '\0') {
      strlcpy(rec->objectType, "tank", sizeof(rec->objectType)); // Default
    }
    
    // Store sensor interface type if provided
    const char *sensorType = t["si"] | t["sensorInterface"] | t["st"] | t["sensorType"] | "";
    if (sensorType && strlen(sensorType) > 0) {
      // Normalize "rpm" to "pulse" for consistency
      if (strcmp(sensorType, "rpm") == 0) {
        strlcpy(rec->sensorType, "pulse", sizeof(rec->sensorType));
      } else {
        strlcpy(rec->sensorType, sensorType, sizeof(rec->sensorType));
      }
    }
    
    // Store measurement unit if provided
    const char *unit = t["mu"] | t["measurementUnit"] | "";
    if (unit && strlen(unit) > 0) {
      strlcpy(rec->measurementUnit, unit, sizeof(rec->measurementUnit));
    }
    
    // Handle raw sensor readings
    float mA = 0.0f;
    float voltage = 0.0f;
    
    if (t.containsKey("ma")) {
      mA = t["ma"].as<float>();
      rec->sensorMa = (mA >= 4.0f) ? mA : 0.0f;
    } else if (t.containsKey("sensorMa")) {
      mA = t["sensorMa"].as<float>();
      rec->sensorMa = (mA >= 4.0f) ? mA : 0.0f;
    }
    if (t.containsKey("vt")) {
      voltage = t["vt"].as<float>();
      rec->sensorVoltage = (voltage > 0.0f) ? voltage : 0.0f;
    }
    
    // Get level - convert from raw sensor data if no level provided
    float newLevel = 0.0f;
    bool isCurrentLoop = (strcmp(rec->sensorType, "currentLoop") == 0);
    bool isAnalog = (strcmp(rec->sensorType, "analog") == 0);
    bool isDigital = (strcmp(rec->sensorType, "digital") == 0);
    bool isPulse = (strcmp(rec->sensorType, "pulse") == 0);
    
    if (isCurrentLoop && mA >= 4.0f) {
      newLevel = convertMaToLevel(clientUid, tankNumber, mA);
    } else if (isAnalog && voltage > 0.0f) {
      newLevel = convertVoltageToLevel(clientUid, tankNumber, voltage);
    } else if (isDigital && t.containsKey("fl")) {
      newLevel = t["fl"].as<float>();
    } else if (isPulse && t.containsKey("rm")) {
      newLevel = t["rm"].as<float>();
    }
    
    double now = (epoch > 0.0) ? epoch : currentEpoch();
    
    // Update 24-hour tracking: if current level is >22 hours old, roll it to previous
    // This ensures we always have a ~24hr baseline for change calculation
    const double HOURS_22 = 22.0 * 3600.0;  // 22 hours in seconds
    if (rec->lastUpdateEpoch > 0.0 && (now - rec->lastUpdateEpoch) >= HOURS_22) {
      rec->previousLevelInches = rec->levelInches;
      rec->previousLevelEpoch = rec->lastUpdateEpoch;
    } else if (rec->previousLevelEpoch == 0.0 && rec->lastUpdateEpoch > 0.0) {
      // Initialize previous level on first update after a gap
      rec->previousLevelInches = rec->levelInches;
      rec->previousLevelEpoch = rec->lastUpdateEpoch;
    }
    
    rec->levelInches = newLevel;
    rec->lastUpdateEpoch = now;
  }
}

static TankRecord *upsertTankRecord(const char *clientUid, uint8_t tankNumber) {
  for (uint8_t i = 0; i < gTankRecordCount; ++i) {
    if (strcmp(gTankRecords[i].clientUid, clientUid) == 0 && gTankRecords[i].tankNumber == tankNumber) {
      return &gTankRecords[i];
    }
  }
  if (gTankRecordCount >= MAX_TANK_RECORDS) {
    return nullptr;
  }
  TankRecord &rec = gTankRecords[gTankRecordCount++];
  memset(&rec, 0, sizeof(TankRecord));
  strlcpy(rec.clientUid, clientUid, sizeof(rec.clientUid));
  rec.tankNumber = tankNumber;
  rec.lastUpdateEpoch = currentEpoch();
  rec.lastSmsAlertEpoch = 0.0;
  rec.smsAlertsInLastHour = 0;
  for (uint8_t i = 0; i < 10; ++i) {
    rec.smsAlertTimestamps[i] = 0.0;
  }
  return &rec;
}

static bool checkSmsRateLimit(TankRecord *rec) {
  if (!rec) {
    return false;
  }

  double now = currentEpoch();
  if (now <= 0.0) {
    return true;  // No time sync yet, allow SMS
  }

  // Check minimum interval since last SMS for this tank
  if (now - rec->lastSmsAlertEpoch < MIN_SMS_ALERT_INTERVAL_SECONDS) {
    Serial.print(F("SMS rate limit: Too soon since last alert for "));
    Serial.print(rec->site);
    Serial.print(F(" #"));
    Serial.println(rec->tankNumber);
    return false;
  }

  // Clean up old timestamps (older than 1 hour)
  double oneHourAgo = now - 3600.0;
  uint8_t validCount = 0;
  // Ensure we don't exceed array bounds (smsAlertTimestamps has 10 elements)
  uint8_t countToCheck = (rec->smsAlertsInLastHour > 10) ? 10 : rec->smsAlertsInLastHour;
  for (uint8_t i = 0; i < countToCheck; ++i) {
    if (rec->smsAlertTimestamps[i] > oneHourAgo) {
      rec->smsAlertTimestamps[validCount++] = rec->smsAlertTimestamps[i];
    }
  }
  rec->smsAlertsInLastHour = validCount;

  // Check hourly limit
  if (rec->smsAlertsInLastHour >= MAX_SMS_ALERTS_PER_HOUR) {
    Serial.print(F("SMS rate limit: Hourly limit exceeded for "));
    Serial.print(rec->site);
    Serial.print(F(" #"));
    Serial.print(rec->tankNumber);
    Serial.print(F(" ("));
    Serial.print(rec->smsAlertsInLastHour);
    Serial.print(F("/"));
    Serial.print(MAX_SMS_ALERTS_PER_HOUR);
    Serial.println(F(")"));
    return false;
  }

  // Update tracking
  rec->lastSmsAlertEpoch = now;
  if (rec->smsAlertsInLastHour < 10) {
    rec->smsAlertTimestamps[rec->smsAlertsInLastHour++] = now;
  }

  return true;
}

static void sendSmsAlert(const char *message) {
  if (strlen(gConfig.smsPrimary) == 0 && strlen(gConfig.smsSecondary) == 0) {
    return;
  }

  DynamicJsonDocument doc(512);
  doc["message"] = message;
  JsonArray numbers = doc.createNestedArray("numbers");
  if (strlen(gConfig.smsPrimary) > 0) {
    numbers.add(gConfig.smsPrimary);
  }
  if (strlen(gConfig.smsSecondary) > 0) {
    numbers.add(gConfig.smsSecondary);
  }

  char buffer[512];
  size_t len = serializeJson(doc, buffer, sizeof(buffer));
  if (len == 0 || len >= sizeof(buffer)) {
    return;
  }
  buffer[len] = '\0';

  J *req = notecard.newRequest("note.add");
  if (!req) {
    return;
  }
  JAddStringToObject(req, "file", "sms.qo");
  JAddBoolToObject(req, "sync", true);
  J *body = JParse(buffer);
  if (!body) {
    notecard.deleteResponse(req);  // Free the request to prevent memory leak
    return;
  }
  JAddItemToObject(req, "body", body);
  notecard.sendRequest(req);

  Serial.print(F("SMS alert dispatched: "));
  Serial.println(message);
}

static void sendDailyEmail() {
  if (strlen(gConfig.dailyEmail) == 0) {
    return;
  }

  // Check rate limit to prevent email spam
  double now = currentEpoch();
  if (now > 0.0 && (now - gLastDailyEmailSentEpoch) < MIN_DAILY_EMAIL_INTERVAL_SECONDS) {
    Serial.print(F("Daily email rate limited ("));
    Serial.print((int)((now - gLastDailyEmailSentEpoch) / 60.0));
    Serial.println(F(" minutes since last)"));
    return;
  }

  // Size JSON document to accommodate all tanks (~230 bytes per tank worst-case)
  // JSON_ARRAY_SIZE + per-object overhead + string storage
  static const size_t EMAIL_JSON_CAPACITY = JSON_ARRAY_SIZE(MAX_TANK_RECORDS) + 
    (MAX_TANK_RECORDS * JSON_OBJECT_SIZE(8)) + (MAX_TANK_RECORDS * 160) + 256;
  DynamicJsonDocument doc(EMAIL_JSON_CAPACITY);
  doc["to"] = gConfig.dailyEmail;
  doc["subject"] = "Daily Tank Summary";
  JsonArray tanks = doc.createNestedArray("tanks");
  for (uint8_t i = 0; i < gTankRecordCount; ++i) {
    JsonObject obj = tanks.createNestedObject();
    obj["client"] = gTankRecords[i].clientUid;
    obj["site"] = gTankRecords[i].site;
    obj["label"] = gTankRecords[i].label;
    obj["tank"] = gTankRecords[i].tankNumber;
    obj["levelInches"] = roundTo(gTankRecords[i].levelInches, 1);
    obj["sensorMa"] = roundTo(gTankRecords[i].sensorMa, 2);
    obj["alarm"] = gTankRecords[i].alarmActive;
    obj["alarmType"] = gTankRecords[i].alarmType;
  }

  // Check if JSON document overflowed during population
  if (doc.overflowed()) {
    Serial.println(F("Daily email JSON document overflowed"));
    return;
  }

  char buffer[MAX_EMAIL_BUFFER];
  size_t len = serializeJson(doc, buffer, sizeof(buffer));
  if (len == 0 || len >= sizeof(buffer)) {
    Serial.println(F("Daily email payload too large"));
    return;
  }
  buffer[len] = '\0';

  J *req = notecard.newRequest("note.add");
  if (!req) {
    return;
  }
  JAddStringToObject(req, "file", "email.qo");
  JAddBoolToObject(req, "sync", true);
  J *body = JParse(buffer);
  if (!body) {
    notecard.deleteResponse(req);  // Free the request to prevent memory leak
    Serial.println(F("Daily email JSON parse failed"));
    return;
  }
  JAddItemToObject(req, "body", body);
  notecard.sendRequest(req);

  gLastDailyEmailSentEpoch = now;
  Serial.println(F("Daily email queued"));
}

static void publishViewerSummary() {
  DynamicJsonDocument doc(TANK_JSON_CAPACITY + 1024);
  doc["sn"] = gConfig.serverName;
  doc["si"] = gServerUid;
  double now = currentEpoch();
  doc["ge"] = now;
  doc["rs"] = VIEWER_SUMMARY_INTERVAL_SECONDS;
  doc["bh"] = VIEWER_SUMMARY_BASE_HOUR;
  JsonArray arr = doc.createNestedArray("tanks");
  for (uint8_t i = 0; i < gTankRecordCount; ++i) {
    JsonObject obj = arr.createNestedObject();
    obj["c"] = gTankRecords[i].clientUid;
    obj["s"] = gTankRecords[i].site;
    obj["n"] = gTankRecords[i].label;
    obj["k"] = gTankRecords[i].tankNumber;
    obj["l"] = roundTo(gTankRecords[i].levelInches, 1);
    // Include raw sensor readings if available
    if (gTankRecords[i].sensorMa >= 4.0f) {
      obj["ma"] = roundTo(gTankRecords[i].sensorMa, 2);
    }
    if (gTankRecords[i].sensorVoltage > 0.0f) {
      obj["vt"] = roundTo(gTankRecords[i].sensorVoltage, 3);
    }
    // Include object type if known (tank, engine, pump, gas, flow)
    if (gTankRecords[i].objectType[0] != '\0') {
      obj["ot"] = gTankRecords[i].objectType;
    }
    // Include sensor interface type if known (digital, analog, currentLoop, pulse)
    if (gTankRecords[i].sensorType[0] != '\0') {
      obj["st"] = gTankRecords[i].sensorType;
    }
    // Include measurement unit if known (inches, rpm, psi, gpm, etc.)
    if (gTankRecords[i].measurementUnit[0] != '\0') {
      obj["mu"] = gTankRecords[i].measurementUnit;
    }
    // Include 24hr change if available
    if (gTankRecords[i].previousLevelEpoch > 0.0) {
      obj["d"] = roundTo(gTankRecords[i].levelInches - gTankRecords[i].previousLevelInches, 1);
    }
    obj["a"] = gTankRecords[i].alarmActive;
    obj["at"] = gTankRecords[i].alarmType;
    obj["u"] = gTankRecords[i].lastUpdateEpoch;
    
    // Add VIN voltage from client metadata if available
    ClientMetadata *meta = findClientMetadata(gTankRecords[i].clientUid);
    if (meta && meta->vinVoltage > 0.0f) {
      obj["v"] = roundTo(meta->vinVoltage, 2);
    }
  }

  String json;
  if (serializeJson(doc, json) == 0) {
    Serial.println(F("Viewer summary serialization failed"));
    return;
  }

  J *req = notecard.newRequest("note.add");
  if (!req) {
    return;
  }
  JAddStringToObject(req, "file", VIEWER_SUMMARY_FILE);
  JAddBoolToObject(req, "sync", true);
  J *body = JParse(json.c_str());
  if (!body) {
    notecard.deleteResponse(req);  // Free the request to prevent memory leak
    Serial.println(F("Viewer summary JSON parse failed"));
    return;
  }
  JAddItemToObject(req, "body", body);
  bool queued = notecard.sendRequest(req);
  if (queued) {
    gLastViewerSummaryEpoch = now;
    Serial.println(F("Viewer summary queued"));
  } else {
    Serial.println(F("Viewer summary queue failed"));
  }
}

static ClientConfigSnapshot *findClientConfigSnapshot(const char *clientUid) {
  if (!clientUid) {
    return nullptr;
  }
  for (uint8_t i = 0; i < gClientConfigCount; ++i) {
    if (strcmp(gClientConfigs[i].uid, clientUid) == 0) {
      return &gClientConfigs[i];
    }
  }
  return nullptr;
}

// Convert raw 4-20mA reading to level/value using sensor config from client config snapshot
// Returns the computed level, or 0.0 if config not found or invalid mA
static float convertMaToLevel(const char *clientUid, uint8_t tankNumber, float mA) {
  if (mA < 4.0f || mA > 20.0f) {
    return 0.0f;  // Invalid mA reading
  }
  
  ClientConfigSnapshot *snap = findClientConfigSnapshot(clientUid);
  if (!snap || strlen(snap->payload) == 0) {
    // No config snapshot available - use simple linear 4-20mA mapping
    // Assume 0-100 range as fallback
    return ((mA - 4.0f) / 16.0f) * 100.0f;
  }
  
  // Parse the config snapshot to find the tank settings
  DynamicJsonDocument doc(1536);
  DeserializationError err = deserializeJson(doc, snap->payload);
  if (err) {
    return ((mA - 4.0f) / 16.0f) * 100.0f;  // Fallback
  }
  
  // Find the tank in the config
  JsonArray tanks = doc["tanks"];
  if (!tanks) {
    return ((mA - 4.0f) / 16.0f) * 100.0f;  // Fallback
  }
  
  for (JsonVariant t : tanks) {
    uint8_t tn = t["tankNumber"] | 0;
    if (tn == tankNumber) {
      // Found the tank - get sensor range settings
      float rangeMin = t["sensorRangeMin"] | 0.0f;
      float rangeMax = t["sensorRangeMax"] | 5.0f;  // Default 0-5 PSI
      float mountHeight = t["sensorMountHeight"] | 0.0f;
      const char *currentLoopType = t["currentLoopType"] | "pressure";
      
      // Calculate the fraction within 4-20mA range
      float fraction = (mA - 4.0f) / 16.0f;
      
      // For pressure sensors: 4mA = rangeMin, 20mA = rangeMax
      // For ultrasonic: 4mA = rangeMin (full), 20mA = rangeMax (empty) - inverted
      float sensorValue;
      if (strcmp(currentLoopType, "ultrasonic") == 0) {
        // Ultrasonic: 4mA = full (rangeMin distance), 20mA = empty (rangeMax distance)
        // Level = mountHeight - distance
        float distance = rangeMin + fraction * (rangeMax - rangeMin);
        sensorValue = mountHeight - distance;
        if (sensorValue < 0.0f) sensorValue = 0.0f;
      } else {
        // Pressure sensor: 4mA = rangeMin, 20mA = rangeMax
        // Return raw sensor value in native units (PSI, bar, etc.)
        sensorValue = rangeMin + fraction * (rangeMax - rangeMin);
        // Add mount height offset for pressure sensors
        // (Note: for pure gas pressure monitoring, mountHeight is typically 0)
      }
      return sensorValue;
    }
  }
  
  // Tank not found in config - use fallback
  return ((mA - 4.0f) / 16.0f) * 100.0f;
}

// Convert raw voltage reading to level/value using sensor config from client config snapshot
// Returns the computed level, or 0.0 if config not found or invalid voltage
static float convertVoltageToLevel(const char *clientUid, uint8_t tankNumber, float voltage) {
  if (voltage < 0.0f || voltage > 12.0f) {
    return 0.0f;  // Invalid voltage reading (allow up to 12V for headroom)
  }
  
  ClientConfigSnapshot *snap = findClientConfigSnapshot(clientUid);
  if (!snap || strlen(snap->payload) == 0) {
    // No config snapshot available - use simple linear 0-10V mapping
    // Assume 0-100 range as fallback
    return (voltage / 10.0f) * 100.0f;
  }
  
  // Parse the config snapshot to find the tank settings
  DynamicJsonDocument doc(1536);
  DeserializationError err = deserializeJson(doc, snap->payload);
  if (err) {
    return (voltage / 10.0f) * 100.0f;  // Fallback
  }
  
  // Find the tank in the config
  JsonArray tanks = doc["tanks"];
  if (!tanks) {
    return (voltage / 10.0f) * 100.0f;  // Fallback
  }
  
  for (JsonVariant t : tanks) {
    uint8_t tn = t["tankNumber"] | 0;
    if (tn == tankNumber) {
      // Found the tank - get sensor range settings
      float voltageMin = t["analogVoltageMin"] | 0.0f;   // e.g., 0V or 1V
      float voltageMax = t["analogVoltageMax"] | 10.0f;  // e.g., 10V or 5V
      float rangeMin = t["sensorRangeMin"] | 0.0f;       // e.g., 0 PSI
      float rangeMax = t["sensorRangeMax"] | 100.0f;     // e.g., 100 inches or 5 PSI
      float mountHeight = t["sensorMountHeight"] | 0.0f;
      
      // Validate voltage range
      float voltageRange = voltageMax - voltageMin;
      if (voltageRange <= 0.0f) {
        return (voltage / 10.0f) * 100.0f;  // Invalid config fallback
      }
      
      // Calculate the fraction within the voltage range
      float fraction = (voltage - voltageMin) / voltageRange;
      if (fraction < 0.0f) fraction = 0.0f;
      if (fraction > 1.0f) fraction = 1.0f;
      
      // Map voltage to sensor's native range
      float sensorValue = rangeMin + fraction * (rangeMax - rangeMin);
      
      // Add mount height offset (for pressure sensors measuring liquid column)
      // Note: for pure pressure monitoring applications, mountHeight is typically 0
      return sensorValue + mountHeight;
    }
  }
  
  // Tank not found in config - use fallback
  return (voltage / 10.0f) * 100.0f;
}

static void loadClientConfigSnapshots() {
  gClientConfigCount = 0;
#ifdef FILESYSTEM_AVAILABLE
  #if defined(ARDUINO_OPTA) || defined(ARDUINO_ARCH_MBED)
    if (!mbedFS) {
      return;
    }
    
    FILE *file = fopen("/fs/client_config_cache.txt", "r");
    if (!file) {
      return;
    }
    
    // Buffer size: uid + tab + payload + newline + null terminator
    char lineBuffer[sizeof(((ClientConfigSnapshot*)0)->uid) + 1 + sizeof(((ClientConfigSnapshot*)0)->payload) + 2];
    while (fgets(lineBuffer, sizeof(lineBuffer), file) != nullptr && gClientConfigCount < MAX_CLIENT_CONFIG_SNAPSHOTS) {
      // Check if line was truncated (no newline at end of non-empty buffer)
      size_t buflen = strlen(lineBuffer);
      if (buflen == sizeof(lineBuffer) - 1 && lineBuffer[sizeof(lineBuffer) - 2] != '\n') {
        Serial.println(F("Warning: truncated line in client config cache"));
        // Skip the rest of the truncated line
        int c;
        while ((c = fgetc(file)) != '\n' && c != EOF) { }
        continue;
      }
      String line = String(lineBuffer);
      line.trim();
      if (line.length() == 0) {
        continue;
      }
      int sep = line.indexOf('\t');
      if (sep <= 0) {
        continue;
      }

      String uid = line.substring(0, sep);
      String json = line.substring(sep + 1);

      ClientConfigSnapshot &snap = gClientConfigs[gClientConfigCount++];
      memset(&snap, 0, sizeof(ClientConfigSnapshot));
      strlcpy(snap.uid, uid.c_str(), sizeof(snap.uid));

      size_t len = json.length();
      if (len >= sizeof(snap.payload)) {
        len = sizeof(snap.payload) - 1;
      }
      memcpy(snap.payload, json.c_str(), len);
      snap.payload[len] = '\0';

      DynamicJsonDocument doc(512);
      if (deserializeJson(doc, snap.payload) == DeserializationError::Ok) {
        const char *site = doc["site"] | "";
        strlcpy(snap.site, site, sizeof(snap.site));
      } else {
        snap.site[0] = '\0';
      }
    }

    fclose(file);
  #else
    if (!LittleFS.exists(CLIENT_CONFIG_CACHE_PATH)) {
      return;
    }

    File file = LittleFS.open(CLIENT_CONFIG_CACHE_PATH, "r");
    if (!file) {
      return;
    }

    while (file.available() && gClientConfigCount < MAX_CLIENT_CONFIG_SNAPSHOTS) {
      String line = file.readStringUntil('\n');
      line.trim();
      if (line.length() == 0) {
        continue;
      }
      int sep = line.indexOf('\t');
      if (sep <= 0) {
        continue;
      }

      String uid = line.substring(0, sep);
      String json = line.substring(sep + 1);

      ClientConfigSnapshot &snap = gClientConfigs[gClientConfigCount++];
      memset(&snap, 0, sizeof(ClientConfigSnapshot));
      strlcpy(snap.uid, uid.c_str(), sizeof(snap.uid));

      size_t len = json.length();
      if (len >= sizeof(snap.payload)) {
        len = sizeof(snap.payload) - 1;
      }
      memcpy(snap.payload, json.c_str(), len);
      snap.payload[len] = '\0';

      DynamicJsonDocument doc(512);
      if (deserializeJson(doc, snap.payload) == DeserializationError::Ok) {
        const char *site = doc["site"] | "";
        strlcpy(snap.site, site, sizeof(snap.site));
      } else {
        snap.site[0] = '\0';
      }
    }

    file.close();
  #endif
#endif
}

static void saveClientConfigSnapshots() {
#ifdef FILESYSTEM_AVAILABLE
  #if defined(ARDUINO_OPTA) || defined(ARDUINO_ARCH_MBED)
    if (!mbedFS) {
      return;
    }
    
    FILE *file = fopen("/fs/client_config_cache.txt", "w");
    if (!file) {
      return;
    }

    for (uint8_t i = 0; i < gClientConfigCount; ++i) {
      if (fprintf(file, "%s\t%s\n", gClientConfigs[i].uid, gClientConfigs[i].payload) < 0) {
        Serial.println(F("Failed to write client config cache"));
        fclose(file);
        remove("/fs/client_config_cache.txt");
        return;
      }
    }

    fclose(file);
  #else
    File file = LittleFS.open(CLIENT_CONFIG_CACHE_PATH, "w");
    if (!file) {
      return;
    }

    for (uint8_t i = 0; i < gClientConfigCount; ++i) {
      file.print(gClientConfigs[i].uid);
      file.print('\t');
      file.println(gClientConfigs[i].payload);
    }

    file.close();
  #endif
#endif
}

static void cacheClientConfigFromBuffer(const char *clientUid, const char *buffer) {
  if (!clientUid || !buffer) {
    return;
  }

  // Validate buffer length before processing
  size_t bufferLen = strlen(buffer);
  if (bufferLen == 0 || bufferLen >= sizeof(((ClientConfigSnapshot*)0)->payload)) {
    Serial.println(F("Config buffer invalid size"));
    return;
  }

  DynamicJsonDocument doc(1024);
  if (deserializeJson(doc, buffer) != DeserializationError::Ok) {
    return;
  }

  ClientConfigSnapshot *snapshot = findClientConfigSnapshot(clientUid);
  if (!snapshot) {
    if (gClientConfigCount >= MAX_CLIENT_CONFIG_SNAPSHOTS) {
      return;
    }
    snapshot = &gClientConfigs[gClientConfigCount++];
    memset(snapshot, 0, sizeof(ClientConfigSnapshot));
    strlcpy(snapshot->uid, clientUid, sizeof(snapshot->uid));
  }

  const char *site = doc["site"] | "";
  strlcpy(snapshot->site, site, sizeof(snapshot->site));

  size_t len = bufferLen;
  if (len >= sizeof(snapshot->payload)) {
    len = sizeof(snapshot->payload) - 1;
  }
  memcpy(snapshot->payload, buffer, len);
  snapshot->payload[len] = '\0';

  saveClientConfigSnapshots();
}

// ============================================================================
// Calibration Learning System Implementation
// ============================================================================

static void sendCalibrationPage(EthernetClient &client) {
  size_t htmlLen = strlen_P(CALIBRATION_HTML);
  client.println(F("HTTP/1.1 200 OK"));
  client.println(F("Content-Type: text/html; charset=utf-8"));
  client.print(F("Content-Length: "));
  client.println(htmlLen);
  client.println(F("Cache-Control: no-cache, no-store, must-revalidate"));
  client.println();

  for (size_t i = 0; i < htmlLen; ++i) {
    char c = pgm_read_byte_near(CALIBRATION_HTML + i);
    client.write(c);
  }
}

static void sendContactsManager(EthernetClient &client) {
  size_t htmlLen = strlen_P(CONTACTS_MANAGER_HTML);
  client.println(F("HTTP/1.1 200 OK"));
  client.println(F("Content-Type: text/html; charset=utf-8"));
  client.print(F("Content-Length: "));
  client.println(htmlLen);
  client.println(F("Cache-Control: no-cache, no-store, must-revalidate"));
  client.println();

  for (size_t i = 0; i < htmlLen; ++i) {
    char c = pgm_read_byte_near(CONTACTS_MANAGER_HTML + i);
    client.write(c);
  }
}

static void sendServerSettings(EthernetClient &client) {
  size_t htmlLen = strlen_P(SERVER_SETTINGS_HTML);
  client.println(F("HTTP/1.1 200 OK"));
  client.println(F("Content-Type: text/html; charset=utf-8"));
  client.print(F("Content-Length: "));
  client.println(htmlLen);
  client.println(F("Cache-Control: no-cache, no-store, must-revalidate"));
  client.println();

  for (size_t i = 0; i < htmlLen; ++i) {
    char c = pgm_read_byte_near(SERVER_SETTINGS_HTML + i);
    client.write(c);
  }
}

static void handleContactsGet(EthernetClient &client) {
  // Build JSON response with contacts data, sites, and alarms
  // Size: contacts(100 max × ~200) + sites(32 × 32) + alarms(32 × 160) + overhead
  // Worst case: 20000 + 1024 + 5120 + 512 = ~27KB, use heap allocation
  static const size_t CONTACTS_JSON_CAPACITY = 32768;  // 32KB
  std::unique_ptr<DynamicJsonDocument> docPtr(new DynamicJsonDocument(CONTACTS_JSON_CAPACITY));
  if (!docPtr) {
    respondStatus(client, 500, F("Server Out of Memory"));
    return;
  }
  DynamicJsonDocument &doc = *docPtr;
  
  // Load contacts from config file if it exists
  JsonArray contactsArray = doc.createNestedArray("contacts");
  JsonArray dailyReportArray = doc.createNestedArray("dailyReportRecipients");
  
  // For now, return empty arrays - this will be populated from stored config
  /*
    Contacts will be stored in a separate config file.

    Planned implementation details:

    - File path: "/contacts_config.json" (stored in LittleFS)
    - Format: JSON object with contacts array and dailyReportRecipients array
    - JSON schema for each contact:
        {
          "id": string,            // Unique identifier (e.g., "contact_1234567890_abc123")
          "name": string,          // Full name of the contact
          "phone": string,         // Phone number in E.164 format (for SMS)
          "email": string,         // Email address (for daily reports)
          "alarmAssociations": [string]  // Array of alarm IDs (format: "clientUid_tankNumber")
        }

    - Example contacts_config.json:
      {
        "contacts": [
          {
            "id": "contact_1234567890_abc123",
            "name": "Alice Smith",
            "phone": "+15551234567",
            "email": "alice@example.com",
            "alarmAssociations": ["dev:123456_1", "dev:789012_2"]
          }
        ],
        "dailyReportRecipients": ["contact_1234567890_abc123"]
      }

    - Integration with ServerConfig:
        The contacts config will be loaded at startup and whenever updated via web UI.
        When building alarm/email notifications, the server will:
        1. Look up contact by alarm ID in alarmAssociations
        2. Send SMS to contact.phone if present
        3. Send email to contact.email if present
        4. For daily reports, iterate dailyReportRecipients and send to each contact.email

    - Migration path:
        Existing hardcoded smsPrimary/smsSecondary/dailyEmail in ServerConfig will be
        migrated to contacts on first boot after upgrade. Migration creates contacts
        from legacy fields if contacts_config.json doesn't exist.
  */
  
  // Build list of unique sites from tank records
  // Use simple linear scan - with typical fleet sizes (< 100 tanks), performance is adequate
  JsonArray sitesArray = doc.createNestedArray("sites");
  for (uint8_t i = 0; i < gTankRecordCount; ++i) {
    if (strlen(gTankRecords[i].site) == 0) continue;
    
    bool alreadySeen = false;
    for (uint8_t j = 0; j < i; ++j) {
      if (strcmp(gTankRecords[i].site, gTankRecords[j].site) == 0) {
        alreadySeen = true;
        break;
      }
    }
    if (!alreadySeen) {
      sitesArray.add(gTankRecords[i].site);
    }
  }
  
  // Build list of alarms (tanks with alarm configurations)
  JsonArray alarmsArray = doc.createNestedArray("alarms");
  for (uint8_t i = 0; i < gTankRecordCount; ++i) {
    TankRecord &tank = gTankRecords[i];
    if (strlen(tank.alarmType) > 0) {
      JsonObject alarmObj = alarmsArray.createNestedObject();
      char alarmId[64];
      snprintf(alarmId, sizeof(alarmId), "%s_%d", tank.clientUid, tank.tankNumber);
      alarmObj["id"] = alarmId;
      alarmObj["site"] = tank.site;
      alarmObj["tank"] = tank.label;
      alarmObj["type"] = tank.alarmType;
    }
  }
  
  String response;
  serializeJson(doc, response);
  // unique_ptr automatically handles cleanup
  respondJson(client, response);
}

static void handleContactsPost(EthernetClient &client, const String &body) {
  // Use larger buffer to match MAX_HTTP_BODY_BYTES (16KB) for large contact lists
  DynamicJsonDocument doc(MAX_HTTP_BODY_BYTES);
  if (deserializeJson(doc, body)) {
    respondStatus(client, 400, F("Invalid JSON"));
    return;
  }
  
  // Validate contacts structure even though not persisted yet
  if (doc.containsKey("contacts") && doc["contacts"].is<JsonArray>()) {
    JsonArray contactsArray = doc["contacts"].as<JsonArray>();
    
    // Limit number of contacts to prevent memory issues
    if (contactsArray.size() > 100) {
      respondStatus(client, 400, F("Too many contacts (max 100)"));
      return;
    }
    
    // Basic validation of each contact
    for (JsonVariant contactVar : contactsArray) {
      JsonObject contact = contactVar.as<JsonObject>();
      
      // Validate required fields
      if (!contact.containsKey("name") || !contact["name"].is<const char*>()) {
        respondStatus(client, 400, F("Contact missing required 'name' field"));
        return;
      }
      
      // Validate that at least phone or email is present
      bool hasPhone = contact.containsKey("phone") && contact["phone"].is<const char*>() && strlen(contact["phone"]) > 0;
      bool hasEmail = contact.containsKey("email") && contact["email"].is<const char*>() && strlen(contact["email"]) > 0;
      if (!hasPhone && !hasEmail) {
        respondStatus(client, 400, F("Contact must have phone or email"));
        return;
      }
      
      // Validate alarm associations array if present
      if (contact.containsKey("alarmAssociations") && !contact["alarmAssociations"].is<JsonArray>()) {
        respondStatus(client, 400, F("alarmAssociations must be an array"));
        return;
      }
    }
  }
  
  // NOTE: Contact persistence not yet implemented
  // Future implementation will:
  // 1. Store validated contacts in "/contacts_config.json"
  // 2. Load contacts during server initialization
  // 3. Integrate with existing SMS/email notification system
  // 4. Replace hardcoded phone/email fields in ServerConfig
  // For now, return success but data is not persisted across reboots
  
  DynamicJsonDocument response(256);
  response["success"] = true;
  response["message"] = "Contacts validated successfully (note: persistence not yet implemented)";
  
  String responseStr;
  serializeJson(response, responseStr);
  respondJson(client, responseStr);
}

static void handleServerSettingsPost(EthernetClient &client, const String &body) {
  StaticJsonDocument<2048> doc;
  DeserializationError error = deserializeJson(doc, body);

  if (error) {
    respondStatus(client, 400, "Invalid JSON");
    return;
  }

  // Require valid PIN for authentication
  const char *pinValue = doc["pin"].as<const char *>();
  if (!requireValidPin(client, pinValue)) {
    return;
  }

  // Extract server settings from JSON
  if (doc.containsKey("server")) {
    JsonObject serverObj = doc["server"];
    
    // Update SMS settings
    if (serverObj.containsKey("smsPrimary")) {
      strlcpy(globalServerConfig.smsPrimary, serverObj["smsPrimary"] | "", sizeof(globalServerConfig.smsPrimary));
    }
    if (serverObj.containsKey("smsSecondary")) {
      strlcpy(globalServerConfig.smsSecondary, serverObj["smsSecondary"] | "", sizeof(globalServerConfig.smsSecondary));
    }
    if (serverObj.containsKey("smsOnHigh")) {
      globalServerConfig.smsOnHigh = serverObj["smsOnHigh"] | false;
    }
    if (serverObj.containsKey("smsOnLow")) {
      globalServerConfig.smsOnLow = serverObj["smsOnLow"] | false;
    }
    if (serverObj.containsKey("smsOnClear")) {
      globalServerConfig.smsOnClear = serverObj["smsOnClear"] | false;
    }

    // Update daily email settings
    if (serverObj.containsKey("dailyHour")) {
      globalServerConfig.dailyHour = serverObj["dailyHour"] | 5;
    }
    if (serverObj.containsKey("dailyMinute")) {
      globalServerConfig.dailyMinute = serverObj["dailyMinute"] | 0;
    }
    if (serverObj.containsKey("dailyEmail")) {
      strlcpy(globalServerConfig.dailyEmail, serverObj["dailyEmail"] | "", sizeof(globalServerConfig.dailyEmail));
    }

    // Update FTP settings
    if (serverObj.containsKey("ftp")) {
      JsonObject ftpObj = serverObj["ftp"];
      if (ftpObj.containsKey("enabled")) {
        globalServerConfig.ftpEnabled = ftpObj["enabled"] | false;
      }
      if (ftpObj.containsKey("passive")) {
        globalServerConfig.ftpPassive = ftpObj["passive"] | true;
      }
      if (ftpObj.containsKey("backupOnChange")) {
        globalServerConfig.ftpBackupOnChange = ftpObj["backupOnChange"] | false;
      }
      if (ftpObj.containsKey("restoreOnBoot")) {
        globalServerConfig.ftpRestoreOnBoot = ftpObj["restoreOnBoot"] | false;
      }
      if (ftpObj.containsKey("host")) {
        strlcpy(globalServerConfig.ftpHost, ftpObj["host"] | "", sizeof(globalServerConfig.ftpHost));
      }
      if (ftpObj.containsKey("port")) {
        globalServerConfig.ftpPort = ftpObj["port"] | 21;
      }
      if (ftpObj.containsKey("user")) {
        strlcpy(globalServerConfig.ftpUser, ftpObj["user"] | "", sizeof(globalServerConfig.ftpUser));
      }
      if (ftpObj.containsKey("pass")) {
        strlcpy(globalServerConfig.ftpPass, ftpObj["pass"] | "", sizeof(globalServerConfig.ftpPass));
      }
      if (ftpObj.containsKey("path")) {
        strlcpy(globalServerConfig.ftpPath, ftpObj["path"] | "/tankalarm/server", sizeof(globalServerConfig.ftpPath));
      }
    }

    // Save configuration
    saveConfig(globalServerConfig);

    // Trigger FTP backup if enabled
    if (globalServerConfig.ftpEnabled && globalServerConfig.ftpBackupOnChange) {
      // FTP backup will be handled by existing FTP backup logic
    }
  }

  // Respond with success
  String responseStr = "{\"success\":true,\"message\":\"Settings saved\"}";
  respondJson(client, responseStr);
}

static TankCalibration *findOrCreateTankCalibration(const char *clientUid, uint8_t tankNumber) {
  if (!clientUid || strlen(clientUid) == 0) {
    return nullptr;
  }
  
  // Search for existing calibration
  for (uint8_t i = 0; i < gTankCalibrationCount; ++i) {
    if (strcmp(gTankCalibrations[i].clientUid, clientUid) == 0 && 
        gTankCalibrations[i].tankNumber == tankNumber) {
      return &gTankCalibrations[i];
    }
  }
  
  // Create new entry if space available
  if (gTankCalibrationCount < MAX_CALIBRATION_TANKS) {
    TankCalibration *cal = &gTankCalibrations[gTankCalibrationCount++];
    memset(cal, 0, sizeof(TankCalibration));
    strlcpy(cal->clientUid, clientUid, sizeof(cal->clientUid));
    cal->tankNumber = tankNumber;
    cal->hasLearnedCalibration = false;
    cal->entryCount = 0;
    cal->learnedSlope = 0.0f;
    cal->learnedOffset = 0.0f;
    cal->rSquared = 0.0f;
    return cal;
  }
  
  return nullptr;
}

// Simple linear regression using calibration log entries
// Returns true if enough data points exist for valid calibration
static void recalculateCalibration(TankCalibration *cal) {
  if (!cal) {
    return;
  }
  
  if (cal->entryCount < 2) {
    cal->hasLearnedCalibration = false;
    return;
  }
  
  // Read calibration entries from file and compute linear regression
  // For simplicity in embedded context, we'll re-read entries when needed
  
#ifdef FILESYSTEM_AVAILABLE
  float sumX = 0, sumY = 0, sumXY = 0, sumX2 = 0, sumY2 = 0;
  int count = 0;
  
  #if defined(ARDUINO_OPTA) || defined(ARDUINO_ARCH_MBED)
    FILE *file = fopen("/fs/calibration_log.txt", "r");
    if (!file) {
      cal->hasLearnedCalibration = false;
      return;
    }
    
    char lineBuffer[256];
    while (fgets(lineBuffer, sizeof(lineBuffer), file) != nullptr && count < MAX_CALIBRATION_ENTRIES) {
      String line = String(lineBuffer);
      line.trim();
      if (line.length() == 0) continue;
      
      // Parse: clientUid\ttankNumber\ttimestamp\tsensorReading\tverifiedLevel\tnotes
      int pos1 = line.indexOf('\t');
      if (pos1 < 0) continue;
      String uid = line.substring(0, pos1);
      
      int pos2 = line.indexOf('\t', pos1 + 1);
      if (pos2 < 0) continue;
      int tankNum = line.substring(pos1 + 1, pos2).toInt();
      
      // Check if this entry matches our tank
      if (uid != String(cal->clientUid) || tankNum != cal->tankNumber) continue;
      
      int pos3 = line.indexOf('\t', pos2 + 1);
      if (pos3 < 0) continue;
      
      int pos4 = line.indexOf('\t', pos3 + 1);
      if (pos4 < 0) continue;
      float sensorReading = line.substring(pos3 + 1, pos4).toFloat();
      
      int pos5 = line.indexOf('\t', pos4 + 1);
      float verifiedLevel;
      if (pos5 < 0) {
        verifiedLevel = line.substring(pos4 + 1).toFloat();
      } else {
        verifiedLevel = line.substring(pos4 + 1, pos5).toFloat();
      }
      
      // Only include valid readings (4-20mA range)
      if (sensorReading >= 4.0f && sensorReading <= 20.0f && verifiedLevel >= 0.0f) {
        sumX += sensorReading;
        sumY += verifiedLevel;
        sumXY += sensorReading * verifiedLevel;
        sumX2 += sensorReading * sensorReading;
        sumY2 += verifiedLevel * verifiedLevel;
        count++;
      }
    }
    
    fclose(file);
  #else
    if (!LittleFS.exists(CALIBRATION_LOG_PATH)) {
      cal->hasLearnedCalibration = false;
      return;
    }
    
    File file = LittleFS.open(CALIBRATION_LOG_PATH, "r");
    if (!file) {
      cal->hasLearnedCalibration = false;
      return;
    }
    
    while (file.available() && count < MAX_CALIBRATION_ENTRIES) {
      String line = file.readStringUntil('\n');
      line.trim();
      if (line.length() == 0) continue;
      
      // Parse: clientUid\ttankNumber\ttimestamp\tsensorReading\tverifiedLevel\tnotes
      int pos1 = line.indexOf('\t');
      if (pos1 < 0) continue;
      String uid = line.substring(0, pos1);
      
      int pos2 = line.indexOf('\t', pos1 + 1);
      if (pos2 < 0) continue;
      int tankNum = line.substring(pos1 + 1, pos2).toInt();
      
      // Check if this entry matches our tank
      if (uid != String(cal->clientUid) || tankNum != cal->tankNumber) continue;
      
      int pos3 = line.indexOf('\t', pos2 + 1);
      if (pos3 < 0) continue;
      
      int pos4 = line.indexOf('\t', pos3 + 1);
      if (pos4 < 0) continue;
      float sensorReading = line.substring(pos3 + 1, pos4).toFloat();
      
      int pos5 = line.indexOf('\t', pos4 + 1);
      float verifiedLevel;
      if (pos5 < 0) {
        verifiedLevel = line.substring(pos4 + 1).toFloat();
      } else {
        verifiedLevel = line.substring(pos4 + 1, pos5).toFloat();
      }
      
      // Only include valid readings (4-20mA range)
      if (sensorReading >= 4.0f && sensorReading <= 20.0f && verifiedLevel >= 0.0f) {
        sumX += sensorReading;
        sumY += verifiedLevel;
        sumXY += sensorReading * verifiedLevel;
        sumX2 += sensorReading * sensorReading;
        sumY2 += verifiedLevel * verifiedLevel;
        count++;
      }
    }
    
    file.close();
  #endif
  
  if (count < 2) {
    cal->hasLearnedCalibration = false;
    cal->entryCount = count;
    return;
  }
  
  // Calculate linear regression: y = slope * x + offset
  // slope = (n * sumXY - sumX * sumY) / (n * sumX2 - sumX^2)
  // offset = (sumY - slope * sumX) / n
  float n = (float)count;
  float denominator = n * sumX2 - sumX * sumX;
  
  if (fabs(denominator) < 0.0001f) {
    // Avoid division by zero - data points are too similar
    cal->hasLearnedCalibration = false;
    cal->entryCount = count;
    return;
  }
  
  cal->learnedSlope = (n * sumXY - sumX * sumY) / denominator;
  cal->learnedOffset = (sumY - cal->learnedSlope * sumX) / n;
  
  // Calculate R-squared (coefficient of determination)
  // R² = (Covariance(X,Y))² / (Variance(X) * Variance(Y))
  // Which equals: ssCovXY² / (ssX * ssTotal)
  float meanX = sumX / n;
  float meanY = sumY / n;
  float ssTotal = sumY2 - n * meanY * meanY;  // = Variance(Y) * n
  float ssX = sumX2 - n * meanX * meanX;       // = Variance(X) * n
  float ssCovXY = sumXY - n * meanX * meanY;   // = Covariance(X,Y) * n
  
  if (ssTotal > 0.0001f && ssX > 0.0001f) {
    // R² = ssCovXY² / (ssX * ssTotal)
    cal->rSquared = (ssCovXY * ssCovXY) / (ssX * ssTotal);
    if (cal->rSquared < 0.0f) cal->rSquared = 0.0f;
    if (cal->rSquared > 1.0f) cal->rSquared = 1.0f;
  } else {
    cal->rSquared = 0.0f;
  }
  
  cal->entryCount = count;
  cal->hasLearnedCalibration = true;
  cal->lastCalibrationEpoch = currentEpoch();
  
  Serial.print(F("Calibration updated for "));
  Serial.print(cal->clientUid);
  Serial.print(F(" tank "));
  Serial.print(cal->tankNumber);
  Serial.print(F(": slope="));
  Serial.print(cal->learnedSlope, 4);
  Serial.print(F(" in/mA, offset="));
  Serial.print(cal->learnedOffset, 2);
  Serial.print(F(" in, R²="));
  Serial.println(cal->rSquared, 3);
  
#else
  cal->hasLearnedCalibration = false;
#endif
}

static void saveCalibrationEntry(const char *clientUid, uint8_t tankNumber, double timestamp, 
                                  float sensorReading, float verifiedLevelInches, const char *notes) {
#ifdef FILESYSTEM_AVAILABLE
  // Format: clientUid\ttankNumber\ttimestamp\tsensorReading\tverifiedLevel\tnotes
  char entry[256];
  snprintf(entry, sizeof(entry), "%s\t%d\t%.0f\t%.2f\t%.2f\t%s\n",
           clientUid, tankNumber, timestamp, sensorReading, verifiedLevelInches, 
           notes ? notes : "");
  
  #if defined(ARDUINO_OPTA) || defined(ARDUINO_ARCH_MBED)
    FILE *file = fopen("/fs/calibration_log.txt", "a");
    if (file) {
      fputs(entry, file);
      fclose(file);
    }
  #else
    File file = LittleFS.open(CALIBRATION_LOG_PATH, "a");
    if (file) {
      file.print(entry);
      file.close();
    }
  #endif
  
  // Update calibration for this tank
  TankCalibration *cal = findOrCreateTankCalibration(clientUid, tankNumber);
  if (cal) {
    // recalculateCalibration will read the file and update entryCount
    recalculateCalibration(cal);
    saveCalibrationData();
  }
#endif
}

static void loadCalibrationData() {
  gTankCalibrationCount = 0;
  
#ifdef FILESYSTEM_AVAILABLE
  #if defined(ARDUINO_OPTA) || defined(ARDUINO_ARCH_MBED)
    FILE *file = fopen("/fs/calibration_data.txt", "r");
    if (!file) return;
    
    char lineBuffer[256];
    while (fgets(lineBuffer, sizeof(lineBuffer), file) != nullptr && gTankCalibrationCount < MAX_CALIBRATION_TANKS) {
      String line = String(lineBuffer);
      line.trim();
      if (line.length() == 0) continue;
      
      // Parse: clientUid\ttankNumber\tslope\toffset\trSquared\tentryCount\thasCalibration\tlastEpoch
      TankCalibration &cal = gTankCalibrations[gTankCalibrationCount];
      memset(&cal, 0, sizeof(TankCalibration));
      
      int pos1 = line.indexOf('\t');
      if (pos1 < 0) continue;
      strlcpy(cal.clientUid, line.substring(0, pos1).c_str(), sizeof(cal.clientUid));
      
      int pos2 = line.indexOf('\t', pos1 + 1);
      if (pos2 < 0) continue;
      cal.tankNumber = line.substring(pos1 + 1, pos2).toInt();
      
      int pos3 = line.indexOf('\t', pos2 + 1);
      if (pos3 < 0) continue;
      cal.learnedSlope = line.substring(pos2 + 1, pos3).toFloat();
      
      int pos4 = line.indexOf('\t', pos3 + 1);
      if (pos4 < 0) continue;
      cal.learnedOffset = line.substring(pos3 + 1, pos4).toFloat();
      
      int pos5 = line.indexOf('\t', pos4 + 1);
      if (pos5 < 0) continue;
      cal.rSquared = line.substring(pos4 + 1, pos5).toFloat();
      
      int pos6 = line.indexOf('\t', pos5 + 1);
      if (pos6 < 0) continue;
      cal.entryCount = line.substring(pos5 + 1, pos6).toInt();
      
      int pos7 = line.indexOf('\t', pos6 + 1);
      if (pos7 < 0) continue;
      cal.hasLearnedCalibration = line.substring(pos6 + 1, pos7).toInt() == 1;
      
      cal.lastCalibrationEpoch = atof(line.substring(pos7 + 1).c_str());
      
      gTankCalibrationCount++;
    }
    
    fclose(file);
  #else
    if (!LittleFS.exists("/calibration_data.txt")) return;
    
    File file = LittleFS.open("/calibration_data.txt", "r");
    if (!file) return;
    
    while (file.available() && gTankCalibrationCount < MAX_CALIBRATION_TANKS) {
      String line = file.readStringUntil('\n');
      line.trim();
      if (line.length() == 0) continue;
      
      TankCalibration &cal = gTankCalibrations[gTankCalibrationCount];
      memset(&cal, 0, sizeof(TankCalibration));
      
      int pos1 = line.indexOf('\t');
      if (pos1 < 0) continue;
      strlcpy(cal.clientUid, line.substring(0, pos1).c_str(), sizeof(cal.clientUid));
      
      int pos2 = line.indexOf('\t', pos1 + 1);
      if (pos2 < 0) continue;
      cal.tankNumber = line.substring(pos1 + 1, pos2).toInt();
      
      int pos3 = line.indexOf('\t', pos2 + 1);
      if (pos3 < 0) continue;
      cal.learnedSlope = line.substring(pos2 + 1, pos3).toFloat();
      
      int pos4 = line.indexOf('\t', pos3 + 1);
      if (pos4 < 0) continue;
      cal.learnedOffset = line.substring(pos3 + 1, pos4).toFloat();
      
      int pos5 = line.indexOf('\t', pos4 + 1);
      if (pos5 < 0) continue;
      cal.rSquared = line.substring(pos4 + 1, pos5).toFloat();
      
      int pos6 = line.indexOf('\t', pos5 + 1);
      if (pos6 < 0) continue;
      cal.entryCount = line.substring(pos5 + 1, pos6).toInt();
      
      int pos7 = line.indexOf('\t', pos6 + 1);
      if (pos7 < 0) continue;
      cal.hasLearnedCalibration = line.substring(pos6 + 1, pos7).toInt() == 1;
      
      cal.lastCalibrationEpoch = atof(line.substring(pos7 + 1).c_str());
      
      gTankCalibrationCount++;
    }
    
    file.close();
  #endif
#endif
  
  Serial.print(F("Loaded "));
  Serial.print(gTankCalibrationCount);
  Serial.println(F(" tank calibration records"));
}

static void saveCalibrationData() {
#ifdef FILESYSTEM_AVAILABLE
  #if defined(ARDUINO_OPTA) || defined(ARDUINO_ARCH_MBED)
    FILE *file = fopen("/fs/calibration_data.txt", "w");
    if (!file) return;
    
    for (uint8_t i = 0; i < gTankCalibrationCount; ++i) {
      TankCalibration &cal = gTankCalibrations[i];
      fprintf(file, "%s\t%d\t%.6f\t%.2f\t%.4f\t%d\t%d\t%.0f\n",
              cal.clientUid, cal.tankNumber, cal.learnedSlope, cal.learnedOffset,
              cal.rSquared, cal.entryCount, cal.hasLearnedCalibration ? 1 : 0,
              cal.lastCalibrationEpoch);
    }
    
    fclose(file);
  #else
    File file = LittleFS.open("/calibration_data.txt", "w");
    if (!file) return;
    
    for (uint8_t i = 0; i < gTankCalibrationCount; ++i) {
      TankCalibration &cal = gTankCalibrations[i];
      file.print(cal.clientUid);
      file.print('\t');
      file.print(cal.tankNumber);
      file.print('\t');
      file.print(cal.learnedSlope, 6);
      file.print('\t');
      file.print(cal.learnedOffset, 2);
      file.print('\t');
      file.print(cal.rSquared, 4);
      file.print('\t');
      file.print(cal.entryCount);
      file.print('\t');
      file.print(cal.hasLearnedCalibration ? 1 : 0);
      file.print('\t');
      file.println(cal.lastCalibrationEpoch, 0);
    }
    
    file.close();
  #endif
#endif
}

static void handleCalibrationGet(EthernetClient &client) {
  // Size: calibrations(20 × ~200 bytes) + logs(50 × ~180 bytes) + overhead
  // ~4000 + ~9000 + 512 = ~14KB, using 24KB for generous margin
  static const size_t CALIBRATION_JSON_CAPACITY = 24576;  // 24KB
  DynamicJsonDocument doc(CALIBRATION_JSON_CAPACITY);
  
  // Add calibration status for each tank
  JsonArray calibrationsArr = doc.createNestedArray("calibrations");
  for (uint8_t i = 0; i < gTankCalibrationCount; ++i) {
    TankCalibration &cal = gTankCalibrations[i];
    JsonObject obj = calibrationsArr.createNestedObject();
    obj["clientUid"] = cal.clientUid;
    obj["tankNumber"] = cal.tankNumber;
    obj["learnedSlope"] = cal.learnedSlope;
    obj["learnedOffset"] = cal.learnedOffset;
    obj["hasLearnedCalibration"] = cal.hasLearnedCalibration;
    obj["entryCount"] = cal.entryCount;
    obj["rSquared"] = cal.rSquared;
    obj["lastCalibrationEpoch"] = cal.lastCalibrationEpoch;
    obj["originalMaxValue"] = cal.originalMaxValue;
    obj["sensorType"] = cal.sensorType;
  }
  
  // Add recent calibration log entries
  JsonArray logsArr = doc.createNestedArray("logs");
  
#ifdef FILESYSTEM_AVAILABLE
  #if defined(ARDUINO_OPTA) || defined(ARDUINO_ARCH_MBED)
    FILE *file = fopen("/fs/calibration_log.txt", "r");
    if (file) {
      char lineBuffer[256];
      int count = 0;
      while (fgets(lineBuffer, sizeof(lineBuffer), file) != nullptr && count < 50) {
        String line = String(lineBuffer);
        line.trim();
        if (line.length() == 0) continue;
        
        // Parse entry
        int pos1 = line.indexOf('\t');
        if (pos1 < 0) continue;
        String uid = line.substring(0, pos1);
        
        int pos2 = line.indexOf('\t', pos1 + 1);
        if (pos2 < 0) continue;
        int tankNum = line.substring(pos1 + 1, pos2).toInt();
        
        int pos3 = line.indexOf('\t', pos2 + 1);
        if (pos3 < 0) continue;
        double timestamp = atof(line.substring(pos2 + 1, pos3).c_str());
        
        int pos4 = line.indexOf('\t', pos3 + 1);
        if (pos4 < 0) continue;
        float sensorReading = line.substring(pos3 + 1, pos4).toFloat();
        
        int pos5 = line.indexOf('\t', pos4 + 1);
        float verifiedLevel;
        String notes = "";
        if (pos5 < 0) {
          verifiedLevel = line.substring(pos4 + 1).toFloat();
        } else {
          verifiedLevel = line.substring(pos4 + 1, pos5).toFloat();
          notes = line.substring(pos5 + 1);
        }
        
        JsonObject logObj = logsArr.createNestedObject();
        logObj["clientUid"] = uid;
        logObj["tankNumber"] = tankNum;
        logObj["timestamp"] = timestamp;
        logObj["sensorReading"] = sensorReading;
        logObj["verifiedLevelInches"] = verifiedLevel;
        logObj["notes"] = notes;
        count++;
      }
      fclose(file);
    }
  #else
    if (LittleFS.exists(CALIBRATION_LOG_PATH)) {
      File file = LittleFS.open(CALIBRATION_LOG_PATH, "r");
      if (file) {
        int count = 0;
        while (file.available() && count < 50) {
          String line = file.readStringUntil('\n');
          line.trim();
          if (line.length() == 0) continue;
          
          // Parse entry
          int pos1 = line.indexOf('\t');
          if (pos1 < 0) continue;
          String uid = line.substring(0, pos1);
          
          int pos2 = line.indexOf('\t', pos1 + 1);
          if (pos2 < 0) continue;
          int tankNum = line.substring(pos1 + 1, pos2).toInt();
          
          int pos3 = line.indexOf('\t', pos2 + 1);
          if (pos3 < 0) continue;
          double timestamp = atof(line.substring(pos2 + 1, pos3).c_str());
          
          int pos4 = line.indexOf('\t', pos3 + 1);
          if (pos4 < 0) continue;
          float sensorReading = line.substring(pos3 + 1, pos4).toFloat();
          
          int pos5 = line.indexOf('\t', pos4 + 1);
          float verifiedLevel;
          String notes = "";
          if (pos5 < 0) {
            verifiedLevel = line.substring(pos4 + 1).toFloat();
          } else {
            verifiedLevel = line.substring(pos4 + 1, pos5).toFloat();
            notes = line.substring(pos5 + 1);
          }
          
          JsonObject logObj = logsArr.createNestedObject();
          logObj["clientUid"] = uid;
          logObj["tankNumber"] = tankNum;
          logObj["timestamp"] = timestamp;
          logObj["sensorReading"] = sensorReading;
          logObj["verifiedLevelInches"] = verifiedLevel;
          logObj["notes"] = notes;
          count++;
        }
        file.close();
      }
    }
  #endif
#endif
  
  String json;
  if (serializeJson(doc, json) == 0) {
    respondStatus(client, 500, F("Failed to encode calibration data"));
    return;
  }
  respondJson(client, json);
}

static void handleCalibrationPost(EthernetClient &client, const String &body) {
  DynamicJsonDocument doc(512);
  DeserializationError err = deserializeJson(doc, body);
  if (err) {
    respondStatus(client, 400, F("Invalid JSON"));
    return;
  }
  
  const char *clientUid = doc["clientUid"].as<const char *>();
  if (!clientUid || strlen(clientUid) == 0) {
    respondStatus(client, 400, F("Missing clientUid"));
    return;
  }
  
  if (!doc.containsKey("tankNumber")) {
    respondStatus(client, 400, F("Missing tankNumber"));
    return;
  }
  uint8_t tankNumber = doc["tankNumber"].as<uint8_t>();
  
  if (!doc.containsKey("verifiedLevelInches")) {
    respondStatus(client, 400, F("Missing verifiedLevelInches"));
    return;
  }
  float verifiedLevelInches = doc["verifiedLevelInches"].as<float>();
  
  // Optional fields
  float sensorReading = doc["sensorReading"].as<float>();
  if (!doc.containsKey("sensorReading") || sensorReading < 4.0f || sensorReading > 20.0f) {
    // Try to get raw sensorMa from tank record (sent directly from client)
    sensorReading = 0.0f;
    
    // Look up tank record to get raw sensorMa from latest telemetry
    for (uint8_t i = 0; i < gTankRecordCount; ++i) {
      if (strcmp(gTankRecords[i].clientUid, clientUid) == 0 && 
          gTankRecords[i].tankNumber == tankNumber) {
        // Use raw sensorMa if available (sent directly from client device)
        if (gTankRecords[i].sensorMa >= 4.0f && gTankRecords[i].sensorMa <= 20.0f) {
          sensorReading = gTankRecords[i].sensorMa;
          Serial.print(F("Using raw sensorMa from telemetry: "));
          Serial.print(sensorReading, 2);
          Serial.println(F(" mA"));
        }
        break;
      }
    }
  }
  
  double timestamp = doc["timestamp"].as<double>();
  if (timestamp <= 0.0) {
    timestamp = currentEpoch();
  }
  
  const char *notes = doc["notes"].as<const char *>();
  
  // Validate sensor reading - warn if not in valid range
  bool sensorReadingValid = (sensorReading >= 4.0f && sensorReading <= 20.0f);
  
  // Save the calibration entry
  saveCalibrationEntry(clientUid, tankNumber, timestamp, sensorReading, verifiedLevelInches, notes);
  
  Serial.print(F("Calibration entry added for "));
  Serial.print(clientUid);
  Serial.print(F(" tank "));
  Serial.print(tankNumber);
  Serial.print(F(": "));
  Serial.print(verifiedLevelInches, 1);
  Serial.print(F(" in @ "));
  Serial.print(sensorReading, 2);
  Serial.println(F(" mA"));
  
  if (!sensorReadingValid) {
    Serial.println(F("Warning: Sensor reading not in valid 4-20mA range, entry logged but won't be used for regression"));
    respondStatus(client, 200, F("Calibration entry saved (note: sensor reading outside 4-20mA range won't be used for calibration)"));
  } else {
    respondStatus(client, 200, F("Calibration entry saved"));
  }
}

