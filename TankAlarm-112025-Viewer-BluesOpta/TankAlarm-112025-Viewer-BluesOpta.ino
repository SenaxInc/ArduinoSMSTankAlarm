/*
  Tank Alarm Viewer 112025 - Arduino Opta + Blues Notecard
  Version: 1.0.0

  Purpose:
  - Read-only kiosk that renders the server dashboard without exposing control paths
  - Fetches a summarized notefile produced by the server every 6 hours starting at 6 AM
  - Suitable for remote sites that cannot talk to the server over LAN

  Hardware:
  - Arduino Opta Lite (Ethernet)
  - Blues Wireless Notecard for Opta adapter

  Created: November 2025
*/

#include <Arduino.h>
#include <Wire.h>
#include <ArduinoJson.h>
#include <Notecard.h>
#if defined(ARDUINO_OPTA) || defined(ARDUINO_PORTENTA_H7_M7) || defined(ARDUINO_PORTENTA_H7_M4)
  #include <PortentaEthernet.h>
  #include <Ethernet.h>
#else
  #include <Ethernet.h>
#endif
#include <math.h>
#include <string.h>

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

// Watchdog support
// Note: Arduino Opta uses Mbed OS, which has different APIs than STM32duino
#if defined(ARDUINO_ARCH_STM32) && !defined(ARDUINO_ARCH_MBED)
  // STM32duino platform (non-Mbed)
  #include <IWatchdog.h>
  #define WATCHDOG_AVAILABLE
  #define WATCHDOG_TIMEOUT_SECONDS 30
#elif defined(ARDUINO_OPTA) || defined(ARDUINO_ARCH_MBED)
  // Arduino Opta with Mbed OS - use Mbed OS Watchdog API
  #include <mbed.h>
  using namespace mbed;
  #define WATCHDOG_AVAILABLE
  #define WATCHDOG_TIMEOUT_SECONDS 30
  static Watchdog &mbedWatchdog = Watchdog::get_instance();
#endif

#ifndef VIEWER_PRODUCT_UID
#define VIEWER_PRODUCT_UID "com.senax.tankalarm112025:viewer"
#endif

#ifndef VIEWER_SUMMARY_FILE
#define VIEWER_SUMMARY_FILE "viewer_summary.qi"
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

#ifndef MAX_TANK_RECORDS
#define MAX_TANK_RECORDS 32
#endif

#ifndef VIEWER_NAME
#define VIEWER_NAME "Tank Alarm Viewer"
#endif

#ifndef WEB_REFRESH_SECONDS
#define WEB_REFRESH_SECONDS 21600
#endif

#ifndef WEB_REFRESH_MINUTES
#define WEB_REFRESH_MINUTES (WEB_REFRESH_SECONDS / 60)
#endif

#ifndef SUMMARY_FETCH_INTERVAL_SECONDS
#define SUMMARY_FETCH_INTERVAL_SECONDS 21600UL
#endif

#ifndef SUMMARY_FETCH_BASE_HOUR
#define SUMMARY_FETCH_BASE_HOUR 6
#endif

// Viewer is intended for GET-only use; cap request bodies to avoid memory exhaustion.
#ifndef MAX_HTTP_BODY_BYTES
#define MAX_HTTP_BODY_BYTES 1024
#endif

#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)

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
  float vinVoltage;  // Blues Notecard VIN voltage
};

static const size_t TANK_JSON_CAPACITY = JSON_ARRAY_SIZE(MAX_TANK_RECORDS) + (MAX_TANK_RECORDS * JSON_OBJECT_SIZE(10)) + 768;

static byte gMacAddress[6] = { 0x02, 0x00, 0x01, 0x11, 0x20, 0x25 };
static IPAddress gStaticIp(192, 168, 1, 210);
static IPAddress gStaticGateway(192, 168, 1, 1);
static IPAddress gStaticSubnet(255, 255, 255, 0);
static IPAddress gStaticDns(8, 8, 8, 8);
static bool gUseStaticIp = true;

static TankRecord gTankRecords[MAX_TANK_RECORDS];
static uint8_t gTankRecordCount = 0;

static Notecard notecard;
static EthernetServer gWebServer(ETHERNET_PORT);
static char gViewerUid[48] = {0};
static char gSourceServerName[32] = {0};
static char gSourceServerUid[48] = {0};
static uint32_t gSourceRefreshSeconds = SUMMARY_FETCH_INTERVAL_SECONDS;
static uint8_t gSourceBaseHour = SUMMARY_FETCH_BASE_HOUR;
static double gLastSummaryGeneratedEpoch = 0.0;
static double gLastSummaryFetchEpoch = 0.0;
static double gNextSummaryFetchEpoch = 0.0;
static double gLastSyncedEpoch = 0.0;
static unsigned long gLastSyncMillis = 0;

static const char VIEWER_DASHBOARD_HTML[] PROGMEM = R"HTML(<!DOCTYPE html><html lang="en"><head><meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1.0"><title>Tank Alarm Viewer</title><style>:root{--bg:#f8fafc;--text:#0f172a;--header-bg:#ffffff;--meta-color:#475569;--card-bg:#ffffff;--table-border:rgba(15,23,42,0.08)}body[data-theme="dark"]{--bg:#0f172a;--text:#e2e8f0;--header-bg:#1e293b;--meta-color:#94a3b8;--card-bg:#1e293b;--table-border:rgba(255,255,255,0.08)}body{margin:0;font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,Helvetica,Arial,sans-serif;background:var(--bg);color:var(--text);transition:background 0.3s,color 0.3s}header{padding:20px 28px;background:var(--header-bg);box-shadow:0 2px 10px rgba(0,0,0,0.15)}header h1{margin:0;font-size:1.7rem}header .meta{margin-top:12px;font-size:0.95rem;color:var(--meta-color);display:flex;gap:16px;flex-wrap:wrap}.title-row{display:flex;justify-content:space-between;gap:16px;flex-wrap:wrap;align-items:flex-start}.header-actions{display:flex;gap:12px;align-items:center}.icon-button{width:40px;height:40px;border-radius:50%;border:1px solid rgba(148,163,184,0.4);background:var(--card-bg);color:var(--text);font-size:1.1rem;cursor:pointer}main{padding:24px;max-width:1400px;margin:0 auto}.card{background:var(--card-bg);border-radius:16px;padding:20px;box-shadow:0 25px 60px rgba(15,23,42,0.15);border:1px solid rgba(15,23,42,0.08)}table{width:100%;border-collapse:collapse;margin-top:12px}th,td{text-align:left;padding:10px 12px;border-bottom:1px solid var(--table-border)}th{text-transform:uppercase;letter-spacing:0.05em;font-size:0.75rem;color:var(--meta-color)}tr:last-child td{border-bottom:none}tr.alarm{background:rgba(220,38,38,0.08)}body[data-theme="dark"] tr.alarm{background:rgba(220,38,38,0.18)}.status-pill{display:inline-flex;align-items:center;gap:6px;border-radius:999px;padding:4px 12px;font-size:0.85rem}.status-pill.ok{background:rgba(16,185,129,0.15);color:#34d399}.status-pill.alarm{background:rgba(248,113,113,0.2);color:#fca5a5}.timestamp{font-feature-settings:"tnum";color:var(--meta-color);font-size:0.9rem}footer{margin-top:20px;color:var(--meta-color);font-size:0.85rem;text-align:center}</style></head><body data-theme="light"><header><div class="title-row"><div><h1 id="viewerName">Tank Alarm Viewer</h1><div class="meta"><span>Viewer UID: <code id="viewerUid">--</code></span><span>Source: <strong id="sourceServer">--</strong> (<code id="sourceUid">--</code>)</span><span>Summary Generated: <span id="summaryGenerated">--</span></span><span>Last Fetch: <span id="lastFetch">--</span></span><span>Next Scheduled Fetch: <span id="nextFetch">--</span></span><span>Server cadence: <span id="refreshHint">6h @ 6 AM</span></span></div></div><div class="header-actions"><button class="icon-button" id="themeToggle" aria-label="Switch to dark mode">&#9789;</button></div></div></header><main><section class="card"><div style="display:flex;justify-content:space-between;align-items:baseline;gap:12px;flex-wrap:wrap"><h2 style="margin:0;font-size:1.2rem">Fleet Snapshot</h2><span class="timestamp">Dashboard auto-refresh: )HTML" STR(WEB_REFRESH_MINUTES) R"HTML( min</span></div><table><thead><tr><th>Site</th><th>Tank</th><th>Level (ft/in)</th><th>24hr Change</th><th>Updated</th></tr></thead><tbody id="tankBody"></tbody></table></section><footer>Viewer nodes are read-only mirrors. Configuration and permissions stay on the server fleet.</footer></main><script>(()=>{const THEME_KEY='tankalarmTheme';const themeToggle=document.getElementById('themeToggle');function applyTheme(next){const theme=next==='dark'?'dark':'light';document.body.dataset.theme=theme;themeToggle.textContent=theme==='dark'?'☀':'☾';themeToggle.setAttribute('aria-label',theme==='dark'?'Switch to light mode':'Switch to dark mode');localStorage.setItem(THEME_KEY,theme)}applyTheme(localStorage.getItem(THEME_KEY)||'light');themeToggle.addEventListener('click',()=>{const next=document.body.dataset.theme==='dark'?'light':'dark';applyTheme(next)});const REFRESH_SECONDS=)HTML" STR(WEB_REFRESH_SECONDS) R"HTML(;const els={viewerName:document.getElementById('viewerName'),viewerUid:document.getElementById('viewerUid'),sourceServer:document.getElementById('sourceServer'),sourceUid:document.getElementById('sourceUid'),summaryGenerated:document.getElementById('summaryGenerated'),lastFetch:document.getElementById('lastFetch'),nextFetch:document.getElementById('nextFetch'),refreshHint:document.getElementById('refreshHint'),tankBody:document.getElementById('tankBody')};const state={tanks:[]};function applyTankData(d){els.viewerName.textContent=d.vn||d.viewerName||'Tank Alarm Viewer';els.viewerUid.textContent=d.vi||d.viewerUid||'--';els.sourceServer.textContent=d.sn||d.sourceServerName||'Server';els.sourceUid.textContent=d.si||d.sourceServerUid||'--';els.summaryGenerated.textContent=formatEpoch(d.ge||d.generatedEpoch);els.lastFetch.textContent=formatEpoch(d.lf||d.lastFetchEpoch);els.nextFetch.textContent=formatEpoch(d.nf||d.nextFetchEpoch);els.refreshHint.textContent=describeCadence(d.rs||d.refreshSeconds,d.bh||d.baseHour);state.tanks=d.tanks||[];renderTankRows()}async function fetchTanks(){try{const res=await fetch('/api/tanks');if(!res.ok)throw new Error('HTTP '+res.status);const data=await res.json();applyTankData(data)}catch(err){console.error('Viewer refresh failed',err)}}function renderTankRows(){const tbody=els.tankBody;tbody.innerHTML='';const rows=state.tanks;if(!rows.length){const tr=document.createElement('tr');tr.innerHTML='<td colspan="5">No tank data available</td>';tbody.appendChild(tr);return}const now=Date.now();const staleThresholdMs=93600000;rows.forEach(t=>{const tr=document.createElement('tr');const alarm=t.a!==undefined?t.a:t.alarm;if(alarm)tr.classList.add('alarm');const lastUpdate=t.u||t.lastUpdate;const isStale=lastUpdate&&((now-(lastUpdate*1000))>staleThresholdMs);const staleWarning=isStale?' ⚠️':'';tr.innerHTML=`<td>${escapeHtml(t.s||t.site,'--')}</td><td>${escapeHtml(t.n||t.label||'Tank')} #${escapeHtml((t.k??t.tank??'?'))}</td><td>${formatFeetInches(t.l!==undefined?t.l:t.levelInches)}</td><td>--</td><td>${formatEpoch(lastUpdate)}${staleWarning}</td>`;if(isStale){tr.style.opacity='0.6';tr.title='Data is over 26 hours old'}tbody.appendChild(tr)})}function statusBadge(t){const alarm=t.a!==undefined?t.a:t.alarm;if(!alarm){return'<span class="status-pill ok">Normal</span>'}const label=escapeHtml(t.at||t.alarmType||'Alarm','Alarm');return`<span class="status-pill alarm">${label}</span>`}function formatFeetInches(inches){if(typeof inches!=='number'||!isFinite(inches)||inches<0)return'--';const feet=Math.floor(inches/12);const remainingInches=inches-(feet*12);return`${feet}' ${remainingInches.toFixed(1)}"`}function formatEpoch(epoch){if(!epoch)return'--';const date=new Date(epoch*1000);if(isNaN(date.getTime()))return'--';return date.toLocaleString()}function describeCadence(seconds,baseHour){const hours=seconds?(seconds/3600).toFixed(1).replace(/\.0$/,''):'6';const hourLabel=(typeof baseHour==='number')?baseHour:6;return`${hours} h cadence · starts ${hourLabel}:00`}function escapeHtml(value,fallback=''){if(value===undefined||value===null||value==='')return fallback;const entityMap={'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'};return String(value).replace(/[&<>"']/g,c=>entityMap[c]||c)}fetchTanks();setInterval(()=>fetchTanks(),REFRESH_SECONDS*1000)})();</script></body></html>)HTML";

static void initializeNotecard();
static void initializeEthernet();
static void handleWebRequests();
static bool readHttpRequest(EthernetClient &client, String &method, String &path, String &body, size_t &contentLength, bool &bodyTooLarge);
static void respondHtml(EthernetClient &client, const char *body, size_t len);
static void respondJson(EthernetClient &client, const String &body);
static void respondStatus(EthernetClient &client, int status, const char *message);
static void sendDashboard(EthernetClient &client);
static void sendTankJson(EthernetClient &client);
static void ensureTimeSync();
static double currentEpoch();
static double computeNextAlignedEpoch(double epoch, uint8_t baseHour, uint32_t intervalSeconds);
static void scheduleNextSummaryFetch();
static void fetchViewerSummary();
static void handleViewerSummary(JsonDocument &doc, double epoch);

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 2000) {
    delay(10);
  }
  Serial.println();
  Serial.print(F("Tank Alarm Viewer 112025 v"));
  Serial.print(F(FIRMWARE_VERSION));
  Serial.print(F(" ("));
  Serial.print(F(FIRMWARE_BUILD_DATE));
  Serial.println(F(")"));

  Wire.begin();
  Wire.setClock(NOTECARD_I2C_FREQUENCY);

  initializeNotecard();
  ensureTimeSync();
  fetchViewerSummary();  // Drain any queued summaries before serving UI
  scheduleNextSummaryFetch();
  initializeEthernet();
  gWebServer.begin();

#ifdef WATCHDOG_AVAILABLE
  #if defined(ARDUINO_OPTA) || defined(ARDUINO_ARCH_MBED)
    uint32_t timeoutMs = WATCHDOG_TIMEOUT_SECONDS * 1000;
    if (mbedWatchdog.start(timeoutMs)) {
      Serial.print(F("Mbed Watchdog enabled ("));
      Serial.print(WATCHDOG_TIMEOUT_SECONDS);
      Serial.println(F(" s)"));
    } else {
      Serial.println(F("Warning: Watchdog initialization failed"));
    }
  #else
    IWatchdog.begin(WATCHDOG_TIMEOUT_SECONDS * 1000000UL);
    Serial.print(F("Watchdog enabled ("));
    Serial.print(WATCHDOG_TIMEOUT_SECONDS);
    Serial.println(F(" s)"));
  #endif
#else
  Serial.println(F("Watchdog not available on this platform"));
#endif

  Serial.println(F("Viewer setup complete"));
}

void loop() {
#ifdef WATCHDOG_AVAILABLE
  #if defined(ARDUINO_OPTA) || defined(ARDUINO_ARCH_MBED)
    mbedWatchdog.kick();
  #else
    IWatchdog.reload();
  #endif
#endif

  handleWebRequests();
  ensureTimeSync();

  if (gNextSummaryFetchEpoch > 0.0 && currentEpoch() >= gNextSummaryFetchEpoch) {
    fetchViewerSummary();
    scheduleNextSummaryFetch();
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

  req = notecard.newRequest("hub.set");
  if (req) {
    JAddStringToObject(req, "product", VIEWER_PRODUCT_UID);
    JAddStringToObject(req, "mode", "continuous");
    notecard.sendRequest(req);
  }

  req = notecard.newRequest("card.uuid");
  J *rsp = notecard.requestAndResponse(req);
  if (rsp) {
    const char *uid = JGetString(rsp, "uuid");
    if (uid) {
      strlcpy(gViewerUid, uid, sizeof(gViewerUid));
    }
    notecard.deleteResponse(rsp);
  }

  Serial.print(F("Viewer Notecard UID: "));
  Serial.println(gViewerUid);
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
  unsigned long delta = millis() - gLastSyncMillis;
  return gLastSyncedEpoch + (double)delta / 1000.0;
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

static void scheduleNextSummaryFetch() {
  double epoch = currentEpoch();
  uint32_t interval = (gSourceRefreshSeconds > 0) ? gSourceRefreshSeconds : SUMMARY_FETCH_INTERVAL_SECONDS;
  uint8_t baseHour = gSourceBaseHour;
  gNextSummaryFetchEpoch = computeNextAlignedEpoch(epoch, baseHour, interval);
}

static void initializeEthernet() {
  Serial.print(F("Initializing Ethernet..."));
  int status;
  if (gUseStaticIp) {
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
  } else if (method == "GET" && path == "/api/tanks") {
    sendTankJson(client);
  } else {
    respondStatus(client, 404, "Not Found");
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
        break;
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

static void respondHtml(EthernetClient &client, const char *body, size_t len) {
  client.println(F("HTTP/1.1 200 OK"));
  client.println(F("Content-Type: text/html; charset=utf-8"));
  client.print(F("Content-Length: "));
  client.println(len);
  client.println(F("Cache-Control: no-cache, no-store, must-revalidate"));
  client.println();
  client.write((const uint8_t *)body, len);
}

static void respondJson(EthernetClient &client, const String &body) {
  client.println(F("HTTP/1.1 200 OK"));
  client.println(F("Content-Type: application/json"));
  client.print(F("Content-Length: "));
  client.println(body.length());
  client.println(F("Cache-Control: no-cache, no-store, must-revalidate"));
  client.println();
  client.print(body);
}

static void respondStatus(EthernetClient &client, int status, const char *message) {
  const char *msg = message ? message : "";
  size_t len = strlen(msg);

  client.print(F("HTTP/1.1 "));
  client.print(status);
  client.print(F(" "));
  switch (status) {
    case 200: client.println(F("OK")); break;
    case 400: client.println(F("Bad Request")); break;
    case 404: client.println(F("Not Found")); break;
    default: client.println(F("Error")); break;
  }
  client.println(F("Content-Type: text/plain"));
  client.print(F("Content-Length: "));
  client.println(len);
  client.println();
  client.print(msg);
}

static void sendDashboard(EthernetClient &client) {
  size_t htmlLen = strlen_P(VIEWER_DASHBOARD_HTML);
  client.println(F("HTTP/1.1 200 OK"));
  client.println(F("Content-Type: text/html; charset=utf-8"));
  client.print(F("Content-Length: "));
  client.println(htmlLen);
  client.println(F("Cache-Control: no-cache, no-store, must-revalidate"));
  client.println();
  for (size_t i = 0; i < htmlLen; ++i) {
    char c = pgm_read_byte_near(VIEWER_DASHBOARD_HTML + i);
    client.write(c);
  }
}

static void sendTankJson(EthernetClient &client) {
  DynamicJsonDocument doc(TANK_JSON_CAPACITY + 256);
  doc["vn"] = VIEWER_NAME;
  doc["vi"] = gViewerUid;
  doc["sn"] = gSourceServerName;
  doc["si"] = gSourceServerUid;
  doc["ge"] = gLastSummaryGeneratedEpoch;
  doc["lf"] = gLastSummaryFetchEpoch;
  doc["nf"] = gNextSummaryFetchEpoch;
  doc["rs"] = gSourceRefreshSeconds;
  doc["bh"] = gSourceBaseHour;
  doc["sf"] = VIEWER_SUMMARY_FILE;
  doc["rc"] = gTankRecordCount;
  doc["ls"] = gLastSyncedEpoch;

  JsonArray arr = doc.createNestedArray("tanks");
  for (uint8_t i = 0; i < gTankRecordCount; ++i) {
    JsonObject obj = arr.createNestedObject();
    obj["c"] = gTankRecords[i].clientUid;
    obj["s"] = gTankRecords[i].site;
    obj["n"] = gTankRecords[i].label;
    obj["k"] = gTankRecords[i].tankNumber;
    obj["h"] = gTankRecords[i].heightInches;
    obj["l"] = gTankRecords[i].levelInches;
    obj["p"] = gTankRecords[i].percent;
    obj["a"] = gTankRecords[i].alarmActive;
    obj["at"] = gTankRecords[i].alarmType;
    obj["u"] = gTankRecords[i].lastUpdateEpoch;
    if (gTankRecords[i].vinVoltage > 0.0f) {
      obj["v"] = gTankRecords[i].vinVoltage;
    }
  }

  String body;
  serializeJson(doc, body);
  respondJson(client, body);
}

static void fetchViewerSummary() {
  while (true) {
    J *req = notecard.newRequest("note.get");
    if (!req) {
      return;
    }
    JAddStringToObject(req, "file", VIEWER_SUMMARY_FILE);
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
      DynamicJsonDocument doc(TANK_JSON_CAPACITY + 1024);
      DeserializationError err = deserializeJson(doc, json);
      NoteFree(json);
      if (!err) {
        handleViewerSummary(doc, epoch);
      } else {
        Serial.print(F("Summary parse failed: "));
        Serial.println(err.f_str());
      }
    }

    notecard.deleteResponse(rsp);
  }
}

static void handleViewerSummary(JsonDocument &doc, double epoch) {
  const char *serverName = doc["sn"] | doc["serverName"] | "Tank Alarm Server";
  const char *serverUid = doc["si"] | doc["serverUid"] | "";
  strlcpy(gSourceServerName, serverName, sizeof(gSourceServerName));
  strlcpy(gSourceServerUid, serverUid, sizeof(gSourceServerUid));

  if (doc.containsKey("rs")) {
    gSourceRefreshSeconds = doc["rs"].as<uint32_t>();
  } else if (doc.containsKey("refreshSeconds")) {
    gSourceRefreshSeconds = doc["refreshSeconds"].as<uint32_t>();
  }
  if (gSourceRefreshSeconds == 0) {
    gSourceRefreshSeconds = SUMMARY_FETCH_INTERVAL_SECONDS;
  }

  if (doc.containsKey("bh")) {
    gSourceBaseHour = doc["bh"].as<uint8_t>();
  } else if (doc.containsKey("baseHour")) {
    gSourceBaseHour = doc["baseHour"].as<uint8_t>();
  }

  if (doc.containsKey("ge")) {
    gLastSummaryGeneratedEpoch = doc["ge"].as<double>();
  } else if (doc.containsKey("generatedEpoch")) {
    gLastSummaryGeneratedEpoch = doc["generatedEpoch"].as<double>();
  } else {
    gLastSummaryGeneratedEpoch = (epoch > 0.0) ? epoch : currentEpoch();
  }

  gTankRecordCount = 0;
  JsonArray arr = doc["tanks"].as<JsonArray>();
  if (arr) {
    for (JsonVariantConst item : arr) {
      if (gTankRecordCount >= MAX_TANK_RECORDS) {
        break;
      }
      TankRecord &rec = gTankRecords[gTankRecordCount++];
      memset(&rec, 0, sizeof(TankRecord));
      strlcpy(rec.clientUid, item["c"] | item["client"] | "", sizeof(rec.clientUid));
      strlcpy(rec.site, item["s"] | item["site"] | "", sizeof(rec.site));
      strlcpy(rec.label, item["n"] | item["label"] | "Tank", sizeof(rec.label));
      rec.tankNumber = (item["k"] | item["tank"]).is<uint8_t>() ? (item["k"] | item["tank"]).as<uint8_t>() : gTankRecordCount;
      rec.heightInches = (item["h"] | item["heightInches"]).as<float>();
      rec.levelInches = (item["l"] | item["levelInches"]).as<float>();
      rec.percent = (item["p"] | item["percent"]).as<float>();
      rec.alarmActive = (item["a"] | item["alarm"]).as<bool>();
      strlcpy(rec.alarmType, item["at"] | item["alarmType"] | (rec.alarmActive ? "alarm" : "clear"), sizeof(rec.alarmType));
      rec.lastUpdateEpoch = (item["u"] | item["lastUpdate"]).as<double>();
      rec.vinVoltage = (item["v"] | item["vinVoltage"]).as<float>();
    }
  }

  gLastSummaryFetchEpoch = currentEpoch();
  Serial.print(F("Viewer summary applied ("));
  Serial.print(gTankRecordCount);
  Serial.println(F(" tanks)"));
}
