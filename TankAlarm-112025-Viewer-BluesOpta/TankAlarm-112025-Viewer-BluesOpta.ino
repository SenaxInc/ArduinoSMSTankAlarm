/*
  Tank Alarm Viewer 112025 - Arduino Opta + Blues Notecard
  Version: 1.1.7

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
#include <new>
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

// Optional: Create a "ViewerConfig.h" file in this sketch folder to set
// compile-time defaults (e.g. #define DEFAULT_VIEWER_PRODUCT_UID "com.company.product:project").
// If the file does not exist, the product UID must be set via the viewer's config JSON.
#if __has_include("ViewerConfig.h")
  #include "ViewerConfig.h"
#endif

#ifndef DEFAULT_VIEWER_PRODUCT_UID
#define DEFAULT_VIEWER_PRODUCT_UID ""  // Set via ViewerConfig.h or config JSON
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
  uint8_t sensorIndex;
  uint8_t userNumber;           // Optional user-assigned display number (0 = unset)
  float levelInches;
  bool alarmActive;
  char alarmType[24];
  double lastUpdateEpoch;
  float vinVoltage;  // Blues Notecard VIN voltage
  char objectType[16];       // e.g. "tank", "gas", "rpm"
  char sensorType[16];       // e.g. "ultrasonic", "pressure"
  char measurementUnit[16];  // e.g. "inches", "psi"
  float change24h;           // 24-hour level change
  bool hasChange24h;         // true if change24h was present in data
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

// I2C bus health tracking (required by TankAlarm_I2C.h)
uint32_t gCurrentLoopI2cErrors = 0;    // Not used by Viewer but required by extern
uint32_t gI2cBusRecoveryCount = 0;
static bool gNotecardAvailable = true;
static uint16_t gNotecardFailureCount = 0;
static unsigned long gLastSuccessfulNotecardComm = 0;

static const char VIEWER_DASHBOARD_HTML[] PROGMEM = R"HTML(<!DOCTYPE html><html lang="en"><head><meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1.0"><title>Tank Alarm Viewer</title><style>:root{--bg:#f8fafc;--text:#0f172a;--header-bg:#ffffff;--meta-color:#475569;--card-bg:#ffffff;--table-border:rgba(15,23,42,0.08)}body{margin:0;font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,Helvetica,Arial,sans-serif;background:var(--bg);color:var(--text);transition:background 0.3s,color 0.3s}header{padding:20px 28px;background:var(--header-bg);box-shadow:0 2px 10px rgba(0,0,0,0.15)}header h1{margin:0;font-size:1.7rem}header .meta{margin-top:12px;font-size:0.95rem;color:var(--meta-color);display:flex;gap:16px;flex-wrap:wrap}.title-row{display:flex;justify-content:space-between;gap:16px;flex-wrap:wrap;align-items:flex-start}.header-actions{display:flex;gap:12px;align-items:center}.icon-button{width:40px;height:40px;border:1px solid rgba(148,163,184,0.4);background:var(--card-bg);color:var(--text);font-size:1.1rem;cursor:pointer}main{padding:24px;max-width:1400px;margin:0 auto}.card{background:var(--card-bg);padding:20px;box-shadow:0 25px 60px rgba(15,23,42,0.15);border:1px solid rgba(15,23,42,0.08)}table{width:100%;border-collapse:collapse;margin-top:12px}th,td{text-align:left;padding:10px 12px;border-bottom:1px solid var(--table-border)}th{text-transform:uppercase;letter-spacing:0.05em;font-size:0.75rem;color:var(--meta-color)}tr:last-child td{border-bottom:none}tr.alarm{background:rgba(220,38,38,0.08)}.status-pill{display:inline-flex;align-items:center;gap:6px;padding:4px 12px;font-size:0.85rem}.status-pill.ok{background:rgba(16,185,129,0.15);color:#34d399}.status-pill.alarm{background:rgba(248,113,113,0.2);color:#fca5a5}.timestamp{font-feature-settings:"tnum";color:var(--meta-color);font-size:0.9rem}footer{margin-top:20px;color:var(--meta-color);font-size:0.85rem;text-align:center}</style></head><body><header><div class="title-row"><div><h1 id="viewerName">Tank Alarm Viewer</h1><div class="meta"><span>Viewer UID: <code id="viewerUid">--</code></span><span>Source: <strong id="sourceServer">--</strong> (<code id="sourceUid">--</code>)</span><span>Summary Generated: <span id="summaryGenerated">--</span></span><span>Last Fetch: <span id="lastFetch">--</span></span><span>Next Scheduled Fetch: <span id="nextFetch">--</span></span><span>Server cadence: <span id="refreshHint">6h @ 6 AM</span></span></div></div></div></header><main><section class="card"><div style="display:flex;justify-content:space-between;align-items:baseline;gap:12px;flex-wrap:wrap"><h2 style="margin:0;font-size:1.2rem">Fleet Snapshot</h2><span class="timestamp">Dashboard auto-refresh: )HTML" STR(WEB_REFRESH_MINUTES) R"HTML( min</span></div><table><thead><tr><th>Site</th><th>Tank</th><th>Level (ft/in)</th><th>24hr Change</th><th>Updated</th></tr></thead><tbody id="tankBody"></tbody></table></section><footer>Viewer nodes are read-only mirrors. Configuration and permissions stay on the server fleet.</footer></main><script>(()=>{const REFRESH_SECONDS=)HTML" STR(WEB_REFRESH_SECONDS)R"HTML(;const els={viewerName:document.getElementById('viewerName'),viewerUid:document.getElementById('viewerUid'),sourceServer:document.getElementById('sourceServer'),sourceUid:document.getElementById('sourceUid'),summaryGenerated:document.getElementById('summaryGenerated'),lastFetch:document.getElementById('lastFetch'),nextFetch:document.getElementById('nextFetch'),refreshHint:document.getElementById('refreshHint'),tankBody:document.getElementById('tankBody')};const state={tanks:[]};function applyTankData(d){els.viewerName.textContent=d.vn||'Tank Alarm Viewer';els.viewerUid.textContent=d.vi||'--';els.sourceServer.textContent=d.sn||'Server';els.sourceUid.textContent=d.si||'--';els.summaryGenerated.textContent=formatEpoch(d.ge);els.lastFetch.textContent=formatEpoch(d.lf);els.nextFetch.textContent=formatEpoch(d.nf);els.refreshHint.textContent=describeCadence(d.rs,d.bh);state.tanks=d.tanks||[];renderTankRows()}async function fetchTanks(){try{const res=await fetch('/api/tanks');if(!res.ok)throw new Error('HTTP '+res.status);const data=await res.json();applyTankData(data)}catch(err){console.error('Viewer refresh failed',err)}}function renderTankRows(){const tbody=els.tankBody;tbody.innerHTML='';const rows=state.tanks;if(!rows.length){const tr=document.createElement('tr');tr.innerHTML='<td colspan="5">No tank data available</td>';tbody.appendChild(tr);return}const now=Date.now();const staleThresholdMs=93600000;rows.forEach(t=>{const tr=document.createElement('tr');const alarm=t.a;if(alarm)tr.classList.add('alarm');const lastUpdate=t.u;const isStale=lastUpdate&&((now-(lastUpdate*1000))>staleThresholdMs);const staleWarning=isStale?' ⚠️':'';tr.innerHTML=`<td>${escapeHtml(t.s,'--')}</td><td>${escapeHtml(t.n||'Tank')}${t.un?' #'+t.un:''}</td><td>${formatFeetInches(t.l)}</td><td>${format24hChange(t)}</td><td>${formatEpoch(lastUpdate)}${staleWarning}</td>`;if(isStale){tr.style.opacity='0.6';tr.title='Data is over 26 hours old'}tbody.appendChild(tr)})}function statusBadge(t){const alarm=t.a;if(!alarm){return'<span class="status-pill ok">Normal</span>'}const label=escapeHtml(t.at||'Alarm','Alarm');return`<span class="status-pill alarm">${label}</span>`}function formatFeetInches(inches){if(typeof inches!=='number'||!isFinite(inches)||inches<0)return'--';const feet=Math.floor(inches/12);const remainingInches=inches-(feet*12);return`${feet}' ${remainingInches.toFixed(1)}"`}function format24hChange(t){const d=t.d;if(d===undefined||d===null||typeof d!=='number'||!isFinite(d))return'--';const sign=d>0?'+':'';return`${sign}${d.toFixed(1)}"`}function formatEpoch(epoch){if(!epoch)return'--';const date=new Date(epoch*1000);if(isNaN(date.getTime()))return'--';return date.toLocaleString()}function describeCadence(seconds,baseHour){const hours=seconds?(seconds/3600).toFixed(1).replace(/\.0$/,''):'6';const hourLabel=(typeof baseHour==='number')?baseHour:6;return`${hours}h cadence · starts ${hourLabel}:00`}function escapeHtml(value,fallback=''){if(value===undefined||value===null||value==='')return fallback;const entityMap={'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'};return String(value).replace(/[&<>"']/g,c=>entityMap[c]||c)}fetchTanks();setInterval(()=>fetchTanks(),REFRESH_SECONDS*1000)})();</script></body></html>)HTML";

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
static void deriveMacFromUid();
static void scheduleNextSummaryFetch();
static void fetchViewerSummary();
static void handleViewerSummary(JsonDocument &doc, double epoch);
static void checkForFirmwareUpdate();
static void enableDfuMode();

// ============================================================================
// Diagnostics Helpers
// ============================================================================

static void safeSleep(unsigned long ms) {
  if (ms == 0) {
    return;
  }

#ifdef TANKALARM_WATCHDOG_AVAILABLE
  const unsigned long maxChunk = (WATCHDOG_TIMEOUT_SECONDS * 1000UL) / 2;
#else
  const unsigned long maxChunk = ms;
#endif

  unsigned long remaining = ms;
  while (remaining > 0) {
    unsigned long chunk = (remaining > maxChunk) ? maxChunk : remaining;
    delay(chunk);

#ifdef TANKALARM_WATCHDOG_AVAILABLE
  #if defined(ARDUINO_OPTA) || defined(ARDUINO_ARCH_MBED)
    mbedWatchdog.kick();
  #else
    IWatchdog.reload();
  #endif
#endif

    remaining -= chunk;
  }
}

/**
 * Get current free heap bytes for field diagnostics.
 * Delegates to the shared tankalarm_freeRam() implementation.
 */
static uint32_t freeRam() { return tankalarm_freeRam(); }

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 2000) {
    safeSleep(10);
  }
  Serial.println();
  Serial.print(F("Tank Alarm Viewer 112025 v"));
  Serial.print(F(FIRMWARE_VERSION));
  Serial.print(F(" ("));
  Serial.print(F(FIRMWARE_BUILD_DATE));
  Serial.println(F(")"));

  Wire.begin();
  Wire.setTimeout(I2C_WIRE_TIMEOUT_MS);  // Guard against indefinite blocking on bus hang

  // I2C bus scan: verify Notecard is present
  {
    const uint8_t expectedAddrs[] = { NOTECARD_I2C_ADDRESS };
    const char *expectedNames[] = { "Notecard" };
    tankalarm_scanI2CBus(expectedAddrs, expectedNames, 1);
  }

  initializeNotecard();
  ensureTimeSync();
  deriveMacFromUid();
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

  tankalarm_printHeapStats();

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

  // ---- Notecard I2C health check (with exponential backoff) ----
  {
    static unsigned long lastNcHealthCheck = 0;
    static unsigned long ncHealthInterval = NOTECARD_HEALTH_CHECK_BASE_INTERVAL_MS;
    unsigned long now = millis();
    if (!gNotecardAvailable && (now - lastNcHealthCheck > ncHealthInterval)) {
      lastNcHealthCheck = now;
      J *hcReq = notecard.newRequest("card.version");
      if (hcReq) {
        J *hcRsp = notecard.requestAndResponse(hcReq);
        if (hcRsp) {
          notecard.deleteResponse(hcRsp);
          gNotecardAvailable = true;
          gNotecardFailureCount = 0;
          gLastSuccessfulNotecardComm = millis();
          tankalarm_ensureNotecardBinding(notecard);
          ncHealthInterval = NOTECARD_HEALTH_CHECK_BASE_INTERVAL_MS;
          Serial.println(F("Notecard recovered - online (backoff reset)"));
        } else {
          gNotecardFailureCount++;
          if (gNotecardFailureCount >= I2C_NOTECARD_RECOVERY_THRESHOLD) {
            tankalarm_recoverI2CBus(gDfuInProgress, [](){
              #if defined(ARDUINO_OPTA) || defined(ARDUINO_ARCH_MBED)
                mbedWatchdog.kick();
              #endif
            });
            Serial.print(F("I2C recovery event (trigger=HEALTH_CHECK, count="));
            Serial.print(gI2cBusRecoveryCount);
            Serial.println(F(")"));
            tankalarm_ensureNotecardBinding(notecard);
            gNotecardFailureCount = 0;
          }
          // Exponential backoff up to max
          if (ncHealthInterval < NOTECARD_HEALTH_CHECK_MAX_INTERVAL_MS) {
            ncHealthInterval *= 2;
            if (ncHealthInterval > NOTECARD_HEALTH_CHECK_MAX_INTERVAL_MS) {
              ncHealthInterval = NOTECARD_HEALTH_CHECK_MAX_INTERVAL_MS;
            }
          }
          Serial.print(F("Notecard health check backoff: next in "));
          Serial.print(ncHealthInterval / 60000UL);
          Serial.println(F(" min"));
        }
      }
    }
  }

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
  Serial.println(F("Notecard initialized"));

  J *req = notecard.newRequest("hub.set");
  if (req) {
    // Use configurable product UID (allows fleet-specific deployments without recompilation)
    JAddStringToObject(req, "product", gConfig.productUid);
    JAddStringToObject(req, "mode", "continuous");
    // Join the viewer fleet for fleet-scoped DFU, route filtering, and device management
    JAddStringToObject(req, "fleet", "tankalarm-viewer");
    J *hubRsp = notecard.requestAndResponse(req);
    if (hubRsp) {
      const char *hubErr = JGetString(hubRsp, "err");
      if (hubErr && hubErr[0] != '\0') {
        Serial.print(F("WARNING: hub.set failed: "));
        Serial.println(hubErr);
      }
      notecard.deleteResponse(hubRsp);
    } else {
      Serial.println(F("WARNING: hub.set returned no response"));
    }
  }
  
  Serial.print(F("Product UID: "));
  Serial.println(gConfig.productUid);

  // Retrieve the Notecard's unique device identifier (e.g., "dev:860322068012345").
  // hub.get returns the device serial in the "device" field.
  // card.uuid does NOT exist in the Notecard API.
  req = notecard.newRequest("hub.get");
  if (req) {
    J *rsp = notecard.requestAndResponse(req);
    if (rsp) {
      const char *uid = JGetString(rsp, "device");
      if (uid && uid[0] != '\0') {
        strlcpy(gViewerUid, uid, sizeof(gViewerUid));
      }
      notecard.deleteResponse(rsp);
    }
  }

  Serial.print(F("Viewer Device UID: "));
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

/**
 * Derive a unique MAC address from the Notecard device UID.
 * Uses a simple hash of the UID string to populate the last 4 bytes.
 * Byte 0 is 0x02 (locally administered, unicast).
 * Byte 1 is 0x00 (vendor padding).
 * If UID is empty, falls back to the compile-time default.
 */
static void deriveMacFromUid() {
  if (gViewerUid[0] == '\0') return;  // No UID available — keep default

  // DJB2 hash of UID string
  uint32_t hash = 5381;
  for (const char *p = gViewerUid; *p; p++) {
    hash = ((hash << 5) + hash) + (uint8_t)*p;
  }

  gConfig.macAddress[0] = 0x02;  // Locally administered, unicast
  gConfig.macAddress[1] = 0x00;
  gConfig.macAddress[2] = (uint8_t)(hash >> 24);
  gConfig.macAddress[3] = (uint8_t)(hash >> 16);
  gConfig.macAddress[4] = (uint8_t)(hash >> 8);
  gConfig.macAddress[5] = (uint8_t)(hash);

  Serial.print(F("MAC derived from UID: "));
  for (uint8_t i = 0; i < 6; i++) {
    if (i > 0) Serial.print(':');
    if (gConfig.macAddress[i] < 0x10) Serial.print('0');
    Serial.print(gConfig.macAddress[i], HEX);
  }
  Serial.println();
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
      Serial.println(F("DHCP failed - retrying..."));
    }
    // Retry up to 3 times with increasing delays
    for (uint8_t attempt = 1; attempt <= 3 && status == 0; attempt++) {
      unsigned long retryDelay = (unsigned long)attempt * 5000UL;
      Serial.print(F("Ethernet retry "));
      Serial.print(attempt);
      Serial.print(F("/3 in "));
      Serial.print(retryDelay / 1000);
      Serial.println(F("s..."));
      delay(retryDelay);
      if (gConfig.useStaticIp) {
        status = Ethernet.begin(gConfig.macAddress, staticIp, staticDns, staticGateway, staticSubnet);
      } else {
        status = Ethernet.begin(gConfig.macAddress);
      }
    }
    if (status == 0) {
      Serial.println(F("Ethernet initialization failed after retries"));
    }
  }

  if (status != 0) {
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

  safeSleep(1);
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
      safeSleep(1);
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
    unsigned long bodyStart = millis();
    while (readBytes < contentLength && client.connected() && millis() - bodyStart < 5000UL) {
      while (client.available() && readBytes < contentLength) {
        char c = client.read();
        body += c;
        readBytes++;
      }
      if (readBytes >= MAX_HTTP_BODY_BYTES) {
        bodyTooLarge = true;
        break;
      }
      if (readBytes < contentLength) {
        safeSleep(1);  // Yield CPU + kick watchdog while waiting for more data
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
  std::unique_ptr<JsonDocument> docPtr(new (std::nothrow) JsonDocument());
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
    obj["k"] = gTankRecords[i].sensorIndex;
    if (gTankRecords[i].userNumber > 0) {
      obj["un"] = gTankRecords[i].userNumber;
    }
    obj["l"] = gTankRecords[i].levelInches;
    obj["a"] = gTankRecords[i].alarmActive;
    obj["at"] = gTankRecords[i].alarmType;
    obj["u"] = gTankRecords[i].lastUpdateEpoch;
    if (gTankRecords[i].vinVoltage > 0.0f) {
      obj["v"] = gTankRecords[i].vinVoltage;
    }
    if (gTankRecords[i].objectType[0] != '\0') {
      obj["ot"] = gTankRecords[i].objectType;
    }
    if (gTankRecords[i].sensorType[0] != '\0') {
      obj["st"] = gTankRecords[i].sensorType;
    }
    if (gTankRecords[i].measurementUnit[0] != '\0') {
      obj["mu"] = gTankRecords[i].measurementUnit;
    }
    if (gTankRecords[i].hasChange24h) {
      obj["d"] = gTankRecords[i].change24h;
    }
  }

  String body;
  serializeJson(doc, body);
  respondJson(client, body);
}

static void fetchViewerSummary() {
  uint8_t notesProcessed = 0;
  while (true) {
    // Kick watchdog between iterations — each note.get is a blocking I2C transaction
    #ifdef TANKALARM_WATCHDOG_AVAILABLE
      #if defined(ARDUINO_OPTA) || defined(ARDUINO_ARCH_MBED)
        mbedWatchdog.kick();
      #endif
    #endif
    // Safety cap: don't drain more than 20 notes per call to stay responsive
    if (++notesProcessed > 20) {
      Serial.println(F("fetchViewerSummary: cap reached, will drain remaining next cycle"));
      break;
    }
    J *req = notecard.newRequest("note.get");
    if (!req) {
      gNotecardFailureCount++;
      if (gNotecardFailureCount >= NOTECARD_FAILURE_THRESHOLD && gNotecardAvailable) {
        gNotecardAvailable = false;
        Serial.println(F("Notecard unavailable - I2C health check will attempt recovery"));
      }
      return;
    }
    JAddStringToObject(req, "file", VIEWER_SUMMARY_FILE);
    JAddBoolToObject(req, "delete", true);
    J *rsp = notecard.requestAndResponse(req);
    if (!rsp) {
      gNotecardFailureCount++;
      if (gNotecardFailureCount >= NOTECARD_FAILURE_THRESHOLD && gNotecardAvailable) {
        gNotecardAvailable = false;
        Serial.println(F("Notecard unavailable - I2C health check will attempt recovery"));
      }
      return;
    }

    // Notecard responded — reset failure tracking
    if (!gNotecardAvailable) {
      gNotecardAvailable = true;
      gNotecardFailureCount = 0;
      Serial.println(F("Notecard recovered"));
    }
    gNotecardFailureCount = 0;
    gLastSuccessfulNotecardComm = millis();

    J *body = JGetObject(rsp, "body");
    if (!body) {
      notecard.deleteResponse(rsp);
      break;
    }

    char *json = JConvertToJSONString(body);
    double epoch = JGetNumber(rsp, "time");
    if (json) {
      std::unique_ptr<JsonDocument> docPtr(new (std::nothrow) JsonDocument());
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
      strlcpy(rec.clientUid, item["c"] | "", sizeof(rec.clientUid));
      strlcpy(rec.site, item["s"] | "", sizeof(rec.site));
      strlcpy(rec.label, item["n"] | "Tank", sizeof(rec.label));
      rec.sensorIndex = item["k"].is<uint8_t>() ? item["k"].as<uint8_t>() : gTankRecordCount;
      rec.userNumber = item["un"].is<uint8_t>() ? item["un"].as<uint8_t>() : 0;
      rec.levelInches = item["l"].as<float>();
      rec.alarmActive = item["a"].as<bool>();
      strlcpy(rec.alarmType, item["at"] | (rec.alarmActive ? "alarm" : "clear"), sizeof(rec.alarmType));
      rec.lastUpdateEpoch = item["u"].as<double>();
      rec.vinVoltage = item["v"].as<float>();
      strlcpy(rec.objectType, item["ot"] | "", sizeof(rec.objectType));
      strlcpy(rec.sensorType, item["st"] | "", sizeof(rec.sensorType));
      strlcpy(rec.measurementUnit, item["mu"] | "", sizeof(rec.measurementUnit));
      if (!item["d"].isNull()) {
        rec.change24h = item["d"].as<float>();
        rec.hasChange24h = true;
      }
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

  J *req = notecard.newRequest("dfu.status");
  if (!req) {
    Serial.println(F("DFU request failed (allocation)"));
    return;
  }

  JAddBoolToObject(req, "on", true);
  J *rsp = notecard.requestAndResponse(req);

  if (!rsp) {
    Serial.println(F("DFU request failed (no response)"));
    return;
  }

  if (notecard.responseError(rsp)) {
    const char *err = JGetString(rsp, "err");
    Serial.print(F("DFU enable error: "));
    Serial.println(err ? err : "unknown");
  } else {
    Serial.println(F("DFU enabled. Device will reset after download."));
    gDfuInProgress = true;
  }

  notecard.deleteResponse(rsp);
}
