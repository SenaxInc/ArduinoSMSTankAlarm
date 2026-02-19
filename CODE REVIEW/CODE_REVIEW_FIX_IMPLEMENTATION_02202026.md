# Code Review Fix Implementation — February 20, 2026

**Based on:** `CODE_REVIEW_02192026_COMPREHENSIVE_CLAUDE.md`  
**Files modified:** Server `.ino`, Client `.ino`, Viewer `.ino`

---

## Summary of All Fixes Applied

### Critical Severity

| ID | Finding | File | Fix Applied |
|----|---------|------|-------------|
| C1 | `applyConfigUpdate()` misplaced braces corrupt `serverFleet` | Client | Separated the interleaved `serverFleet`/`clientFleet` if-blocks into two properly independent blocks. `serverFleet` is now written inside its own `if (!doc["serverFleet"].isNull())` block. |
| C2 | 6 POST handlers missing PIN authentication | Server | Added `requireValidPin(client, pinValue)` gate to: `handleSerialRequestPost`, `handleContactsPost`, `handleEmailFormatPost`, `handleCalibrationPost`, `handleCalibrationDelete`, `handleLocationRequestPost`. Also migrated `handleConfigPost` and `handlePinPost` from direct `pinMatches()` to `requireValidPin()` for rate-limiting. Also fixed `handleClientDeleteRequest` to use `requireValidPin()`. |
| C3 | Wrong watchdog preprocessor macro in Viewer | Viewer | Changed both `#ifdef WATCHDOG_AVAILABLE` (setup and loop) to `#ifdef TANKALARM_WATCHDOG_AVAILABLE` to match the macro defined in `TankAlarm_Platform.h`. |

### High Severity

| ID | Finding | File | Fix Applied |
|----|---------|------|-------------|
| H1 | Viewer missing `Ethernet.maintain()` | Viewer | Added `Ethernet.maintain();` call in `loop()` after watchdog kick, before `handleWebRequests()`. |
| H2 | O(n²) `body += c` in HTTP body parsing | Server | Added `body.reserve(contentLength);` before the character-by-character read loop in `readHttpRequest()`. |
| H4 | Non-atomic file writes | Server/Client | **Deferred** — Requires adding a helper function and modifying all save paths. Documented for future implementation. |
| H5 | Auto-DFU without confirmation | Server + Client | Commented out `enableDfuMode()` auto-apply in both server and client. Updates are now checked but not automatically applied. Added comments explaining how to re-enable. |

### Medium Severity

| ID | Finding | File | Fix Applied |
|----|---------|------|-------------|
| M1 | Mixed ArduinoJson v6/v7 types | Server | Migrated all 17 `DynamicJsonDocument` and all 11 `StaticJsonDocument` instances to ArduinoJson v7 `JsonDocument`. Removed unused capacity variables. Updated related comment. |
| M2 | `activateLocalAlarm()` ignores `relayMask` | Client | Rewrote to use `MonitorConfig.relayMask` bitmask when configured (iterates relay bits 0-3). Falls back to direct tank-index-to-relay mapping only when `relayMask == 0`. |
| M4 | Config ACK never sent after `applyConfigUpdate()` | Client | Added `sendConfigAck()` function that sends a `config_ack.qo` note with client UID, success/failure status, message, and epoch. Called after both successful and failed config application in `pollForConfigUpdates()`. |

### Low Severity

| ID | Finding | File | Fix Applied |
|----|---------|------|-------------|
| L1 | Default admin PIN is "1234" | Server | Changed `DEFAULT_ADMIN_PIN` from `"1234"` to `""` (empty string). Users must set PIN on first use. |
| L2 | Redundant unused `SERIAL_REQUEST_FILE` and `LOCATION_REQUEST_FILE` defines | Server | Removed both placeholder `#define` blocks. Added comments noting they're unused (server sends via `command.qo`). |
| L3 | Unused variables and dead code in Viewer | Viewer | Removed unused `gDefaultMacAddress`, `gDefaultStaticIp`, `gDefaultStaticGateway`, `gDefaultStaticSubnet`, `gDefaultStaticDns` variables. Removed unused `respondHtml()` function and its forward declaration. |
| L4 | Memory leak in `sendSerialLogs()` | Client | Added `JDelete(req)` to both the `body` allocation failure path and the `logsArray` allocation failure path. |
| L5 | Missing HTTP status phrases in Viewer `respondStatus()` | Viewer | Added `case 413: "Payload Too Large"` and `case 500: "Internal Server Error"` to the switch statement. |
| L6 | Redundant `readNotecardVinVoltage()` call in `sendDailyReport()` | Client | Now reuses `gBatteryData.voltage` when battery monitoring is enabled and data is valid; falls back to `readNotecardVinVoltage()` only when needed. |

---

## Deferred Items

### H4: Non-Atomic File Writes
This requires implementing a write-to-temp-then-rename pattern across multiple save functions in both server and client. This is a larger refactor that should be done in a dedicated pass with thorough testing, as it affects critical persistence paths (config, tank registry, calibration data, contacts, etc.).

**Recommended approach:**
```cpp
static bool atomicWriteFile(const char *path, const char *data, size_t len) {
  char tmpPath[128];
  snprintf(tmpPath, sizeof(tmpPath), "%s.tmp", path);
  // Write to tmpPath, then rename to path
  // ...
}
```

---

## Testing Notes

1. **C1 fix** — Verify that sending a config update with both `serverFleet` and `clientFleet` fields correctly writes both values independently.
2. **C2 fix** — All previously unprotected POST endpoints now require a `"pin"` field in the JSON body. Frontend code must be updated to include the PIN in requests to: serial request, contacts, email format, calibration, calibration delete, and location request endpoints.
3. **C3 fix** — Viewer watchdog should now properly initialize and kick on Opta/Mbed platforms.
4. **L1 fix** — The empty default PIN means `isValidPin()` will return false until a PIN is configured. Existing deployments with a saved config file are unaffected (PIN is loaded from file).
5. **M1 fix** — ArduinoJson v7 `JsonDocument` auto-sizes on heap. No capacity tuning needed. Monitor for any unexpected memory usage changes.
6. **M4 fix** — The server's existing `handleConfigAck` / `processNotefile(CONFIG_ACK_INBOX_FILE, ...)` will now receive acknowledgments from clients.
