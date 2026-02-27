/*
  TankAlarm 112025 - I2C / Notecard Utility

  Purpose:
  - Shared utility for Client, Server, and Viewer devices
  - Diagnose I2C communication with Blues Notecard
  - Recover common Notecard configuration issues safely

  Board: Arduino Opta family
  Serial: 115200
*/

#include <Wire.h>
#include <Notecard.h>
#include <TankAlarm_Common.h>

// -----------------------------
// User-editable defaults
// -----------------------------
#ifndef DEFAULT_NOTECARD_ADDRESS
#define DEFAULT_NOTECARD_ADDRESS 0x17
#endif

#ifndef SERIAL_BAUD
#define SERIAL_BAUD 115200
#endif

// Optional provisioning values for hub.set
// Leave empty if you only want diagnostics.
static const char PRODUCT_UID[] = "";
static const char DEVICE_FLEET[] = "";
static const char HUB_MODE[] = "continuous";  // continuous or periodic

// -----------------------------
// Globals
// -----------------------------
static Notecard notecard;
static uint8_t gAttachedAddress = DEFAULT_NOTECARD_ADDRESS;
static bool gAttached = false;

// Required by TankAlarm_I2C.h (extern-declared there)
uint32_t gCurrentLoopI2cErrors = 0;
uint32_t gI2cBusRecoveryCount = 0;

// -----------------------------
// Forward declarations
// -----------------------------
static void printBanner();
static void printMenu();
static void scanI2CBus();
static bool i2cAck(uint8_t address);
static bool attachNotecard(uint8_t address);
static bool autoAttachNotecard();
static bool runDiagnostics();
static bool requestAndPrint(const char *reqName);
static bool runHubSet();
static bool runHubSync();
static bool resetI2CAddressToDefault();
static bool runCardRestore();
static bool clearOutboundNotes();
static void printStatus();
static bool readLine(char *buffer, size_t bufferSize, unsigned long timeoutMs);
static bool parseHexAddress(const char *text, uint8_t &addressOut);
static void safeSleep(unsigned long ms);
static uint32_t freeRam();

static void safeSleep(unsigned long ms) {
  delay(ms);
}

static uint32_t freeRam() { return tankalarm_freeRam(); }

void setup() {
  Serial.begin(SERIAL_BAUD);
  while (!Serial && millis() < 3000) {
    safeSleep(10);
  }

  Wire.begin();
  safeSleep(50);

  printBanner();
  scanI2CBus();

  if (!attachNotecard(DEFAULT_NOTECARD_ADDRESS)) {
    Serial.println(F("Initial attach at 0x17 failed. Use 'a' to auto-detect or 'n' for manual address."));
  }

  tankalarm_printHeapStats();

  printMenu();
}

void loop() {
  if (!Serial.available()) {
    safeSleep(10);
    return;
  }

  char cmd = (char)Serial.read();
  if (cmd == '\r' || cmd == '\n') {
    return;
  }

  switch (cmd) {
    case 'h':
    case '?':
      printMenu();
      break;

    case 's':
      scanI2CBus();
      break;

    case 'p':
      printStatus();
      break;

    case 'a':
      autoAttachNotecard();
      break;

    case 'n': {
      Serial.println(F("Enter I2C address in hex (examples: 17, 0x17, 1A):"));
      char line[32] = {0};
      if (!readLine(line, sizeof(line), 60000UL)) {
        Serial.println(F("Timed out waiting for address input."));
        break;
      }

      uint8_t addr = 0;
      if (!parseHexAddress(line, addr)) {
        Serial.println(F("Invalid address format."));
        break;
      }

      if (addr < 0x08 || addr > 0x77) {
        Serial.println(F("Address out of valid 7-bit I2C range (0x08-0x77)."));
        break;
      }

      attachNotecard(addr);
      break;
    }

    case 'd':
      runDiagnostics();
      break;

    case 'u':
      runHubSet();
      break;

    case 'y':
      runHubSync();
      break;

    case 'r':
      resetI2CAddressToDefault();
      break;

    case 'c':
      clearOutboundNotes();
      break;

    case 'x':
      runCardRestore();
      break;

    default:
      Serial.print(F("Unknown command: "));
      Serial.println(cmd);
      printMenu();
      break;
  }
}

static void printBanner() {
  Serial.println();
  Serial.println(F("=============================================="));
  Serial.println(F(" TankAlarm-112025 I2C / Notecard Utility"));
  Serial.println(F("=============================================="));
  Serial.print(F("Build: "));
  Serial.print(__DATE__);
  Serial.print(F(" "));
  Serial.println(__TIME__);
}

static void printMenu() {
  Serial.println();
  Serial.println(F("Commands:"));
  Serial.println(F("  h or ?  - Show this menu"));
  Serial.println(F("  s       - Scan I2C bus"));
  Serial.println(F("  p       - Print attach/status"));
  Serial.println(F("  a       - Auto-detect and attach Notecard"));
  Serial.println(F("  n       - Attach using manual I2C address"));
  Serial.println(F("  d       - Run Notecard diagnostics (hub.get, card.version, card.wireless)"));
  Serial.println(F("  u       - Send hub.set using PRODUCT_UID / DEVICE_FLEET / HUB_MODE"));
  Serial.println(F("  y       - Send hub.sync"));
  Serial.println(F("  c       - Clear all pending outbound notes from Notecard"));
  Serial.println(F("  r       - Reset Notecard I2C address to default (card.io i2c:-1)"));
  Serial.println(F("  x       - Factory restore Notecard (card.restore delete:true) [DESTRUCTIVE]"));
  Serial.println();
}

static bool i2cAck(uint8_t address) {
  Wire.beginTransmission(address);
  int result = Wire.endTransmission();
  return (result == 0);
}

static void scanI2CBus() {
  Serial.println();
  Serial.println(F("Scanning I2C bus..."));

  // Use shared scan for known TankAlarm devices
  const uint8_t expectedAddrs[] = { 0x17 };  // Notecard default
  const char *expectedNames[] = { "Notecard" };
  tankalarm_scanI2CBus(expectedAddrs, expectedNames, 1);
}

static bool attachNotecard(uint8_t address) {
  Serial.print(F("Attaching Notecard at 0x"));
  if (address < 0x10) {
    Serial.print('0');
  }
  Serial.println(address, HEX);

  if (!i2cAck(address)) {
    Serial.println(F("  No I2C ACK at that address."));
    gAttached = false;
    return false;
  }

  notecard.begin(address);
  gAttachedAddress = address;
  gAttached = true;

  J *req = notecard.newRequest("card.version");
  if (!req) {
    Serial.println(F("  Failed to allocate card.version request."));
    gAttached = false;
    return false;
  }

  J *rsp = notecard.requestAndResponse(req);
  if (!rsp) {
    Serial.println(F("  Notecard request failed (null response)."));
    gAttached = false;
    return false;
  }

  const char *err = JGetString(rsp, "err");
  if (err && err[0] != '\0') {
    Serial.print(F("  card.version error: "));
    Serial.println(err);
    notecard.deleteResponse(rsp);
    gAttached = false;
    return false;
  }

  const char *version = JGetString(rsp, "version");
  Serial.print(F("  Attached. Notecard version: "));
  Serial.println((version && version[0] != '\0') ? version : "(unknown)");
  notecard.deleteResponse(rsp);
  return true;
}

static bool autoAttachNotecard() {
  Serial.println(F("Auto-detecting Notecard..."));

  // Try default first
  if (attachNotecard(DEFAULT_NOTECARD_ADDRESS)) {
    return true;
  }

  // Try any ACKing address on the bus (supports alternate/custom setups)
  for (uint8_t addr = 0x08; addr <= 0x77; ++addr) {
    if (addr == DEFAULT_NOTECARD_ADDRESS) {
      continue;
    }

    if (!i2cAck(addr)) {
      continue;
    }

    Serial.print(F("  Candidate ACK at 0x"));
    if (addr < 0x10) {
      Serial.print('0');
    }
    Serial.println(addr, HEX);

    if (attachNotecard(addr)) {
      Serial.println(F("Auto-detect success."));
      return true;
    }
  }

  Serial.println(F("Auto-detect failed. No responsive Notecard found."));
  return false;
}

static bool requestAndPrint(const char *reqName) {
  if (!gAttached) {
    Serial.println(F("Notecard is not attached. Use 'a' or 'n' first."));
    return false;
  }

  J *req = notecard.newRequest(reqName);
  if (!req) {
    Serial.print(F("newRequest failed for "));
    Serial.println(reqName);
    return false;
  }

  J *rsp = notecard.requestAndResponse(req);
  if (!rsp) {
    Serial.print(reqName);
    Serial.println(F(": null response (I2C failure?)"));
    return false;
  }

  char *json = JPrintUnformatted(rsp);
  if (json) {
    Serial.print(reqName);
    Serial.print(F(": "));
    Serial.println(json);
    JFree(json);
  } else {
    Serial.print(reqName);
    Serial.println(F(": response received (failed to print JSON)"));
  }

  notecard.deleteResponse(rsp);
  return true;
}

static bool runDiagnostics() {
  Serial.println();
  Serial.println(F("Running diagnostics..."));

  bool ok = true;
  ok = requestAndPrint("hub.get") && ok;
  ok = requestAndPrint("card.version") && ok;
  ok = requestAndPrint("card.wireless") && ok;

  if (ok) {
    Serial.println(F("Diagnostics complete: PASS"));
  } else {
    Serial.println(F("Diagnostics complete: FAIL"));
  }

  return ok;
}

static bool runHubSet() {
  if (!gAttached) {
    Serial.println(F("Notecard is not attached. Use 'a' or 'n' first."));
    return false;
  }

  if (PRODUCT_UID[0] == '\0') {
    Serial.println(F("PRODUCT_UID is empty. Edit this sketch before using 'u'."));
    return false;
  }

  J *req = notecard.newRequest("hub.set");
  if (!req) {
    Serial.println(F("Failed to create hub.set request."));
    return false;
  }

  JAddStringToObject(req, "product", PRODUCT_UID);
  JAddStringToObject(req, "mode", HUB_MODE);
  if (DEVICE_FLEET[0] != '\0') {
    JAddStringToObject(req, "fleet", DEVICE_FLEET);
  }

  J *rsp = notecard.requestAndResponse(req);
  if (!rsp) {
    Serial.println(F("hub.set returned null response (I2C failure?)"));
    return false;
  }

  const char *err = JGetString(rsp, "err");
  if (err && err[0] != '\0') {
    Serial.print(F("hub.set error: "));
    Serial.println(err);
    notecard.deleteResponse(rsp);
    return false;
  }

  Serial.println(F("hub.set success."));
  notecard.deleteResponse(rsp);
  return true;
}

static bool runHubSync() {
  if (!gAttached) {
    Serial.println(F("Notecard is not attached. Use 'a' or 'n' first."));
    return false;
  }

  J *req = notecard.newRequest("hub.sync");
  if (!req) {
    Serial.println(F("Failed to create hub.sync request."));
    return false;
  }

  J *rsp = notecard.requestAndResponse(req);
  if (!rsp) {
    Serial.println(F("hub.sync returned null response (I2C failure?)"));
    return false;
  }

  const char *err = JGetString(rsp, "err");
  if (err && err[0] != '\0') {
    Serial.print(F("hub.sync error: "));
    Serial.println(err);
    notecard.deleteResponse(rsp);
    return false;
  }

  Serial.println(F("hub.sync success."));
  notecard.deleteResponse(rsp);
  return true;
}

static bool resetI2CAddressToDefault() {
  if (!gAttached) {
    Serial.println(F("Notecard is not attached. Use 'a' or 'n' first."));
    return false;
  }

  Serial.println(F("Resetting Notecard I2C address to default using card.io {\"i2c\":-1} ..."));

  J *req = notecard.newRequest("card.io");
  if (!req) {
    Serial.println(F("Failed to create card.io request."));
    return false;
  }

  JAddIntToObject(req, "i2c", -1);

  J *rsp = notecard.requestAndResponse(req);
  if (!rsp) {
    Serial.println(F("card.io returned null response (I2C failure?)"));
    return false;
  }

  const char *err = JGetString(rsp, "err");
  if (err && err[0] != '\0') {
    Serial.print(F("card.io error: "));
    Serial.println(err);
    notecard.deleteResponse(rsp);
    return false;
  }

  notecard.deleteResponse(rsp);
  Serial.println(F("card.io succeeded. Re-attaching at default address 0x17..."));
  safeSleep(200);
  return attachNotecard(DEFAULT_NOTECARD_ADDRESS);
}

static bool clearOutboundNotes() {
  if (!gAttached) {
    Serial.println(F("Notecard is not attached. Use 'a' or 'n' first."));
    return false;
  }

  Serial.println();
  Serial.println(F("Clearing all outbound (.qo) notefiles..."));

  // All known TankAlarm outbound notefiles (from TankAlarm_Common.h)
  static const char * const qoFiles[] = {
    TELEMETRY_OUTBOX_FILE,          // "telemetry.qo"
    ALARM_OUTBOX_FILE,              // "alarm.qo"
    DAILY_OUTBOX_FILE,              // "daily.qo"
    UNLOAD_OUTBOX_FILE,             // "unload.qo"
    CONFIG_ACK_OUTBOX_FILE,         // "config_ack.qo"
    COMMAND_OUTBOX_FILE,            // "command.qo"
    RELAY_FORWARD_OUTBOX_FILE,      // "relay_forward.qo"
    SERIAL_LOG_OUTBOX_FILE,         // "serial_log.qo"
    SERIAL_ACK_OUTBOX_FILE,         // "serial_ack.qo"
    LOCATION_RESPONSE_OUTBOX_FILE,  // "location_response.qo"
    VIEWER_SUMMARY_OUTBOX_FILE,     // "viewer_summary.qo"
    HEALTH_OUTBOX_FILE,             // "health.qo"
    DIAG_OUTBOX_FILE,               // "diag.qo"
  };
  static const size_t qoCount = sizeof(qoFiles) / sizeof(qoFiles[0]);

  uint32_t totalDeleted = 0;
  bool anyError = false;

  for (size_t f = 0; f < qoCount; ++f) {
    uint32_t fileDeleted = 0;

    // Drain all notes from this notefile using note.get with delete:true.
    // When the file is empty the Notecard returns an error string
    // (e.g. "note does not exist"), which signals us to move on.
    while (true) {
      J *getReq = notecard.newRequest("note.get");
      if (!getReq) {
        anyError = true;
        break;
      }

      JAddStringToObject(getReq, "file", qoFiles[f]);
      JAddBoolToObject(getReq, "delete", true);

      J *getRsp = notecard.requestAndResponse(getReq);
      if (!getRsp) {
        anyError = true;
        break;
      }

      const char *getErr = JGetString(getRsp, "err");
      if (getErr && getErr[0] != '\0') {
        // Notefile empty or doesn't exist — not a real error
        notecard.deleteResponse(getRsp);
        break;
      }

      notecard.deleteResponse(getRsp);
      ++fileDeleted;
    }

    if (fileDeleted > 0) {
      Serial.print(F("  "));
      Serial.print(qoFiles[f]);
      Serial.print(F(": "));
      Serial.print(fileDeleted);
      Serial.println(F(" note(s) deleted"));
      totalDeleted += fileDeleted;
    }
  }

  if (totalDeleted == 0) {
    Serial.println(F("No pending outbound notes found."));
  } else {
    Serial.print(F("Clear complete. Total notes deleted: "));
    Serial.println(totalDeleted);
  }
  if (anyError) {
    Serial.println(F("WARNING: Some I2C/alloc errors occurred during clearing."));
  }
  return !anyError;
}

static bool runCardRestore() {
  if (!gAttached) {
    Serial.println(F("Notecard is not attached. Use 'a' or 'n' first."));
    return false;
  }

  Serial.println(F("WARNING: card.restore is destructive."));
  Serial.println(F("Type RESTORE to confirm:"));

  char line[32] = {0};
  if (!readLine(line, sizeof(line), 60000UL)) {
    Serial.println(F("Timed out waiting for confirmation."));
    return false;
  }

  if (strcmp(line, "RESTORE") != 0) {
    Serial.println(F("Restore cancelled."));
    return false;
  }

  J *req = notecard.newRequest("card.restore");
  if (!req) {
    Serial.println(F("Failed to create card.restore request."));
    return false;
  }

  JAddBoolToObject(req, "delete", true);

  J *rsp = notecard.requestAndResponse(req);
  if (!rsp) {
    Serial.println(F("card.restore returned null response (device may be rebooting)."));
    return false;
  }

  const char *err = JGetString(rsp, "err");
  if (err && err[0] != '\0') {
    Serial.print(F("card.restore error: "));
    Serial.println(err);
    notecard.deleteResponse(rsp);
    return false;
  }

  notecard.deleteResponse(rsp);
  Serial.println(F("card.restore sent successfully. Wait for reboot, then run 'a'."));
  gAttached = false;
  return true;
}

static void printStatus() {
  Serial.println();
  Serial.println(F("Status:"));
  Serial.print(F("  Attached: "));
  Serial.println(gAttached ? F("yes") : F("no"));
  Serial.print(F("  Address: 0x"));
  if (gAttachedAddress < 0x10) {
    Serial.print('0');
  }
  Serial.println(gAttachedAddress, HEX);
  Serial.print(F("  I2C ACK at address: "));
  Serial.println(i2cAck(gAttachedAddress) ? F("yes") : F("no"));
}

static bool readLine(char *buffer, size_t bufferSize, unsigned long timeoutMs) {
  if (!buffer || bufferSize == 0) {
    return false;
  }

  unsigned long start = millis();
  size_t index = 0;

  while (millis() - start < timeoutMs) {
    while (Serial.available()) {
      char ch = (char)Serial.read();

      if (ch == '\r') {
        continue;
      }

      if (ch == '\n') {
        buffer[index] = '\0';
        return index > 0;
      }

      if (index + 1 < bufferSize) {
        buffer[index++] = ch;
      }
    }
    safeSleep(5);
  }

  buffer[0] = '\0';
  return false;
}

static bool parseHexAddress(const char *text, uint8_t &addressOut) {
  if (!text || text[0] == '\0') {
    return false;
  }

  // strtoul with base 16 handles optional 0x prefix
  char *endPtr = nullptr;
  unsigned long value = strtoul(text, &endPtr, 16);
  if (endPtr == text || *endPtr != '\0' || value > 0x7F) {
    return false;
  }

  addressOut = (uint8_t)value;
  return true;
}
