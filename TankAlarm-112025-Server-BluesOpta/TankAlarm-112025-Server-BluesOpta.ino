/*
  Tank Alarm Server 112025 - Arduino Opta + Blues Notecard

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
#include <Ethernet.h>
#include <math.h>
#include <string.h>
#include <ctype.h>
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
  // Arduino Opta with Mbed OS - filesystem and watchdog disabled for now
  // TODO: Implement Mbed OS LittleFileSystem and mbed::Watchdog support
  #warning "LittleFS and Watchdog features disabled on Mbed OS platform"
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

#ifndef MAX_EMAIL_BUFFER
#define MAX_EMAIL_BUFFER 2048
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

static const size_t TANK_JSON_CAPACITY = JSON_ARRAY_SIZE(MAX_TANK_RECORDS) + (MAX_TANK_RECORDS * JSON_OBJECT_SIZE(10)) + 512;
static const size_t CLIENT_JSON_CAPACITY = 24576;

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
};

struct TankRecord {
  char clientUid[48];
  char site[32];
  char label[24];
  uint8_t tankNumber;
  float heightInches;
  float levelInches;
  float percent;
  bool alarmActive;
  char alarmType[24];
  double lastUpdateEpoch;
  // Rate limiting for SMS alerts
  double lastSmsAlertEpoch;
  uint8_t smsAlertsInLastHour;
  double smsAlertTimestamps[10];  // Track last 10 SMS alerts per tank
};

static ServerConfig gConfig;
static bool gConfigDirty = false;

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

// Email rate limiting
static double gLastDailyEmailSentEpoch = 0.0;
#define MIN_DAILY_EMAIL_INTERVAL_SECONDS 3600  // Minimum 1 hour between daily emails

#ifndef ARDUINO_TANKALARM_HAS_STRLCPY
#define ARDUINO_TANKALARM_HAS_STRLCPY
size_t strlcpy(char *dst, const char *src, size_t size) {
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

static const char CONFIG_GENERATOR_HTML[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Config Generator</title>
  <style>
    :root {
      font-family: "Segoe UI", Arial, sans-serif;
      color-scheme: light dark;
    }
    * {
      box-sizing: border-box;
    }
    body {
      margin: 0;
      min-height: 100vh;
      background: var(--bg);
      color: var(--text);
      transition: background 0.2s ease, color 0.2s ease;
    }
    body[data-theme="light"] {
      --bg: #f8fafc;
      --surface: #ffffff;
      --text: #1f2933;
      --muted: #475569;
      --header-bg: #e2e8f0;
      --card-border: rgba(15,23,42,0.08);
      --card-shadow: rgba(15,23,42,0.08);
      --accent: #2563eb;
      --accent-strong: #1d4ed8;
      --accent-contrast: #f8fafc;
      --chip: #f8fafc;
      --input-border: #cbd5e1;
      --danger: #ef4444;
      --pill-bg: rgba(37,99,235,0.12);
    }
    body[data-theme="dark"] {
      --bg: #0f172a;
      --surface: #1e293b;
      --text: #e2e8f0;
      --muted: #94a3b8;
      --header-bg: #16213d;
      --card-border: rgba(15,23,42,0.55);
      --card-shadow: rgba(0,0,0,0.55);
      --accent: #38bdf8;
      --accent-strong: #22d3ee;
      --accent-contrast: #0f172a;
      --chip: rgba(148,163,184,0.15);
      --input-border: rgba(148,163,184,0.4);
      --danger: #f87171;
      --pill-bg: rgba(56,189,248,0.18);
    }
    header {
      background: var(--header-bg);
      padding: 28px 24px;
      box-shadow: 0 20px 45px var(--card-shadow);
    }
    header .bar {
      display: flex;
      justify-content: space-between;
      gap: 16px;
      flex-wrap: wrap;
      align-items: flex-start;
    }
    header h1 {
      margin: 0;
      font-size: 1.9rem;
    }
    header p {
      margin: 8px 0 0;
      color: var(--muted);
      max-width: 640px;
      line-height: 1.4;
    }
    .header-actions {
      display: flex;
      gap: 12px;
      flex-wrap: wrap;
      align-items: center;
    }
    .pill {
      border-radius: 999px;
      padding: 10px 20px;
      text-decoration: none;
      font-weight: 600;
      background: var(--pill-bg);
      color: var(--accent);
      border: 1px solid transparent;
      transition: transform 0.15s ease;
    }
    .pill:hover {
      transform: translateY(-1px);
    }
    .icon-button {
      width: 42px;
      height: 42px;
      border-radius: 50%;
      border: 1px solid var(--card-border);
      background: var(--surface);
      color: var(--text);
      font-size: 1.2rem;
      cursor: pointer;
      transition: transform 0.15s ease;
    }
    .icon-button:hover {
      transform: translateY(-1px);
    }
    main {
      padding: 24px;
      max-width: 1000px;
      margin: 0 auto;
      width: 100%;
    }
    .card {
      background: var(--surface);
      border-radius: 24px;
      border: 1px solid var(--card-border);
      padding: 20px;
      box-shadow: 0 25px 55px var(--card-shadow);
    }
    h2 {
      margin-top: 0;
      font-size: 1.3rem;
    }
    h3 {
      margin: 20px 0 10px;
      font-size: 1.1rem;
      border-bottom: 1px solid var(--card-border);
      padding-bottom: 6px;
      color: var(--text);
    }
    .field {
      display: flex;
      flex-direction: column;
      margin-bottom: 12px;
    }
    .field span {
      font-size: 0.9rem;
      color: var(--muted);
      margin-bottom: 4px;
    }
    .field input, .field select {
      padding: 10px 12px;
      border-radius: 8px;
      border: 1px solid var(--input-border);
      font-size: 0.95rem;
      background: var(--bg);
      color: var(--text);
    }
    .form-grid {
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
      gap: 12px;
    }
    .sensor-card {
      background: var(--chip);
      border: 1px solid var(--card-border);
      border-radius: 12px;
      padding: 16px;
      margin-bottom: 16px;
      position: relative;
    }
    .sensor-header {
      display: flex;
      justify-content: space-between;
      margin-bottom: 12px;
    }
    .sensor-title {
      font-weight: 600;
      color: var(--text);
    }
    .remove-btn {
      color: var(--danger);
      cursor: pointer;
      font-size: 0.9rem;
      border: none;
      background: none;
      padding: 0;
      font-weight: 600;
    }
    .remove-btn:hover {
      opacity: 0.8;
    }
    .actions {
      margin-top: 24px;
      display: flex;
      gap: 12px;
      flex-wrap: wrap;
    }
    button {
      border: none;
      border-radius: 10px;
      padding: 10px 16px;
      font-size: 0.95rem;
      font-weight: 600;
      cursor: pointer;
      background: var(--accent);
      color: var(--accent-contrast);
      transition: transform 0.15s ease;
    }
    button.secondary {
      background: transparent;
      border: 1px solid var(--card-border);
      color: var(--text);
    }
    button:hover {
      transform: translateY(-1px);
    }
    button:disabled {
      opacity: 0.5;
      cursor: not-allowed;
      transform: none;
    }
  </style>
</head>
<body data-theme="light">
  <header>
    <div class="bar">
      <div>
        <h1>Config Generator</h1>
        <p>
          Create new client configurations with sensor definitions and upload settings for Tank Alarm field units.
        </p>
      </div>
      <div class="header-actions">
        <button class="icon-button" id="themeToggle" aria-label="Switch to dark mode">&#9789;</button>
        <a class="pill" href="/">&larr; Back to Dashboard</a>
      </div>
    </div>
  </header>
  <main>
    <div class="card">
      <h2>New Client Configuration</h2>
      <form id="generatorForm">
        <div class="form-grid">
          <label class="field"><span>Site Name</span><input id="siteName" type="text" placeholder="Site Name" required></label>
          <label class="field"><span>Device Label</span><input id="deviceLabel" type="text" placeholder="Device Label" required></label>
          <label class="field"><span>Server Fleet</span><input id="serverFleet" type="text" value="tankalarm-server"></label>
          <label class="field"><span>Sample Seconds</span><input id="sampleSeconds" type="number" value="1800"></label>
          <label class="field"><span>Level Change Threshold (in)</span><input id="levelChangeThreshold" type="number" step="0.1" value="0" placeholder="0 = disabled"></label>
          <label class="field"><span>Report Hour</span><input id="reportHour" type="number" value="5"></label>
          <label class="field"><span>Report Minute</span><input id="reportMinute" type="number" value="0"></label>
          <label class="field"><span>Daily Email</span><input id="dailyEmail" type="email"></label>
        </div>
        
        <h3>Sensors</h3>
        <div id="sensorsContainer"></div>
        
        <div class="actions">
          <button type="button" id="addSensorBtn" class="secondary">+ Add Sensor</button>
          <button type="button" id="downloadBtn">Download Config</button>
        </div>
      </form>
    </div>
  </main>
  <script>
    // Theme support
    const THEME_KEY = 'tankalarmTheme';
    const themeToggle = document.getElementById('themeToggle');
    
    function applyTheme(next) {
      const theme = next === 'dark' ? 'dark' : 'light';
      document.body.dataset.theme = theme;
      themeToggle.textContent = theme === 'dark' ? '☀' : '☾';
      themeToggle.setAttribute('aria-label', theme === 'dark' ? 'Switch to light mode' : 'Switch to dark mode');
      localStorage.setItem(THEME_KEY, theme);
    }
    
    applyTheme(localStorage.getItem(THEME_KEY) || 'light');
    themeToggle.addEventListener('click', () => {
      const next = document.body.dataset.theme === 'dark' ? 'light' : 'dark';
      applyTheme(next);
    });

    const sensorTypes = [
      { value: 0, label: 'Digital Input' },
      { value: 1, label: 'Analog Input (0-10V)' },
      { value: 2, label: 'Current Loop (4-20mA)' }
    ];

    const monitorTypes = [
      { value: 'tank', label: 'Tank Level' },
      { value: 'gas', label: 'Gas Pressure' }
    ];
    
    const optaPins = [
      { value: 0, label: 'I1' },
      { value: 1, label: 'I2' },
      { value: 2, label: 'I3' },
      { value: 3, label: 'I4' },
      { value: 4, label: 'I5' },
      { value: 5, label: 'I6' },
      { value: 6, label: 'I7' },
      { value: 7, label: 'I8' }
    ];

    const expansionChannels = [
      { value: 0, label: 'I1' },
      { value: 1, label: 'I2' },
      { value: 2, label: 'I3' },
      { value: 3, label: 'I4' },
      { value: 4, label: 'I5' },
      { value: 5, label: 'I6' }
    ];

    let sensorCount = 0;

    function createSensorHtml(id) {
      return `
        <div class="sensor-card" id="sensor-${id}">
          <div class="sensor-header">
            <span class="sensor-title">Sensor #${id + 1}</span>
            <button type="button" class="remove-btn" onclick="removeSensor(${id})">Remove</button>
          </div>
          <div class="form-grid">
            <label class="field"><span>Monitor Type</span>
              <select class="monitor-type" onchange="updateMonitorFields(${id})">
                ${monitorTypes.map(t => `<option value="${t.value}">${t.label}</option>`).join('')}
              </select>
            </label>
            <label class="field tank-num-field"><span>Tank Number</span><input type="number" class="tank-num" value="${id + 1}"></label>
            <label class="field"><span><span class="name-label">Tank Name</span></span><input type="text" class="tank-name" placeholder="Name"></label>
            <label class="field"><span>Sensor Type</span>
              <select class="sensor-type" onchange="updatePinOptions(${id})">
                ${sensorTypes.map(t => `<option value="${t.value}">${t.label}</option>`).join('')}
              </select>
            </label>
            <label class="field"><span>Pin / Channel</span>
              <select class="sensor-pin">
                ${optaPins.map(p => `<option value="${p.value}">${p.label}</option>`).join('')}
              </select>
            </label>
            <label class="field"><span><span class="height-label">Height (in)</span></span><input type="number" class="tank-height" value="120"></label>
            <label class="field"><span>High Alarm</span><input type="number" class="high-alarm" value="100"></label>
            <label class="field"><span>Low Alarm</span><input type="number" class="low-alarm" value="20"></label>
          </div>
        </div>
      `;
    }

    function addSensor() {
      const container = document.getElementById('sensorsContainer');
      const div = document.createElement('div');
      div.innerHTML = createSensorHtml(sensorCount);
      container.appendChild(div.firstElementChild);
      sensorCount++;
    }

    window.removeSensor = function(id) {
      const el = document.getElementById(`sensor-${id}`);
      if (el) el.remove();
    };

    window.updateMonitorFields = function(id) {
      const card = document.getElementById(`sensor-${id}`);
      const type = card.querySelector('.monitor-type').value;
      const numField = card.querySelector('.tank-num-field');
      const nameLabel = card.querySelector('.name-label');
      const heightLabel = card.querySelector('.height-label');
      
      if (type === 'gas') {
        numField.style.display = 'none';
        nameLabel.textContent = 'System Name';
        heightLabel.textContent = 'Max Pressure';
      } else {
        numField.style.display = 'flex';
        nameLabel.textContent = 'Tank Name';
        heightLabel.textContent = 'Height (in)';
      }
    };

    window.updatePinOptions = function(id) {
      const card = document.getElementById(`sensor-${id}`);
      const typeSelect = card.querySelector('.sensor-type');
      const pinSelect = card.querySelector('.sensor-pin');
      const type = parseInt(typeSelect.value);
      
      pinSelect.innerHTML = '';
      let options = [];
      
      if (type === 2) { // Current Loop
        options = expansionChannels;
      } else { // Digital or Analog
        options = optaPins;
      }
      
      options.forEach(opt => {
        const option = document.createElement('option');
        option.value = opt.value;
        option.textContent = opt.label;
        pinSelect.appendChild(option);
      });
    };

    document.getElementById('addSensorBtn').addEventListener('click', addSensor);

    function sensorKeyFromValue(value) {
      switch (value) {
        case 0: return 'digital';
        case 2: return 'current';
        default: return 'analog';
      }
    }

    document.getElementById('downloadBtn').addEventListener('click', () => {
      const levelChange = parseFloat(document.getElementById('levelChangeThreshold').value);
      const config = {
        site: document.getElementById('siteName').value.trim(),
        deviceLabel: document.getElementById('deviceLabel').value.trim() || 'Client-112025',
        serverFleet: document.getElementById('serverFleet').value.trim() || 'tankalarm-server',
        sampleSeconds: parseInt(document.getElementById('sampleSeconds').value, 10) || 1800,
        levelChangeThreshold: Math.max(0, isNaN(levelChange) ? 0 : levelChange),
        reportHour: parseInt(document.getElementById('reportHour').value, 10) || 5,
        reportMinute: parseInt(document.getElementById('reportMinute').value, 10) || 0,
        dailyEmail: document.getElementById('dailyEmail').value.trim(),
        tanks: []
      };

      const sensorCards = document.querySelectorAll('.sensor-card');
      if (!sensorCards.length) {
        alert('Add at least one sensor before downloading a configuration.');
        return;
      }
      
      sensorCards.forEach((card, index) => {
        const monitorType = card.querySelector('.monitor-type').value;
        const type = parseInt(card.querySelector('.sensor-type').value);
        const pin = parseInt(card.querySelector('.sensor-pin').value);
        
        // For gas sensors, we hide the number but still need one for the firmware.
        // We'll use the index + 1.
        let tankNum = parseInt(card.querySelector('.tank-num').value) || (index + 1);
        let name = card.querySelector('.tank-name').value;
        
        if (monitorType === 'gas') {
           if (!name) name = `Gas System ${index + 1}`;
        } else {
           if (!name) name = `Tank ${index + 1}`;
        }
        
        const sensor = sensorKeyFromValue(type);

        const tank = {
          id: String.fromCharCode(65 + index), // A, B, C...
          name: name,
          number: tankNum,
          sensor: sensor,
          primaryPin: sensor === 'current' ? 0 : pin,
          secondaryPin: -1,
          loopChannel: sensor === 'current' ? pin : -1,
          heightInches: parseFloat(card.querySelector('.tank-height').value) || 120,
          highAlarm: parseFloat(card.querySelector('.high-alarm').value) || 100,
          lowAlarm: parseFloat(card.querySelector('.low-alarm').value) || 20,
          hysteresis: 2.0,
          daily: true,
          alarmSms: true,
          upload: true
        };
        config.tanks.push(tank);
      });

      const blob = new Blob([JSON.stringify(config, null, 2)], { type: 'application/json' });
      const url = URL.createObjectURL(blob);
      const a = document.createElement('a');
      a.href = url;
      a.download = 'client_config.json';
      document.body.appendChild(a);
      a.click();
      document.body.removeChild(a);
      URL.revokeObjectURL(url);
    });

    // Add one sensor by default
    addSensor();
  </script>
</body>
</html>
)HTML";

static const char DASHBOARD_HTML[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Tank Alarm Server</title>
  <style>
    :root {
      font-family: "Segoe UI", Arial, sans-serif;
      color-scheme: light dark;
    }
    * {
      box-sizing: border-box;
    }
    body {
      margin: 0;
      min-height: 100vh;
      background: var(--bg);
      color: var(--text);
      transition: background 0.2s ease, color 0.2s ease;
    }
    body[data-theme="light"] {
      --bg: #f8fafc;
      --surface: #ffffff;
      --muted: #475569;
      --header-bg: #e2e8f0;
      --card-border: rgba(15,23,42,0.08);
      --card-shadow: rgba(15,23,42,0.08);
      --accent: #2563eb;
      --accent-strong: #1d4ed8;
      --accent-contrast: #f8fafc;
      --chip: #eceff7;
      --table-border: rgba(15,23,42,0.08);
      --pill-bg: rgba(37,99,235,0.12);
      --alarm: #b91c1c;
      --ok: #0f766e;
    }
    body[data-theme="dark"] {
      --bg: #0f172a;
      --surface: #1e293b;
      --muted: #94a3b8;
      --header-bg: #16213d;
      --card-border: rgba(15,23,42,0.55);
      --card-shadow: rgba(0,0,0,0.55);
      --accent: #38bdf8;
      --accent-strong: #22d3ee;
      --accent-contrast: #0f172a;
      --chip: rgba(148,163,184,0.15);
      --table-border: rgba(255,255,255,0.12);
      --pill-bg: rgba(56,189,248,0.18);
      --alarm: #f87171;
      --ok: #34d399;
    }
    header {
      background: var(--header-bg);
      padding: 28px 24px;
      box-shadow: 0 20px 45px var(--card-shadow);
    }
    header .bar {
      display: flex;
      justify-content: space-between;
      gap: 16px;
      flex-wrap: wrap;
      align-items: flex-start;
    }
    header h1 {
      margin: 0;
      font-size: 1.9rem;
    }
    header p {
      margin: 8px 0 0;
      color: var(--muted);
      max-width: 640px;
      line-height: 1.4;
    }
    .header-actions {
      display: flex;
      gap: 12px;
      flex-wrap: wrap;
      align-items: center;
    }
    .pill {
      border-radius: 999px;
      padding: 10px 20px;
      text-decoration: none;
      font-weight: 600;
      background: var(--pill-bg);
      color: var(--accent);
      border: 1px solid transparent;
      transition: transform 0.15s ease;
    }
    .pill.secondary {
      background: transparent;
      border-color: var(--card-border);
      color: var(--muted);
    }
    .pill:hover {
      transform: translateY(-1px);
    }
    .icon-button {
      width: 42px;
      height: 42px;
      border-radius: 50%;
      border: 1px solid var(--card-border);
      background: var(--surface);
      color: var(--text);
      font-size: 1.2rem;
      cursor: pointer;
      transition: transform 0.15s ease;
    }
    .icon-button:hover {
      transform: translateY(-1px);
    }
    .meta-grid {
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(220px, 1fr));
      gap: 12px;
      margin-top: 20px;
    }
    .meta-card {
      background: var(--surface);
      border-radius: 16px;
      border: 1px solid var(--card-border);
      padding: 16px;
      box-shadow: 0 15px 35px var(--card-shadow);
    }
    .meta-card span {
      display: block;
      font-size: 0.8rem;
      letter-spacing: 0.08em;
      text-transform: uppercase;
      color: var(--muted);
    }
    .meta-card strong {
      display: block;
      margin-top: 6px;
      font-size: 1.05rem;
      word-break: break-all;
    }
    main {
      padding: 24px;
      max-width: 1400px;
      margin: 0 auto;
      width: 100%;
    }
    .stats-grid {
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(180px, 1fr));
      gap: 16px;
      margin-bottom: 20px;
    }
    .stat-card {
      background: var(--surface);
      border-radius: 16px;
      padding: 18px;
      border: 1px solid var(--card-border);
      box-shadow: 0 12px 30px var(--card-shadow);
    }
    .stat-card span {
      font-size: 0.85rem;
      color: var(--muted);
      text-transform: uppercase;
      letter-spacing: 0.08em;
    }
    .stat-card strong {
      display: block;
      margin-top: 8px;
      font-size: 1.8rem;
    }
    .filter-bar {
      display: flex;
      flex-wrap: wrap;
      gap: 16px;
      align-items: flex-end;
      background: var(--surface);
      border: 1px solid var(--card-border);
      border-radius: 20px;
      padding: 16px;
      box-shadow: 0 18px 40px var(--card-shadow);
      margin-bottom: 20px;
    }
    .filter-bar label {
      display: flex;
      flex-direction: column;
      font-size: 0.9rem;
      color: var(--muted);
      min-width: 220px;
    }
    select {
      appearance: none;
      border-radius: 10px;
      border: 1px solid var(--card-border);
      padding: 10px 12px;
      margin-top: 6px;
      background: var(--bg);
      color: var(--text);
    }
    .filter-actions {
      display: flex;
      gap: 12px;
      flex-wrap: wrap;
      align-items: center;
    }
    .btn {
      border: none;
      border-radius: 999px;
      padding: 10px 20px;
      font-weight: 600;
      cursor: pointer;
      background: linear-gradient(135deg, var(--accent), var(--accent-strong));
      color: var(--accent-contrast);
      box-shadow: 0 18px 40px rgba(37,99,235,0.35);
      transition: transform 0.15s ease, box-shadow 0.15s ease;
    }
    .btn.secondary {
      background: transparent;
      border: 1px solid var(--card-border);
      color: var(--text);
      box-shadow: none;
    }
    .btn:disabled {
      opacity: 0.5;
      cursor: not-allowed;
      transform: none;
      box-shadow: none;
    }
    .btn:not(:disabled):hover {
      transform: translateY(-1px);
    }
    .card {
      background: var(--surface);
      border-radius: 24px;
      border: 1px solid var(--card-border);
      padding: 20px;
      box-shadow: 0 25px 55px var(--card-shadow);
    }
    .card-head {
      display: flex;
      align-items: baseline;
      justify-content: space-between;
      gap: 12px;
      flex-wrap: wrap;
    }
    .badge {
      display: inline-flex;
      align-items: center;
      gap: 6px;
      border-radius: 999px;
      padding: 6px 12px;
      background: var(--chip);
      color: var(--muted);
      font-size: 0.8rem;
      font-weight: 600;
    }
    table {
      width: 100%;
      border-collapse: collapse;
      margin-top: 18px;
    }
    th, td {
      text-align: left;
      padding: 12px 10px;
      border-bottom: 1px solid var(--table-border);
      font-size: 0.9rem;
    }
    th {
      text-transform: uppercase;
      letter-spacing: 0.05em;
      font-size: 0.75rem;
      color: var(--muted);
    }
    tr:last-child td {
      border-bottom: none;
    }
    tr.alarm {
      background: rgba(220,38,38,0.08);
    }
    body[data-theme="dark"] tr.alarm {
      background: rgba(248,113,113,0.08);
    }
    .status-pill {
      display: inline-flex;
      align-items: center;
      gap: 6px;
      border-radius: 999px;
      padding: 4px 10px;
      font-size: 0.8rem;
      font-weight: 600;
    }
    .status-pill.ok {
      background: rgba(16,185,129,0.15);
      color: var(--ok);
    }
    .status-pill.alarm {
      background: rgba(220,38,38,0.15);
      color: var(--alarm);
    }
    .timestamp {
      font-size: 0.9rem;
      color: var(--muted);
    }
    #toast {
      position: fixed;
      left: 50%;
      bottom: 24px;
      transform: translateX(-50%);
      background: #0284c7;
      color: #fff;
      padding: 12px 18px;
      border-radius: 999px;
      box-shadow: 0 10px 30px rgba(15,23,42,0.25);
      opacity: 0;
      pointer-events: none;
      transition: opacity 0.3s ease;
      font-weight: 600;
    }
    #toast.show {
      opacity: 1;
    }
    .relay-btn {
      padding: 4px 8px;
      margin: 0 2px;
      border: 1px solid var(--card-border);
      border-radius: 4px;
      background: var(--surface);
      color: var(--text);
      font-size: 0.75rem;
      cursor: pointer;
      transition: all 0.2s ease;
    }
    .relay-btn:hover {
      background: var(--accent);
      color: var(--accent-contrast);
      border-color: var(--accent);
    }
    .relay-btn.active {
      background: var(--accent);
      color: var(--accent-contrast);
      border-color: var(--accent);
      font-weight: bold;
    }
    .relay-btn:disabled {
      opacity: 0.5;
      cursor: not-allowed;
    }
  </style>
</head>
<body data-theme="light">
  <header>
    <div class="bar">
      <div>
        <p class="timestamp">Tank Alarm Fleet · Live server telemetry</p>
        <h1 id="serverName">Tank Alarm Server</h1>
        <p>
          Monitor every field unit in one place. Filter by site, highlight alarms, and jump into the client console when you need to push configuration updates.
        </p>
      </div>
      <div class="header-actions">
        <button class="icon-button" id="themeToggle" aria-label="Switch to dark mode">&#9789;</button>
        <a class="pill" href="/client-console">Client Console</a>
        <a class="pill secondary" href="/config-generator">Config Generator</a>
      </div>
    </div>
    <div class="meta-grid">
      <div class="meta-card">
        <span>Server UID</span>
        <strong id="serverUid">--</strong>
      </div>
      <div class="meta-card">
        <span>Client Fleet</span>
        <strong id="fleetName">--</strong>
      </div>
      <div class="meta-card">
        <span>Next Daily Email</span>
        <strong id="nextEmail">--</strong>
      </div>
      <div class="meta-card">
        <span>Last Time Sync</span>
        <strong id="lastSync">--</strong>
      </div>
      <div class="meta-card">
        <span>PIN Status</span>
        <strong id="pinStatus">--</strong>
      </div>
      <div class="meta-card">
        <span>Last Dashboard Refresh</span>
        <strong id="lastRefresh">--</strong>
      </div>
    </div>
  </header>
  <main>
    <div class="stats-grid">
      <div class="stat-card">
        <span>Total Clients</span>
        <strong id="statClients">0</strong>
      </div>
      <div class="stat-card">
        <span>Active Tanks</span>
        <strong id="statTanks">0</strong>
      </div>
      <div class="stat-card">
        <span>Active Alarms</span>
        <strong id="statAlarms">0</strong>
      </div>
      <div class="stat-card">
        <span>Stale Tanks (&gt;60m)</span>
        <strong id="statStale">0</strong>
      </div>
    </div>
    <section class="filter-bar">
      <label>
        Site Filter
        <select id="siteFilter">
          <option value="">All Sites</option>
        </select>
      </label>
      <div class="filter-actions">
        <button class="btn" id="refreshSiteBtn">Refresh Selected Site</button>
        <button class="btn secondary" id="refreshAllBtn">Refresh All Sites</button>
        <span class="badge" id="autoRefreshHint">UI refresh 60 s · Server cadence 6 h</span>
      </div>
    </section>
    <section class="card">
      <div class="card-head">
        <h2 style="margin:0;">Fleet Telemetry</h2>
        <span class="timestamp">Rows update automatically while this page remains open.</span>
      </div>
      <table>
        <thead>
          <tr>
            <th>Client</th>
            <th>Site</th>
            <th>Tank</th>
            <th>Level (in)</th>
            <th>% Full</th>
            <th>Status</th>
            <th>Updated</th>
            <th>Relays</th>
          </tr>
        </thead>
        <tbody id="tankBody"></tbody>
      </table>
    </section>
  </main>
  <div id="toast"></div>
  <script>
    (() => {
      const THEME_KEY = 'tankalarmTheme';
      const DEFAULT_REFRESH_SECONDS = 60;
      const STALE_MINUTES = 60;

      const els = {
        themeToggle: document.getElementById('themeToggle'),
        serverName: document.getElementById('serverName'),
        serverUid: document.getElementById('serverUid'),
        fleetName: document.getElementById('fleetName'),
        nextEmail: document.getElementById('nextEmail'),
        lastSync: document.getElementById('lastSync'),
        lastRefresh: document.getElementById('lastRefresh'),
        pinStatus: document.getElementById('pinStatus'),
        autoRefreshHint: document.getElementById('autoRefreshHint'),
        siteFilter: document.getElementById('siteFilter'),
        refreshSiteBtn: document.getElementById('refreshSiteBtn'),
        refreshAllBtn: document.getElementById('refreshAllBtn'),
        tankBody: document.getElementById('tankBody'),
        statClients: document.getElementById('statClients'),
        statTanks: document.getElementById('statTanks'),
        statAlarms: document.getElementById('statAlarms'),
        statStale: document.getElementById('statStale'),
        toast: document.getElementById('toast')
      };

      const state = {
        clients: [],
        tanks: [],
        selected: '',
        refreshing: false,
        timer: null,
        uiRefreshSeconds: DEFAULT_REFRESH_SECONDS
      };

      function applyTheme(next) {
        const theme = next === 'dark' ? 'dark' : 'light';
        document.body.dataset.theme = theme;
        els.themeToggle.textContent = theme === 'dark' ? '☀' : '☾';
        els.themeToggle.setAttribute('aria-label', theme === 'dark' ? 'Switch to light mode' : 'Switch to dark mode');
        localStorage.setItem(THEME_KEY, theme);
      }
      applyTheme(localStorage.getItem(THEME_KEY) || 'light');
      els.themeToggle.addEventListener('click', () => {
        const next = document.body.dataset.theme === 'dark' ? 'light' : 'dark';
        applyTheme(next);
      });

      function showToast(message, isError) {
        els.toast.textContent = message;
        els.toast.style.background = isError ? '#dc2626' : '#0284c7';
        els.toast.classList.add('show');
        setTimeout(() => els.toast.classList.remove('show'), 2500);
      }

      function formatNumber(value) {
        return (typeof value === 'number' && isFinite(value)) ? value.toFixed(1) : '--';
      }

      function formatEpoch(epoch) {
        if (!epoch) return '--';
        const date = new Date(epoch * 1000);
        if (isNaN(date.getTime())) return '--';
        return date.toLocaleString();
      }

      function describeCadence(seconds) {
        if (!seconds) return '6 h';
        if (seconds < 3600) {
          return `${Math.round(seconds / 60)} m`;
        }
        const hours = (seconds / 3600).toFixed(1).replace(/\.0$/, '');
        return `${hours} h`;
      }

      function flattenTanks(clients) {
        const rows = [];
        clients.forEach(client => {
          const tanks = Array.isArray(client.tanks) ? client.tanks : [];
          if (!tanks.length) {
            rows.push({
              client: client.client,
              site: client.site,
              label: client.label || 'Tank',
              tank: client.tank || '--',
              levelInches: client.levelInches,
              percent: client.percent,
              alarm: client.alarm,
              alarmType: client.alarmType,
              lastUpdate: client.lastUpdate
            });
            return;
          }
          tanks.forEach(tank => {
            rows.push({
              client: client.client,
              site: client.site,
              label: tank.label || client.label || 'Tank',
              tank: tank.tank || '--',
              levelInches: tank.levelInches,
              percent: tank.percent,
              alarm: tank.alarm,
              alarmType: tank.alarmType || client.alarmType,
              lastUpdate: tank.lastUpdate
            });
          });
        });
        return rows;
      }

      function populateSiteFilter(preferredUid) {
        const select = els.siteFilter;
        const map = new Map();
        state.clients.forEach(client => {
          if (!client.client) return;
          const suffix = client.client.length > 6 ? client.client.slice(-6) : client.client;
          const label = client.site ? `${client.site} (${suffix})` : `Client ${suffix}`;
          map.set(client.client, label);
        });
        const previous = select.value;
        select.innerHTML = '<option value="">All Sites</option>';
        map.forEach((label, uid) => {
          const option = document.createElement('option');
          option.value = uid;
          option.textContent = label;
          select.appendChild(option);
        });
        const desired = preferredUid || previous;
        if (desired && map.has(desired)) {
          select.value = desired;
          state.selected = desired;
        } else if (!map.has(state.selected)) {
          state.selected = '';
          select.value = '';
        } else {
          select.value = state.selected || '';
        }
        updateButtonState();
      }

      function renderTankRows() {
        const tbody = els.tankBody;
        tbody.innerHTML = '';
        const rows = state.selected ? state.tanks.filter(t => t.client === state.selected) : state.tanks;
        if (!rows.length) {
          const tr = document.createElement('tr');
          tr.innerHTML = '<td colspan="7">No telemetry available</td>';
          tbody.appendChild(tr);
          return;
        }
        rows.forEach(row => {
          const tr = document.createElement('tr');
          if (row.alarm) tr.classList.add('alarm');
          tr.innerHTML = `
            <td><code>${row.client || '--'}</code></td>
            <td>${row.site || '--'}</td>
            <td>${row.label || 'Tank'} #${row.tank || '?'}</td>
            <td>${formatNumber(row.levelInches)}</td>
            <td>${formatNumber(row.percent)}</td>
            <td>${statusBadge(row)}</td>
            <td>${formatEpoch(row.lastUpdate)}</td>
            <td>${relayButtons(row)}</td>`;
          tbody.appendChild(tr);
        });
      }

      function statusBadge(row) {
        if (!row.alarm) {
          return '<span class="status-pill ok">Normal</span>';
        }
        const label = row.alarmType ? row.alarmType : 'Alarm';
        return `<span class="status-pill alarm">${label}</span>`;
      }

      function escapeHtml(unsafe) {
        if (!unsafe) return '';
        return String(unsafe)
          .replace(/&/g, '&amp;')
          .replace(/</g, '&lt;')
          .replace(/>/g, '&gt;')
          .replace(/"/g, '&quot;')
          .replace(/'/g, '&#039;');
      }

      function relayButtons(row) {
        if (!row.client || row.client === '--') return '--';
        const MAX_RELAYS = 4;
        const relays = Array.from({length: MAX_RELAYS}, (_, i) => i + 1);
        const escapedClient = escapeHtml(row.client);
        return relays.map(num => 
          `<button class="relay-btn" onclick="toggleRelay('${escapedClient}', ${num}, event)" title="Toggle Relay ${num}">R${num}</button>`
        ).join(' ');
      }

      async function toggleRelay(clientUid, relayNum, event) {
        const btn = event.target;
        const wasActive = btn.classList.contains('active');
        const newState = !wasActive;
        
        btn.disabled = true;
        try {
          const res = await fetch('/api/relay', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({
              clientUid: clientUid,
              relay: relayNum,
              state: newState
            })
          });
          if (!res.ok) {
            const text = await res.text();
            throw new Error(text || 'Relay command failed');
          }
          btn.classList.toggle('active', newState);
          showToast(`Relay ${relayNum} ${newState ? 'ON' : 'OFF'} command sent`);
        } catch (err) {
          showToast(err.message || 'Relay control failed', true);
        } finally {
          btn.disabled = false;
        }
      }
      window.toggleRelay = toggleRelay;

      function updateStats() {
        const clientIds = new Set();
        state.tanks.forEach(t => {
          if (t.client) {
            clientIds.add(t.client);
          }
        });
        els.statClients.textContent = clientIds.size;
        els.statTanks.textContent = state.tanks.length;
        els.statAlarms.textContent = state.tanks.filter(t => t.alarm).length;
        const cutoff = Date.now() - STALE_MINUTES * 60 * 1000;
        const stale = state.tanks.filter(t => !t.lastUpdate || (t.lastUpdate * 1000) < cutoff).length;
        els.statStale.textContent = stale;
      }

      function updateButtonState() {
        els.refreshAllBtn.disabled = state.refreshing;
        els.refreshSiteBtn.disabled = state.refreshing || !state.selected;
      }

      function scheduleUiRefresh() {
        if (state.timer) {
          clearInterval(state.timer);
        }
        state.timer = setInterval(() => {
          refreshData(state.selected);
        }, state.uiRefreshSeconds * 1000);
      }

      function updateRefreshHint(serverInfo) {
        const cadence = describeCadence(serverInfo && serverInfo.webRefreshSeconds);
        els.autoRefreshHint.textContent = `UI refresh ${state.uiRefreshSeconds} s · Server cadence ${cadence}`;
      }

      function applyServerData(data, preferredUid) {
        state.clients = data.clients || [];
        state.tanks = flattenTanks(state.clients);
        if (preferredUid) {
          state.selected = preferredUid;
        }
        const serverInfo = data.server || {};
        els.serverName.textContent = serverInfo.name || 'Tank Alarm Server';
        els.serverUid.textContent = data.serverUid || '--';
        els.fleetName.textContent = serverInfo.clientFleet || 'tankalarm-clients';
        els.nextEmail.textContent = formatEpoch(data.nextDailyEmailEpoch);
        els.lastSync.textContent = formatEpoch(data.lastSyncEpoch);
        els.pinStatus.textContent = serverInfo.pinConfigured ? 'Configured' : 'Not Set';
        els.lastRefresh.textContent = new Date().toLocaleString();
        state.uiRefreshSeconds = DEFAULT_REFRESH_SECONDS;
        updateRefreshHint(serverInfo);
        populateSiteFilter(preferredUid);
        renderTankRows();
        updateStats();
        scheduleUiRefresh();
      }

      async function refreshData(preferredUid) {
        try {
          const res = await fetch('/api/clients');
          if (!res.ok) {
            throw new Error('Failed to fetch fleet data');
          }
          const data = await res.json();
          applyServerData(data, preferredUid || state.selected);
        } catch (err) {
          showToast(err.message || 'Fleet refresh failed', true);
        }
      }

      async function triggerManualRefresh(targetUid) {
        const payload = targetUid ? { client: targetUid } : {};
        state.refreshing = true;
        updateButtonState();
        try {
          const res = await fetch('/api/refresh', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(payload)
          });
          if (!res.ok) {
            const text = await res.text();
            throw new Error(text || 'Refresh failed');
          }
          const data = await res.json();
          applyServerData(data, targetUid || state.selected);
          showToast(targetUid ? 'Selected site refreshed' : 'Fleet refresh queued');
        } catch (err) {
          showToast(err.message || 'Refresh failed', true);
        } finally {
          state.refreshing = false;
          updateButtonState();
        }
      }

      els.siteFilter.addEventListener('change', event => {
        state.selected = event.target.value;
        renderTankRows();
        updateButtonState();
      });
      els.refreshSiteBtn.addEventListener('click', () => {
        if (!state.selected) {
          showToast('Pick a site first.', true);
          return;
        }
        triggerManualRefresh(state.selected);
      });
      els.refreshAllBtn.addEventListener('click', () => {
        triggerManualRefresh(null);
      });

      refreshData();
    })();
  </script>
</body>
</html>
)HTML";

static const char CLIENT_CONSOLE_HTML[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Tank Alarm Client Console</title>
  <style>
    :root {
      font-family: "Segoe UI", Arial, sans-serif;
      color-scheme: light dark;
    }
    * {
      box-sizing: border-box;
    }
    body {
      margin: 0;
      min-height: 100vh;
      background: var(--bg);
      color: var(--text);
      transition: background 0.2s ease, color 0.2s ease;
    }
    body[data-theme="light"] {
      --bg: #f8fafc;
      --surface: #ffffff;
      --muted: #475569;
      --header-bg: #eef2ff;
      --card-border: rgba(15,23,42,0.08);
      --card-shadow: rgba(15,23,42,0.08);
      --accent: #2563eb;
      --accent-strong: #1d4ed8;
      --accent-contrast: #f8fafc;
      --chip: #e2e8f0;
      --danger: #dc2626;
    }
    body[data-theme="dark"] {
      --bg: #0f172a;
      --surface: #1e293b;
      --muted: #94a3b8;
      --header-bg: #16213d;
      --card-border: rgba(15,23,42,0.55);
      --card-shadow: rgba(0,0,0,0.55);
      --accent: #38bdf8;
      --accent-strong: #22d3ee;
      --accent-contrast: #0f172a;
      --chip: rgba(148,163,184,0.2);
      --danger: #f87171;
    }
    header {
      background: var(--header-bg);
      padding: 28px 24px;
      box-shadow: 0 18px 40px var(--card-shadow);
    }
    header .bar {
      display: flex;
      justify-content: space-between;
      gap: 16px;
      flex-wrap: wrap;
      align-items: flex-start;
    }
    h1 {
      margin: 0 0 8px;
    }
    p {
      margin: 0;
      color: var(--muted);
    }
    .header-actions {
      display: flex;
      gap: 12px;
      flex-wrap: wrap;
      align-items: center;
    }
    .pill {
      border-radius: 999px;
      padding: 10px 20px;
      text-decoration: none;
      font-weight: 600;
      background: rgba(37,99,235,0.12);
      color: var(--accent);
      border: 1px solid transparent;
    }
    .pill.secondary {
      background: transparent;
      border-color: var(--card-border);
      color: var(--muted);
    }
    .icon-button {
      width: 42px;
      height: 42px;
      border-radius: 50%;
      border: 1px solid var(--card-border);
      background: var(--surface);
      color: var(--text);
      font-size: 1.2rem;
      cursor: pointer;
    }
    main {
      padding: 24px;
      max-width: 1400px;
      margin: 0 auto;
      width: 100%;
      display: flex;
      flex-direction: column;
      gap: 20px;
    }
    .card {
      background: var(--surface);
      border-radius: 20px;
      border: 1px solid var(--card-border);
      padding: 20px;
      box-shadow: 0 18px 40px var(--card-shadow);
    }
    .console-layout {
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(280px, 1fr));
      gap: 20px;
    }
    label.field {
      display: flex;
      flex-direction: column;
      margin-bottom: 12px;
      font-size: 0.9rem;
      color: var(--muted);
    }
    .field span {
      margin-bottom: 4px;
    }
    input, select {
      border-radius: 8px;
      border: 1px solid var(--card-border);
      padding: 10px 12px;
      font-size: 0.95rem;
      background: var(--bg);
      color: var(--text);
    }
    select {
      width: 100%;
    }
    .form-grid {
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
      gap: 12px;
    }
    .actions {
      display: flex;
      gap: 12px;
      flex-wrap: wrap;
      margin-top: 16px;
    }
    button {
      border: none;
      border-radius: 10px;
      padding: 10px 16px;
      font-weight: 600;
      cursor: pointer;
      background: var(--accent);
      color: var(--accent-contrast);
    }
    button.secondary {
      background: transparent;
      border: 1px solid var(--card-border);
      color: var(--text);
    }
    button.destructive {
      background: var(--danger);
      color: #fff;
    }
    button:disabled {
      opacity: 0.5;
      cursor: not-allowed;
    }
    table {
      width: 100%;
      border-collapse: collapse;
      margin-top: 12px;
    }
    th, td {
      border-bottom: 1px solid var(--card-border);
      padding: 8px 8px;
      text-align: left;
      font-size: 0.85rem;
    }
    th {
      text-transform: uppercase;
      letter-spacing: 0.05em;
      font-size: 0.75rem;
      color: var(--muted);
    }
    tr.alarm {
      background: rgba(220,38,38,0.08);
    }
    .toggle-group {
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(220px, 1fr));
      gap: 12px;
      margin: 16px 0;
    }
    .toggle {
      display: flex;
      align-items: center;
      justify-content: space-between;
      border: 1px solid var(--card-border);
      border-radius: 12px;
      padding: 10px 14px;
      background: var(--chip);
    }
    .toggle span {
      font-size: 0.9rem;
      color: var(--muted);
    }
    .pin-actions,
    .refresh-actions {
      display: flex;
      gap: 12px;
      flex-wrap: wrap;
      margin: 12px 0;
    }
    .details {
      margin: 12px 0;
      color: var(--muted);
      font-size: 0.95rem;
    }
    .details code {
      background: var(--chip);
      padding: 2px 6px;
      border-radius: 4px;
    }
    .checkbox-cell {
      text-align: center;
    }
    .checkbox-cell input {
      width: 16px;
      height: 16px;
    }
    #toast {
      position: fixed;
      left: 50%;
      bottom: 24px;
      transform: translateX(-50%);
      background: #0284c7;
      color: #fff;
      padding: 12px 18px;
      border-radius: 999px;
      box-shadow: 0 10px 30px rgba(15,23,42,0.25);
      opacity: 0;
      pointer-events: none;
      transition: opacity 0.3s ease;
      font-weight: 600;
    }
    #toast.show {
      opacity: 1;
    }
    .modal {
      position: fixed;
      inset: 0;
      background: rgba(15,23,42,0.65);
      display: flex;
      align-items: center;
      justify-content: center;
      z-index: 999;
    }
    .modal.hidden {
      opacity: 0;
      pointer-events: none;
    }
    .modal-card {
      background: var(--surface);
      border-radius: 16px;
      padding: 24px;
      width: min(420px, 90%);
      border: 1px solid var(--card-border);
      box-shadow: 0 20px 40px var(--card-shadow);
    }
    .modal-card h2 {
      margin-top: 0;
    }
    .hidden {
      display: none !important;
    }
  </style>
</head>
<body data-theme="light">
  <header>
    <div class="bar">
      <div>
        <p>Queue configuration changes for remote Tank Alarm clients.</p>
        <h1>Client Console</h1>
        <p>
          Select a client, review the cached configuration, and dispatch updates with PIN-protected controls.
        </p>
        <div class="details" style="margin-top:12px;">
          <span>Server: <strong id="serverName">Tank Alarm Server</strong></span>
          &nbsp;•&nbsp;
          <span>Server UID: <code id="serverUid">--</code></span>
          &nbsp;•&nbsp;
          <span>Next Email: <span id="nextEmail">--</span></span>
        </div>
      </div>
      <div class="header-actions">
        <button class="icon-button" id="themeToggle" aria-label="Switch to dark mode">&#9789;</button>
        <a class="pill" href="/">Dashboard</a>
        <a class="pill secondary" href="/config-generator">Config Generator</a>
      </div>
    </div>
  </header>
  <main>
    <div class="console-layout">
      <section class="card">
        <label class="field">
          <span>Select Client</span>
          <select id="clientSelect"></select>
        </label>
        <div id="clientDetails" class="details">Select a client to review configuration.</div>
        <div class="refresh-actions">
          <button type="button" id="refreshSelectedBtn">Refresh Selected Site</button>
          <button type="button" class="secondary" id="refreshAllBtn">Refresh All Sites</button>
        </div>
        <div class="pin-actions">
          <button type="button" class="secondary" id="changePinBtn" data-pin-control="true">Change PIN</button>
          <button type="button" class="secondary" id="lockPinBtn" data-pin-control="true">Lock Console</button>
        </div>
      </section>
      <section class="card">
        <h2 style="margin-top:0;">Active Sites</h2>
        <table id="telemetryTable">
          <thead>
            <tr>
              <th>Client UID</th>
              <th>Site</th>
              <th>Tank</th>
              <th>Level (in)</th>
              <th>%</th>
              <th>Alarm</th>
              <th>Updated</th>
            </tr>
          </thead>
          <tbody></tbody>
        </table>
      </section>
    </div>

    <section class="card">
      <h2 style="margin-top:0;">Client Configuration</h2>
      <form id="configForm">
        <div class="form-grid">
          <label class="field"><span>Site Name</span><input id="siteInput" type="text" placeholder="Site name"></label>
          <label class="field"><span>Device Label</span><input id="deviceLabelInput" type="text" placeholder="Device label"></label>
          <label class="field"><span>Server Fleet</span><input id="routeInput" type="text" placeholder="tankalarm-server"></label>
          <label class="field"><span>Sample Seconds</span><input id="sampleSecondsInput" type="number" min="30" step="30"></label>
          <label class="field"><span>Level Change Threshold (in)</span><input id="levelChangeThresholdInput" type="number" min="0" step="0.1" placeholder="0 = disabled"></label>
          <label class="field"><span>Report Hour (0-23)</span><input id="reportHourInput" type="number" min="0" max="23"></label>
          <label class="field"><span>Report Minute (0-59)</span><input id="reportMinuteInput" type="number" min="0" max="59"></label>
          <label class="field"><span>SMS Primary</span><input id="smsPrimaryInput" type="text" placeholder="+1234567890"></label>
          <label class="field"><span>SMS Secondary</span><input id="smsSecondaryInput" type="text" placeholder="+1234567890"></label>
          <label class="field"><span>Daily Report Email</span><input id="dailyEmailInput" type="email" placeholder="reports@example.com"></label>
        </div>
        <h3 style="margin-top:24px;">Server SMS Alerts</h3>
        <div class="toggle-group">
          <label class="toggle">
            <span>Send SMS on High Alarm</span>
            <input type="checkbox" id="smsHighToggle">
          </label>
          <label class="toggle">
            <span>Send SMS on Low Alarm</span>
            <input type="checkbox" id="smsLowToggle">
          </label>
          <label class="toggle">
            <span>Send SMS on Clear Alarm</span>
            <input type="checkbox" id="smsClearToggle">
          </label>
        </div>
        <h3>Tanks</h3>
        <table class="tank-table" id="tankTable">
          <thead>
            <tr>
              <th>ID</th>
              <th>Name</th>
              <th>#</th>
              <th>Sensor</th>
              <th>Primary Pin</th>
              <th>Secondary Pin</th>
              <th>Loop Ch</th>
              <th>Height (in)</th>
              <th>High Alarm</th>
              <th>Low Alarm</th>
              <th class="checkbox-cell">Daily</th>
              <th class="checkbox-cell">Alarm SMS</th>
              <th class="checkbox-cell">Upload</th>
              <th></th>
            </tr>
          </thead>
          <tbody></tbody>
        </table>
        <div class="actions">
          <button type="button" class="secondary" id="addTank">Add Tank</button>
          <button type="submit">Send Configuration</button>
        </div>
      </form>
    </section>
  </main>
  <div id="toast"></div>
  <div id="pinModal" class="modal hidden">
    <div class="modal-card">
      <h2 id="pinModalTitle">Set Admin PIN</h2>
      <p id="pinModalDescription">Enter a 4-digit PIN to unlock configuration changes.</p>
      <form id="pinForm">
        <label class="field hidden" id="pinCurrentGroup">
          <span>Current PIN</span>
          <input type="password" id="pinCurrentInput" inputmode="numeric" pattern="\d*" maxlength="4" autocomplete="off">
        </label>
        <label class="field" id="pinPrimaryGroup">
          <span id="pinPrimaryLabel">PIN</span>
          <input type="password" id="pinInput" inputmode="numeric" pattern="\d*" maxlength="4" autocomplete="off" required>
        </label>
        <label class="field hidden" id="pinConfirmGroup">
          <span>Confirm PIN</span>
          <input type="password" id="pinConfirmInput" inputmode="numeric" pattern="\d*" maxlength="4" autocomplete="off">
        </label>
        <div class="actions">
          <button type="submit" id="pinSubmit">Save PIN</button>
          <button type="button" class="secondary" id="pinCancel">Cancel</button>
        </div>
      </form>
    </div>
  </div>
  <script>
    (function() {
      const THEME_KEY = 'tankalarmTheme';
      const themeToggle = document.getElementById('themeToggle');
      function applyTheme(next) {
        const theme = next === 'dark' ? 'dark' : 'light';
        document.body.dataset.theme = theme;
        themeToggle.textContent = theme === 'dark' ? '☀' : '☾';
        themeToggle.setAttribute('aria-label', theme === 'dark' ? 'Switch to light mode' : 'Switch to dark mode');
        localStorage.setItem(THEME_KEY, theme);
      }
      applyTheme(localStorage.getItem(THEME_KEY) || 'light');
      themeToggle.addEventListener('click', () => {
        const next = document.body.dataset.theme === 'dark' ? 'light' : 'dark';
        applyTheme(next);
      });

      const PIN_STORAGE_KEY = 'tankalarmPin';
      const state = {
        data: null,
        selected: null
      };

      const pinState = {
        value: sessionStorage.getItem(PIN_STORAGE_KEY) || null,
        configured: false,
        mode: 'unlock'
      };

      const els = {
        serverName: document.getElementById('serverName'),
        serverUid: document.getElementById('serverUid'),
        nextEmail: document.getElementById('nextEmail'),
        telemetryBody: document.querySelector('#telemetryTable tbody'),
        clientSelect: document.getElementById('clientSelect'),
        clientDetails: document.getElementById('clientDetails'),
        site: document.getElementById('siteInput'),
        deviceLabel: document.getElementById('deviceLabelInput'),
        route: document.getElementById('routeInput'),
        sampleSeconds: document.getElementById('sampleSecondsInput'),
        levelChangeThreshold: document.getElementById('levelChangeThresholdInput'),
        reportHour: document.getElementById('reportHourInput'),
        reportMinute: document.getElementById('reportMinuteInput'),
        smsPrimary: document.getElementById('smsPrimaryInput'),
        smsSecondary: document.getElementById('smsSecondaryInput'),
        dailyEmail: document.getElementById('dailyEmailInput'),
        smsHighToggle: document.getElementById('smsHighToggle'),
        smsLowToggle: document.getElementById('smsLowToggle'),
        smsClearToggle: document.getElementById('smsClearToggle'),
        tankBody: document.querySelector('#tankTable tbody'),
        toast: document.getElementById('toast'),
        addTank: document.getElementById('addTank'),
        form: document.getElementById('configForm'),
        changePinBtn: document.getElementById('changePinBtn'),
        lockPinBtn: document.getElementById('lockPinBtn'),
        refreshSelectedBtn: document.getElementById('refreshSelectedBtn'),
        refreshAllBtn: document.getElementById('refreshAllBtn')
      };

      const pinEls = {
        modal: document.getElementById('pinModal'),
        title: document.getElementById('pinModalTitle'),
        description: document.getElementById('pinModalDescription'),
        currentGroup: document.getElementById('pinCurrentGroup'),
        current: document.getElementById('pinCurrentInput'),
        pin: document.getElementById('pinInput'),
        confirmGroup: document.getElementById('pinConfirmGroup'),
        confirm: document.getElementById('pinConfirmInput'),
        primaryLabel: document.getElementById('pinPrimaryLabel'),
        form: document.getElementById('pinForm'),
        submit: document.getElementById('pinSubmit'),
        cancel: document.getElementById('pinCancel')
      };

      els.smsHighToggle.checked = true;
      els.smsLowToggle.checked = true;
      els.smsClearToggle.checked = false;


      function setFormDisabled(disabled) {
        const controls = els.form.querySelectorAll('input, select, button');
        controls.forEach(control => {
          if (control.dataset && control.dataset.pinControl === 'true') {
            return;
          }
          control.disabled = disabled;
        });
        els.addTank.disabled = disabled;
      }

      function invalidatePin() {
        pinState.value = null;
        sessionStorage.removeItem(PIN_STORAGE_KEY);
        setFormDisabled(true);
      }

      function isPinModalVisible() {
        return !pinEls.modal.classList.contains('hidden');
      }

      function resetPinForm() {
        pinEls.form.reset();
        pinEls.currentGroup.classList.add('hidden');
        pinEls.confirmGroup.classList.add('hidden');
        pinEls.primaryLabel.textContent = 'PIN';
      }

      function showPinModal(mode) {
        pinState.mode = mode;
        resetPinForm();
        if (mode === 'setup') {
          pinEls.title.textContent = 'Set Admin PIN';
          pinEls.description.textContent = 'Choose a 4-digit PIN to secure configuration changes.';
          pinEls.confirmGroup.classList.remove('hidden');
          pinEls.cancel.classList.add('hidden');
        } else if (mode === 'change') {
          pinEls.title.textContent = 'Change Admin PIN';
          pinEls.description.textContent = 'Enter your current PIN and the new PIN you would like to use.';
          pinEls.currentGroup.classList.remove('hidden');
          pinEls.confirmGroup.classList.remove('hidden');
          pinEls.primaryLabel.textContent = 'New PIN';
          pinEls.cancel.classList.remove('hidden');
        } else {
          pinEls.title.textContent = 'Enter Admin PIN';
          pinEls.description.textContent = 'Enter the admin PIN to unlock configuration controls.';
          pinEls.cancel.classList.remove('hidden');
        }
        pinEls.modal.classList.remove('hidden');
        setFormDisabled(true);
        setTimeout(() => pinEls.pin.focus(), 50);
      }

      function hidePinModal() {
        if (isPinModalVisible()) {
          pinEls.modal.classList.add('hidden');
          resetPinForm();
        }
      }

      async function requestPin(payload) {
        const res = await fetch('/api/pin', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify(payload)
        });
        if (!res.ok) {
          const text = await res.text();
          const error = new Error(text || 'PIN request failed');
          error.serverRejected = true;
          throw error;
        }
        const data = await res.json();
        pinState.configured = !!data.pinConfigured;
        return data;
      }

      async function handlePinSubmit(event) {
        event.preventDefault();
        let payload;
        let pinToStore = null;
        try {
          if (pinState.mode === 'setup') {
            const newPin = pinEls.pin.value.trim();
            const confirmPin = pinEls.confirm.value.trim();
            if (!isFourDigits(newPin)) {
              throw new Error('PIN must be exactly 4 digits.');
            }
            if (newPin !== confirmPin) {
              throw new Error('PIN confirmation does not match.');
            }
            payload = { newPin };
            pinToStore = newPin;
          } else if (pinState.mode === 'change') {
            const currentPin = pinEls.current.value.trim();
            const newPin = pinEls.pin.value.trim();
            const confirmPin = pinEls.confirm.value.trim();
            if (!isFourDigits(currentPin) || !isFourDigits(newPin)) {
              throw new Error('PINs must be exactly 4 digits.');
            }
            if (newPin !== confirmPin) {
              throw new Error('PIN confirmation does not match.');
            }
            payload = { pin: currentPin, newPin };
            pinToStore = newPin;
          } else {
            const pin = pinEls.pin.value.trim();
            if (!isFourDigits(pin)) {
              throw new Error('PIN must be exactly 4 digits.');
            }
            payload = { pin };
            pinToStore = pin;
          }

          const result = await requestPin(payload);
          pinState.value = pinToStore;
          sessionStorage.setItem(PIN_STORAGE_KEY, pinToStore);
          hidePinModal();
          setFormDisabled(false);
          showToast((result && result.message) || 'PIN updated');
          updatePinLock();
        } catch (err) {
          if (err.serverRejected) {
            invalidatePin();
          }
          showToast(err.message || 'PIN action failed', true);
        }
      }

      function isFourDigits(value) {
        return /^\d{4}$/.test(value || '');
      }

      function updatePinLock() {
        const configured = !!(state.data && state.data.server && state.data.server.pinConfigured);
        pinState.configured = configured;
        if (!configured) {
          if (!isPinModalVisible() || pinState.mode !== 'setup') {
            invalidatePin();
            showPinModal('setup');
          }
          return;
        }

        if (!pinState.value) {
          if (!isPinModalVisible()) {
            showPinModal('unlock');
          }
        } else {
          setFormDisabled(false);
          hidePinModal();
        }
      }

      setFormDisabled(true);

      function showToast(message, isError) {
        els.toast.textContent = message;
        els.toast.style.background = isError ? '#dc2626' : '#0284c7';
        els.toast.classList.add('show');
        setTimeout(() => els.toast.classList.remove('show'), 2500);
      }

      function formatNumber(value) {
        return (typeof value === 'number' && isFinite(value)) ? value.toFixed(1) : '--';
      }

      function valueOr(value, fallback) {
        return (value === undefined || value === null) ? fallback : value;
      }

      function syncServerSettings(serverInfo) {
        els.smsPrimary.value = valueOr(serverInfo && serverInfo.smsPrimary, '');
        els.smsSecondary.value = valueOr(serverInfo && serverInfo.smsSecondary, '');
        els.smsHighToggle.checked = !!valueOr(serverInfo && serverInfo.smsOnHigh, true);
        els.smsLowToggle.checked = !!valueOr(serverInfo && serverInfo.smsOnLow, true);
        els.smsClearToggle.checked = !!valueOr(serverInfo && serverInfo.smsOnClear, false);
      }

      function formatEpoch(epoch) {
        if (!epoch) return '--';
        const date = new Date(epoch * 1000);
        if (isNaN(date.getTime())) return '--';
        return date.toLocaleString();
      }

      function rowHtml(value) {
        return (value === undefined || value === null || value === '') ? '--' : value;
      }

      function renderTelemetry() {
        els.telemetryBody.innerHTML = '';
        if (!state.data || !state.data.clients) return;
        state.data.clients.forEach(client => {
          const tr = document.createElement('tr');
          if (client.alarm) {
            tr.classList.add('alarm');
          }
          tr.innerHTML = `
            <td><code>${client.client}</code></td>
            <td>${rowHtml(client.site)}</td>
            <td>${rowHtml(client.label)} #${rowHtml(client.tank)}</td>
            <td>${formatNumber(client.levelInches)}</td>
            <td>${formatNumber(client.percent)}</td>
            <td>${rowHtml(client.alarmType || (client.alarm ? 'active' : 'clear'))}</td>
            <td>${formatEpoch(client.lastUpdate)}</td>
          `;
          els.telemetryBody.appendChild(tr);
        });
      }

      function ensureOption(uid, label) {
        let option = Array.from(els.clientSelect.options).find(opt => opt.value === uid);
        if (!option) {
          option = document.createElement('option');
          option.value = uid;
          els.clientSelect.appendChild(option);
        }
        option.textContent = label;
      }

      function populateClientSelect() {
        els.clientSelect.innerHTML = '';
        if (!state.data || !state.data.clients) return;
        state.data.clients.forEach(client => {
          const label = `${client.site || 'Site'} – ${client.label || 'Tank'} (#${client.tank || '?'})`;
          ensureOption(client.client, label);
        });
        if (state.data.configs) {
          state.data.configs.forEach(entry => {
            ensureOption(entry.client, `${entry.site || 'Site'} – Stored config`);
          });
        }
        if (!state.selected && els.clientSelect.options.length) {
          state.selected = els.clientSelect.options[0].value;
        }
        if (state.selected) {
          els.clientSelect.value = state.selected;
        }
      }

      function lookupClient(uid) {
        if (!state.data || !state.data.clients) return null;
        return state.data.clients.find(c => c.client === uid) || null;
      }

      function lookupConfig(uid) {
        if (!state.data || !state.data.configs) return null;
        const entry = state.data.configs.find(c => c.client === uid);
        if (!entry) return null;
        if (entry.config) {
          return entry.config;
        }
        if (entry.configJson) {
          try {
            entry.config = JSON.parse(entry.configJson);
            return entry.config;
          } catch (err) {
            console.warn('Stored config failed to parse', err);
          }
        }
        return null;
      }

      function buildDefaultConfig(uid) {
        const client = lookupClient(uid);
        const serverDefaults = (state.data && state.data.server) ? state.data.server : {};
        const tankList = (client && Array.isArray(client.tanks)) ? client.tanks : [];
        const firstTank = tankList.length ? tankList[0] : null;
        const tankId = firstTank && firstTank.tank ? firstTank.tank : 'A';
        const parsedNumber = firstTank && firstTank.tank ? parseInt(firstTank.tank, 10) : NaN;
        const defaultTank = {
          id: tankId || 'A',
          name: firstTank ? (firstTank.label || `Tank ${tankId || 'A'}`) : 'Tank A',
          number: isFinite(parsedNumber) && parsedNumber > 0 ? parsedNumber : 1,
          sensor: 'analog',
          primaryPin: 0,
          secondaryPin: -1,
          loopChannel: -1,
          heightInches: firstTank && typeof firstTank.heightInches === 'number' ? firstTank.heightInches : 120,
          highAlarm: 100,
          lowAlarm: 20,
          daily: true,
          alarmSms: true,
          upload: true
        };
        return {
          site: client ? (client.site || '') : '',
          deviceLabel: client ? `${((client.site || 'Client')).replace(/\s+/g, '-')}-${client.tank || tankId || 'A'}` : 'Client-112025',
          serverFleet: 'tankalarm-server',
          sampleSeconds: 1800,
          levelChangeThreshold: 0,
          reportHour: 5,
          reportMinute: 0,
          dailyEmail: serverDefaults.dailyEmail || '',
          tanks: tankList.length ? tankList.map(t => ({
            id: t.tank || 'A',
            name: t.label || (t.tank ? `Tank ${t.tank}` : 'Tank'),
            number: t.tank ? (parseInt(t.tank, 10) || 1) : 1,
            sensor: 'analog',
            primaryPin: 0,
            secondaryPin: -1,
            loopChannel: -1,
            heightInches: typeof t.heightInches === 'number' ? t.heightInches : 120,
            highAlarm: 100,
            lowAlarm: 20,
            daily: true,
            alarmSms: true,
            upload: true
          })) : [defaultTank]
        };
      }

      function populateTankRows(tanks) {
        els.tankBody.innerHTML = '';
        (tanks || []).forEach(tank => addTankRow(tank));
        if (!tanks || !tanks.length) {
          addTankRow();
        }
      }

      function addTankRow(tank) {
        const defaults = tank || {
          id: 'A',
          name: 'Tank',
          number: 1,
          sensor: 'analog',
          primaryPin: 0,
          secondaryPin: -1,
          loopChannel: -1,
          heightInches: 120,
          highAlarm: 100,
          lowAlarm: 20,
          daily: true,
          alarmSms: true,
          upload: true
        };
        const tr = document.createElement('tr');
        tr.innerHTML = `
          <td><input type="text" class="tank-id" maxlength="1" value="${defaults.id || ''}"></td>
          <td><input type="text" class="tank-name" value="${defaults.name || ''}"></td>
          <td><input type="number" class="tank-number" min="1" value="${valueOr(defaults.number, 1)}"></td>
          <td>
            <select class="tank-sensor">
              <option value="analog" ${defaults.sensor === 'analog' ? 'selected' : ''}>Analog</option>
              <option value="digital" ${defaults.sensor === 'digital' ? 'selected' : ''}>Digital</option>
              <option value="current" ${defaults.sensor === 'current' ? 'selected' : ''}>Current Loop</option>
            </select>
          </td>
          <td><input type="number" class="tank-primary" value="${valueOr(defaults.primaryPin, 0)}"></td>
          <td><input type="number" class="tank-secondary" value="${valueOr(defaults.secondaryPin, -1)}"></td>
          <td><input type="number" class="tank-loop" value="${valueOr(defaults.loopChannel, -1)}"></td>
          <td><input type="number" class="tank-height" step="0.1" value="${valueOr(defaults.heightInches, 120)}"></td>
          <td><input type="number" class="tank-high" step="0.1" value="${valueOr(defaults.highAlarm, 100)}"></td>
          <td><input type="number" class="tank-low" step="0.1" value="${valueOr(defaults.lowAlarm, 20)}"></td>
          <td class="checkbox-cell"><input type="checkbox" class="tank-daily" ${defaults.daily ? 'checked' : ''}></td>
          <td class="checkbox-cell"><input type="checkbox" class="tank-alarm" ${defaults.alarmSms ? 'checked' : ''}></td>
          <td class="checkbox-cell"><input type="checkbox" class="tank-upload" ${defaults.upload ? 'checked' : ''}></td>
          <td><button type="button" class="destructive remove">Remove</button></td>
        `;
        tr.querySelector('.remove').addEventListener('click', () => tr.remove());
        els.tankBody.appendChild(tr);
      }

      function loadConfigIntoForm(uid) {
        if (!uid) return;
        const client = lookupClient(uid);
        const stored = lookupConfig(uid);
        const config = stored ? JSON.parse(JSON.stringify(stored)) : buildDefaultConfig(uid);

        state.selected = uid;
    els.site.value = config.site || '';
    els.deviceLabel.value = config.deviceLabel || '';
    els.route.value = config.serverFleet || '';
    els.sampleSeconds.value = valueOr(config.sampleSeconds, 1800);
    els.levelChangeThreshold.value = valueOr(config.levelChangeThreshold, 0);
    els.reportHour.value = valueOr(config.reportHour, 5);
    els.reportMinute.value = valueOr(config.reportMinute, 0);
        els.dailyEmail.value = config.dailyEmail || '';
        populateTankRows(config.tanks);

        const detailParts = [];
        if (client) {
          detailParts.push(`<strong>Site:</strong> ${client.site || 'Unknown'}`);
          detailParts.push(`<strong>Latest Tank:</strong> ${client.label || 'Tank'} #${client.tank || '?'} at ${formatNumber(client.levelInches)} in`);
        }
        detailParts.push(`<strong>Target UID:</strong> <code>${uid}</code>`);
        els.clientDetails.innerHTML = detailParts.join(' · ');
      }

      function collectConfig() {
        const tanks = [];
        els.tankBody.querySelectorAll('tr').forEach(row => {
          const tank = {
            id: row.querySelector('.tank-id').value.trim().substring(0, 1) || 'A',
            name: row.querySelector('.tank-name').value.trim() || 'Tank',
            number: parseInt(row.querySelector('.tank-number').value, 10) || 1,
            sensor: row.querySelector('.tank-sensor').value || 'analog',
            primaryPin: parseInt(row.querySelector('.tank-primary').value, 10) || 0,
            secondaryPin: parseInt(row.querySelector('.tank-secondary').value, 10) || -1,
            loopChannel: parseInt(row.querySelector('.tank-loop').value, 10) || -1,
            heightInches: parseFloat(row.querySelector('.tank-height').value) || 120,
            highAlarm: parseFloat(row.querySelector('.tank-high').value) || 100,
            lowAlarm: parseFloat(row.querySelector('.tank-low').value) || 20,
            daily: row.querySelector('.tank-daily').checked,
            alarmSms: row.querySelector('.tank-alarm').checked,
            upload: row.querySelector('.tank-upload').checked
          };
          tanks.push(tank);
        });

        if (!tanks.length) {
          throw new Error('Add at least one tank before sending.');
        }

        const config = {
          site: els.site.value.trim(),
          deviceLabel: els.deviceLabel.value.trim(),
          serverFleet: els.route.value.trim() || 'tankalarm-server',
          sampleSeconds: parseInt(els.sampleSeconds.value, 10) || 1800,
          levelChangeThreshold: Math.max(0, parseFloat(els.levelChangeThreshold.value) || 0),
          reportHour: parseInt(els.reportHour.value, 10) || 5,
          reportMinute: parseInt(els.reportMinute.value, 10) || 0,
          dailyEmail: els.dailyEmail.value.trim(),
          tanks
        };

        return config;
      }

      function collectServerSettings() {
        return {
          smsPrimary: els.smsPrimary.value.trim(),
          smsSecondary: els.smsSecondary.value.trim(),
          smsOnHigh: !!els.smsHighToggle.checked,
          smsOnLow: !!els.smsLowToggle.checked,
          smsOnClear: !!els.smsClearToggle.checked
        };
      }

      async function submitConfig(event) {
        event.preventDefault();
        const uid = state.selected;
        if (!uid) {
          showToast('Select a client first.', true);
          return;
        }
        let config;
        try {
          config = collectConfig();
        } catch (err) {
          showToast(err.message, true);
          return;
        }

        if (pinState.configured && !pinState.value) {
          showPinModal('unlock');
          showToast('Enter the admin PIN to send configurations.', true);
          return;
        }

        const payload = { client: uid, config, server: collectServerSettings() };
        if (pinState.value) {
          payload.pin = pinState.value;
        }
        try {
          const res = await fetch('/api/config', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(payload)
          });
          if (!res.ok) {
            if (res.status === 403) {
              invalidatePin();
              showPinModal(pinState.configured ? 'unlock' : 'setup');
              throw new Error('PIN required or invalid.');
            }
            const text = await res.text();
            throw new Error(text || 'Server rejected configuration');
          }
          showToast('Configuration queued for delivery');
          await refreshData(state.selected);
        } catch (err) {
          showToast(err.message || 'Failed to send config', true);
        }
      }

      function applyServerData(data, preferredUid) {
        state.data = data;
        const serverInfo = (state.data && state.data.server) ? state.data.server : {};
        els.serverName.textContent = serverInfo.name || 'Tank Alarm Server';
        syncServerSettings(serverInfo);
        els.serverUid.textContent = state.data.serverUid || '--';
        els.nextEmail.textContent = formatEpoch(state.data.nextDailyEmailEpoch);
        if (preferredUid) {
          state.selected = preferredUid;
        }
        renderTelemetry();
        populateClientSelect();
        if (state.selected) {
          loadConfigIntoForm(state.selected);
        } else if (els.clientSelect.options.length) {
          loadConfigIntoForm(els.clientSelect.value);
        }
        updatePinLock();
      }

      async function refreshData(preferredUid) {
        try {
          const query = preferredUid ? `?client=${encodeURIComponent(preferredUid)}` : '';
          const res = await fetch(`/api/clients${query}`);
          if (!res.ok) {
            throw new Error('Failed to fetch server data');
          }
          const data = await res.json();
          applyServerData(data, preferredUid || state.selected);
        } catch (err) {
          showToast(err.message || 'Initialization failed', true);
        }
      }

      async function triggerManualRefresh(targetUid) {
        const payload = targetUid ? { client: targetUid } : {};
        try {
          const res = await fetch('/api/refresh', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(payload)
          });
          if (!res.ok) {
            const text = await res.text();
            throw new Error(text || 'Refresh failed');
          }
          const data = await res.json();
          applyServerData(data, targetUid || state.selected);
          showToast(targetUid ? 'Selected site updated' : 'All sites updated');
        } catch (err) {
          showToast(err.message || 'Refresh failed', true);
        }
      }

      pinEls.form.addEventListener('submit', handlePinSubmit);
      pinEls.cancel.addEventListener('click', () => {
        hidePinModal();
      });
      els.changePinBtn.addEventListener('click', () => {
        if (!pinState.configured) {
          showPinModal('setup');
        } else if (!pinState.value) {
          showPinModal('unlock');
        } else {
          showPinModal('change');
        }
      });
      els.lockPinBtn.addEventListener('click', () => {
        invalidatePin();
        showToast('Console locked', false);
        if (pinState.configured) {
          showPinModal('unlock');
        } else {
          showPinModal('setup');
        }
      });
      els.addTank.addEventListener('click', () => addTankRow());
      els.clientSelect.addEventListener('change', event => {
        loadConfigIntoForm(event.target.value);
      });
      els.form.addEventListener('submit', submitConfig);
      els.refreshSelectedBtn.addEventListener('click', () => {
        const target = state.selected || (els.clientSelect.value || '');
        if (!target) {
          showToast('Select a client first.', true);
          return;
        }
        triggerManualRefresh(target);
      });
      els.refreshAllBtn.addEventListener('click', () => {
        triggerManualRefresh(null);
      });

      refreshData();
    })();
  </script>
</body>
</html>
)HTML";

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
static bool readHttpRequest(EthernetClient &client, String &method, String &path, String &body, size_t &contentLength);
static void respondHtml(EthernetClient &client, const String &body);
static void respondJson(EthernetClient &client, const String &body, int status = 200);
static void respondStatus(EthernetClient &client, int status, const String &message);
static void sendDashboard(EthernetClient &client);
static void sendClientConsole(EthernetClient &client);
static void sendTankJson(EthernetClient &client);
static void sendClientDataJson(EthernetClient &client);
static void handleConfigPost(EthernetClient &client, const String &body);
// Enum definitions
enum class ConfigDispatchStatus : uint8_t {
  Ok = 0,
  PayloadTooLarge,
  NotecardFailure
};

// Forward declarations
static void handlePinPost(EthernetClient &client, const String &body);
static void handleRefreshPost(EthernetClient &client, const String &body);
static void handleRelayPost(EthernetClient &client, const String &body);
static void sendConfigGenerator(EthernetClient &client);
static ConfigDispatchStatus dispatchClientConfig(const char *clientUid, JsonVariantConst cfgObj);
static bool sendRelayCommand(const char *clientUid, uint8_t relayNum, bool state, const char *source);
static void pollNotecard();
static void processNotefile(const char *fileName, void (*handler)(JsonDocument &, double));
static void handleTelemetry(JsonDocument &doc, double epoch);
static void handleAlarm(JsonDocument &doc, double epoch);
static void handleDaily(JsonDocument &doc, double epoch);
static TankRecord *upsertTankRecord(const char *clientUid, uint8_t tankNumber);
static void sendSmsAlert(const char *message);
static void sendDailyEmail();
static void loadClientConfigSnapshots();
static void saveClientConfigSnapshots();
static void cacheClientConfigFromBuffer(const char *clientUid, const char *buffer);
static ClientConfigSnapshot *findClientConfigSnapshot(const char *clientUid);
static bool checkSmsRateLimit(TankRecord *rec);
static void publishViewerSummary();
static double computeNextAlignedEpoch(double epoch, uint8_t baseHour, uint32_t intervalSeconds);

static void handleRefreshPost(EthernetClient &client, const String &body) {
  char clientUid[64] = {0};
  if (body.length() > 0) {
    DynamicJsonDocument doc(128);
    if (deserializeJson(doc, body) == DeserializationError::Ok) {
      const char *uid = doc["client"] | "";
      if (uid && *uid) {
        strlcpy(clientUid, uid, sizeof(clientUid));
      }
    }
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

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 2000) {
    delay(10);
  }

  Serial.println();
  Serial.println(F("Tank Alarm Server 112025 starting"));

  initializeStorage();
  ensureConfigLoaded();
  printHardwareRequirements();

  Wire.begin();
  Wire.setClock(NOTECARD_I2C_FREQUENCY);

  initializeNotecard();
  ensureTimeSync();
  scheduleNextDailyEmail();
  scheduleNextViewerSummary();

  initializeEthernet();
  gWebServer.begin();

#ifdef WATCHDOG_AVAILABLE
  // Initialize watchdog timer
  IWatchdog.begin(WATCHDOG_TIMEOUT_SECONDS * 1000000UL);  // Timeout in microseconds
  Serial.print(F("Watchdog timer enabled: "));
  Serial.print(WATCHDOG_TIMEOUT_SECONDS);
  Serial.println(F(" seconds"));
#else
  Serial.println(F("Warning: Watchdog timer not available on this platform"));
#endif

  Serial.println(F("Server setup complete"));
}

void loop() {
#ifdef WATCHDOG_AVAILABLE
  // Reset watchdog timer to prevent system reset
  IWatchdog.reload();
#endif

  handleWebRequests();

  unsigned long now = millis();
  if (now - gLastPollMillis > 5000UL) {
    gLastPollMillis = now;
    pollNotecard();
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
    }
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
  if (!loadConfig(gConfig)) {
    createDefaultConfig(gConfig);
    saveConfig(gConfig);
    Serial.println(F("Default server configuration created"));
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
  cfg.useStaticIp = true;
  cfg.smsOnHigh = true;
  cfg.smsOnLow = true;
  cfg.smsOnClear = false;
}

static bool loadConfig(ServerConfig &cfg) {
#ifdef FILESYSTEM_AVAILABLE
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
  cfg.useStaticIp = doc["useStaticIp"].is<bool>() ? doc["useStaticIp"].as<bool>() : true;
  cfg.smsOnHigh = doc["smsOnHigh"].is<bool>() ? doc["smsOnHigh"].as<bool>() : true;
  cfg.smsOnLow = doc["smsOnLow"].is<bool>() ? doc["smsOnLow"].as<bool>() : true;
  cfg.smsOnClear = doc["smsOnClear"].is<bool>() ? doc["smsOnClear"].as<bool>() : false;

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
#else
  return false; // Filesystem not available
#endif
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
  notecard.setDebugOutputStream(Serial);
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
  if (gLastSyncedEpoch <= 0.0 || millis() - gLastSyncMillis > 6UL * 60UL * 60UL * 1000UL) {
    J *req = notecard.newRequest("card.time");
    if (!req) {
      return;
    }
    JAddStringToObject(req, "mode", "auto");
    J *rsp = notecard.requestAndResponse(req);
    if (!rsp) {
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
  unsigned long delta = millis() - gLastSyncMillis;
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
  int status;
  if (gConfig.useStaticIp) {
    status = Ethernet.begin(gMacAddress, gStaticIp, gStaticDns, gStaticGateway, gStaticSubnet);
  } else {
    status = Ethernet.begin(gMacAddress);
  }

  if (status == 0) {
    Serial.println(F(" failed"));
  } else {
    Serial.println(F(" ok"));
    Serial.print(F("IP Address: "));
    Serial.println(Ethernet.localIP());
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

  if (!readHttpRequest(client, method, path, body, contentLength)) {
    respondStatus(client, 400, F("Bad Request"));
    client.stop();
    return;
  }

  if (method == "GET" && path == "/") {
    sendDashboard(client);
  } else if (method == "GET" && path == "/client-console") {
    sendClientConsole(client);
  } else if (method == "GET" && path == "/config-generator") {
    sendConfigGenerator(client);
  } else if (method == "GET" && path == "/api/tanks") {
    sendTankJson(client);
  } else if (method == "GET" && path == "/api/clients") {
    sendClientDataJson(client);
  } else if (method == "POST" && path == "/api/config") {
    handleConfigPost(client, body);
  } else if (method == "POST" && path == "/api/pin") {
    handlePinPost(client, body);
  } else if (method == "POST" && path == "/api/refresh") {
    handleRefreshPost(client, body);
  } else if (method == "POST" && path == "/api/relay") {
    handleRelayPost(client, body);
  } else {
    respondStatus(client, 404, F("Not Found"));
  }

  delay(1);
  client.stop();
}

static bool readHttpRequest(EthernetClient &client, String &method, String &path, String &body, size_t &contentLength) {
  method = "";
  path = "";
  contentLength = 0;
  body = "";

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

  if (contentLength > 0) {
    size_t readBytes = 0;
    while (readBytes < contentLength && client.connected()) {
      while (client.available() && readBytes < contentLength) {
        char c = client.read();
        body += c;
        readBytes++;
      }
    }
  }

  return true;
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

static void respondStatus(EthernetClient &client, int status, const String &message) {
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
  client.println(message.length());
  client.println();
  client.print(message);
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

static void sendTankJson(EthernetClient &client) {
  DynamicJsonDocument doc(TANK_JSON_CAPACITY);
  JsonArray arr = doc.createNestedArray("tanks");
  for (uint8_t i = 0; i < gTankRecordCount; ++i) {
    JsonObject obj = arr.createNestedObject();
    obj["client"] = gTankRecords[i].clientUid;
    obj["site"] = gTankRecords[i].site;
    obj["label"] = gTankRecords[i].label;
    obj["tank"] = gTankRecords[i].tankNumber;
    obj["heightInches"] = gTankRecords[i].heightInches;
    obj["levelInches"] = gTankRecords[i].levelInches;
    obj["percent"] = gTankRecords[i].percent;
    obj["alarm"] = gTankRecords[i].alarmActive;
    obj["alarmType"] = gTankRecords[i].alarmType;
    obj["lastUpdate"] = gTankRecords[i].lastUpdateEpoch;
  }

  String json;
  if (serializeJson(doc, json) == 0) {
    respondStatus(client, 500, F("Failed to encode tank data"));
    return;
  }
  respondJson(client, json);
}

static void sendClientDataJson(EthernetClient &client) {
  DynamicJsonDocument doc(CLIENT_JSON_CAPACITY);

  JsonObject serverObj = doc.createNestedObject("server");
  serverObj["name"] = gConfig.serverName;
  serverObj["clientFleet"] = gConfig.clientFleet;
  serverObj["smsPrimary"] = gConfig.smsPrimary;
  serverObj["smsSecondary"] = gConfig.smsSecondary;
  serverObj["dailyEmail"] = gConfig.dailyEmail;
  serverObj["dailyHour"] = gConfig.dailyHour;
  serverObj["dailyMinute"] = gConfig.dailyMinute;
  serverObj["webRefreshSeconds"] = gConfig.webRefreshSeconds;
  serverObj["smsOnHigh"] = gConfig.smsOnHigh;
  serverObj["smsOnLow"] = gConfig.smsOnLow;
  serverObj["smsOnClear"] = gConfig.smsOnClear;
  serverObj["pinConfigured"] = (gConfig.configPin[0] != '\0');

  doc["serverUid"] = gServerUid;
  doc["nextDailyEmailEpoch"] = gNextDailyEmailEpoch;
  doc["lastSyncEpoch"] = gLastSyncedEpoch;

  JsonArray clientsArr = doc.createNestedArray("clients");
  for (uint8_t i = 0; i < gTankRecordCount; ++i) {
    const TankRecord &rec = gTankRecords[i];

    JsonObject clientObj;
    for (JsonObject existing : clientsArr) {
      const char *uid = existing["client"];
      if (uid && strcmp(uid, rec.clientUid) == 0) {
        clientObj = existing;
        break;
      }
    }

    if (!clientObj) {
      clientObj = clientsArr.createNestedObject();
      clientObj["client"] = rec.clientUid;
      clientObj["site"] = rec.site;
      clientObj["alarm"] = false;
      clientObj["lastUpdate"] = 0.0;
    }

    const char *existingSite = clientObj.containsKey("site") ? clientObj["site"].as<const char *>() : nullptr;
    if (!existingSite || strlen(existingSite) == 0) {
      clientObj["site"] = rec.site;
    }

    if (rec.alarmActive) {
      clientObj["alarm"] = true;
      clientObj["alarmType"] = rec.alarmType;
    }

    double previousUpdate = clientObj["lastUpdate"].is<double>() ? clientObj["lastUpdate"].as<double>() : 0.0;
    if (rec.lastUpdateEpoch > previousUpdate) {
      clientObj["label"] = rec.label;
      clientObj["tank"] = rec.tankNumber;
      clientObj["levelInches"] = rec.levelInches;
      clientObj["percent"] = rec.percent;
      clientObj["lastUpdate"] = rec.lastUpdateEpoch;
      clientObj["alarmType"] = rec.alarmType;
    }

    JsonArray tankList;
    if (!clientObj.containsKey("tanks")) {
      tankList = clientObj.createNestedArray("tanks");
    } else {
      tankList = clientObj["tanks"].as<JsonArray>();
    }
    JsonObject tankObj = tankList.createNestedObject();
    tankObj["label"] = rec.label;
    tankObj["tank"] = rec.tankNumber;
    tankObj["heightInches"] = rec.heightInches;
    tankObj["levelInches"] = rec.levelInches;
    tankObj["percent"] = rec.percent;
    tankObj["alarm"] = rec.alarmActive;
    tankObj["alarmType"] = rec.alarmType;
    tankObj["lastUpdate"] = rec.lastUpdateEpoch;

    clientObj["tanksCount"] = tankList.size();
  }

  JsonArray configsArr = doc.createNestedArray("configs");
  for (uint8_t i = 0; i < gClientConfigCount; ++i) {
    ClientConfigSnapshot &snap = gClientConfigs[i];
    JsonObject cfgEntry = configsArr.createNestedObject();
    cfgEntry["client"] = snap.uid;
    cfgEntry["site"] = snap.site;
    cfgEntry["configJson"] = snap.payload;
  }

  String json;
  if (serializeJson(doc, json) == 0) {
    respondStatus(client, 500, F("Failed to encode client data"));
    return;
  }
  respondJson(client, json);
}

static void handleConfigPost(EthernetClient &client, const String &body) {
  DynamicJsonDocument doc(4096);
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
  DynamicJsonDocument doc(512);
  if (deserializeJson(doc, body)) {
    respondStatus(client, 400, F("Invalid JSON"));
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

static ConfigDispatchStatus dispatchClientConfig(const char *clientUid, JsonVariantConst cfgObj) {
  char buffer[1536];
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
}

static void processNotefile(const char *fileName, void (*handler)(JsonDocument &, double)) {
  while (true) {
    J *req = notecard.newRequest("note.get");
    if (!req) {
      return;
    }
    JAddStringToObject(req, "file", fileName);
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
  }
}

static void handleTelemetry(JsonDocument &doc, double epoch) {
  const char *clientUid = doc["client"] | "";
  uint8_t tankNumber = doc["tank"].as<uint8_t>();
  TankRecord *rec = upsertTankRecord(clientUid, tankNumber);
  if (!rec) {
    return;
  }

  strlcpy(rec->site, doc["site"] | "", sizeof(rec->site));
  strlcpy(rec->label, doc["label"] | "Tank", sizeof(rec->label));
  rec->levelInches = doc["levelInches"].as<float>();
  rec->percent = doc["percent"].as<float>();
  if (doc.containsKey("heightInches")) {
    rec->heightInches = doc["heightInches"].as<float>();
  }
  rec->lastUpdateEpoch = (epoch > 0.0) ? epoch : currentEpoch();
}

static void handleAlarm(JsonDocument &doc, double epoch) {
  const char *clientUid = doc["client"] | "";
  uint8_t tankNumber = doc["tank"].as<uint8_t>();
  TankRecord *rec = upsertTankRecord(clientUid, tankNumber);
  if (!rec) {
    return;
  }

  const char *type = doc["type"] | "";
  float inches = doc["levelInches"].as<float>();
  bool isDiagnostic = (strcmp(type, "sensor-fault") == 0) ||
                      (strcmp(type, "sensor-stuck") == 0) ||
                      (strcmp(type, "sensor-recovered") == 0);
  bool isRecovery = (strcmp(type, "sensor-recovered") == 0);

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
  rec->levelInches = inches;
  if (doc.containsKey("heightInches")) {
    rec->heightInches = doc["heightInches"].as<float>();
  }
  if (doc.containsKey("percent")) {
    rec->percent = doc["percent"].as<float>();
  } else if (rec->heightInches > 0.1f) {
    rec->percent = (inches / rec->heightInches) * 100.0f;
  }
  rec->lastUpdateEpoch = (epoch > 0.0) ? epoch : currentEpoch();

  // Check rate limit before sending SMS
  bool smsEnabled = !doc.containsKey("smsEnabled") || doc["smsEnabled"].as<bool>();
  bool smsAllowedByServer = true;
  if (strcmp(type, "high") == 0) {
    smsAllowedByServer = gConfig.smsOnHigh;
  } else if (strcmp(type, "low") == 0) {
    smsAllowedByServer = gConfig.smsOnLow;
  } else if (strcmp(type, "clear") == 0) {
    smsAllowedByServer = gConfig.smsOnClear;
  }

  if (!isDiagnostic && smsEnabled && smsAllowedByServer && checkSmsRateLimit(rec)) {
    char message[160];
    snprintf(message, sizeof(message), "%s #%d %s alarm %.1f in", rec->site, rec->tankNumber, rec->alarmType, inches);
    sendSmsAlert(message);
  }
}

static void handleDaily(JsonDocument &doc, double epoch) {
  (void)doc;
  (void)epoch;
  // Daily reports are persisted in sendDailyEmail; nothing to do for inbound ack
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
  for (uint8_t i = 0; i < rec->smsAlertsInLastHour; ++i) {
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

  DynamicJsonDocument doc(2048);
  doc["to"] = gConfig.dailyEmail;
  doc["subject"] = "Daily Tank Summary";
  JsonArray tanks = doc.createNestedArray("tanks");
  for (uint8_t i = 0; i < gTankRecordCount; ++i) {
    JsonObject obj = tanks.createNestedObject();
    obj["client"] = gTankRecords[i].clientUid;
    obj["site"] = gTankRecords[i].site;
    obj["label"] = gTankRecords[i].label;
    obj["tank"] = gTankRecords[i].tankNumber;
    obj["levelInches"] = gTankRecords[i].levelInches;
    obj["percent"] = gTankRecords[i].percent;
    obj["alarm"] = gTankRecords[i].alarmActive;
    obj["alarmType"] = gTankRecords[i].alarmType;
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
    return;
  }
  JAddItemToObject(req, "body", body);
  notecard.sendRequest(req);

  gLastDailyEmailSentEpoch = now;
  Serial.println(F("Daily email queued"));
}

static void publishViewerSummary() {
  DynamicJsonDocument doc(TANK_JSON_CAPACITY + 1024);
  doc["serverName"] = gConfig.serverName;
  doc["serverUid"] = gServerUid;
  double now = currentEpoch();
  doc["generatedEpoch"] = now;
  doc["refreshSeconds"] = VIEWER_SUMMARY_INTERVAL_SECONDS;
  doc["baseHour"] = VIEWER_SUMMARY_BASE_HOUR;
  JsonArray arr = doc.createNestedArray("tanks");
  for (uint8_t i = 0; i < gTankRecordCount; ++i) {
    JsonObject obj = arr.createNestedObject();
    obj["client"] = gTankRecords[i].clientUid;
    obj["site"] = gTankRecords[i].site;
    obj["label"] = gTankRecords[i].label;
    obj["tank"] = gTankRecords[i].tankNumber;
    obj["heightInches"] = gTankRecords[i].heightInches;
    obj["levelInches"] = gTankRecords[i].levelInches;
    obj["percent"] = gTankRecords[i].percent;
    obj["alarm"] = gTankRecords[i].alarmActive;
    obj["alarmType"] = gTankRecords[i].alarmType;
    obj["lastUpdate"] = gTankRecords[i].lastUpdateEpoch;
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

static void loadClientConfigSnapshots() {
  gClientConfigCount = 0;
#ifdef FILESYSTEM_AVAILABLE
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
}

static void saveClientConfigSnapshots() {
#ifdef FILESYSTEM_AVAILABLE
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
