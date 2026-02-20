/*
  Tank Alarm Viewer 112025 - Arduino Opta + Blues Notecard
  Version: 1.1.1

  Purpose:
  - Read-only kiosk that renders the server dashboard without exposing control paths
  - Fetches a summarized notefile produced by the server every 6 hours starting at 6 AM
  - Suitable for remote sites that cannot talk to the server over LAN

  Hardware:
  - Arduino Opta Lite (Ethernet)
  - Blues Wireless Notecard for Opta adapter

  Created: November 2025
*/

// Shared library - common constants and utilities
#include <TankAlarm_Common.h>

#include <Arduino.h>
#include <Wire.h>
#include <ArduinoJson.h>
#include <memory>
#if defined(ARDUINO_OPTA) || defined(ARDUINO_PORTENTA_H7_M7) || defined(ARDUINO_PORTENTA_H7_M4)
  #include <PortentaEthernet.h>
  #include <Ethernet.h>
#else
  #include <Ethernet.h>
#endif
#include <math.h>
#include <string.h>

// Debug mode - controls Serial output and Notecard debug logging
// For PRODUCTION: Leave commented out (default) to save power consumption
// For DEVELOPMENT: Uncomment the line below for troubleshooting and monitoring
//#define DEBUG_MODE

// Watchdog support - use shared library helper
#if defined(ARDUINO_OPTA) || defined(ARDUINO_ARCH_MBED)
  static MbedWatchdogHelper mbedWatchdog;
#elif defined(ARDUINO_ARCH_STM32)
  #include <IWatchdog.h>
#endif

#ifndef DEFAULT_VIEWER_PRODUCT_UID
#define DEFAULT_VIEWER_PRODUCT_UID "com.senax.tankalarm112025:viewer"
#endif

#ifndef VIEWER_SUMMARY_FILE
#define VIEWER_SUMMARY_FILE VIEWER_SUMMARY_INBOX_FILE  // "viewer_summary.qi" — viewer reads inbound
#endif

#ifndef VIEWER_CONFIG_PATH
#define VIEWER_CONFIG_PATH "/viewer_config.json"
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

// Viewer configuration - supports DHCP by default or static IP via config file
struct ViewerConfig {
  char viewerName[32];           // Display name for this viewer
  char productUid[64];           // Notehub product UID (can be customized per fleet)
  bool useStaticIp;              // false = DHCP (default), true = use static settings below
  uint8_t macAddress[6];         // MAC address for Ethernet
  uint8_t staticIp[4];           // Static IP address
  uint8_t staticGateway[4];      // Gateway IP
  uint8_t staticSubnet[4];       // Subnet mask
  uint8_t staticDns[4];          // DNS server
};

struct TankRecord {
  char clientUid[48];
  char site[32];
  char label[24];
  uint8_t tankNumber;
  float levelInches;
  bool alarmActive;
  char alarmType[24];
  double lastUpdateEpoch;
  float vinVoltage;  // Blues Notecard VIN voltage
};

// Default network configuration lives in gConfig initializer below

// Global configuration instance with defaults
static ViewerConfig gConfig = {
  "Tank Alarm Viewer",           // viewerName
  DEFAULT_VIEWER_PRODUCT_UID,    // productUid - default, can be overridden
  false,                         // useStaticIp - DHCP by default
  { 0x02, 0x00, 0x01, 0x11, 0x20, 0x25 },  // macAddress
  { 192, 168, 1, 210 },          // staticIp
  { 192, 168, 1, 1 },            // staticGateway  
  { 255, 255, 255, 0 },          // staticSubnet
  { 8, 8, 8, 8 }                 // staticDns
};

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

// DFU State
static unsigned long gLastDfuCheckMillis = 0;
static bool gDfuUpdateAvailable = false;
static char gDfuVersion[32] = {0};
static bool gDfuInProgress = false;

static const char VIEWER_DASHBOARD_HTML[] PROGMEM = R"HTML(<!DOCTYPE html><html lang="en"><head><meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1.0"><title>Tank Alarm Viewer</title><style>:root{--bg:#f8fafc;--text:#0f172a;--header-bg:#ffffff;--meta-color:#475569;--card-bg:#ffffff;--table-border:rgba(15,23,42,0.08)}body{margin:0;font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,Helvetica,Arial,sans-serif;background:var(--bg);color:var(--text);transition:background 0.3s,color 0.3s}header{padding:20px 28px;background:var(--header-bg);box-shadow:0 2px 10px rgba(0,0,0,0.15)}header h1{margin:0;font-size:1.7rem}header .meta{margin-top:12px;font-size:0.95rem;color:var(--meta-color);display:flex;gap:16px;flex-wrap:wrap}.title-row{display:flex;justify-content:space-between;gap:16px;flex-wrap:wrap;align-items:flex-start}.header-actions{display:flex;gap:12px;align-items:center}.icon-button{width:40px;height:40px;border:1px solid rgba(148,163,184,0.4);background:var(--card-bg);color:var(--text);font-size:1.1rem;cursor:pointer}main{padding:24px;max-width:1400px;margin:0 auto}.card{background:var(--card-bg);padding:20px;box-shadow:0 25px 60px rgba(15,23,42,0.15);border:1px solid rgba(15,23,42,0.08)}table{width:100%;border-collapse:collapse;margin-top:12px}th,td{text-align:left;padding:10px 12px;border-bottom:1px solid var(--table-border)}th{text-transform:uppercase;letter-spacing:0.05em;font-size:0.75rem;color:var(--meta-color)}tr:last-child td{border-bottom:none}tr.alarm{background:rgba(220,38,38,0.08)}.status-pill{display:inline-flex;align-items:center;gap:6px;padding:4px 12px;font-size:0.85rem}.status-pill.ok{background:rgba(16,185,129,0.15);color:#34d399}.status-pill.alarm{background:rgba(248,113,113,0.2);color:#fca5a5}.timestamp{font-feature-settings:"tnum";color:var(--meta-color);font-size:0.9rem}footer{margin-top:20px;color:var(--meta-color);font-size:0.85rem;text-align:center}</style></head><body><header><div class="title-row"><div><h1 id="viewerName">Tank Alarm Viewer</h1><div class="meta"><span>Viewer UID: <code id="viewerUid">--</code></span><span>Source: <strong id="sourceServer">--</strong> (<code id="sourceUid">--</code>)</span><span>Summary Generated: <span id="summaryGenerated">--</span></span><span>Last Fetch: <span id="lastFetch">--</span></span><span>Next Scheduled Fetch: <span id="nextFetch">--</span></span><span>Server cadence: <span id="refreshHint">6h @ 6 AM</span></span></div></div></div></header><main><section class="card"><div style="display:flex;justify-content:space-between;align-items:baseline;gap:12px;flex-wrap:wrap"><h2 style="margin:0;font-size:1.2rem">Fleet Snapshot</h2><span class="timestamp">Dashboard auto-refresh: )HTML" STR(WEB_REFRESH_MINUTES) R"HTML( min</span></div><table><thead><tr><th>Site</th><th>Tank</th><th>Level (ft/in)</th><th>24hr Change</th><th>Updated</th></tr></thead><tbody id="tankBody"></tbody></table></section><footer>Viewer nodes are read-only mirrors. Configuration and permissions stay on the server fleet.</footer></main><script>(()=>{const REFRESH_SECONDS=)HTML" STR(WEB_REFRESH_SECONDS)R"HTML(;const els={viewerName:document.getElementById('viewerName'),viewerUid:document.getElementById('viewerUid'),sourceServer:document.getElementById('sourceServer'),sourceUid:document.getElementById('sourceUid'),summaryGenerated:document.getElementById('summaryGenerated'),lastFetch:document.getElementById('lastFetch'),nextFetch:document.getElementById('nextFetch'),refreshHint:document.getElementById('refreshHint'),tankBody:document.getElementById('tankBody')};const state={tanks:[]};function applyTankData(d){els.viewerName.textContent=d.vn||d.viewerName||'Tank Alarm Viewer';els.viewerUid.textContent=d.vi||d.viewerUid||'--';els.sourceServer.textContent=d.sn||d.sourceServerName||'Server';els.sourceUid.textContent=d.si||d.sourceServerUid||'--';els.summaryGenerated.textContent=formatEpoch(d.ge||d.generatedEpoch);els.lastFetch.textContent=formatEpoch(d.lf||d.lastFetchEpoch);els.nextFetch.textContent=formatEpoch(d.nf||d.nextFetchEpoch);els.refreshHint.textContent=describeCadence(d.rs||d.refreshSeconds,d.bh||d.baseHour);state.tanks=d.tanks||[];renderTankRows()}async function fetchTanks(){try{const res=await fetch('/api/tanks');if(!res.ok)throw new Error('HTTP '+res.status);const data=await res.json();applyTankData(data)}catch(err){console.error('Viewer refresh failed',err)}}function renderTankRows(){const tbody=els.tankBody;tbody.innerHTML='';const rows=state.tanks;if(!rows.length){const tr=document.createElement('tr');tr.innerHTML='<td colspan="5">No tank data available</td>';tbody.appendChild(tr);return}const now=Date.now();const staleThresholdMs=93600000;rows.forEach(t=>{const tr=document.createElement('tr');const alarm=t.a!==undefined?t.a:t.alarm;if(alarm)tr.classList.add('alarm');const lastUpdate=t.u||t.lastUpdate;const isStale=lastUpdate&&((now-(lastUpdate*1000))>staleThresholdMs);const staleWarning=isStale?' ⚠️':'';tr.innerHTML=`<td>${escapeHtml(t.s||t.site,'--')}</td><td>${escapeHtml(t.n||t.label||'Tank')}&nbsp;#${escapeHtml((t.k??t.tank??'?'))}</td><td>${formatFeetInches(t.l!==undefined?t.l:t.levelInches)}</td><td>--</td><td>${formatEpoch(lastUpdate)}${staleWarning}</td>`;if(isStale){tr.style.opacity='0.6';tr.title='Data is over 26 hours old'}tbody.appendChild(tr)})}function statusBadge(t){const alarm=t.a!==undefined?t.a:t.alarm;if(!alarm){return'<span class="status-pill ok">Normal</span>'}const label=escapeHtml(t.at||t.alarmType||'Alarm','Alarm');return`<span class="status-pill alarm">${label}</span>`}function formatFeetInches(inches){if(typeof inches!=='number'||!isFinite(inches)||inches<0)return'--';const feet=Math.floor(inches/12);const remainingInches=inches-(feet*12);return`${feet}' ${remainingInches.toFixed(1)}"`}function formatEpoch(epoch){if(!epoch)return'--';const date=new Date(epoch*1000);if(isNaN(date.getTime()))return'--';return date.toLocaleString()}function describeCadence(seconds,baseHour){const hours=seconds?(seconds/3600).toFixed(1).replace(/\.0$/,''):'6';const hourLabel=(typeof baseHour==='number')?baseHour:6;return`${hours}h cadence · starts ${hourLabel}:00`}function escapeHtml(value,fallback=''){if(value===undefined||value===null||value==='')return fallback;const entityMap={'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'};return String(value).replace(/[&<>"']/g,c=>entityMap[c]||c)}fetchTanks();setInterval(()=>fetchTanks(),REFRESH_SECONDS*1000)})();</script></body></html>)HTML";

static void initializeNotecard();
static void initializeEthernet();
static void handleWebRequests();
static bool readHttpRequest(EthernetClient &client, String &method, String &path, String &body, size_t &contentLength, bool &bodyTooLarge);
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
static void checkForFirmwareUpdate();
static void enableDfuMode();

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

#ifdef TANKALARM_WATCHDOG_AVAILABLE
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
#ifdef TANKALARM_WATCHDOG_AVAILABLE
  #if defined(ARDUINO_OPTA) || defined(ARDUINO_ARCH_MBED)
    mbedWatchdog.kick();
  #else
    IWatchdog.reload();
  #endif
#endif

  Ethernet.maintain();  // Renew DHCP lease if needed

  handleWebRequests();
  ensureTimeSync();

  if (gNextSummaryFetchEpoch > 0.0 && currentEpoch() >= gNextSummaryFetchEpoch) {
    fetchViewerSummary();
    scheduleNextSummaryFetch();
  }

  // Check for firmware updates every hour
  unsigned long currentMillis = millis();
  if (!gDfuInProgress && (currentMillis - gLastDfuCheckMillis >= DFU_CHECK_INTERVAL_MS)) {
    gLastDfuCheckMillis = currentMillis;
    checkForFirmwareUpdate();
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
    // Use configurable product UID (allows fleet-specific deployments without recompilation)
    JAddStringToObject(req, "product", gConfig.productUid);
    JAddStringToObject(req, "mode", "continuous");
    // Join the viewer fleet for fleet-scoped DFU, route filtering, and device management
    JAddStringToObject(req, "fleet", "tankalarm-viewer");
    notecard.sendRequest(req);
  }
  
  Serial.print(F("Product UID: "));
  Serial.println(gConfig.productUid);

  req = notecard.newRequest("card.uuid");
  if (req) {
    J *rsp = notecard.requestAndResponse(req);
    if (rsp) {
      const char *uid = JGetString(rsp, "uuid");
      if (uid) {
        strlcpy(gViewerUid, uid, sizeof(gViewerUid));
      }
      notecard.deleteResponse(rsp);
    }
  }

  Serial.print(F("Viewer Notecard UID: "));
  Serial.println(gViewerUid);
}

static void ensureTimeSync() {
  tankalarm_ensureTimeSync(notecard, gLastSyncedEpoch, gLastSyncMillis);
}

static double currentEpoch() {
  return tankalarm_currentEpoch(gLastSyncedEpoch, gLastSyncMillis);
}

static double computeNextAlignedEpoch(double epoch, uint8_t baseHour, uint32_t intervalSeconds) {
  return tankalarm_computeNextAlignedEpoch(epoch, baseHour, intervalSeconds);
}

static void scheduleNextSummaryFetch() {
  double epoch = currentEpoch();
  uint32_t interval = (gSourceRefreshSeconds > 0) ? gSourceRefreshSeconds : SUMMARY_FETCH_INTERVAL_SECONDS;
  uint8_t baseHour = gSourceBaseHour;
  gNextSummaryFetchEpoch = computeNextAlignedEpoch(epoch, baseHour, interval);
}

static void initializeEthernet() {
  Serial.print(F("Initializing Ethernet..."));
  
  // Prepare IP addresses from config
  IPAddress staticIp(gConfig.staticIp[0], gConfig.staticIp[1], gConfig.staticIp[2], gConfig.staticIp[3]);
  IPAddress staticGateway(gConfig.staticGateway[0], gConfig.staticGateway[1], gConfig.staticGateway[2], gConfig.staticGateway[3]);
  IPAddress staticSubnet(gConfig.staticSubnet[0], gConfig.staticSubnet[1], gConfig.staticSubnet[2], gConfig.staticSubnet[3]);
  IPAddress staticDns(gConfig.staticDns[0], gConfig.staticDns[1], gConfig.staticDns[2], gConfig.staticDns[3]);
  
  int status;
  if (gConfig.useStaticIp) {
    Serial.print(F(" (static) "));
    status = Ethernet.begin(gConfig.macAddress, staticIp, staticDns, staticGateway, staticSubnet);
  } else {
    Serial.print(F(" (DHCP) "));
    status = Ethernet.begin(gConfig.macAddress);
  }
  
  if (status == 0) {
    Serial.println(F(" FAILED"));
    if (!gConfig.useStaticIp) {
      Serial.println(F("DHCP failed - check network cable and DHCP server"));
    }
  } else {
    Serial.println(F(" ok"));
    Serial.print(F("IP Address: "));
    Serial.println(Ethernet.localIP());
    Serial.print(F("Gateway: "));
    Serial.println(Ethernet.gatewayIP());
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

static void respondJson(EthernetClient &client, const String &body) {
  client.println(F("HTTP/1.1 200 OK"));
  client.println(F("Content-Type: application/json"));
  client.print(F("Content-Length: "));
  client.println(body.length());
  client.println(F("Cache-Control: no-cache, no-store, must-revalidate"));
  client.println();
  
  // Send in chunks to avoid memory issues with large strings
  const size_t chunkSize = 512;
  size_t remaining = body.length();
  size_t offset = 0;
  
  while (remaining > 0) {
    size_t toSend = (remaining < chunkSize) ? remaining : chunkSize;
    client.write((const uint8_t*)body.c_str() + offset, toSend);
    offset += toSend;
    remaining -= toSend;
  }
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
    case 413: client.println(F("Payload Too Large")); break;
    case 500: client.println(F("Internal Server Error")); break;
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

  const size_t bufSize = 128;
  uint8_t buffer[bufSize];
  size_t remaining = htmlLen;
  const char* ptr = VIEWER_DASHBOARD_HTML;

  while (remaining > 0) {
    size_t chunk = (remaining < bufSize) ? remaining : bufSize;
    for (size_t i = 0; i < chunk; i++) {
        buffer[i] = pgm_read_byte_near(ptr++);
    }
    client.write(buffer, chunk);
    remaining -= chunk;
  }
}

static void sendTankJson(EthernetClient &client) {
  std::unique_ptr<JsonDocument> docPtr(new JsonDocument());
  if (!docPtr) {
    respondStatus(client, 500, "Out of Memory");
    return;
  }
  JsonDocument &doc = *docPtr;

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

  JsonArray arr = doc["tanks"].to<JsonArray>();
  for (uint8_t i = 0; i < gTankRecordCount; ++i) {
    JsonObject obj = arr.add<JsonObject>();
    obj["c"] = gTankRecords[i].clientUid;
    obj["s"] = gTankRecords[i].site;
    obj["n"] = gTankRecords[i].label;
    obj["k"] = gTankRecords[i].tankNumber;
    obj["l"] = gTankRecords[i].levelInches;
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
      std::unique_ptr<JsonDocument> docPtr(new JsonDocument());
      if (docPtr) {
        JsonDocument &doc = *docPtr;
        DeserializationError err = deserializeJson(doc, json);
        NoteFree(json);
        if (!err) {
          handleViewerSummary(doc, epoch);
        } else {
          Serial.print(F("Summary parse failed: "));
          Serial.println(err.f_str());
        }
      } else {
        NoteFree(json);
        Serial.println(F("OOM processing summary"));
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
      rec.levelInches = (item["l"] | item["levelInches"]).as<float>();
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

// ========================== DFU Functions ==========================

/**
 * @brief Check for firmware updates via Blues Notecard DFU
 *
 * Queries the Notecard for available firmware updates. If an update is found,
 * marks it as available in global state. Auto-enables DFU if DFU_AUTO_ENABLE is true.
 */
static void checkForFirmwareUpdate() {
  Serial.println(F("Checking for firmware update..."));

  J *req = notecard.newRequest("dfu.status");
  if (!req) {
    Serial.println(F("DFU status request failed (allocation)"));
    return;
  }

  J *rsp = notecard.requestAndResponse(req);
  if (!rsp) {
    Serial.println(F("DFU status request failed (no response)"));
    return;
  }

  // Check for errors
  if (notecard.responseError(rsp)) {
    const char *err = JGetString(rsp, "err");
    Serial.print(F("DFU status error: "));
    Serial.println(err ? err : "unknown");
    notecard.deleteResponse(rsp);
    return;
  }

  // Check mode field
  const char *mode = JGetString(rsp, "mode");
  const char *body = JGetString(rsp, "body");

  // If mode is already "downloading" or "download-pending", update is in progress
  if (mode && (strcmp(mode, "downloading") == 0 || strcmp(mode, "download-pending") == 0)) {
    Serial.println(F("DFU download in progress..."));
    gDfuInProgress = true;
    notecard.deleteResponse(rsp);
    return;
  }

  // If mode is "ready" and body includes version info, an update is available
  bool updateAvailable = false;
  if (mode && strcmp(mode, "ready") == 0 && body && strlen(body) > 0) {
    updateAvailable = true;
  }

  if (updateAvailable) {
    Serial.print(F("Firmware update available: "));
    Serial.println(body);
    gDfuUpdateAvailable = true;
    strlcpy(gDfuVersion, body, sizeof(gDfuVersion));

#ifdef DFU_AUTO_ENABLE
    Serial.println(F("Auto-enabling DFU..."));
    enableDfuMode();
#else
    Serial.println(F("DFU available but auto-enable is disabled"));
#endif
  } else {
    Serial.println(F("No firmware update available"));
    gDfuUpdateAvailable = false;
    gDfuVersion[0] = '\0';
  }

  notecard.deleteResponse(rsp);
}

/**
 * @brief Enable DFU mode to trigger firmware update
 *
 * Sends dfu.mode request to the Notecard, transitioning it into download mode.
 * The Notecard will download the firmware from Notehub and then reset the host MCU.
 */
static void enableDfuMode() {
  if (!gDfuUpdateAvailable) {
    Serial.println(F("No DFU update available to enable"));
    return;
  }

  Serial.print(F("Enabling DFU mode for version: "));
  Serial.println(gDfuVersion);

  J *req = notecard.newRequest("dfu.mode");
  if (!req) {
    Serial.println(F("DFU mode request failed (allocation)"));
    return;
  }

  JAddBoolToObject(req, "on", true);
  J *rsp = notecard.requestAndResponse(req);

  if (!rsp) {
    Serial.println(F("DFU mode request failed (no response)"));
    return;
  }

  if (notecard.responseError(rsp)) {
    const char *err = JGetString(rsp, "err");
    Serial.print(F("DFU mode error: "));
    Serial.println(err ? err : "unknown");
  } else {
    Serial.println(F("DFU mode enabled. Device will reset after download."));
    gDfuInProgress = true;
  }

  notecard.deleteResponse(rsp);
}
