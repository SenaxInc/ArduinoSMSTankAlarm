// TankAlarm-112025-FTPS_Server_Test
// SPDX-License-Identifier: CC0-1.0
//
// FTPS library integration test for Arduino Opta, designed to be
// interchangeable with TankAlarm-112025-Server-BluesOpta via DFU.
//
// To switch firmware:
//   1. Open the current firmware's web UI.
//   2. Select the desired target firmware (TankAlarm Server or FTPS Test).
//   3. Click "Check for Updates" to inspect the latest GitHub release assets.
//   4. Click "Install Selected Firmware" to flash the chosen image.
//   5. The device will reboot into the selected sketch when flashing completes.
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

#include <netsocket/NetworkInterface.h>
#include <netsocket/SocketAddress.h>
#include <netsocket/TCPSocket.h>
#include <netsocket/TLSSocketWrapper.h>
#include <mbedtls/ssl.h>
#include <mbedtls/sha256.h>

// Pull in TankAlarm common headers for DFU, platform, and config compatibility
#include <TankAlarm_Common.h>
#include <TankAlarm_Platform.h>
#include <TankAlarm_DFU.h>

// ============================================================================
// Build Info
// ============================================================================
#define FTPS_TEST_VERSION "0.0.2"
#define FTPS_TEST_PACKAGE_VERSION FIRMWARE_VERSION
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
static bool gUseStaticIp = true;

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

struct GitHubFirmwareTargetState {
  uint8_t target;
  bool checked;
  bool updateAvailable;
  bool assetAvailable;
  char latestVersion[32];
  char releaseUrl[128];
  char assetUrl[256];
  uint32_t assetSize;
  char assetSha256[65];
  char error[96];
};

#define DFU_CHECK_INTERVAL_MS (30UL * 60UL * 1000UL)  // 30 minutes
#define GITHUB_REPO_OWNER "SenaxInc"
#define GITHUB_REPO_NAME  "SenaxTankAlarm"
#define GITHUB_DIRECT_DOWNLOAD_CHUNK_SIZE 1024U
#define GITHUB_DIRECT_MAX_REDIRECTS 4
#define GITHUB_DIRECT_HTTP_TIMEOUT_MS 15000
#define GITHUB_DIRECT_HEADER_LINE_MAX 2048
#define FIRMWARE_TARGET_SERVER 0
#define FIRMWARE_TARGET_FTPS_TEST 1

static GitHubFirmwareTargetState gSelectedFirmwareTargetState = {
  FIRMWARE_TARGET_FTPS_TEST,
  false,
  false,
  false,
  {0},
  {0},
  {0},
  0,
  {0},
  {0}
};

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

static int compareFirmwareVersions(const char *left, const char *right) {
  if (left == nullptr || left[0] == '\0') {
    return (right == nullptr || right[0] == '\0') ? 0 : -1;
  }
  if (right == nullptr || right[0] == '\0') {
    return 1;
  }

  while (left[0] != '\0' || right[0] != '\0') {
    unsigned long leftValue = 0;
    unsigned long rightValue = 0;

    while (left[0] != '\0' && left[0] != '.') {
      if (isdigit((unsigned char)left[0])) {
        leftValue = (leftValue * 10UL) + (unsigned long)(left[0] - '0');
      }
      ++left;
    }

    while (right[0] != '\0' && right[0] != '.') {
      if (isdigit((unsigned char)right[0])) {
        rightValue = (rightValue * 10UL) + (unsigned long)(right[0] - '0');
      }
      ++right;
    }

    if (leftValue < rightValue) {
      return -1;
    }
    if (leftValue > rightValue) {
      return 1;
    }

    if (left[0] == '.') {
      ++left;
    }
    if (right[0] == '.') {
      ++right;
    }
  }

  return 0;
}

static bool normalizeSha256Hex(const char *input, char *out, size_t outSize) {
  if (input == nullptr || out == nullptr || outSize < 65) {
    return false;
  }

  size_t count = 0;
  for (const char *cursor = input; *cursor != '\0'; ++cursor) {
    char ch = *cursor;
    if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') {
      continue;
    }
    if (ch >= 'a' && ch <= 'f') {
      ch = (char)(ch - 'a' + 'A');
    }
    if (!((ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'F'))) {
      return false;
    }
    if (count >= 64) {
      return false;
    }
    out[count++] = ch;
  }

  if (count != 64) {
    return false;
  }

  out[count] = '\0';
  return true;
}

static bool parseGitHubAssetDigest(const char *input, char *out, size_t outSize) {
  if (input == nullptr || input[0] == '\0') {
    return false;
  }

  const char *hex = input;
  if (strncmp(input, "sha256:", 7) == 0 || strncmp(input, "SHA256:", 7) == 0) {
    hex = input + 7;
  }

  return normalizeSha256Hex(hex, out, outSize);
}

static bool isGitHubDirectTargetReady(const GitHubFirmwareTargetState &state) {
  return state.assetAvailable && state.assetUrl[0] != '\0' &&
         state.assetSize > 0 && state.assetSha256[0] != '\0';
}

static const char *firmwareTargetId(uint8_t target) {
  return (target == FIRMWARE_TARGET_FTPS_TEST) ? "ftps-test" : "server";
}

static const char *firmwareTargetLabel(uint8_t target) {
  return (target == FIRMWARE_TARGET_FTPS_TEST) ? "FTPS Test" : "TankAlarm Server";
}

static const char *firmwareTargetAssetNamingConvention(uint8_t target) {
  return (target == FIRMWARE_TARGET_FTPS_TEST)
             ? "TankAlarm-FTPS-Test-vX.Y.Z.bin"
             : "TankAlarm-Server-vX.Y.Z.bin";
}

static bool parseFirmwareTargetValue(const char *value, uint8_t &target) {
  target = FIRMWARE_TARGET_FTPS_TEST;
  if (value == nullptr || value[0] == '\0') {
    return true;
  }
  if (strcmp(value, "server") == 0 || strcmp(value, "main") == 0 ||
      strcmp(value, "main-server") == 0) {
    target = FIRMWARE_TARGET_SERVER;
    return true;
  }
  if (strcmp(value, "ftps") == 0 || strcmp(value, "ftps-test") == 0 ||
      strcmp(value, "ftps_test") == 0) {
    target = FIRMWARE_TARGET_FTPS_TEST;
    return true;
  }
  return false;
}

static void clearGitHubFirmwareTargetState(GitHubFirmwareTargetState &state, uint8_t target) {
  memset(&state, 0, sizeof(state));
  state.target = target;
}

static void setGitHubFirmwareTargetError(GitHubFirmwareTargetState &state, const char *message) {
  if (message == nullptr) {
    state.error[0] = '\0';
    return;
  }
  strlcpy(state.error, message, sizeof(state.error));
}

static bool buildExpectedFirmwareAssetName(uint8_t target,
                                           const char *version,
                                           char *out,
                                           size_t outSize) {
  if (version == nullptr || version[0] == '\0' || out == nullptr || outSize == 0) {
    return false;
  }

  int written = 0;
  if (target == FIRMWARE_TARGET_FTPS_TEST) {
    written = snprintf(out, outSize, "TankAlarm-FTPS-Test-v%s.bin", version);
  } else {
    written = snprintf(out, outSize, "TankAlarm-Server-v%s.bin", version);
  }

  return written > 0 && (size_t)written < outSize;
}

#if defined(ARDUINO_OPTA) || defined(ARDUINO_ARCH_MBED)
static bool writeUpperHexDigest(const unsigned char *digest,
                                size_t digestLen,
                                char *out,
                                size_t outLen) {
  if (digest == nullptr || out == nullptr || outLen < (digestLen * 2U) + 1U) {
    return false;
  }

  for (size_t index = 0; index < digestLen; ++index) {
    snprintf(out + (index * 2U), outLen - (index * 2U), "%02X", digest[index]);
  }

  out[digestLen * 2U] = '\0';
  return true;
}

static bool sha256Start(mbedtls_sha256_context &context) {
#if defined(MBEDTLS_VERSION_NUMBER) && (MBEDTLS_VERSION_NUMBER >= 0x02070000)
  return mbedtls_sha256_starts_ret(&context, 0) == 0;
#else
  mbedtls_sha256_starts(&context, 0);
  return true;
#endif
}

static bool sha256Update(mbedtls_sha256_context &context, const uint8_t *data, size_t len) {
#if defined(MBEDTLS_VERSION_NUMBER) && (MBEDTLS_VERSION_NUMBER >= 0x02070000)
  return mbedtls_sha256_update_ret(&context, data, len) == 0;
#else
  mbedtls_sha256_update(&context, data, len);
  return true;
#endif
}

static bool sha256Finish(mbedtls_sha256_context &context, unsigned char *digest) {
#if defined(MBEDTLS_VERSION_NUMBER) && (MBEDTLS_VERSION_NUMBER >= 0x02070000)
  return mbedtls_sha256_finish_ret(&context, digest) == 0;
#else
  mbedtls_sha256_finish(&context, digest);
  return true;
#endif
}

static bool headerStartsWithIgnoreCase(const char *line, const char *prefix) {
  if (line == nullptr || prefix == nullptr) {
    return false;
  }

  while (*prefix != '\0') {
    if (*line == '\0') {
      return false;
    }

    char left = *line;
    char right = *prefix;
    if (left >= 'A' && left <= 'Z') {
      left = (char)(left - 'A' + 'a');
    }
    if (right >= 'A' && right <= 'Z') {
      right = (char)(right - 'A' + 'a');
    }
    if (left != right) {
      return false;
    }

    ++line;
    ++prefix;
  }

  return true;
}

static bool containsIgnoreCase(const char *text, const char *needle) {
  if (text == nullptr || needle == nullptr || needle[0] == '\0') {
    return false;
  }

  const size_t needleLen = strlen(needle);
  for (const char *cursor = text; *cursor != '\0'; ++cursor) {
    size_t matched = 0;
    while (matched < needleLen && cursor[matched] != '\0') {
      char left = cursor[matched];
      char right = needle[matched];
      if (left >= 'A' && left <= 'Z') {
        left = (char)(left - 'A' + 'a');
      }
      if (right >= 'A' && right <= 'Z') {
        right = (char)(right - 'A' + 'a');
      }
      if (left != right) {
        break;
      }
      ++matched;
    }

    if (matched == needleLen) {
      return true;
    }
  }

  return false;
}

static bool parseHttpsUrl(const String &url,
                          String &host,
                          uint16_t &port,
                          String &path,
                          String &errorMessage) {
  host = "";
  path = "/";
  port = 443;
  errorMessage = "";

  if (!url.startsWith("https://")) {
    errorMessage = "Only HTTPS GitHub asset URLs are supported";
    return false;
  }

  int hostStart = 8;
  int pathStart = url.indexOf('/', hostStart);
  String authority = (pathStart >= 0) ? url.substring(hostStart, pathStart)
                                      : url.substring(hostStart);
  path = (pathStart >= 0) ? url.substring(pathStart) : "/";

  if (authority.length() == 0) {
    errorMessage = "GitHub asset URL is missing a host";
    return false;
  }

  int colon = authority.lastIndexOf(':');
  if (colon >= 0) {
    long parsedPort = authority.substring(colon + 1).toInt();
    if (parsedPort <= 0 || parsedPort > 65535) {
      errorMessage = "GitHub asset URL port is invalid";
      return false;
    }
    host = authority.substring(0, colon);
    port = (uint16_t)parsedPort;
  } else {
    host = authority;
  }

  if (host.length() == 0) {
    errorMessage = "GitHub asset URL is missing a host";
    return false;
  }

  return true;
}

static bool sendTlsRequest(TLSSocketWrapper &socket,
                           const char *data,
                           size_t len,
                           String &errorMessage) {
  size_t sent = 0;
  while (sent < len) {
    int result = socket.send(data + sent, len - sent);
    if (result <= 0) {
      errorMessage = "HTTPS request send failed";
      return false;
    }
    sent += (size_t)result;
  }
  return true;
}

static bool readHttpLine(TLSSocketWrapper &socket,
                         char *line,
                         size_t lineSize,
                         String &errorMessage) {
  if (line == nullptr || lineSize < 2) {
    errorMessage = "HTTP header buffer is invalid";
    return false;
  }

  size_t length = 0;
  while (length + 1 < lineSize) {
    char ch = 0;
    int result = socket.recv(&ch, 1);
    if (result != 1) {
      errorMessage = "HTTPS header read failed";
      return false;
    }
    if (ch == '\r') {
      continue;
    }
    if (ch == '\n') {
      line[length] = '\0';
      return true;
    }
    line[length++] = ch;
  }

  line[lineSize - 1] = '\0';
  errorMessage = "HTTP header line exceeds buffer";
  return false;
}

static bool readHttpResponseHeaders(TLSSocketWrapper &socket,
                                    int &statusCode,
                                    String &location,
                                    uint32_t &contentLength,
                                    bool &hasContentLength,
                                    bool &chunked,
                                    String &errorMessage) {
  char line[GITHUB_DIRECT_HEADER_LINE_MAX] = {0};
  if (!readHttpLine(socket, line, sizeof(line), errorMessage)) {
    return false;
  }

  statusCode = 0;
  if (sscanf(line, "HTTP/%*d.%*d %d", &statusCode) != 1) {
    errorMessage = "Invalid HTTP status line from GitHub";
    return false;
  }

  location = "";
  contentLength = 0;
  hasContentLength = false;
  chunked = false;

  while (true) {
    if (!readHttpLine(socket, line, sizeof(line), errorMessage)) {
      return false;
    }
    if (line[0] == '\0') {
      return true;
    }

    if (headerStartsWithIgnoreCase(line, "Location:")) {
      const char *value = line + 9;
      while (*value == ' ' || *value == '\t') {
        ++value;
      }
      location = value;
    } else if (headerStartsWithIgnoreCase(line, "Content-Length:")) {
      const char *value = line + 15;
      while (*value == ' ' || *value == '\t') {
        ++value;
      }
      contentLength = (uint32_t)strtoul(value, nullptr, 10);
      hasContentLength = true;
    } else if (headerStartsWithIgnoreCase(line, "Transfer-Encoding:")) {
      const char *value = line + 18;
      while (*value == ' ' || *value == '\t') {
        ++value;
      }
      chunked = containsIgnoreCase(value, "chunked");
    }
  }
}

static bool checkGitHubReleaseForTarget(uint8_t target,
                                        const char *currentVersion,
                                        uint8_t currentTarget,
                                        GitHubFirmwareTargetState &state,
                                        bool logResults) {
  clearGitHubFirmwareTargetState(state, target);

  J *req = notecard.newRequest("web.get");
  if (!req) {
    setGitHubFirmwareTargetError(state, "Failed to create GitHub release request");
    return false;
  }

  char url[128];
  snprintf(url,
           sizeof(url),
           "https://api.github.com/repos/%s/%s/releases/latest",
           GITHUB_REPO_OWNER,
           GITHUB_REPO_NAME);
  JAddStringToObject(req, "url", url);

  J *hdrs = JAddObjectToObject(req, "headers");
  if (hdrs) {
    char ua[48];
    snprintf(ua, sizeof(ua), "FTPS-Test/%s", FTPS_TEST_PACKAGE_VERSION);
    JAddStringToObject(hdrs, "User-Agent", ua);
    JAddStringToObject(hdrs, "Accept", "application/vnd.github.v3+json");
  }

  J *rsp = notecard.requestAndResponse(req);
  if (!rsp) {
    setGitHubFirmwareTargetError(state, "GitHub release check returned no response");
    return false;
  }

  if (notecard.responseError(rsp)) {
    setGitHubFirmwareTargetError(state, JGetString(rsp, "err"));
    notecard.deleteResponse(rsp);
    return false;
  }

  int httpResult = JGetInt(rsp, "result");
  if (httpResult != 200) {
    char message[72];
    snprintf(message, sizeof(message), "GitHub Releases API returned HTTP %d", httpResult);
    setGitHubFirmwareTargetError(state, message);
    notecard.deleteResponse(rsp);
    return false;
  }

  const char *body = JGetString(rsp, "body");
  if (body == nullptr || body[0] == '\0') {
    setGitHubFirmwareTargetError(state, "GitHub release response body was empty");
    notecard.deleteResponse(rsp);
    return false;
  }

  StaticJsonDocument<512> filter;
  filter["tag_name"] = true;
  filter["html_url"] = true;
  for (uint8_t i = 0; i < 8; ++i) {
    filter["assets"][i]["name"] = true;
    filter["assets"][i]["browser_download_url"] = true;
    filter["assets"][i]["size"] = true;
    filter["assets"][i]["digest"] = true;
  }

  StaticJsonDocument<2560> doc;
  DeserializationError error = deserializeJson(doc, body, DeserializationOption::Filter(filter));
  notecard.deleteResponse(rsp);

  if (error != DeserializationError::Ok) {
    char message[96];
    snprintf(message, sizeof(message), "GitHub release JSON parse failed: %s", error.c_str());
    setGitHubFirmwareTargetError(state, message);
    return false;
  }

  const char *tagName = doc["tag_name"];
  const char *htmlUrl = doc["html_url"];
  if (tagName == nullptr || tagName[0] == '\0') {
    setGitHubFirmwareTargetError(state, "GitHub release metadata did not include a tag");
    return false;
  }

  state.checked = true;
  const char *ghVersion = (tagName[0] == 'v' || tagName[0] == 'V') ? tagName + 1 : tagName;
  strlcpy(state.latestVersion, ghVersion, sizeof(state.latestVersion));
  if (htmlUrl != nullptr) {
    strlcpy(state.releaseUrl, htmlUrl, sizeof(state.releaseUrl));
  }

  char expectedAssetName[96];
  if (!buildExpectedFirmwareAssetName(target, ghVersion, expectedAssetName, sizeof(expectedAssetName))) {
    setGitHubFirmwareTargetError(state, "Failed to build expected firmware asset name");
    return false;
  }

  JsonArray assets = doc["assets"].as<JsonArray>();
  if (!assets.isNull()) {
    for (JsonObject asset : assets) {
      const char *assetName = asset["name"] | "";
      if (strcmp(assetName, expectedAssetName) != 0) {
        continue;
      }

      const char *assetUrl = asset["browser_download_url"] | "";
      if (assetUrl[0] != '\0') {
        state.assetAvailable = true;
        strlcpy(state.assetUrl, assetUrl, sizeof(state.assetUrl));
        state.assetSize = asset["size"] | 0;
        const char *assetDigest = asset["digest"] | "";
        if (!parseGitHubAssetDigest(assetDigest, state.assetSha256, sizeof(state.assetSha256))) {
          state.assetSha256[0] = '\0';
        }
      }
      break;
    }
  }

  const int versionComparison = compareFirmwareVersions(ghVersion, currentVersion);
  const bool targetMatchesCurrent = (target == currentTarget);
  state.updateAvailable = state.assetAvailable &&
                          (versionComparison > 0 ||
                           (versionComparison == 0 && !targetMatchesCurrent));

  if (logResults) {
    char msg[128];
    snprintf(msg,
             sizeof(msg),
             "%s GitHub package: %s (current %s)",
             firmwareTargetLabel(target),
             ghVersion,
             currentVersion);
    addLog(msg, state.updateAvailable ? "info" : "warn");
  }

  return true;
}

static void buildSelectedFirmwareTargetStatus(const GitHubFirmwareTargetState &state,
                                              char *out,
                                              size_t outSize,
                                              bool &installEnabled) {
  const char *label = firmwareTargetLabel(state.target);
  const bool directReady = isGitHubDirectTargetReady(state);
  installEnabled = directReady && state.updateAvailable;

  if (out == nullptr || outSize == 0) {
    return;
  }

  if (gDfuInProgress) {
    snprintf(out, outSize, "Firmware update in progress... (%s)", gDfuMode);
    return;
  }

  if (state.error[0] != '\0') {
    strlcpy(out, state.error, outSize);
    return;
  }

  if (!state.checked) {
    snprintf(out, outSize, "Select %s, then click Check for Update", label);
    return;
  }

  if (installEnabled) {
    snprintf(out,
             outSize,
             "%s v%s is ready to install from GitHub",
             label,
             state.latestVersion[0] != '\0' ? state.latestVersion : FTPS_TEST_PACKAGE_VERSION);
    return;
  }

  if (directReady && state.target == FIRMWARE_TARGET_FTPS_TEST) {
    snprintf(out,
             outSize,
             "%s is already current (package v%s)",
             label,
             FTPS_TEST_PACKAGE_VERSION);
    return;
  }

  if (directReady && state.target != FIRMWARE_TARGET_FTPS_TEST) {
    snprintf(out,
             outSize,
             "Latest %s package (v%s) is older than the running firmware package",
             label,
             state.latestVersion[0] != '\0' ? state.latestVersion : FTPS_TEST_PACKAGE_VERSION);
    return;
  }

  if (state.assetAvailable) {
    snprintf(out,
             outSize,
             "%s asset was found on GitHub, but its digest metadata is incomplete",
             label);
    return;
  }

  if (state.latestVersion[0] != '\0') {
    snprintf(out,
             outSize,
             "Latest GitHub release v%s does not include a %s asset",
             state.latestVersion,
             label);
    return;
  }

  snprintf(out, outSize, "No published %s release asset is ready to install", label);
}

static bool failGitHubDirectInstall(String &statusMessage, const String &message) {
  statusMessage = message;
  gDfuInProgress = false;
  strlcpy(gDfuMode, "error", sizeof(gDfuMode));
  strlcpy(gDfuError, statusMessage.c_str(), sizeof(gDfuError));
  addLog(statusMessage.c_str(), "error");
  return false;
}

static bool attemptGitHubDirectInstall(String &statusMessage) {
  if (!gSelectedFirmwareTargetState.assetAvailable || gSelectedFirmwareTargetState.assetUrl[0] == '\0') {
    return failGitHubDirectInstall(statusMessage,
                                   "GitHub release found but the selected firmware asset is missing");
  }
  if (gSelectedFirmwareTargetState.assetSize == 0 || gSelectedFirmwareTargetState.assetSha256[0] == '\0') {
    return failGitHubDirectInstall(statusMessage,
                                   "GitHub asset metadata is incomplete for direct install");
  }

  NetworkInterface *network = Ethernet.getNetwork();
  if (network == nullptr) {
    return failGitHubDirectInstall(statusMessage,
                                   "Ethernet NetworkInterface is unavailable for GitHub Direct install");
  }

  gDfuInProgress = true;
  strlcpy(gDfuMode, "github", sizeof(gDfuMode));
  gDfuError[0] = '\0';

  addLog("GitHub Direct install started", "info");

  String currentUrl = gSelectedFirmwareTargetState.assetUrl;

  for (int redirectCount = 0; redirectCount <= GITHUB_DIRECT_MAX_REDIRECTS; ++redirectCount) {
    String host;
    String path;
    String parseError;
    uint16_t port = 443;
    if (!parseHttpsUrl(currentUrl, host, port, path, parseError)) {
      return failGitHubDirectInstall(statusMessage,
                                     String("GitHub asset URL parse failed: ") + parseError);
    }

    SocketAddress address;
    if (network->gethostbyname(host.c_str(), &address) != NSAPI_ERROR_OK) {
      return failGitHubDirectInstall(statusMessage,
                                     String("DNS lookup failed for ") + host);
    }
    address.set_port(port);

    TCPSocket tcp;
    if (tcp.open(network) != NSAPI_ERROR_OK) {
      return failGitHubDirectInstall(statusMessage, "Failed to open TCP socket for GitHub download");
    }
    tcp.set_timeout(GITHUB_DIRECT_HTTP_TIMEOUT_MS);
    if (tcp.connect(address) != NSAPI_ERROR_OK) {
      tcp.close();
      return failGitHubDirectInstall(statusMessage,
                                     String("TCP connect failed for ") + host);
    }

    TLSSocketWrapper tls(&tcp, host.c_str(), TLSSocketWrapper::TRANSPORT_KEEP);
    mbedtls_ssl_config *sslConfig = tls.get_ssl_config();
    if (sslConfig == nullptr) {
      tls.close();
      tcp.close();
      return failGitHubDirectInstall(statusMessage,
                                     "TLS configuration was unavailable for GitHub download");
    }
    mbedtls_ssl_conf_authmode(sslConfig, MBEDTLS_SSL_VERIFY_NONE);
    tls.set_timeout(GITHUB_DIRECT_HTTP_TIMEOUT_MS);
    if (tls.connect() != NSAPI_ERROR_OK) {
      tls.close();
      tcp.close();
      return failGitHubDirectInstall(statusMessage,
                                     String("TLS handshake failed for ") + host);
    }

    char userAgent[48];
    snprintf(userAgent, sizeof(userAgent), "FTPS-Test/%s", FTPS_TEST_PACKAGE_VERSION);

    if (!sendTlsRequest(tls, "GET ", 4, statusMessage) ||
        !sendTlsRequest(tls, path.c_str(), path.length(), statusMessage) ||
        !sendTlsRequest(tls, " HTTP/1.1\r\nHost: ", 17, statusMessage) ||
        !sendTlsRequest(tls, host.c_str(), host.length(), statusMessage) ||
        !sendTlsRequest(tls, "\r\nUser-Agent: ", 14, statusMessage) ||
        !sendTlsRequest(tls, userAgent, strlen(userAgent), statusMessage) ||
        !sendTlsRequest(tls,
                        "\r\nAccept: application/octet-stream\r\nAccept-Encoding: identity\r\nConnection: close\r\n\r\n",
                        84,
                        statusMessage)) {
      tls.close();
      tcp.close();
      return failGitHubDirectInstall(statusMessage, statusMessage);
    }

    int statusCode = 0;
    String location;
    uint32_t contentLength = 0;
    bool hasContentLength = false;
    bool chunked = false;
    if (!readHttpResponseHeaders(tls,
                                 statusCode,
                                 location,
                                 contentLength,
                                 hasContentLength,
                                 chunked,
                                 statusMessage)) {
      tls.close();
      tcp.close();
      return failGitHubDirectInstall(statusMessage, statusMessage);
    }

    if (statusCode == 301 || statusCode == 302 || statusCode == 303 ||
        statusCode == 307 || statusCode == 308) {
      tls.close();
      tcp.close();
      if (location.length() == 0) {
        return failGitHubDirectInstall(statusMessage,
                                       "GitHub redirect response did not include a Location header");
      }
      currentUrl = location.startsWith("/") ? (String("https://") + host + location)
                                             : location;
      continue;
    }

    if (statusCode != 200) {
      tls.close();
      tcp.close();
      return failGitHubDirectInstall(statusMessage,
                                     String("GitHub asset download returned HTTP ") + String(statusCode));
    }
    if (chunked) {
      tls.close();
      tcp.close();
      return failGitHubDirectInstall(statusMessage,
                                     "GitHub asset download used chunked transfer encoding; direct install requires Content-Length");
    }
    if (!hasContentLength) {
      tls.close();
      tcp.close();
      return failGitHubDirectInstall(statusMessage,
                                     "GitHub asset download did not include Content-Length");
    }
    if (contentLength != gSelectedFirmwareTargetState.assetSize) {
      tls.close();
      tcp.close();
      return failGitHubDirectInstall(statusMessage,
                                     String("GitHub asset size mismatch: expected ") +
                                         String(gSelectedFirmwareTargetState.assetSize) + String(", got ") +
                                         String(contentLength));
    }

    mbed::FlashIAP flash;
    int flashResult = flash.init();
    if (flashResult != 0) {
      tls.close();
      tcp.close();
      return failGitHubDirectInstall(statusMessage,
                                     String("FlashIAP init failed: ") + String(flashResult));
    }

    uint32_t flashStart = flash.get_flash_start();
    uint32_t flashSize = flash.get_flash_size();
    uint32_t pageSize = flash.get_page_size();
    uint32_t appStart = flashStart + 0x40000UL;

    if (gSelectedFirmwareTargetState.assetSize > (flashStart + flashSize - appStart)) {
      flash.deinit();
      tls.close();
      tcp.close();
      return failGitHubDirectInstall(statusMessage,
                                     "Firmware image is too large for the application flash region");
    }

    uint32_t eraseSize = 0;
    while (eraseSize < gSelectedFirmwareTargetState.assetSize) {
      eraseSize += flash.get_sector_size(appStart + eraseSize);
    }

    dfuKickWatchdog();
    flashResult = flash.erase(appStart, eraseSize);
    if (flashResult != 0) {
      flash.deinit();
      tls.close();
      tcp.close();
      return failGitHubDirectInstall(statusMessage,
                                     String("Flash erase failed: ") + String(flashResult));
    }

    const uint8_t eraseValue = flash.get_erase_value();
    uint32_t bufferSize = ((GITHUB_DIRECT_DOWNLOAD_CHUNK_SIZE + pageSize - 1U) / pageSize) * pageSize;
    uint8_t *programBuffer = (uint8_t *)malloc(bufferSize);
    if (programBuffer == nullptr) {
      flash.deinit();
      tls.close();
      tcp.close();
      return failGitHubDirectInstall(statusMessage,
                                     "Failed to allocate GitHub Direct download buffer");
    }
    memset(programBuffer, eraseValue, bufferSize);

    mbedtls_sha256_context downloadSha;
    mbedtls_sha256_init(&downloadSha);
    if (!sha256Start(downloadSha)) {
      free(programBuffer);
      flash.deinit();
      tls.close();
      tcp.close();
      return failGitHubDirectInstall(statusMessage,
                                     "Failed to initialize SHA-256 for GitHub download");
    }

    uint32_t downloaded = 0;
    uint32_t flashed = 0;
    uint32_t buffered = 0;
    while (downloaded < gSelectedFirmwareTargetState.assetSize) {
      dfuKickWatchdog();
      size_t receiveSize = bufferSize - buffered;
      uint32_t remaining = gSelectedFirmwareTargetState.assetSize - downloaded;
      if (receiveSize > remaining) {
        receiveSize = remaining;
      }

      int receiveResult = tls.recv(programBuffer + buffered, receiveSize);
      if (receiveResult <= 0) {
        mbedtls_sha256_free(&downloadSha);
        free(programBuffer);
        flash.deinit();
        tls.close();
        tcp.close();
        return failGitHubDirectInstall(statusMessage,
                                       "GitHub asset body read failed during firmware download");
      }

      if (!sha256Update(downloadSha, programBuffer + buffered, (size_t)receiveResult)) {
        mbedtls_sha256_free(&downloadSha);
        free(programBuffer);
        flash.deinit();
        tls.close();
        tcp.close();
        return failGitHubDirectInstall(statusMessage,
                                       "Failed to update SHA-256 digest during GitHub download");
      }

      buffered += (uint32_t)receiveResult;
      downloaded += (uint32_t)receiveResult;

      if (buffered == bufferSize || downloaded == gSelectedFirmwareTargetState.assetSize) {
        uint32_t programSize = ((buffered + pageSize - 1U) / pageSize) * pageSize;
        if (programSize > buffered) {
          memset(programBuffer + buffered, eraseValue, programSize - buffered);
        }

        flashResult = flash.program(programBuffer, appStart + flashed, programSize);
        if (flashResult != 0) {
          mbedtls_sha256_free(&downloadSha);
          free(programBuffer);
          flash.deinit();
          tls.close();
          tcp.close();
          return failGitHubDirectInstall(statusMessage,
                                         String("Flash program failed: ") + String(flashResult));
        }

        flashed += buffered;
        buffered = 0;
        memset(programBuffer, eraseValue, bufferSize);
      }
    }

    unsigned char downloadDigest[32] = {0};
    if (!sha256Finish(downloadSha, downloadDigest)) {
      mbedtls_sha256_free(&downloadSha);
      free(programBuffer);
      flash.deinit();
      tls.close();
      tcp.close();
      return failGitHubDirectInstall(statusMessage,
                                     "Failed to finalize GitHub download SHA-256 digest");
    }
    mbedtls_sha256_free(&downloadSha);

    char actualSha256[65] = {0};
    if (!writeUpperHexDigest(downloadDigest, sizeof(downloadDigest), actualSha256, sizeof(actualSha256))) {
      free(programBuffer);
      flash.deinit();
      tls.close();
      tcp.close();
      return failGitHubDirectInstall(statusMessage,
                                     "Failed to format GitHub download SHA-256 digest");
    }
    if (strcmp(actualSha256, gSelectedFirmwareTargetState.assetSha256) != 0) {
      free(programBuffer);
      flash.deinit();
      tls.close();
      tcp.close();
      return failGitHubDirectInstall(statusMessage,
                                     String("GitHub asset SHA-256 mismatch: expected ") +
                                         String(gSelectedFirmwareTargetState.assetSha256) + String(", got ") +
                                         String(actualSha256));
    }

    mbedtls_sha256_context flashSha;
    mbedtls_sha256_init(&flashSha);
    if (!sha256Start(flashSha)) {
      free(programBuffer);
      flash.deinit();
      tls.close();
      tcp.close();
      return failGitHubDirectInstall(statusMessage,
                                     "Failed to initialize SHA-256 for flash verification");
    }

    const uint8_t *flashPointer = (const uint8_t *)appStart;
    uint32_t verifyOffset = 0;
    while (verifyOffset < gSelectedFirmwareTargetState.assetSize) {
      dfuKickWatchdog();
      uint32_t verifyChunk = gSelectedFirmwareTargetState.assetSize - verifyOffset;
      if (verifyChunk > GITHUB_DIRECT_DOWNLOAD_CHUNK_SIZE) {
        verifyChunk = GITHUB_DIRECT_DOWNLOAD_CHUNK_SIZE;
      }
      if (!sha256Update(flashSha, flashPointer + verifyOffset, verifyChunk)) {
        mbedtls_sha256_free(&flashSha);
        free(programBuffer);
        flash.deinit();
        tls.close();
        tcp.close();
        return failGitHubDirectInstall(statusMessage,
                                       "Failed to update flash verification SHA-256 digest");
      }
      verifyOffset += verifyChunk;
    }

    unsigned char flashDigest[32] = {0};
    if (!sha256Finish(flashSha, flashDigest)) {
      mbedtls_sha256_free(&flashSha);
      free(programBuffer);
      flash.deinit();
      tls.close();
      tcp.close();
      return failGitHubDirectInstall(statusMessage,
                                     "Failed to finalize flash verification SHA-256 digest");
    }
    mbedtls_sha256_free(&flashSha);

    char flashSha256[65] = {0};
    if (!writeUpperHexDigest(flashDigest, sizeof(flashDigest), flashSha256, sizeof(flashSha256))) {
      free(programBuffer);
      flash.deinit();
      tls.close();
      tcp.close();
      return failGitHubDirectInstall(statusMessage,
                                     "Failed to format flash verification SHA-256 digest");
    }
    if (strcmp(flashSha256, gSelectedFirmwareTargetState.assetSha256) != 0) {
      free(programBuffer);
      flash.deinit();
      tls.close();
      tcp.close();
      return failGitHubDirectInstall(statusMessage,
                                     String("Flash SHA-256 mismatch after programming: expected ") +
                                         String(gSelectedFirmwareTargetState.assetSha256) + String(", got ") +
                                         String(flashSha256));
    }

    free(programBuffer);
    flash.deinit();
    tls.close();
    tcp.close();

    addLog("GitHub Direct update complete; rebooting", "info");
    Serial.flush();
    delay(500);
    NVIC_SystemReset();
    return true;
  }

  return failGitHubDirectInstall(statusMessage,
                                 "GitHub asset redirect chain exceeded the supported limit");
}
#else
static bool checkGitHubReleaseForTarget(uint8_t target,
                                        const char *currentVersion,
                                        uint8_t currentTarget,
                                        GitHubFirmwareTargetState &state,
                                        bool logResults) {
  (void)target;
  (void)currentVersion;
  (void)currentTarget;
  (void)logResults;
  clearGitHubFirmwareTargetState(state, FIRMWARE_TARGET_FTPS_TEST);
  setGitHubFirmwareTargetError(state, "GitHub Direct install requires an Opta/Mbed build");
  return false;
}

static void buildSelectedFirmwareTargetStatus(const GitHubFirmwareTargetState &state,
                                              char *out,
                                              size_t outSize,
                                              bool &installEnabled) {
  installEnabled = false;
  strlcpy(out, state.error[0] != '\0' ? state.error : "GitHub Direct install requires an Opta/Mbed build", outSize);
}

static bool attemptGitHubDirectInstall(String &statusMessage) {
  statusMessage = "GitHub Direct install requires an Opta/Mbed build";
  return false;
}
#endif

// ============================================================================
// FTPS Test State
// ============================================================================
enum FtpsTestStep : uint8_t {
  FTPS_TEST_STEP_NONE = 0,
  FTPS_TEST_STEP_BEGIN,
  FTPS_TEST_STEP_CONNECT,
  FTPS_TEST_STEP_MKD_PARENT,
  FTPS_TEST_STEP_MKD_NESTED,
  FTPS_TEST_STEP_STORE,
  FTPS_TEST_STEP_SIZE,
  FTPS_TEST_STEP_RETRIEVE,
  FTPS_TEST_STEP_QUIT,
};

static int gTestsPassed = 0;
static int gTestsFailed = 0;
static bool gTestRunning = false;
static bool gTestComplete = false;
static char gTestSummary[256] = "No test run yet";
static uint8_t gTestStopAfterStep = FTPS_TEST_STEP_QUIT;
static uint8_t gLastAttemptedFtpsStep = FTPS_TEST_STEP_NONE;
static uint8_t gLastCompletedFtpsStep = FTPS_TEST_STEP_NONE;
static int gLastFtpsErrorCode = 0;
static char gLastFtpsError[192] = {0};
static char gLastFtpsInternalPhase[48] = "idle";
static bool gLastTestRunInterrupted = false;

static const char *FTPS_TEST_STATE_FILE = "/fs/ftps_test_state.txt";

static const char *ftpsTestStepValueId(uint8_t step) {
  switch (step) {
    case FTPS_TEST_STEP_BEGIN: return "begin";
    case FTPS_TEST_STEP_CONNECT: return "connect";
    case FTPS_TEST_STEP_MKD_PARENT: return "mkd-parent";
    case FTPS_TEST_STEP_MKD_NESTED: return "mkd-nested";
    case FTPS_TEST_STEP_STORE: return "store";
    case FTPS_TEST_STEP_SIZE: return "size";
    case FTPS_TEST_STEP_RETRIEVE: return "retrieve";
    case FTPS_TEST_STEP_QUIT: return "quit";
    case FTPS_TEST_STEP_NONE:
    default:
      return "none";
  }
}

static const char *ftpsTestStepDisplayLabel(uint8_t step) {
  switch (step) {
    case FTPS_TEST_STEP_BEGIN: return "begin()";
    case FTPS_TEST_STEP_CONNECT: return "connect()";
    case FTPS_TEST_STEP_MKD_PARENT: return "mkd() parent";
    case FTPS_TEST_STEP_MKD_NESTED: return "mkd() nested";
    case FTPS_TEST_STEP_STORE: return "store()";
    case FTPS_TEST_STEP_SIZE: return "size()";
    case FTPS_TEST_STEP_RETRIEVE: return "retrieve()";
    case FTPS_TEST_STEP_QUIT: return "quit()";
    case FTPS_TEST_STEP_NONE:
    default:
      return "Not started";
  }
}

static bool parseFtpsTestStepValue(const char *value, uint8_t &step) {
  step = FTPS_TEST_STEP_QUIT;
  if (value == nullptr || value[0] == '\0' || strcmp(value, "full") == 0 ||
      strcmp(value, "quit") == 0) {
    return true;
  }
  if (strcmp(value, "begin") == 0) {
    step = FTPS_TEST_STEP_BEGIN;
    return true;
  }
  if (strcmp(value, "connect") == 0) {
    step = FTPS_TEST_STEP_CONNECT;
    return true;
  }
  if (strcmp(value, "mkd-parent") == 0) {
    step = FTPS_TEST_STEP_MKD_PARENT;
    return true;
  }
  if (strcmp(value, "mkd-nested") == 0) {
    step = FTPS_TEST_STEP_MKD_NESTED;
    return true;
  }
  if (strcmp(value, "store") == 0) {
    step = FTPS_TEST_STEP_STORE;
    return true;
  }
  if (strcmp(value, "size") == 0) {
    step = FTPS_TEST_STEP_SIZE;
    return true;
  }
  if (strcmp(value, "retrieve") == 0) {
    step = FTPS_TEST_STEP_RETRIEVE;
    return true;
  }
  return false;
}

static void sanitizeFtpsTestStateValue(const char *input, char *out, size_t outSize) {
  if (out == nullptr || outSize == 0) {
    return;
  }

  if (input == nullptr) {
    out[0] = '\0';
    return;
  }

  size_t writeIndex = 0;
  for (const char *cursor = input; *cursor != '\0' && writeIndex + 1 < outSize; ++cursor) {
    char ch = *cursor;
    if (ch == '\r' || ch == '\n') {
      ch = ' ';
    }
    out[writeIndex++] = ch;
  }
  out[writeIndex] = '\0';
}

static void saveFtpsTestState(bool runActive) {
#ifdef TANKALARM_POSIX_FILE_IO_AVAILABLE
  FILE *f = fopen(FTPS_TEST_STATE_FILE, "w");
  if (!f) {
    return;
  }

  char sanitizedSummary[sizeof(gTestSummary)] = {0};
  char sanitizedError[sizeof(gLastFtpsError)] = {0};
  char sanitizedInternalPhase[sizeof(gLastFtpsInternalPhase)] = {0};
  sanitizeFtpsTestStateValue(gTestSummary, sanitizedSummary, sizeof(sanitizedSummary));
  sanitizeFtpsTestStateValue(gLastFtpsError, sanitizedError, sizeof(sanitizedError));
  sanitizeFtpsTestStateValue(gLastFtpsInternalPhase,
                             sanitizedInternalPhase,
                             sizeof(sanitizedInternalPhase));

  fprintf(f, "runActive=%d\n", runActive ? 1 : 0);
  fprintf(f, "stopAfter=%s\n", ftpsTestStepValueId(gTestStopAfterStep));
  fprintf(f, "attempted=%s\n", ftpsTestStepValueId(gLastAttemptedFtpsStep));
  fprintf(f, "completed=%s\n", ftpsTestStepValueId(gLastCompletedFtpsStep));
  fprintf(f, "testsPassed=%d\n", gTestsPassed);
  fprintf(f, "testsFailed=%d\n", gTestsFailed);
  fprintf(f, "errorCode=%d\n", gLastFtpsErrorCode);
  fprintf(f, "internalPhase=%s\n", sanitizedInternalPhase);
  fprintf(f, "error=%s\n", sanitizedError);
  fprintf(f, "summary=%s\n", sanitizedSummary);
  fclose(f);
#else
  (void)runActive;
#endif
}

static void loadFtpsTestState() {
#ifdef TANKALARM_POSIX_FILE_IO_AVAILABLE
  FILE *f = fopen(FTPS_TEST_STATE_FILE, "r");
  if (!f) {
    return;
  }

  bool runActive = false;
  char line[384] = {0};
  while (fgets(line, sizeof(line), f)) {
    size_t len = strlen(line);
    while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
      line[--len] = '\0';
    }

    if (strncmp(line, "runActive=", 10) == 0) {
      runActive = atoi(line + 10) != 0;
    } else if (strncmp(line, "stopAfter=", 10) == 0) {
      uint8_t parsed = FTPS_TEST_STEP_QUIT;
      if (parseFtpsTestStepValue(line + 10, parsed)) {
        gTestStopAfterStep = parsed;
      }
    } else if (strncmp(line, "attempted=", 10) == 0) {
      uint8_t parsed = FTPS_TEST_STEP_NONE;
      if (parseFtpsTestStepValue(line + 10, parsed)) {
        gLastAttemptedFtpsStep = parsed;
      }
    } else if (strncmp(line, "completed=", 10) == 0) {
      uint8_t parsed = FTPS_TEST_STEP_NONE;
      if (parseFtpsTestStepValue(line + 10, parsed)) {
        gLastCompletedFtpsStep = parsed;
      }
    } else if (strncmp(line, "testsPassed=", 12) == 0) {
      gTestsPassed = atoi(line + 12);
    } else if (strncmp(line, "testsFailed=", 12) == 0) {
      gTestsFailed = atoi(line + 12);
    } else if (strncmp(line, "errorCode=", 10) == 0) {
      gLastFtpsErrorCode = atoi(line + 10);
    } else if (strncmp(line, "internalPhase=", 14) == 0) {
      strlcpy(gLastFtpsInternalPhase, line + 14, sizeof(gLastFtpsInternalPhase));
    } else if (strncmp(line, "error=", 6) == 0) {
      strlcpy(gLastFtpsError, line + 6, sizeof(gLastFtpsError));
    } else if (strncmp(line, "summary=", 8) == 0) {
      strlcpy(gTestSummary, line + 8, sizeof(gTestSummary));
    }
  }
  fclose(f);

  if (runActive) {
    gTestRunning = false;
    gTestComplete = true;
    gLastTestRunInterrupted = true;
    if (gTestsFailed == 0) {
      gTestsFailed = 1;
    }
    if (gLastFtpsErrorCode == 0) {
      gLastFtpsErrorCode = -1;
    }
    if (gLastFtpsError[0] == '\0') {
      strlcpy(gLastFtpsError,
              "Device reset or test aborted before completion",
              sizeof(gLastFtpsError));
    }
    if (gLastFtpsInternalPhase[0] != '\0' && strcmp(gLastFtpsInternalPhase, "idle") != 0) {
      snprintf(gTestSummary,
               sizeof(gTestSummary),
               "Interrupted after %s while attempting %s at %s",
               ftpsTestStepDisplayLabel(gLastCompletedFtpsStep),
               ftpsTestStepDisplayLabel(gLastAttemptedFtpsStep),
               gLastFtpsInternalPhase);
    } else {
      snprintf(gTestSummary,
               sizeof(gTestSummary),
               "Interrupted after %s while attempting %s",
               ftpsTestStepDisplayLabel(gLastCompletedFtpsStep),
               ftpsTestStepDisplayLabel(gLastAttemptedFtpsStep));
    }
    saveFtpsTestState(false);
    addLog(gTestSummary, "warn");
  }
#endif
}

static void updateFtpsInternalPhase(const char *phase) {
  char sanitizedPhase[sizeof(gLastFtpsInternalPhase)] = {0};
  sanitizeFtpsTestStateValue(phase, sanitizedPhase, sizeof(sanitizedPhase));
  if (sanitizedPhase[0] == '\0') {
    strlcpy(sanitizedPhase, "idle", sizeof(sanitizedPhase));
  }
  if (strcmp(gLastFtpsInternalPhase, sanitizedPhase) == 0) {
    return;
  }
  strlcpy(gLastFtpsInternalPhase, sanitizedPhase, sizeof(gLastFtpsInternalPhase));
  saveFtpsTestState(gTestRunning);
}

static void ftpsTracePhaseCallback(const char *phase) {
  Serial.print(F("[FTPS] "));
  Serial.println(phase);
  Serial.flush();
#ifdef TANKALARM_WATCHDOG_AVAILABLE
  #if defined(ARDUINO_OPTA) || defined(ARDUINO_ARCH_MBED)
    mbed::Watchdog::get_instance().kick();
  #endif
#endif
  updateFtpsInternalPhase(phase);
}

static void startFtpsTestRun() {
  gTestsPassed = 0;
  gTestsFailed = 0;
  gTestRunning = true;
  gTestComplete = false;
  gLastAttemptedFtpsStep = FTPS_TEST_STEP_NONE;
  gLastCompletedFtpsStep = FTPS_TEST_STEP_NONE;
  gLastFtpsErrorCode = 0;
  gLastFtpsError[0] = '\0';
  strlcpy(gLastFtpsInternalPhase, "idle", sizeof(gLastFtpsInternalPhase));
  gLastTestRunInterrupted = false;
  snprintf(gTestSummary,
           sizeof(gTestSummary),
           "Running FTPS test (stop after %s)",
           ftpsTestStepDisplayLabel(gTestStopAfterStep));
  saveFtpsTestState(true);
}

static void recordFtpsTestAttempt(uint8_t step, const char *message) {
#ifdef TANKALARM_WATCHDOG_AVAILABLE
  #if defined(ARDUINO_OPTA) || defined(ARDUINO_ARCH_MBED)
    mbed::Watchdog::get_instance().kick();
  #endif
#endif
  gLastAttemptedFtpsStep = step;
  if (message != nullptr) {
    addLog(message);
  }
  saveFtpsTestState(true);
}

static void recordFtpsTestSuccess(uint8_t step, const char *message) {
#ifdef TANKALARM_WATCHDOG_AVAILABLE
  #if defined(ARDUINO_OPTA) || defined(ARDUINO_ARCH_MBED)
    mbed::Watchdog::get_instance().kick();
  #endif
#endif
  gLastCompletedFtpsStep = step;
  gTestsPassed++;
  if (message != nullptr) {
    addLog(message, "pass");
  }
  saveFtpsTestState(true);
}

static void recordFtpsTestNoteFailure(const char *message) {
  gTestsFailed++;
  addLog(message, "fail");
  saveFtpsTestState(true);
}

static void recordFtpsTestFailure(uint8_t step,
                                  int errorCode,
                                  const char *detail,
                                  const char *message) {
  gLastAttemptedFtpsStep = step;
  gLastFtpsErrorCode = errorCode;
  strlcpy(gLastFtpsError, detail != nullptr ? detail : "", sizeof(gLastFtpsError));
  gTestsFailed++;
  gTestRunning = false;
  gTestComplete = true;

  if (message != nullptr) {
    addLog(message, "fail");
  }

  if (detail != nullptr && detail[0] != '\0' &&
      gLastFtpsInternalPhase[0] != '\0' && strcmp(gLastFtpsInternalPhase, "idle") != 0) {
    snprintf(gTestSummary,
             sizeof(gTestSummary),
             "%s failed at %s: %s",
             ftpsTestStepDisplayLabel(step),
             gLastFtpsInternalPhase,
             detail);
  } else if (detail != nullptr && detail[0] != '\0') {
    snprintf(gTestSummary,
             sizeof(gTestSummary),
             "%s failed: %s",
             ftpsTestStepDisplayLabel(step),
             detail);
  } else {
    snprintf(gTestSummary,
             sizeof(gTestSummary),
             "%s failed with error %d",
             ftpsTestStepDisplayLabel(step),
             errorCode);
  }

  saveFtpsTestState(false);
}

static bool shouldStopAfterFtpsStep(uint8_t step) {
  if (gTestStopAfterStep != step) {
    return false;
  }

  gTestRunning = false;
  gTestComplete = true;
  gLastFtpsErrorCode = 0;
  gLastFtpsError[0] = '\0';
  snprintf(gTestSummary,
           sizeof(gTestSummary),
           "Stopped after %s by request",
           ftpsTestStepDisplayLabel(step));
  addLog(gTestSummary, "warn");
  saveFtpsTestState(false);
  return true;
}

static void finalizeFtpsTestRun() {
  if (gTestComplete) {
    return;
  }

  gTestRunning = false;
  gTestComplete = true;
  snprintf(gTestSummary, sizeof(gTestSummary), "%d passed, %d failed",
           gTestsPassed, gTestsFailed);

  char msg[160];
  snprintf(msg, sizeof(msg), "=== TEST COMPLETE: %s ===",
           gTestsFailed == 0 ? "ALL PASSED" : "FAILURES DETECTED");
  addLog(msg, gTestsFailed == 0 ? "pass" : "fail");
  saveFtpsTestState(false);
}

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

  if (fseek(f, 0, SEEK_END) != 0) {
    fclose(f);
    addLog("Failed to seek server_config.json", "warn");
    return;
  }

  long configSize = ftell(f);
  if (configSize <= 0 || configSize > 8192) {
    fclose(f);
    addLog("server_config.json size is invalid", "warn");
    return;
  }

  rewind(f);

  char *configJson = (char *)malloc((size_t)configSize + 1U);
  if (!configJson) {
    fclose(f);
    addLog("Failed to allocate server config buffer", "warn");
    return;
  }

  size_t bytesRead = fread(configJson, 1, (size_t)configSize, f);
  fclose(f);
  configJson[bytesRead] = '\0';

  if (bytesRead != (size_t)configSize) {
    free(configJson);
    addLog("Failed to read server_config.json", "warn");
    return;
  }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, configJson);
  free(configJson);
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
  if (doc["staticIp"]) {
    JsonArrayConst ip = doc["staticIp"].as<JsonArrayConst>();
    if (ip.size() == 4) {
      gStaticIp = IPAddress(ip[0], ip[1], ip[2], ip[3]);
    }
  }
  if (doc["gateway"]) {
    JsonArrayConst gateway = doc["gateway"].as<JsonArrayConst>();
    if (gateway.size() == 4) {
      gStaticGateway = IPAddress(gateway[0], gateway[1], gateway[2], gateway[3]);
    }
  }
  if (doc["subnet"]) {
    JsonArrayConst subnet = doc["subnet"].as<JsonArrayConst>();
    if (subnet.size() == 4) {
      gStaticSubnet = IPAddress(subnet[0], subnet[1], subnet[2], subnet[3]);
    }
  }
  if (doc["dns"]) {
    JsonArrayConst dns = doc["dns"].as<JsonArrayConst>();
    if (dns.size() == 4) {
      gStaticDns = IPAddress(dns[0], dns[1], dns[2], dns[3]);
    }
  }

  // Read FTP settings as defaults for FTPS test when legacy plaintext fields exist.
  JsonObjectConst ftp = doc["ftp"].as<JsonObjectConst>();
  const char *ftpHost = nullptr;
  if (!ftp.isNull()) {
    ftpHost = ftp["host"].as<const char *>();
  }
  if ((ftpHost == nullptr || ftpHost[0] == '\0') && doc["ftpHost"].is<const char *>()) {
    ftpHost = doc["ftpHost"].as<const char *>();
  }
  if (ftpHost != nullptr && ftpHost[0] != '\0') {
    strlcpy(gFtpsHost, ftpHost, sizeof(gFtpsHost));
  }

  uint16_t ftpPort = 0;
  if (!ftp.isNull() && (ftp["port"].is<uint16_t>() || ftp["port"].is<int>())) {
    ftpPort = ftp["port"].as<uint16_t>();
  } else if (doc["ftpPort"].is<uint16_t>() || doc["ftpPort"].is<int>()) {
    ftpPort = doc["ftpPort"].as<uint16_t>();
  }
  if (ftpPort != 0) {
    gFtpsPort = ftpPort;
  }

  const char *ftpUser = nullptr;
  if (!ftp.isNull()) {
    ftpUser = ftp["user"].as<const char *>();
  }
  if ((ftpUser == nullptr || ftpUser[0] == '\0') && doc["ftpUser"].is<const char *>()) {
    ftpUser = doc["ftpUser"].as<const char *>();
  }
  if (ftpUser != nullptr && ftpUser[0] != '\0') {
    strlcpy(gFtpsUser, ftpUser, sizeof(gFtpsUser));
  }

  const char *ftpPass = nullptr;
  if (!ftp.isNull()) {
    ftpPass = ftp["pass"].as<const char *>();
  }
  if ((ftpPass == nullptr || ftpPass[0] == '\0') && doc["ftpPass"].is<const char *>()) {
    ftpPass = doc["ftpPass"].as<const char *>();
  }
  if (ftpPass != nullptr && ftpPass[0] != '\0') {
    strlcpy(gFtpsPass, ftpPass, sizeof(gFtpsPass));
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
    Serial.print(F("Ethernet: static IP "));
    Serial.println(gStaticIp);
    status = Ethernet.begin(gMacAddress, gStaticIp, gStaticDns, gStaticGateway, gStaticSubnet);
  } else {
    Serial.println(F("Ethernet: DHCP..."));
    status = Ethernet.begin(gMacAddress);
  }
  Serial.print(F("Ethernet.begin returned: "));
  Serial.println(status);

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
    "  \"library\": \"FTPSclientOPTA\",\r\n"
    "  \"message\": \"If you can read this file, FTPS upload succeeded.\",\r\n"
    "  \"sensor_sample\": {\r\n"
    "    \"tank_id\": \"TANK-001\",\r\n"
    "    \"level_inches\": 42.5,\r\n"
    "    \"alarm_active\": false,\r\n"
    "    \"battery_v\": 12.8\r\n"
    "  }\r\n"
    "}\r\n";

static void runFtpsTest() {
  startFtpsTestRun();

  addLog("=== FTPS TEST STARTING ===", "info");

  char msg[160];
  snprintf(msg, sizeof(msg), "Target: %s:%u  User: %s  Trust: %s",
           gFtpsHost, gFtpsPort, gFtpsUser,
           gFtpsTrustMode == FtpsTrustMode::Fingerprint ? "Fingerprint" : "ImportedCert");
  addLog(msg);

  FtpsClient ftps;
  ftps.setTraceCallback(ftpsTracePhaseCallback);
  char error[192] = {};

  // begin()
  recordFtpsTestAttempt(FTPS_TEST_STEP_BEGIN, "FtpsClient.begin()...");
  if (!ftps.begin(Ethernet.getNetwork(), error, sizeof(error))) {
    snprintf(msg, sizeof(msg), "begin() FAILED (err=%d): %s",
             static_cast<int>(ftps.lastError()), error);
    recordFtpsTestFailure(FTPS_TEST_STEP_BEGIN,
                          static_cast<int>(ftps.lastError()),
                          error,
                          msg);
    goto done;
  }
  recordFtpsTestSuccess(FTPS_TEST_STEP_BEGIN, "begin() OK");
  if (shouldStopAfterFtpsStep(FTPS_TEST_STEP_BEGIN)) {
    goto done;
  }

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

    recordFtpsTestAttempt(FTPS_TEST_STEP_CONNECT, "FtpsClient.connect()...");
    if (!ftps.connect(config, error, sizeof(error))) {
      snprintf(msg, sizeof(msg), "connect() FAILED at %s (err=%d): %s",
               ftps.lastPhase(), static_cast<int>(ftps.lastError()), error);
      recordFtpsTestFailure(FTPS_TEST_STEP_CONNECT,
                            static_cast<int>(ftps.lastError()),
                            error,
                            msg);
      goto done;
    }
    recordFtpsTestSuccess(FTPS_TEST_STEP_CONNECT, "connect() OK");
    if (shouldStopAfterFtpsStep(FTPS_TEST_STEP_CONNECT)) {
      goto done;
    }
  }

  // mkd() parent
  recordFtpsTestAttempt(FTPS_TEST_STEP_MKD_PARENT, "mkd() parent directory...");
  if (!ftps.mkd(REMOTE_PARENT_DIR, error, sizeof(error))) {
    snprintf(msg, sizeof(msg), "mkd() parent FAILED (err=%d): %s",
             static_cast<int>(ftps.lastError()), error);
    recordFtpsTestFailure(FTPS_TEST_STEP_MKD_PARENT,
                          static_cast<int>(ftps.lastError()),
                          error,
                          msg);
    goto done;
  }
  snprintf(msg, sizeof(msg), "mkd() parent OK: %s", REMOTE_PARENT_DIR);
  recordFtpsTestSuccess(FTPS_TEST_STEP_MKD_PARENT, msg);
  if (shouldStopAfterFtpsStep(FTPS_TEST_STEP_MKD_PARENT)) {
    goto done;
  }

  // mkd() nested
  recordFtpsTestAttempt(FTPS_TEST_STEP_MKD_NESTED, "mkd() nested directory...");
  if (!ftps.mkd(REMOTE_NESTED_DIR, error, sizeof(error))) {
    snprintf(msg, sizeof(msg), "mkd() nested FAILED (err=%d): %s",
             static_cast<int>(ftps.lastError()), error);
    recordFtpsTestFailure(FTPS_TEST_STEP_MKD_NESTED,
                          static_cast<int>(ftps.lastError()),
                          error,
                          msg);
    goto done;
  }
  snprintf(msg, sizeof(msg), "mkd() nested OK: %s", REMOTE_NESTED_DIR);
  recordFtpsTestSuccess(FTPS_TEST_STEP_MKD_NESTED, msg);
  if (shouldStopAfterFtpsStep(FTPS_TEST_STEP_MKD_NESTED)) {
    goto done;
  }

  // store()
  {
    size_t payloadLen = strlen(kUploadPayload);
    snprintf(msg, sizeof(msg), "store() uploading %u bytes...", (unsigned)payloadLen);
    recordFtpsTestAttempt(FTPS_TEST_STEP_STORE, msg);
    if (!ftps.store(REMOTE_TEST_FILE,
                    reinterpret_cast<const uint8_t *>(kUploadPayload),
                    payloadLen, error, sizeof(error))) {
      snprintf(msg, sizeof(msg), "store() FAILED at %s (err=%d): %s",
           ftps.lastPhase(), static_cast<int>(ftps.lastError()), error);
      recordFtpsTestFailure(FTPS_TEST_STEP_STORE,
                            static_cast<int>(ftps.lastError()),
                            error,
                            msg);
      goto done;
    }
    snprintf(msg, sizeof(msg), "store() OK: %s (%u bytes)", REMOTE_TEST_FILE, (unsigned)payloadLen);
    recordFtpsTestSuccess(FTPS_TEST_STEP_STORE, msg);
    if (shouldStopAfterFtpsStep(FTPS_TEST_STEP_STORE)) {
      goto done;
    }
  }

  // size()
  {
    size_t remoteBytes = 0;
    recordFtpsTestAttempt(FTPS_TEST_STEP_SIZE, "size() querying remote file...");
    if (!ftps.size(REMOTE_TEST_FILE, remoteBytes, error, sizeof(error))) {
      snprintf(msg, sizeof(msg), "size() FAILED (err=%d): %s",
               static_cast<int>(ftps.lastError()), error);
      recordFtpsTestFailure(FTPS_TEST_STEP_SIZE,
                            static_cast<int>(ftps.lastError()),
                            error,
                            msg);
      goto done;
    }
    snprintf(msg, sizeof(msg), "size() OK: %u bytes", (unsigned)remoteBytes);
    recordFtpsTestSuccess(FTPS_TEST_STEP_SIZE, msg);

    size_t expectedLen = strlen(kUploadPayload);
    if (remoteBytes == expectedLen) {
      recordFtpsTestSuccess(FTPS_TEST_STEP_SIZE, "Size matches upload payload");
    } else {
      snprintf(msg, sizeof(msg), "Size mismatch: expected %u, got %u",
               (unsigned)expectedLen, (unsigned)remoteBytes);
      recordFtpsTestNoteFailure(msg);
    }

    if (shouldStopAfterFtpsStep(FTPS_TEST_STEP_SIZE)) {
      goto done;
    }
  }

  // retrieve()
  {
    uint8_t dlBuffer[1024] = {};
    size_t bytesRead = 0;
    recordFtpsTestAttempt(FTPS_TEST_STEP_RETRIEVE, "retrieve() downloading...");
    if (!ftps.retrieve(REMOTE_TEST_FILE, dlBuffer, sizeof(dlBuffer),
                       bytesRead, error, sizeof(error))) {
      snprintf(msg, sizeof(msg), "retrieve() FAILED (err=%d): %s",
               static_cast<int>(ftps.lastError()), error);
      recordFtpsTestFailure(FTPS_TEST_STEP_RETRIEVE,
                            static_cast<int>(ftps.lastError()),
                            error,
                            msg);
      goto done;
    }
    snprintf(msg, sizeof(msg), "retrieve() OK: %u bytes", (unsigned)bytesRead);
    recordFtpsTestSuccess(FTPS_TEST_STEP_RETRIEVE, msg);

    size_t expectedLen = strlen(kUploadPayload);
    if (bytesRead == expectedLen &&
        memcmp(dlBuffer, kUploadPayload, expectedLen) == 0) {
      recordFtpsTestSuccess(FTPS_TEST_STEP_RETRIEVE, "Content verification: matches upload");
    } else {
      recordFtpsTestNoteFailure("Content verification: MISMATCH");
    }

    if (shouldStopAfterFtpsStep(FTPS_TEST_STEP_RETRIEVE)) {
      goto done;
    }
  }

  // quit()
  recordFtpsTestAttempt(FTPS_TEST_STEP_QUIT, "FtpsClient.quit()...");
  ftps.quit();
  if (ftps.lastError() == FtpsError::QuitFailed) {
    addLog("quit() warned: no 221 reply", "warn");
  } else {
    recordFtpsTestSuccess(FTPS_TEST_STEP_QUIT, "quit() OK");
  }

done:
  finalizeFtpsTestRun();
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
<h1>FTPSclientOPTA Library Test</h1>
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
  <span class="label">Package:</span><span class="value">)HTML"));
  client.print(F(FTPS_TEST_PACKAGE_VERSION));
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
Choose the published firmware image to install directly from GitHub Releases.
Use this to switch between the FTPS test sketch and the regular TankAlarm Server firmware.</p>
<div class="info-grid">
  <span class="label">Current:</span><span class="value">)HTML"));
  client.print(F(FTPS_TEST_SKETCH_NAME));
  client.print(F(R"HTML(</span>
  <span class="label">Target:</span><span class="value"><select id="dfuTargetSelect"><option value="ftps-test">FTPS Test</option><option value="server">TankAlarm Server</option></select></span>
  <span class="label">Status:</span><span class="value" id="dfuMode"></span>
  <span class="label">GitHub Asset:</span><span class="value" id="dfuAvail"></span>
</div>
<div style="margin-top:12px;display:flex;gap:8px">
  <button onclick="dfuCheck()">Check for Updates</button>
  <button class="danger" id="dfuInstallBtn" onclick="dfuInstall()" disabled>Install Selected Firmware</button>
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
<div class="row"><label>Stop After:</label><select id="testStopAfterStep"><option value="begin">begin()</option><option value="connect">connect()</option><option value="mkd-parent">mkd() parent</option><option value="mkd-nested">mkd() nested</option><option value="store">store()</option><option value="size">size()</option><option value="retrieve">retrieve()</option><option value="quit" selected>Full test (quit)</option></select></div>
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
<div class="info-grid">
  <span class="label">Stop After:</span><span class="value" id="testStopAfterLabel">Full test (quit)</span>
  <span class="label">Last Attempted:</span><span class="value" id="testLastAttempted">Not started</span>
  <span class="label">Last Completed:</span><span class="value" id="testLastCompleted">Not started</span>
  <span class="label">Library Phase:</span><span class="value" id="testLastInternal">idle</span>
  <span class="label">Last Error:</span><span class="value" id="testLastError">None</span>
</div>
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
  if(d.selectedTargetStatusText!==undefined){
    const target=document.getElementById('dfuTargetSelect');
    if(target&&d.selectedTarget)target.value=d.selectedTarget;
    document.getElementById('dfuMode').textContent=d.selectedTargetLabel||'Selected firmware';
    const el=document.getElementById('dfuAvail');
    if(d.selectedTargetAvailableVersion){
      el.textContent='v'+d.selectedTargetAvailableVersion;
      if(d.selectedTargetInstallEnabled)el.classList.add('dfu-available');
      else el.classList.remove('dfu-available');
    }else{
      el.textContent=d.selectedTargetAssetNaming||'Waiting for check';
      el.classList.remove('dfu-available');
    }
    document.getElementById('dfuMsg').textContent=d.selectedTargetStatusText||'';
    document.getElementById('dfuInstallBtn').disabled=!d.selectedTargetInstallEnabled;
  }
  if(d.testComplete!==undefined){
    const stopAfter=document.getElementById('testStopAfterStep');
    if(stopAfter&&d.testStopAfterStep)stopAfter.value=d.testStopAfterStep;
    const sum=document.getElementById('testSummary');
    sum.textContent=d.testSummary;
    document.getElementById('testStopAfterLabel').textContent=d.testStopAfterStepLabel||'Full test (quit)';
    document.getElementById('testLastAttempted').textContent=d.lastAttemptedStepLabel||'Not started';
    document.getElementById('testLastCompleted').textContent=d.lastCompletedStepLabel||'Not started';
    document.getElementById('testLastInternal').textContent=d.lastInternalPhase||'idle';
    document.getElementById('testLastError').textContent=d.lastErrorMessage||'None';
    sum.className='summary '+((d.testLastRunInterrupted||d.testsFailed>0)?'fail':(d.testComplete?'pass':'idle'));
  }
}
async function runTest(){
  const stopAfter=document.getElementById('testStopAfterStep');
  document.getElementById('runTestBtn').disabled=true;
  document.getElementById('testSummary').textContent='Running...';
  document.getElementById('testSummary').className='summary idle';
  document.getElementById('testLastError').textContent='None';
  await api('/api/test/run',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({stopAfterStep:stopAfter?stopAfter.value:'quit'})});
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
  const target=document.getElementById('dfuTargetSelect');
  document.getElementById('dfuMsg').textContent='Checking...';
  const d=await api('/api/dfu/check',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({target:target?target.value:'ftps-test'})});
  document.getElementById('dfuMsg').textContent=d.message||d.error||'Done';
  refreshStatus();
}
async function dfuInstall(){
  const target=document.getElementById('dfuTargetSelect');
  const targetLabel=target?target.options[target.selectedIndex].text:'selected firmware';
  if(!confirm('Install '+targetLabel+' now? Device will reboot and run the new firmware.'))return;
  document.getElementById('dfuMsg').textContent='Installing... device will reboot.';
  const d=await api('/api/dfu/enable',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({target:target?target.value:'ftps-test'})});
  if(d&&d.error){
    document.getElementById('dfuMsg').textContent=d.error;
  }
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
  char selectedTargetStatusText[160] = {0};
  bool selectedTargetInstallEnabled = false;
  buildSelectedFirmwareTargetStatus(gSelectedFirmwareTargetState,
                                    selectedTargetStatusText,
                                    sizeof(selectedTargetStatusText),
                                    selectedTargetInstallEnabled);

  JsonDocument doc;
  doc["sketch"] = FTPS_TEST_SKETCH_NAME;
  doc["version"] = FTPS_TEST_VERSION;
  doc["packageVersion"] = FTPS_TEST_PACKAGE_VERSION;
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
  doc["selectedTarget"] = firmwareTargetId(gSelectedFirmwareTargetState.target);
  doc["selectedTargetLabel"] = firmwareTargetLabel(gSelectedFirmwareTargetState.target);
  doc["selectedTargetChecked"] = gSelectedFirmwareTargetState.checked;
  doc["selectedTargetUpdateAvailable"] = gSelectedFirmwareTargetState.updateAvailable;
  doc["selectedTargetAssetAvailable"] = gSelectedFirmwareTargetState.assetAvailable;
  doc["selectedTargetAvailableVersion"] = gSelectedFirmwareTargetState.latestVersion;
  doc["selectedTargetAssetNaming"] = firmwareTargetAssetNamingConvention(gSelectedFirmwareTargetState.target);
  doc["selectedTargetDirectReady"] = isGitHubDirectTargetReady(gSelectedFirmwareTargetState);
  doc["selectedTargetStatusText"] = selectedTargetStatusText;
  doc["selectedTargetInstallEnabled"] = selectedTargetInstallEnabled;
  doc["selectedTargetError"] = gSelectedFirmwareTargetState.error;

  // FTPS test
  doc["testRunning"] = gTestRunning;
  doc["testComplete"] = gTestComplete;
  doc["testsPassed"] = gTestsPassed;
  doc["testsFailed"] = gTestsFailed;
  doc["testSummary"] = gTestSummary;
  doc["testStopAfterStep"] = ftpsTestStepValueId(gTestStopAfterStep);
  doc["testStopAfterStepLabel"] = ftpsTestStepDisplayLabel(gTestStopAfterStep);
  doc["lastAttemptedStep"] = ftpsTestStepValueId(gLastAttemptedFtpsStep);
  doc["lastAttemptedStepLabel"] = ftpsTestStepDisplayLabel(gLastAttemptedFtpsStep);
  doc["lastCompletedStep"] = ftpsTestStepValueId(gLastCompletedFtpsStep);
  doc["lastCompletedStepLabel"] = ftpsTestStepDisplayLabel(gLastCompletedFtpsStep);
  doc["lastInternalPhase"] = gLastFtpsInternalPhase;
  doc["lastErrorCode"] = gLastFtpsErrorCode;
  doc["lastErrorMessage"] = gLastFtpsError;
  doc["testLastRunInterrupted"] = gLastTestRunInterrupted;

  // FTPS config
  doc["ftpsHost"] = gFtpsHost;
  doc["ftpsPort"] = gFtpsPort;
  doc["ftpsUser"] = gFtpsUser;

  char buf[2048];
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

static void handleApiTestRun(EthernetClient &client, const String &body) {
  if (gTestRunning) {
    respondJson(client, "{\"success\":false,\"error\":\"Test already running\"}");
    return;
  }

  if (body.length() > 0) {
    JsonDocument doc;
    if (deserializeJson(doc, body)) {
      respondJson(client, "{\"success\":false,\"error\":\"Invalid JSON\"}");
      return;
    }

    uint8_t stopAfterStep = FTPS_TEST_STEP_QUIT;
    if (!parseFtpsTestStepValue(doc["stopAfterStep"] | "quit", stopAfterStep)) {
      respondJson(client, "{\"success\":false,\"error\":\"Invalid FTPS stopAfterStep\"}");
      return;
    }
    gTestStopAfterStep = stopAfterStep;
  } else {
    gTestStopAfterStep = FTPS_TEST_STEP_QUIT;
  }

  respondJson(client, "{\"success\":true,\"message\":\"Test started\"}");
  client.stop();
  runFtpsTest();
}

static void handleApiDfuCheck(EthernetClient &client, const String &body) {
  JsonDocument doc;
  if (deserializeJson(doc, body)) {
    respondJson(client, "{\"success\":false,\"error\":\"Invalid JSON\"}");
    return;
  }

  uint8_t target = FIRMWARE_TARGET_FTPS_TEST;
  if (!parseFirmwareTargetValue(doc["target"] | "ftps-test", target)) {
    respondJson(client, "{\"success\":false,\"error\":\"Invalid firmware target\"}");
    return;
  }

  gSelectedFirmwareTargetState.target = target;
  if (!checkGitHubReleaseForTarget(target,
                                   FTPS_TEST_PACKAGE_VERSION,
                                   FIRMWARE_TARGET_FTPS_TEST,
                                   gSelectedFirmwareTargetState,
                                   true)) {
    addLog(gSelectedFirmwareTargetState.error[0] != '\0'
               ? gSelectedFirmwareTargetState.error
               : "Selected firmware GitHub check failed",
           "error");
  }

  handleApiDfuStatus(client);
}

static void handleApiDfuEnable(EthernetClient &client, const String &body) {
  JsonDocument doc;
  if (deserializeJson(doc, body)) {
    respondJson(client, "{\"success\":false,\"error\":\"Invalid JSON\"}");
    return;
  }

  uint8_t target = FIRMWARE_TARGET_FTPS_TEST;
  if (!parseFirmwareTargetValue(doc["target"] | firmwareTargetId(gSelectedFirmwareTargetState.target), target)) {
    respondJson(client, "{\"success\":false,\"error\":\"Invalid firmware target\"}");
    return;
  }

  if (gSelectedFirmwareTargetState.target != target ||
      (!gSelectedFirmwareTargetState.checked && gSelectedFirmwareTargetState.error[0] == '\0')) {
    if (!checkGitHubReleaseForTarget(target,
                                     FTPS_TEST_PACKAGE_VERSION,
                                     FIRMWARE_TARGET_FTPS_TEST,
                                     gSelectedFirmwareTargetState,
                                     true)) {
      gSelectedFirmwareTargetState.target = target;
    }
  }

  if (!isGitHubDirectTargetReady(gSelectedFirmwareTargetState) ||
      !gSelectedFirmwareTargetState.updateAvailable) {
    const char *errorMessage = gSelectedFirmwareTargetState.error[0] != '\0'
                                   ? gSelectedFirmwareTargetState.error
                                   : "No installable GitHub firmware asset is ready for the selected target";
    String response = String("{\"success\":false,\"error\":\"") + errorMessage + "\"}";
    respondJson(client, response.c_str());
    return;
  }

  String response = String("{\"success\":true,\"message\":\"") +
                    firmwareTargetLabel(target) +
                    " install starting from GitHub\"}";
  addLog(response.c_str(), "warn");
  respondJson(client, response.c_str());
  client.stop();
  delay(100);

  String installStatus;
  if (!attemptGitHubDirectInstall(installStatus)) {
    addLog(installStatus.c_str(), "error");
  }
}

// DFU status API (compatible with server's /api/dfu/status for tooling)
static void handleApiDfuStatus(EthernetClient &client) {
  char selectedTargetStatusText[160] = {0};
  bool selectedTargetInstallEnabled = false;
  buildSelectedFirmwareTargetStatus(gSelectedFirmwareTargetState,
                                    selectedTargetStatusText,
                                    sizeof(selectedTargetStatusText),
                                    selectedTargetInstallEnabled);

  JsonDocument doc;
  doc["currentVersion"] = FTPS_TEST_VERSION;
  doc["packageVersion"] = FTPS_TEST_PACKAGE_VERSION;
  doc["sketchName"] = FTPS_TEST_SKETCH_NAME;
  doc["buildDate"] = __DATE__;
  doc["buildTime"] = __TIME__;
  doc["updateAvailable"] = gDfuUpdateAvailable;
  doc["availableVersion"] = gDfuVersion;
  doc["availableLength"] = gDfuFirmwareLength;
  doc["dfuMode"] = gDfuMode;
  doc["dfuInProgress"] = gDfuInProgress;
  doc["dfuError"] = gDfuError;
  doc["selectedTarget"] = firmwareTargetId(gSelectedFirmwareTargetState.target);
  doc["selectedTargetLabel"] = firmwareTargetLabel(gSelectedFirmwareTargetState.target);
  doc["selectedTargetChecked"] = gSelectedFirmwareTargetState.checked;
  doc["selectedTargetUpdateAvailable"] = gSelectedFirmwareTargetState.updateAvailable;
  doc["selectedTargetAssetAvailable"] = gSelectedFirmwareTargetState.assetAvailable;
  doc["selectedTargetAvailableVersion"] = gSelectedFirmwareTargetState.latestVersion;
  doc["selectedTargetAssetNaming"] = firmwareTargetAssetNamingConvention(gSelectedFirmwareTargetState.target);
  doc["selectedTargetDirectReady"] = isGitHubDirectTargetReady(gSelectedFirmwareTargetState);
  doc["selectedTargetStatusText"] = selectedTargetStatusText;
  doc["selectedTargetInstallEnabled"] = selectedTargetInstallEnabled;
  doc["selectedTargetError"] = gSelectedFirmwareTargetState.error;

  char buf[1024];
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
    handleApiTestRun(client, body);
  } else if (method == "POST" && path == "/api/dfu/check") {
    handleApiDfuCheck(client, body);
  } else if (method == "POST" && path == "/api/dfu/enable") {
    handleApiDfuEnable(client, body);
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

  loadFtpsTestState();
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
