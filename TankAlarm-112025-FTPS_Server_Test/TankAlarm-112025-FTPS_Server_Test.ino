// TankAlarm-112025-FTPS_Server_Test
// SPDX-License-Identifier: CC0-1.0
//
// FTPS library integration test for Arduino Opta, designed to be
// interchangeable with TankAlarm-112025-Server-BluesOpta via DFU.
//
// To switch firmware:
//   1. Upload the .bin to Notehub (Host Firmware > Upload)
//   2. From the current firmware's web UI, go to Server Settings > Check for Updates
//   3. Click "Install Update" and confirm with PIN
//   4. Device will flash, reboot, and come up running this sketch
//   5. To switch back, repeat with the regular server .bin
//
// This sketch preserves:
//   - Same Ethernet IP / MAC (reads from hardware + saved config)
//   - Same Notecard product UID / fleet (reads from saved config)
//   - DFU system (can update back to the regular server sketch)
//   - Web UI on port 80 with serial log, DFU controls, FTPS test controls
//   - LittleFS config (reads but does not modify server_config.json)

#include <Arduino.h>

#if defined(ARDUINO_OPTA) || defined(ARDUINO_PORTENTA_H7_M7)
  #include <PortentaEthernet.h>
  #include <Ethernet.h>
#else
  #error "This sketch is designed for Arduino Opta only"
#endif

#include <Notecard.h>
#include <Wire.h>
#include <ArduinoJson.h>
#include <FtpsClient.h>

// Pull in TankAlarm common headers for DFU, platform, and config compatibility
#include <TankAlarm_Common.h>
#include <TankAlarm_Platform.h>
#include <TankAlarm_DFU.h>

// ============================================================================
// Build Info
// ============================================================================
#define FTPS_TEST_VERSION "0.0.2"
#define FTPS_TEST_SKETCH_NAME "FTPS_Server_Test"

// ============================================================================
// FTPS TEST CONFIGURATION
// ============================================================================

// IP address of the PC running ftps_server.py (same LAN as Opta).
// This can also be updated via the web UI without reflashing.
static char gFtpsHost[64] = "192.168.7.151";
static uint16_t gFtpsPort = 21;
static char gFtpsUser[32] = "optauser";
static char gFtpsPass[32] = "optapass";

// TLS server name (CN in the self-signed cert from gen_cert.py)
static char gFtpsTlsServerName[64] = "ftps-test-server";

// Trust mode: 0 = Fingerprint, 1 = ImportedCert
static FtpsTrustMode gFtpsTrustMode = FtpsTrustMode::Fingerprint;

// SHA-256 fingerprint of the pyftpdlib server cert (from gen_cert.py output).
// Update this if you regenerate the certificate.
static char gFtpsFingerprint[65] =
    "9C53B46CC12DF51F97DE4B886B4B249C9EA49912DE28CE034BFEFFFD8279CCCF";

// Remote paths
static const char *REMOTE_PARENT_DIR = "/ftps_test";
static const char *REMOTE_NESTED_DIR = "/ftps_test/tankalarm";
static const char *REMOTE_TEST_FILE  = "/ftps_test/tankalarm/opta_ftps_test.txt";

// ============================================================================
// Notecard
// ============================================================================
static Notecard notecard;
static char gServerUid[48] = {0};

// ============================================================================
// Ethernet
// ============================================================================
static byte gMacAddress[6] = {0};
static EthernetServer gWebServer(ETHERNET_PORT);  // Port 80

// Static IP defaults (match the regular server sketch)
static IPAddress gStaticIp(192, 168, 7, 117);
static IPAddress gStaticGateway(192, 168, 7, 1);
static IPAddress gStaticSubnet(255, 255, 255, 0);
static IPAddress gStaticDns(8, 8, 8, 8);
static bool gUseStaticIp = false;

// ============================================================================
// DFU State
// ============================================================================
static unsigned long gLastDfuCheckMillis = 0;
static bool gDfuUpdateAvailable = false;
static char gDfuVersion[32] = {0};
static uint32_t gDfuFirmwareLength = 0;
static char gDfuMode[16] = "idle";
static bool gDfuInProgress = false;
static char gDfuError[128] = {0};

#define DFU_CHECK_INTERVAL_MS (30UL * 60UL * 1000UL)  // 30 minutes

// ============================================================================
// Serial Log Ring Buffer (mirrors TankAlarm server pattern)
// ============================================================================
#define LOG_BUFFER_SIZE 60
#define LOG_MSG_LEN 160

struct LogEntry {
  unsigned long ms;
  char message[LOG_MSG_LEN];
  char level[8];      // info, warn, error, pass, fail
};

static LogEntry gLogBuffer[LOG_BUFFER_SIZE];
static uint8_t gLogWriteIndex = 0;
static uint8_t gLogCount = 0;

static void addLog(const char *message, const char *level = "info") {
  LogEntry &e = gLogBuffer[gLogWriteIndex];
  e.ms = millis();
  strlcpy(e.message, message, LOG_MSG_LEN);
  strlcpy(e.level, level ? level : "info", sizeof(e.level));
  gLogWriteIndex = (gLogWriteIndex + 1) % LOG_BUFFER_SIZE;
  if (gLogCount < LOG_BUFFER_SIZE) gLogCount++;

  // Also print to Serial
  Serial.print("[");
  Serial.print(level ? level : "INFO");
  Serial.print("] ");
  Serial.println(message);
}

static void dfuKickWatchdog() {
#ifdef TANKALARM_WATCHDOG_AVAILABLE
  #if defined(ARDUINO_OPTA) || defined(ARDUINO_ARCH_MBED)
    mbed::Watchdog::get_instance().kick();
  #endif
#endif
}

// ============================================================================
// FTPS Test State
// ============================================================================
static int gTestsPassed = 0;
static int gTestsFailed = 0;
static bool gTestRunning = false;
static bool gTestComplete = false;
static char gTestSummary[256] = "No test run yet";

// ============================================================================
// Config loading from LittleFS (read-only — we don't modify the server config)
// ============================================================================

static char gProductUid[64] = {0};
static char gServerFleet[32] = "tankalarm-server";

static void loadConfigFromFilesystem() {
#ifdef TANKALARM_POSIX_FILE_IO_AVAILABLE
  FILE *f = fopen("/fs/server_config.json", "r");
  if (!f) {
    addLog("No server_config.json found — using defaults", "warn");
    return;
  }

  char buf[2048];
  size_t len = fread(buf, 1, sizeof(buf) - 1, f);
  fclose(f);
  buf[len] = '\0';

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, buf, len);
  if (err) {
    addLog("Failed to parse server_config.json", "warn");
    return;
  }

  // Read Notecard config
  if (doc["productUid"].is<const char *>()) {
    strlcpy(gProductUid, doc["productUid"].as<const char *>(), sizeof(gProductUid));
  }
  if (doc["serverFleet"].is<const char *>()) {
    strlcpy(gServerFleet, doc["serverFleet"].as<const char *>(), sizeof(gServerFleet));
  }

  // Read IP config
  if (doc["useStaticIp"].is<bool>()) {
    gUseStaticIp = doc["useStaticIp"].as<bool>();
  }

  // Read FTP settings as defaults for FTPS test (if present)
  if (doc["ftpHost"].is<const char *>() && strlen(doc["ftpHost"].as<const char *>()) > 0) {
    strlcpy(gFtpsHost, doc["ftpHost"].as<const char *>(), sizeof(gFtpsHost));
  }
  if (doc["ftpPort"].is<uint16_t>()) {
    gFtpsPort = doc["ftpPort"].as<uint16_t>();
  }
  if (doc["ftpUser"].is<const char *>() && strlen(doc["ftpUser"].as<const char *>()) > 0) {
    strlcpy(gFtpsUser, doc["ftpUser"].as<const char *>(), sizeof(gFtpsUser));
  }
  if (doc["ftpPass"].is<const char *>() && strlen(doc["ftpPass"].as<const char *>()) > 0) {
    strlcpy(gFtpsPass, doc["ftpPass"].as<const char *>(), sizeof(gFtpsPass));
  }

  addLog("Loaded config from server_config.json");
#else
  addLog("POSIX FS not available — using default config", "warn");
#endif
}

// ============================================================================
// Notecard initialization (match server sketch pattern)
// ============================================================================

static void initializeNotecard() {
  notecard.begin(NOTECARD_I2C_ADDRESS);
  addLog("Notecard initialized");

  // Set product UID and fleet (same as server sketch)
  J *req = notecard.newRequest("hub.set");
  if (req) {
    if (gProductUid[0] != '\0') {
      JAddStringToObject(req, "product", gProductUid);
      JAddStringToObject(req, "mode", "continuous");
      const char *fleet = (gServerFleet[0] != '\0') ? gServerFleet : "tankalarm-server";
      JAddStringToObject(req, "fleet", fleet);
      J *rsp = notecard.requestAndResponse(req);
      if (rsp) {
        const char *hubErr = JGetString(rsp, "err");
        if (hubErr && hubErr[0] != '\0') {
          addLog("hub.set failed", "warn");
        } else {
          char msg[96];
          snprintf(msg, sizeof(msg), "Product UID: %s", gProductUid);
          addLog(msg);
        }
        notecard.deleteResponse(rsp);
      }
    } else {
      notecard.deleteResponse(notecard.requestAndResponse(req));
      addLog("Product UID not configured — set in server config first", "warn");
    }
  }

  // Get device UID
  req = notecard.newRequest("hub.get");
  if (req) {
    J *rsp = notecard.requestAndResponse(req);
    if (rsp) {
      const char *uid = JGetString(rsp, "device");
      if (uid && uid[0] != '\0') {
        strlcpy(gServerUid, uid, sizeof(gServerUid));
      }
      notecard.deleteResponse(rsp);
    }
  }

  if (gServerUid[0] == '\0') {
    req = notecard.newRequest("card.version");
    if (req) {
      J *rsp = notecard.requestAndResponse(req);
      if (rsp) {
        const char *uid = JGetString(rsp, "device");
        if (uid && uid[0] != '\0') {
          strlcpy(gServerUid, uid, sizeof(gServerUid));
        }
        notecard.deleteResponse(rsp);
      }
    }
  }

  char uidMsg[80];
  snprintf(uidMsg, sizeof(uidMsg), "Device UID: %s",
           gServerUid[0] != '\0' ? gServerUid : "(not available)");
  addLog(uidMsg);

  // Enable IAP DFU for chunk-based firmware reading from Notehub.
  tankalarm_enableIapDfu(notecard);
  addLog("IAP DFU enabled - firmware downloads from Notehub accepted");
}

// ============================================================================
// DFU Check (mirrors server sketch)
// ============================================================================

static void checkForFirmwareUpdate() {
  J *req = notecard.newRequest("dfu.status");
  if (!req) return;
  JAddStringToObject(req, "name", "user");

  J *rsp = notecard.requestAndResponse(req);
  if (!rsp) return;

  const char *mode = JGetString(rsp, "mode");
  const char *version = JGetString(rsp, "version");
  const char *err = JGetString(rsp, "err");
  J *body = JGetObject(rsp, "body");
  uint32_t firmwareLength = 0;
  if (body) {
    firmwareLength = (uint32_t)JGetNumber(body, "length");
  }

  if (mode && mode[0] != '\0') {
    strlcpy(gDfuMode, mode, sizeof(gDfuMode));
  }
  if (version && version[0] != '\0') {
    strlcpy(gDfuVersion, version, sizeof(gDfuVersion));
  }
  if (err && err[0] != '\0') {
    strlcpy(gDfuError, err, sizeof(gDfuError));
  } else {
    gDfuError[0] = '\0';
  }

  gDfuUpdateAvailable = (strcmp(gDfuMode, "ready") == 0);
  gDfuFirmwareLength = gDfuUpdateAvailable ? firmwareLength : 0;

  if (!gDfuUpdateAvailable) {
    gDfuVersion[0] = '\0';
  }

  if (gDfuUpdateAvailable) {
    char msg[96];
    snprintf(msg,
             sizeof(msg),
             "Firmware update available: v%s (%lu bytes)",
             gDfuVersion,
             (unsigned long)gDfuFirmwareLength);
    addLog(msg, "info");
  }

  notecard.deleteResponse(rsp);
  gLastDfuCheckMillis = millis();
}

// ============================================================================
// Ethernet initialization (mirrors server sketch for same IP)
// ============================================================================

static void initializeEthernet() {
  if (Ethernet.hardwareStatus() == EthernetNoHardware) {
    addLog("No Ethernet hardware detected!", "error");
    return;
  }

  if (gMacAddress[0] == 0 && gMacAddress[1] == 0 && gMacAddress[2] == 0 &&
      gMacAddress[3] == 0 && gMacAddress[4] == 0 && gMacAddress[5] == 0) {
    Ethernet.MACAddress(gMacAddress);
  }

  int status;
  if (gUseStaticIp) {
    status = Ethernet.begin(gMacAddress, gStaticIp, gStaticDns, gStaticGateway, gStaticSubnet);
  } else {
    status = Ethernet.begin(gMacAddress);
  }

  if (status == 0) {
    addLog(gUseStaticIp ? "Static IP configuration failed" : "DHCP failed", "error");
    return;
  }

  char ipMsg[80];
  snprintf(ipMsg, sizeof(ipMsg), "IP: %d.%d.%d.%d",
           Ethernet.localIP()[0], Ethernet.localIP()[1],
           Ethernet.localIP()[2], Ethernet.localIP()[3]);
  addLog(ipMsg);
}

// ============================================================================
// FTPS Test Runner
// ============================================================================

static const char kUploadPayload[] =
    "{\r\n"
    "  \"_type\": \"ftps_library_test\",\r\n"
    "  \"firmware_version\": \"" FTPS_TEST_VERSION "\",\r\n"
    "  \"device\": \"Arduino Opta\",\r\n"
    "  \"library\": \"ArduinoOPTA-FTPS\",\r\n"
    "  \"message\": \"If you can read this file, FTPS upload succeeded.\",\r\n"
    "  \"sensor_sample\": {\r\n"
    "    \"tank_id\": \"TANK-001\",\r\n"
    "    \"level_inches\": 42.5,\r\n"
    "    \"alarm_active\": false,\r\n"
    "    \"battery_v\": 12.8\r\n"
    "  }\r\n"
    "}\r\n";

static void runFtpsTest() {
  gTestRunning = true;
  gTestComplete = false;
  gTestsPassed = 0;
  gTestsFailed = 0;

  addLog("=== FTPS TEST STARTING ===", "info");

  char msg[160];
  snprintf(msg, sizeof(msg), "Target: %s:%u  User: %s  Trust: %s",
           gFtpsHost, gFtpsPort, gFtpsUser,
           gFtpsTrustMode == FtpsTrustMode::Fingerprint ? "Fingerprint" : "ImportedCert");
  addLog(msg);

  FtpsClient ftps;
  char error[192] = {};

  // begin()
  addLog("FtpsClient.begin()...");
  if (!ftps.begin(Ethernet.getNetwork(), error, sizeof(error))) {
    snprintf(msg, sizeof(msg), "begin() FAILED (err=%d): %s",
             static_cast<int>(ftps.lastError()), error);
    addLog(msg, "fail");
    gTestsFailed++;
    goto done;
  }
  addLog("begin() OK", "pass");
  gTestsPassed++;

  // connect()
  {
    FtpsServerConfig config;
    config.host = gFtpsHost;
    config.port = gFtpsPort;
    config.user = gFtpsUser;
    config.password = gFtpsPass;
    config.tlsServerName = gFtpsTlsServerName[0] != '\0' ? gFtpsTlsServerName : nullptr;
    config.trustMode = gFtpsTrustMode;
    config.fingerprint = gFtpsFingerprint;
    config.rootCaPem = nullptr;
    config.validateServerCert = true;

    addLog("FtpsClient.connect()...");
    if (!ftps.connect(config, error, sizeof(error))) {
      snprintf(msg, sizeof(msg), "connect() FAILED (err=%d): %s",
               static_cast<int>(ftps.lastError()), error);
      addLog(msg, "fail");
      gTestsFailed++;
      goto done;
    }
    addLog("connect() OK", "pass");
    gTestsPassed++;
  }

  // mkd() parent
  addLog("mkd() parent directory...");
  if (!ftps.mkd(REMOTE_PARENT_DIR, error, sizeof(error))) {
    snprintf(msg, sizeof(msg), "mkd() parent FAILED (err=%d): %s",
             static_cast<int>(ftps.lastError()), error);
    addLog(msg, "fail");
    gTestsFailed++;
    ftps.quit();
    goto done;
  }
  snprintf(msg, sizeof(msg), "mkd() parent OK: %s", REMOTE_PARENT_DIR);
  addLog(msg, "pass");
  gTestsPassed++;

  // mkd() nested
  addLog("mkd() nested directory...");
  if (!ftps.mkd(REMOTE_NESTED_DIR, error, sizeof(error))) {
    snprintf(msg, sizeof(msg), "mkd() nested FAILED (err=%d): %s",
             static_cast<int>(ftps.lastError()), error);
    addLog(msg, "fail");
    gTestsFailed++;
    ftps.quit();
    goto done;
  }
  snprintf(msg, sizeof(msg), "mkd() nested OK: %s", REMOTE_NESTED_DIR);
  addLog(msg, "pass");
  gTestsPassed++;

  // store()
  {
    size_t payloadLen = strlen(kUploadPayload);
    snprintf(msg, sizeof(msg), "store() uploading %u bytes...", (unsigned)payloadLen);
    addLog(msg);
    if (!ftps.store(REMOTE_TEST_FILE,
                    reinterpret_cast<const uint8_t *>(kUploadPayload),
                    payloadLen, error, sizeof(error))) {
      snprintf(msg, sizeof(msg), "store() FAILED (err=%d): %s",
               static_cast<int>(ftps.lastError()), error);
      addLog(msg, "fail");
      gTestsFailed++;
      ftps.quit();
      goto done;
    }
    snprintf(msg, sizeof(msg), "store() OK: %s (%u bytes)", REMOTE_TEST_FILE, (unsigned)payloadLen);
    addLog(msg, "pass");
    gTestsPassed++;
  }

  // size()
  {
    size_t remoteBytes = 0;
    addLog("size() querying remote file...");
    if (!ftps.size(REMOTE_TEST_FILE, remoteBytes, error, sizeof(error))) {
      snprintf(msg, sizeof(msg), "size() FAILED (err=%d): %s",
               static_cast<int>(ftps.lastError()), error);
      addLog(msg, "fail");
      gTestsFailed++;
      ftps.quit();
      goto done;
    }
    snprintf(msg, sizeof(msg), "size() OK: %u bytes", (unsigned)remoteBytes);
    addLog(msg, "pass");
    gTestsPassed++;

    size_t expectedLen = strlen(kUploadPayload);
    if (remoteBytes == expectedLen) {
      addLog("Size matches upload payload", "pass");
      gTestsPassed++;
    } else {
      snprintf(msg, sizeof(msg), "Size mismatch: expected %u, got %u",
               (unsigned)expectedLen, (unsigned)remoteBytes);
      addLog(msg, "fail");
      gTestsFailed++;
    }
  }

  // retrieve()
  {
    uint8_t dlBuffer[1024] = {};
    size_t bytesRead = 0;
    addLog("retrieve() downloading...");
    if (!ftps.retrieve(REMOTE_TEST_FILE, dlBuffer, sizeof(dlBuffer),
                       bytesRead, error, sizeof(error))) {
      snprintf(msg, sizeof(msg), "retrieve() FAILED (err=%d): %s",
               static_cast<int>(ftps.lastError()), error);
      addLog(msg, "fail");
      gTestsFailed++;
      ftps.quit();
      goto done;
    }
    snprintf(msg, sizeof(msg), "retrieve() OK: %u bytes", (unsigned)bytesRead);
    addLog(msg, "pass");
    gTestsPassed++;

    size_t expectedLen = strlen(kUploadPayload);
    if (bytesRead == expectedLen &&
        memcmp(dlBuffer, kUploadPayload, expectedLen) == 0) {
      addLog("Content verification: matches upload", "pass");
      gTestsPassed++;
    } else {
      addLog("Content verification: MISMATCH", "fail");
      gTestsFailed++;
    }
  }

  // quit()
  addLog("FtpsClient.quit()...");
  ftps.quit();
  if (ftps.lastError() == FtpsError::QuitFailed) {
    addLog("quit() warned: no 221 reply", "warn");
  } else {
    addLog("quit() OK", "pass");
    gTestsPassed++;
  }

done:
  gTestRunning = false;
  gTestComplete = true;

  snprintf(gTestSummary, sizeof(gTestSummary), "%d passed, %d failed",
           gTestsPassed, gTestsFailed);

  snprintf(msg, sizeof(msg), "=== TEST COMPLETE: %s ===",
           gTestsFailed == 0 ? "ALL PASSED" : "FAILURES DETECTED");
  addLog(msg, gTestsFailed == 0 ? "pass" : "fail");
}

// ============================================================================
// Web Server — lightweight HTTP server on port 80
// ============================================================================

// Serve the main web page (embedded HTML)
static void serveMainPage(EthernetClient &client) {
  client.println(F("HTTP/1.1 200 OK"));
  client.println(F("Content-Type: text/html"));
  client.println(F("Connection: close"));
  client.println();
  client.print(F(R"HTML(<!DOCTYPE html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>FTPS Library Test</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:system-ui,-apple-system,sans-serif;background:#1a1a2e;color:#e0e0e0;padding:16px}
h1{color:#00d4ff;margin-bottom:4px;font-size:1.4em}
h2{color:#7fdbca;margin:16px 0 8px;font-size:1.1em;border-bottom:1px solid #333;padding-bottom:4px}
.subtitle{color:#888;font-size:0.85em;margin-bottom:16px}
.card{background:#16213e;border:1px solid #333;border-radius:8px;padding:16px;margin-bottom:16px}
.row{display:flex;gap:8px;align-items:center;margin-bottom:8px;flex-wrap:wrap}
label{min-width:100px;font-size:0.85em;color:#aaa}
input,select{background:#0f3460;border:1px solid #444;color:#e0e0e0;padding:4px 8px;border-radius:4px;font-size:0.85em;flex:1;min-width:120px}
input:focus{border-color:#00d4ff;outline:none}
button{background:#00d4ff;color:#000;border:none;padding:8px 16px;border-radius:4px;cursor:pointer;font-weight:bold;font-size:0.85em}
button:hover{background:#00b4e0}
button.danger{background:#e94560;color:#fff}
button.danger:hover{background:#c73e54}
button:disabled{opacity:0.5;cursor:not-allowed}
.log-box{background:#0a0a1a;border:1px solid #333;border-radius:4px;padding:8px;
  max-height:400px;overflow-y:auto;font-family:'Courier New',monospace;font-size:0.8em;line-height:1.6}
.log-pass{color:#7fdbca}.log-fail{color:#e94560}.log-warn{color:#f0a500}.log-info{color:#aaa}
.log-error{color:#ff6b6b}
.summary{font-size:1.1em;padding:8px;border-radius:4px;text-align:center;margin:8px 0}
.summary.pass{background:#1a4a3a;color:#7fdbca;border:1px solid #7fdbca}
.summary.fail{background:#4a1a2a;color:#e94560;border:1px solid #e94560}
.summary.idle{background:#1a2a4a;color:#aaa;border:1px solid #444}
.info-grid{display:grid;grid-template-columns:auto 1fr;gap:4px 12px;font-size:0.85em}
.info-grid .label{color:#888}.info-grid .value{color:#e0e0e0}
.dfu-available{color:#00d4ff;font-weight:bold}
</style>
</head><body>
<h1>ArduinoOPTA-FTPS Library Test</h1>
<div class="subtitle">TankAlarm-112025-FTPS_Server_Test v)HTML"));
  client.print(F(FTPS_TEST_VERSION));
  client.print(F(R"HTML( &mdash; )HTML"));
  client.print(F(__DATE__));
  client.print(F(R"HTML(</div>

<!-- System Info -->
<div class="card">
<h2>System Info</h2>
<div class="info-grid">
  <span class="label">Sketch:</span><span class="value">)HTML"));
  client.print(F(FTPS_TEST_SKETCH_NAME));
  client.print(F(R"HTML(</span>
  <span class="label">Version:</span><span class="value">)HTML"));
  client.print(F(FTPS_TEST_VERSION));
  client.print(F(R"HTML(</span>
  <span class="label">Build:</span><span class="value">)HTML"));
  client.print(F(__DATE__));
  client.print(F(" "));
  client.print(F(__TIME__));
  client.print(F(R"HTML(</span>
  <span class="label">IP Address:</span><span class="value">)HTML"));
  client.print(Ethernet.localIP());
  client.print(F(R"HTML(</span>
  <span class="label">Device UID:</span><span class="value">)HTML"));
  client.print(gServerUid[0] != '\0' ? gServerUid : "(not available)");
  client.print(F(R"HTML(</span>
  <span class="label">Uptime:</span><span class="value">)HTML"));
  {
    unsigned long sec = millis() / 1000;
    unsigned long m = sec / 60;
    unsigned long h = m / 60;
    client.print(h); client.print("h ");
    client.print(m % 60); client.print("m ");
    client.print(sec % 60); client.print("s");
  }
  client.print(F(R"HTML(</span>
</div></div>

<!-- DFU / Firmware Update -->
<div class="card">
<h2>Firmware Update (DFU)</h2>
<p style="font-size:0.8em;color:#888;margin-bottom:8px">
Use this to switch back to the regular TankAlarm Server firmware, or to update this test sketch.
Upload the .bin to Notehub first, then check &amp; install here.</p>
<div class="info-grid">
  <span class="label">DFU Mode:</span><span class="value" id="dfuMode"></span>
  <span class="label">Available:</span><span class="value" id="dfuAvail"></span>
</div>
<div style="margin-top:12px;display:flex;gap:8px">
  <button onclick="dfuCheck()">Check for Updates</button>
  <button class="danger" id="dfuInstallBtn" onclick="dfuInstall()" disabled>Install Update</button>
</div>
<div id="dfuMsg" style="margin-top:8px;font-size:0.85em;color:#888"></div>
</div>

<!-- FTPS Config -->
<div class="card">
<h2>FTPS Test Configuration</h2>
<p style="font-size:0.8em;color:#888;margin-bottom:8px">
Configure the target FTPS server (PC running ftps_server.py). Changes are saved in RAM only.</p>
<div class="row"><label>Host:</label><input id="ftpHost" value=")HTML"));
  client.print(gFtpsHost);
  client.print(F(R"HTML("></div>
<div class="row"><label>Port:</label><input id="ftpPort" type="number" value=")HTML"));
  client.print(gFtpsPort);
  client.print(F(R"HTML("></div>
<div class="row"><label>User:</label><input id="ftpUser" value=")HTML"));
  client.print(gFtpsUser);
  client.print(F(R"HTML("></div>
<div class="row"><label>Password:</label><input id="ftpPass" type="password" value=")HTML"));
  client.print(gFtpsPass);
  client.print(F(R"HTML("></div>
<div class="row"><label>TLS Name:</label><input id="tlsName" value=")HTML"));
  client.print(gFtpsTlsServerName);
  client.print(F(R"HTML("></div>
<div class="row"><label>Fingerprint:</label><input id="fingerprint" value=")HTML"));
  client.print(gFtpsFingerprint);
  client.print(F(R"HTML(" style="font-family:monospace;font-size:0.75em"></div>
<div style="margin-top:8px;display:flex;gap:8px">
  <button onclick="updateConfig()">Save Config</button>
  <button onclick="runTest()" id="runTestBtn">Run FTPS Test</button>
</div>
</div>

<!-- Test Results -->
<div class="card">
<h2>Test Results</h2>
<div id="testSummary" class="summary idle">)HTML"));
  client.print(gTestSummary);
  client.print(F(R"HTML(</div>
</div>

<!-- Serial Log -->
<div class="card">
<h2>Serial Log</h2>
<div style="margin-bottom:8px"><button onclick="refreshLog()">Refresh</button></div>
<div class="log-box" id="logBox">Loading...</div>
</div>

<script>
async function api(path,opts){
  try{const r=await fetch(path,opts);return await r.json();}
  catch(e){return{error:e.message};}
}
async function refreshLog(){
  const d=await api('/api/log');
  if(d.entries){
    const box=document.getElementById('logBox');
    box.innerHTML=d.entries.map(e=>{
      const cls='log-'+(e.level||'info');
      const ts=Math.floor(e.ms/1000);
      const m=Math.floor(ts/60),s=ts%60;
      return '<div class="'+cls+'">'+m+'m'+String(s).padStart(2,'0')+'s '+e.message+'</div>';
    }).join('');
    box.scrollTop=box.scrollHeight;
  }
}
async function refreshStatus(){
  const d=await api('/api/status');
  if(d.dfuMode!==undefined){
    document.getElementById('dfuMode').textContent=d.dfuMode;
    const avail=d.dfuUpdateAvailable;
    const el=document.getElementById('dfuAvail');
    el.textContent=avail?'v'+d.dfuVersion+' available':'No update available';
    if(avail)el.classList.add('dfu-available');
    else el.classList.remove('dfu-available');
    document.getElementById('dfuInstallBtn').disabled=!avail;
  }
  if(d.testComplete!==undefined){
    const sum=document.getElementById('testSummary');
    sum.textContent=d.testSummary;
    sum.className='summary '+(d.testsFailed===0&&d.testComplete?'pass':d.testsFailed>0?'fail':'idle');
  }
}
async function runTest(){
  document.getElementById('runTestBtn').disabled=true;
  document.getElementById('testSummary').textContent='Running...';
  document.getElementById('testSummary').className='summary idle';
  await api('/api/test/run',{method:'POST'});
  setTimeout(()=>{refreshLog();refreshStatus();document.getElementById('runTestBtn').disabled=false;},2000);
}
async function updateConfig(){
  const cfg={
    host:document.getElementById('ftpHost').value,
    port:parseInt(document.getElementById('ftpPort').value),
    user:document.getElementById('ftpUser').value,
    pass:document.getElementById('ftpPass').value,
    tlsName:document.getElementById('tlsName').value,
    fingerprint:document.getElementById('fingerprint').value
  };
  const d=await api('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(cfg)});
  alert(d.success?'Config saved':'Error: '+(d.error||'unknown'));
}
async function dfuCheck(){
  document.getElementById('dfuMsg').textContent='Checking...';
  const d=await api('/api/dfu/check',{method:'POST'});
  document.getElementById('dfuMsg').textContent=d.message||d.error||'Done';
  refreshStatus();
}
async function dfuInstall(){
  if(!confirm('Install firmware update? Device will reboot and run the new firmware.'))return;
  document.getElementById('dfuMsg').textContent='Installing... device will reboot.';
  await api('/api/dfu/enable',{method:'POST'});
}
// Initial load
refreshLog();refreshStatus();
setInterval(refreshStatus,15000);
setInterval(refreshLog,10000);
</script>
</body></html>)HTML"));
}

// Parse a simple HTTP request. Returns false on failure.
static bool readHttpRequest(EthernetClient &client,
                            String &method, String &path, String &body) {
  String line = client.readStringUntil('\n');
  line.trim();
  int sp1 = line.indexOf(' ');
  int sp2 = line.indexOf(' ', sp1 + 1);
  if (sp1 < 0 || sp2 < 0) return false;

  method = line.substring(0, sp1);
  path = line.substring(sp1 + 1, sp2);

  // Read headers, find Content-Length
  size_t contentLength = 0;
  while (client.available() || client.connected()) {
    String hdr = client.readStringUntil('\n');
    hdr.trim();
    if (hdr.length() == 0) break;  // End of headers
    if (hdr.startsWith("Content-Length:") || hdr.startsWith("content-length:")) {
      contentLength = hdr.substring(hdr.indexOf(':') + 1).toInt();
    }
  }

  // Read body if present (cap at 2KB for safety)
  if (contentLength > 0 && contentLength < 2048) {
    unsigned long start = millis();
    while (body.length() < contentLength && (millis() - start) < 3000) {
      if (client.available()) {
        body += (char)client.read();
      }
    }
  }

  return true;
}

static void respondJson(EthernetClient &client, const char *json) {
  client.println(F("HTTP/1.1 200 OK"));
  client.println(F("Content-Type: application/json"));
  client.println(F("Connection: close"));
  client.println();
  client.print(json);
}

static void handleApiStatus(EthernetClient &client) {
  JsonDocument doc;
  doc["sketch"] = FTPS_TEST_SKETCH_NAME;
  doc["version"] = FTPS_TEST_VERSION;
  doc["buildDate"] = __DATE__;
  doc["buildTime"] = __TIME__;
  doc["uptimeMs"] = millis();
  doc["ip"] = Ethernet.localIP().toString();
  doc["deviceUid"] = gServerUid;

  // DFU
  doc["dfuMode"] = gDfuMode;
  doc["dfuUpdateAvailable"] = gDfuUpdateAvailable;
  doc["dfuVersion"] = gDfuVersion;
  doc["dfuFirmwareLength"] = gDfuFirmwareLength;
  doc["dfuError"] = gDfuError;
  doc["dfuInProgress"] = gDfuInProgress;

  // FTPS test
  doc["testRunning"] = gTestRunning;
  doc["testComplete"] = gTestComplete;
  doc["testsPassed"] = gTestsPassed;
  doc["testsFailed"] = gTestsFailed;
  doc["testSummary"] = gTestSummary;

  // FTPS config
  doc["ftpsHost"] = gFtpsHost;
  doc["ftpsPort"] = gFtpsPort;
  doc["ftpsUser"] = gFtpsUser;

  char buf[768];
  serializeJson(doc, buf, sizeof(buf));
  respondJson(client, buf);
}

static void handleApiLog(EthernetClient &client) {
  client.println(F("HTTP/1.1 200 OK"));
  client.println(F("Content-Type: application/json"));
  client.println(F("Connection: close"));
  client.println();

  client.print(F("{\"entries\":["));
  uint8_t startIdx = (gLogCount < LOG_BUFFER_SIZE) ? 0 : gLogWriteIndex;
  for (uint8_t i = 0; i < gLogCount; i++) {
    uint8_t idx = (startIdx + i) % LOG_BUFFER_SIZE;
    if (i > 0) client.print(',');
    client.print(F("{\"ms\":"));
    client.print(gLogBuffer[idx].ms);
    client.print(F(",\"level\":\""));
    client.print(gLogBuffer[idx].level);
    client.print(F("\",\"message\":\""));
    // Escape JSON special chars in message
    const char *m = gLogBuffer[idx].message;
    while (*m) {
      if (*m == '"') client.print("\\\"");
      else if (*m == '\\') client.print("\\\\");
      else if (*m == '\n') client.print("\\n");
      else if (*m == '\r') { /* skip */ }
      else client.print(*m);
      m++;
    }
    client.print(F("\"}"));
  }
  client.print(F("]}"));
}

static void handleApiConfigPost(EthernetClient &client, const String &body) {
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, body);
  if (err) {
    respondJson(client, "{\"success\":false,\"error\":\"Invalid JSON\"}");
    return;
  }

  if (doc["host"].is<const char *>()) strlcpy(gFtpsHost, doc["host"], sizeof(gFtpsHost));
  if (doc["port"].is<int>()) gFtpsPort = doc["port"].as<uint16_t>();
  if (doc["user"].is<const char *>()) strlcpy(gFtpsUser, doc["user"], sizeof(gFtpsUser));
  if (doc["pass"].is<const char *>()) strlcpy(gFtpsPass, doc["pass"], sizeof(gFtpsPass));
  if (doc["tlsName"].is<const char *>()) strlcpy(gFtpsTlsServerName, doc["tlsName"], sizeof(gFtpsTlsServerName));
  if (doc["fingerprint"].is<const char *>()) strlcpy(gFtpsFingerprint, doc["fingerprint"], sizeof(gFtpsFingerprint));

  addLog("FTPS config updated via web UI");
  respondJson(client, "{\"success\":true}");
}

static void handleApiTestRun(EthernetClient &client) {
  if (gTestRunning) {
    respondJson(client, "{\"success\":false,\"error\":\"Test already running\"}");
    return;
  }
  respondJson(client, "{\"success\":true,\"message\":\"Test started\"}");
  client.stop();
  runFtpsTest();
}

static void handleApiDfuCheck(EthernetClient &client) {
  checkForFirmwareUpdate();
  char buf[128];
  snprintf(buf, sizeof(buf),
           "{\"success\":true,\"message\":\"DFU check complete\",\"mode\":\"%s\",\"available\":%s}",
           gDfuMode, gDfuUpdateAvailable ? "true" : "false");
  respondJson(client, buf);
}

static void handleApiDfuEnable(EthernetClient &client) {
  if (!gDfuUpdateAvailable) {
    respondJson(client, "{\"success\":false,\"error\":\"No update available\"}");
    return;
  }

  if (gDfuFirmwareLength == 0) {
    respondJson(client, "{\"success\":false,\"error\":\"Firmware length unavailable\"}");
    return;
  }

  addLog("IAP DFU install triggered via web UI - device will reboot", "warn");
  respondJson(client, "{\"success\":true,\"message\":\"DFU enabled - device will update and restart\"}");
  client.stop();
  delay(100);

  // Begin IAP update. Success reboots the device from inside the helper.
  gDfuInProgress = true;

  bool success = tankalarm_performIapUpdate(
      notecard,
      gDfuFirmwareLength,
      "continuous",
      dfuKickWatchdog);

  // If we get here, IAP failed
  gDfuInProgress = false;
  if (!success) {
    addLog("IAP update failed - device did not reboot", "error");
  }
}

// DFU status API (compatible with server's /api/dfu/status for tooling)
static void handleApiDfuStatus(EthernetClient &client) {
  JsonDocument doc;
  doc["currentVersion"] = FTPS_TEST_VERSION;
  doc["sketchName"] = FTPS_TEST_SKETCH_NAME;
  doc["buildDate"] = __DATE__;
  doc["buildTime"] = __TIME__;
  doc["updateAvailable"] = gDfuUpdateAvailable;
  doc["availableVersion"] = gDfuVersion;
  doc["availableLength"] = gDfuFirmwareLength;
  doc["dfuMode"] = gDfuMode;
  doc["dfuInProgress"] = gDfuInProgress;
  doc["dfuError"] = gDfuError;

  char buf[384];
  serializeJson(doc, buf, sizeof(buf));
  respondJson(client, buf);
}

static void handleWebRequests() {
  EthernetClient client = gWebServer.available();
  if (!client) return;

  String method, path, body;
  if (!readHttpRequest(client, method, path, body)) {
    client.println(F("HTTP/1.1 400 Bad Request"));
    client.println(F("Connection: close"));
    client.println();
    client.stop();
    return;
  }

  // Route dispatch
  if (method == "GET" && path == "/") {
    serveMainPage(client);
  } else if (method == "GET" && path == "/api/status") {
    handleApiStatus(client);
  } else if (method == "GET" && path == "/api/log") {
    handleApiLog(client);
  } else if (method == "GET" && path == "/api/dfu/status") {
    handleApiDfuStatus(client);
  } else if (method == "POST" && path == "/api/config") {
    handleApiConfigPost(client, body);
  } else if (method == "POST" && path == "/api/test/run") {
    handleApiTestRun(client);
  } else if (method == "POST" && path == "/api/dfu/check") {
    handleApiDfuCheck(client);
  } else if (method == "POST" && path == "/api/dfu/enable") {
    handleApiDfuEnable(client);
  } else {
    client.println(F("HTTP/1.1 404 Not Found"));
    client.println(F("Connection: close"));
    client.println();
  }

  client.stop();
}

// ============================================================================
// Setup / Loop
// ============================================================================

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 2000) { }

  Serial.println();
  Serial.println(F("============================================================"));
  Serial.print(F("  TankAlarm FTPS Library Test v"));
  Serial.println(F(FTPS_TEST_VERSION));
  Serial.print(F("  Build: "));
  Serial.print(F(__DATE__));
  Serial.print(F(" "));
  Serial.println(F(__TIME__));
  Serial.println(F("============================================================"));

  addLog("Boot: FTPS Library Test starting");

#ifdef TANKALARM_POSIX_FILE_IO_AVAILABLE
  // Initialize LittleFS (same as server sketch)
  {
    static mbed::LittleFileSystem lfs("fs");
    static mbed::BlockDevice *bd = mbed::BlockDevice::get_default_instance();
    if (bd && lfs.mount(bd) != 0) {
      addLog("LittleFS mount failed — formatting", "warn");
      lfs.reformat(bd);
    }
  }
#endif

  loadConfigFromFilesystem();

  Wire.begin();

  initializeNotecard();
  initializeEthernet();
  gWebServer.begin();

  checkForFirmwareUpdate();

#ifdef TANKALARM_WATCHDOG_AVAILABLE
  #if defined(ARDUINO_OPTA) || defined(ARDUINO_ARCH_MBED)
    mbed::Watchdog &wd = mbed::Watchdog::get_instance();
    wd.start(30000);
    addLog("Watchdog started (30s)");
  #endif
#endif

  addLog("Setup complete — web UI active");

  Serial.print(F("Web UI: http://"));
  Serial.println(Ethernet.localIP());
  Serial.println(F("============================================================"));
}

void loop() {
#ifdef TANKALARM_WATCHDOG_AVAILABLE
  #if defined(ARDUINO_OPTA) || defined(ARDUINO_ARCH_MBED)
    mbed::Watchdog::get_instance().kick();
  #endif
#endif

  // Handle web requests
  handleWebRequests();

  // Maintain Ethernet (DHCP lease renewal)
  Ethernet.maintain();

  // Periodic DFU check
  if ((millis() - gLastDfuCheckMillis) > DFU_CHECK_INTERVAL_MS) {
    checkForFirmwareUpdate();
  }
}
