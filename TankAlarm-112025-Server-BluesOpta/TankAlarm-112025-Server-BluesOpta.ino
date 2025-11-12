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
#include <LittleFS.h>
#include <Notecard.h>
#include <Ethernet.h>
#include <math.h>
#include <string.h>
#include <pgmspace.h>

// Watchdog support for STM32H7 (Arduino Opta)
#if defined(ARDUINO_OPTA) || defined(STM32H7xx)
  #include <IWatchdog.h>
  #define WATCHDOG_AVAILABLE
  #define WATCHDOG_TIMEOUT_SECONDS 30
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

static byte gMacAddress[6] = { 0x02, 0x00, 0x01, 0x12, 0x20, 0x25 };
static IPAddress gStaticIp(192, 168, 1, 200);
static IPAddress gStaticGateway(192, 168, 1, 1);
static IPAddress gStaticSubnet(255, 255, 255, 0);
static IPAddress gStaticDns(8, 8, 8, 8);

struct ServerConfig {
  char serverName[32];
  char smsPrimary[20];
  char smsSecondary[20];
  char dailyEmail[64];
  uint8_t dailyHour;
  uint8_t dailyMinute;
  uint8_t webRefreshSeconds;
  bool useStaticIp;
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
  char alarmType[8];
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

static unsigned long gLastPollMillis = 0;

// Email rate limiting
static double gLastDailyEmailSentEpoch = 0.0;
#define MIN_DAILY_EMAIL_INTERVAL_SECONDS 3600  // Minimum 1 hour between daily emails

#ifndef strlcpy
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

static const char DASHBOARD_HTML[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Tank Alarm Server</title>
  <style>
    :root {
      color-scheme: light dark;
      font-family: "Segoe UI", Arial, sans-serif;
    }
    body {
      margin: 0;
      background: #f4f6f8;
      color: #1f2933;
    }
    header {
      padding: 16px 24px;
      background: #1d3557;
      color: #fff;
      box-shadow: 0 2px 6px rgba(0,0,0,0.2);
    }
    header h1 {
      margin: 0 0 4px 0;
      font-size: 1.6rem;
    }
    header .meta {
      display: flex;
      gap: 16px;
      flex-wrap: wrap;
      font-size: 0.95rem;
    }
    main {
      padding: 20px;
      max-width: 1200px;
      margin: 0 auto;
    }
    .layout {
      display: grid;
      gap: 20px;
    }
    @media (min-width: 960px) {
      .layout {
        grid-template-columns: 1fr 1.3fr;
      }
    }
    .card {
      background: #fff;
      border-radius: 12px;
      box-shadow: 0 10px 30px rgba(15,23,42,0.08);
      padding: 20px;
    }
    h2 {
      margin-top: 0;
      font-size: 1.3rem;
    }
    table {
      width: 100%;
      border-collapse: collapse;
      margin-top: 10px;
    }
    th, td {
      padding: 8px 10px;
      border-bottom: 1px solid #e2e8f0;
      text-align: left;
      vertical-align: middle;
    }
    th {
      background: #f1f5f9;
      font-weight: 600;
    }
    tr.alarm {
      background: #ffe3e3;
    }
    .field {
      display: flex;
      flex-direction: column;
      margin-bottom: 12px;
    }
    .field span {
      font-size: 0.9rem;
      color: #475569;
      margin-bottom: 4px;
    }
    .field input,
    .field select {
      padding: 8px 10px;
      border-radius: 6px;
      border: 1px solid #cbd5f5;
      font-size: 0.95rem;
    }
    .form-grid {
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(180px, 1fr));
      gap: 12px;
    }
    .actions {
      margin-top: 16px;
      display: flex;
      gap: 12px;
      flex-wrap: wrap;
    }
    button {
      border: none;
      border-radius: 6px;
      padding: 10px 16px;
      font-size: 0.95rem;
      cursor: pointer;
      background: #1d4ed8;
      color: #fff;
      transition: transform 0.1s ease;
    }
    button.secondary {
      background: #64748b;
    }
    button.destructive {
      background: #e11d48;
    }
    button:hover {
      transform: translateY(-1px);
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
    }
    #toast.show {
      opacity: 1;
    }
    .checkbox-cell {
      text-align: center;
    }
    .checkbox-cell input {
      width: 18px;
      height: 18px;
    }
    .details {
      margin: 10px 0 18px;
      font-size: 0.95rem;
      color: #475569;
    }
    .details code {
      background: #e2e8f0;
      padding: 2px 6px;
      border-radius: 4px;
      font-size: 0.85rem;
    }
    .tank-table th,
    .tank-table td {
      font-size: 0.85rem;
    }
    .tank-table input,
    .tank-table select {
      width: 100%;
      padding: 4px 6px;
      font-size: 0.85rem;
    }
    .tank-table button {
      padding: 4px 8px;
      font-size: 0.8rem;
    }
  </style>
</head>
<body>
  <header>
    <h1 id="serverName">Tank Alarm Server</h1>
    <div class="meta">
      <span>Server UID: <code id="serverUid">--</code></span>
      <span>Next Daily Email: <span id="nextEmail">--</span></span>
    </div>
  </header>
  <main>
    <div class="layout">
      <section class="card">
        <h2>Active Sites</h2>
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
      <section class="card">
        <h2>Client Configuration</h2>
        <label class="field">
          <span>Select Client</span>
          <select id="clientSelect"></select>
        </label>
        <div id="clientDetails" class="details">Select a client to review configuration.</div>
        <form id="configForm">
          <div class="form-grid">
            <label class="field"><span>Site Name</span><input id="siteInput" type="text" placeholder="Site name"></label>
            <label class="field"><span>Device Label</span><input id="deviceLabelInput" type="text" placeholder="Device label"></label>
            <label class="field"><span>Server Route</span><input id="routeInput" type="text" placeholder="default-route"></label>
            <label class="field"><span>Sample Seconds</span><input id="sampleSecondsInput" type="number" min="30" step="30"></label>
            <label class="field"><span>Report Hour (0-23)</span><input id="reportHourInput" type="number" min="0" max="23"></label>
            <label class="field"><span>Report Minute (0-59)</span><input id="reportMinuteInput" type="number" min="0" max="59"></label>
            <label class="field"><span>SMS Primary</span><input id="smsPrimaryInput" type="text" placeholder="+1234567890"></label>
            <label class="field"><span>SMS Secondary</span><input id="smsSecondaryInput" type="text" placeholder="+1234567890"></label>
            <label class="field"><span>Daily Report Email</span><input id="dailyEmailInput" type="email" placeholder="reports@example.com"></label>
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
    </div>
  </main>
  <div id="toast"></div>
  <script>
    (function() {
      const state = {
        data: null,
        selected: null
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
        reportHour: document.getElementById('reportHourInput'),
        reportMinute: document.getElementById('reportMinuteInput'),
        smsPrimary: document.getElementById('smsPrimaryInput'),
        smsSecondary: document.getElementById('smsSecondaryInput'),
        dailyEmail: document.getElementById('dailyEmailInput'),
        tankBody: document.querySelector('#tankTable tbody'),
        toast: document.getElementById('toast'),
        addTank: document.getElementById('addTank'),
        form: document.getElementById('configForm')
      };

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
        return entry ? entry.config : null;
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
          serverRoute: 'default-route',
          sampleSeconds: 300,
          reportHour: 5,
          reportMinute: 0,
          sms: {
            primary: serverDefaults.smsPrimary || '',
            secondary: serverDefaults.smsSecondary || ''
          },
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
    els.route.value = config.serverRoute || '';
    els.sampleSeconds.value = valueOr(config.sampleSeconds, 300);
    els.reportHour.value = valueOr(config.reportHour, 5);
    els.reportMinute.value = valueOr(config.reportMinute, 0);
    const smsConfig = config.sms || {};
    els.smsPrimary.value = smsConfig.primary || '';
    els.smsSecondary.value = smsConfig.secondary || '';
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
        const sms = {
          primary: els.smsPrimary.value.trim(),
          secondary: els.smsSecondary.value.trim()
        };

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
          serverRoute: els.route.value.trim() || 'default-route',
          sampleSeconds: parseInt(els.sampleSeconds.value, 10) || 300,
          reportHour: parseInt(els.reportHour.value, 10) || 5,
          reportMinute: parseInt(els.reportMinute.value, 10) || 0,
          sms,
          dailyEmail: els.dailyEmail.value.trim(),
          tanks
        };

        return config;
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

        const payload = { client: uid, config };
        try {
          const res = await fetch('/api/config', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(payload)
          });
          if (!res.ok) {
            throw new Error('Server rejected configuration');
          }
          showToast('Configuration queued for delivery');
          await refreshData();
        } catch (err) {
          showToast(err.message || 'Failed to send config', true);
        }
      }

      async function refreshData() {
        const res = await fetch('/api/clients');
        if (!res.ok) {
          throw new Error('Failed to fetch server data');
        }
        state.data = await res.json();
  const serverInfo = (state.data && state.data.server) ? state.data.server : {};
  els.serverName.textContent = serverInfo.name || 'Tank Alarm Server';
        els.serverUid.textContent = state.data.serverUid || '--';
        els.nextEmail.textContent = formatEpoch(state.data.nextDailyEmailEpoch);
        renderTelemetry();
        populateClientSelect();
        if (state.selected) {
          loadConfigIntoForm(state.selected);
        } else if (els.clientSelect.options.length) {
          loadConfigIntoForm(els.clientSelect.value);
        }
      }

      els.addTank.addEventListener('click', () => addTankRow());
      els.clientSelect.addEventListener('change', event => {
        loadConfigIntoForm(event.target.value);
      });
      els.form.addEventListener('submit', submitConfig);

      refreshData().catch(err => showToast(err.message || 'Initialization failed', true));
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
static void initializeEthernet();
static void handleWebRequests();
static bool readHttpRequest(EthernetClient &client, String &method, String &path, String &body, size_t &contentLength);
static void respondHtml(EthernetClient &client, const String &body);
static void respondJson(EthernetClient &client, const String &body, int status = 200);
static void respondStatus(EthernetClient &client, int status, const String &message);
static void sendDashboard(EthernetClient &client);
static void sendTankJson(EthernetClient &client);
static void sendClientDataJson(EthernetClient &client);
static void handleConfigPost(EthernetClient &client, const String &body);
static void dispatchClientConfig(const char *clientUid, JsonVariantConst cfgObj);
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

  if (gConfigDirty) {
    if (saveConfig(gConfig)) {
      gConfigDirty = false;
    }
  }
}

static void initializeStorage() {
  if (!LittleFS.begin()) {
    Serial.println(F("LittleFS init failed; halting"));
    while (true) {
      delay(1000);
    }
  }
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
  strlcpy(cfg.smsPrimary, "+12223334444", sizeof(cfg.smsPrimary));
  strlcpy(cfg.smsSecondary, "+15556667777", sizeof(cfg.smsSecondary));
  strlcpy(cfg.dailyEmail, "reports@example.com", sizeof(cfg.dailyEmail));
  cfg.dailyHour = DAILY_EMAIL_HOUR_DEFAULT;
  cfg.dailyMinute = DAILY_EMAIL_MINUTE_DEFAULT;
  cfg.webRefreshSeconds = 15;
  cfg.useStaticIp = true;
}

static bool loadConfig(ServerConfig &cfg) {
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
  strlcpy(cfg.smsPrimary, doc["smsPrimary"].as<const char *>() ? doc["smsPrimary"].as<const char *>() : "+12223334444", sizeof(cfg.smsPrimary));
  strlcpy(cfg.smsSecondary, doc["smsSecondary"].as<const char *>() ? doc["smsSecondary"].as<const char *>() : "+15556667777", sizeof(cfg.smsSecondary));
  strlcpy(cfg.dailyEmail, doc["dailyEmail"].as<const char *>() ? doc["dailyEmail"].as<const char *>() : "reports@example.com", sizeof(cfg.dailyEmail));
  cfg.dailyHour = doc["dailyHour"].is<uint8_t>() ? doc["dailyHour"].as<uint8_t>() : DAILY_EMAIL_HOUR_DEFAULT;
  cfg.dailyMinute = doc["dailyMinute"].is<uint8_t>() ? doc["dailyMinute"].as<uint8_t>() : DAILY_EMAIL_MINUTE_DEFAULT;
  cfg.webRefreshSeconds = doc["webRefreshSeconds"].is<uint8_t>() ? doc["webRefreshSeconds"].as<uint8_t>() : 15;
  cfg.useStaticIp = doc["useStaticIp"].is<bool>() ? doc["useStaticIp"].as<bool>() : true;

  if (doc.containsKey("staticIp")) {
    JsonArray ip = doc["staticIp"].as<JsonArray>();
    if (ip.size() == 4) {
      gStaticIp = IPAddress(ip[0], ip[1], ip[2], ip[3]);
    }
  }
  if (doc.containsKey("gateway")) {
    JsonArray gw = doc["gateway"].as<JsonArray>();
    if (gw.size() == 4) {
      gStaticGateway = IPAddress(gw[0], gw[1], gw[2], gw[3]);
    }
  }
  if (doc.containsKey("subnet")) {
    JsonArray sn = doc["subnet"].as<JsonArray>();
    if (sn.size() == 4) {
      gStaticSubnet = IPAddress(sn[0], sn[1], sn[2], sn[3]);
    }
  }
  if (doc.containsKey("dns")) {
    JsonArray dns = doc["dns"].as<JsonArray>();
    if (dns.size() == 4) {
      gStaticDns = IPAddress(dns[0], dns[1], dns[2], dns[3]);
    }
  }

  return true;
}

static bool saveConfig(const ServerConfig &cfg) {
  DynamicJsonDocument doc(2048);
  doc["serverName"] = cfg.serverName;
  doc["smsPrimary"] = cfg.smsPrimary;
  doc["smsSecondary"] = cfg.smsSecondary;
  doc["dailyEmail"] = cfg.dailyEmail;
  doc["dailyHour"] = cfg.dailyHour;
  doc["dailyMinute"] = cfg.dailyMinute;
  doc["webRefreshSeconds"] = cfg.webRefreshSeconds;
  doc["useStaticIp"] = cfg.useStaticIp;

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
  } else if (method == "GET" && path == "/api/tanks") {
    sendTankJson(client);
  } else if (method == "GET" && path == "/api/clients") {
    sendClientDataJson(client);
  } else if (method == "POST" && path == "/api/config") {
    handleConfigPost(client, body);
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
  client.println(status == 200 ? F(" OK") : "");
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

static void sendTankJson(EthernetClient &client) {
  DynamicJsonDocument doc(4096);
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
  serializeJson(doc, json);
  respondJson(client, json);
}

static void sendClientDataJson(EthernetClient &client) {
  DynamicJsonDocument doc(16384);

  JsonObject serverObj = doc.createNestedObject("server");
  serverObj["name"] = gConfig.serverName;
  serverObj["smsPrimary"] = gConfig.smsPrimary;
  serverObj["smsSecondary"] = gConfig.smsSecondary;
  serverObj["dailyEmail"] = gConfig.dailyEmail;
  serverObj["dailyHour"] = gConfig.dailyHour;
  serverObj["dailyMinute"] = gConfig.dailyMinute;
  serverObj["webRefreshSeconds"] = gConfig.webRefreshSeconds;

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

    DynamicJsonDocument cfgDoc(2048);
    if (deserializeJson(cfgDoc, snap.payload) == DeserializationError::Ok) {
      cfgEntry["config"] = cfgDoc.as<JsonVariantConst>();
    }
  }

  String json;
  serializeJson(doc, json);
  respondJson(client, json);
}

static void handleConfigPost(EthernetClient &client, const String &body) {
  DynamicJsonDocument doc(4096);
  DeserializationError err = deserializeJson(doc, body);
  if (err) {
    respondStatus(client, 400, F("Invalid JSON"));
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
      gConfig.webRefreshSeconds = serverObj["webRefreshSeconds"].as<uint8_t>();
    }
    gConfigDirty = true;
    scheduleNextDailyEmail();
  }

  if (doc.containsKey("client") && doc.containsKey("config")) {
    const char *clientUid = doc["client"].as<const char *>();
    if (clientUid && strlen(clientUid) > 0) {
      dispatchClientConfig(clientUid, doc["config"]);
    }
  }

  respondStatus(client, 200, F("OK"));
}

static void dispatchClientConfig(const char *clientUid, JsonVariantConst cfgObj) {
  char buffer[1536];
  size_t len = serializeJson(cfgObj, buffer, sizeof(buffer));
  if (len == 0 || len >= sizeof(buffer)) {
    Serial.println(F("Client config payload too large"));
    return;
  }
  buffer[len] = '\0';

  J *req = notecard.newRequest("note.add");
  if (!req) {
    return;
  }
  JAddStringToObject(req, "file", CONFIG_OUTBOX_FILE);
  JAddBoolToObject(req, "sync", true);
  JAddStringToObject(req, "device", clientUid);

  J *body = JParse(buffer);
  if (!body) {
    notecard.deleteRequest(req);
    return;
  }
  JAddItemToObject(req, "body", body);
  notecard.sendRequest(req);

  cacheClientConfigFromBuffer(clientUid, buffer);

  Serial.print(F("Queued config update for client " ));
  Serial.println(clientUid);
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

    char *json = JConvertToJson(body);
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

  if (strcmp(type, "clear") == 0) {
    rec->alarmActive = false;
    strlcpy(rec->alarmType, "clear", sizeof(rec->alarmType));
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
  if (checkSmsRateLimit(rec)) {
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
    notecard.deleteRequest(req);
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
    notecard.deleteRequest(req);
    return;
  }
  JAddItemToObject(req, "body", body);
  notecard.sendRequest(req);

  gLastDailyEmailSentEpoch = now;
  Serial.println(F("Daily email queued"));
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
}

static void saveClientConfigSnapshots() {
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
