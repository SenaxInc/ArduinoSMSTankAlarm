# Comprehensive Code Review — February 19, 2026

**Reviewer:** GitHub Copilot (Claude Opus 4.6)  
**Repository:** SenaxInc/ArduinoSMSTankAlarm  
**Branch:** master  
**Scope:** Full audit of active 112025 codebase — Server, Client, Viewer, and Common library  
**Version Reviewed:** 1.1.1  
**Prior Reviews Referenced:** CODE_REVIEW_02192026_COMPREHENSIVE, CODE_REVIEW_02062026, CODE_REVIEW_SUMMARY_02202026

---

## Executive Summary

This review covers the entire active codebase: Server (10,344 lines), Client (5,488 lines), Viewer (687 lines), and the Common shared library (8 header/source files). The system implements a Blues Notecard-based IoT tank monitoring fleet with an Ethernet web dashboard, SMS/email alerting, relay control, solar/battery monitoring, and OTA firmware updates.

### Severity Overview

| Severity | Count | Change from Prior |
|----------|-------|-------------------|
| **Critical** | 3 | +2 new |
| **High** | 5 | +3 new |
| **Medium** | 8 | +5 new |
| **Low** | 7 | +4 new |

### Overall Assessment

The codebase demonstrates mature embedded engineering with strong defensive patterns (bounded string copies via `strlcpy`, body size caps, constant-time PIN comparison, hash table bounds checks, power conservation state machine). However, three categories of issues remain:

1. **A confirmed code-level bug** in the client's `applyConfigUpdate()` that will corrupt configuration
2. **Inconsistent authorization enforcement** on server endpoints (previously flagged, still present)
3. **Architectural patterns** that risk long-term stability on the constrained target (heap fragmentation, monolithic files, dead code)

**Deployment readiness: Functional for trusted LAN environments**, but the client config bug (C1) must be fixed before any OTA config push, and the auth gaps (C2) should be addressed before deployment on shared or untrusted networks.

---

## Review Method

- Full source read of all `.ino` files and Common library headers/sources
- Cross-reference with prior code reviews (Feb 6, Feb 19, Feb 20, 2026)
- Manual inspection of auth flows, memory management, file I/O, Notecard patterns, sensor logic, and embedded HTML/JS
- Verification of critical findings against source line numbers

---

## Findings

---

## 🔴 Critical

### C1. Client `applyConfigUpdate()` — Misplaced Braces Corrupt `serverFleet` Field

**File:** `TankAlarm-112025-Client-BluesOpta.ino`, lines 2247–2253  
**Status:** NEW — Not previously identified

**Evidence:**
```cpp
if (!doc["serverFleet"].isNull()) {
   
if (!doc["clientFleet"].isNull()) {
  strlcpy(gConfig.clientFleet, doc["clientFleet"].as<const char *>(), sizeof(gConfig.clientFleet));
} strlcpy(gConfig.serverFleet, doc["serverFleet"].as<const char *>(), sizeof(gConfig.serverFleet));
}
```

The opening brace for the `serverFleet` `if` block is missing. The `strlcpy` for `serverFleet` executes unconditionally after the `clientFleet` block's closing brace. When the server pushes a config update that does **not** include `"serverFleet"`, `doc["serverFleet"].as<const char *>()` returns `nullptr`, and `strlcpy` from a null pointer is **undefined behavior** (crash or memory corruption on most platforms).

**Impact:** Any config push from the server that omits `serverFleet` will crash or corrupt the client's fleet routing, severing Notecard communication.

**Recommended fix:**
```cpp
if (!doc["serverFleet"].isNull()) {
  strlcpy(gConfig.serverFleet, doc["serverFleet"].as<const char *>(), sizeof(gConfig.serverFleet));
}
if (!doc["clientFleet"].isNull()) {
  strlcpy(gConfig.clientFleet, doc["clientFleet"].as<const char *>(), sizeof(gConfig.clientFleet));
}
```

---

### C2. Missing Server-Side PIN Enforcement on State-Changing Endpoints

**File:** `TankAlarm-112025-Server-BluesOpta.ino`  
**Status:** CONFIRMED — Previously identified in CODE_REVIEW_02192026, still unfixed

**Unauthenticated mutation handlers:**

| Handler | Line | Action |
|---------|------|--------|
| `handleSerialRequestPost()` | ~1791 | Triggers client to send serial debug logs |
| `handleContactsPost()` | ~8672 | Modifies contact list (phone numbers, emails) |
| `handleEmailFormatPost()` | ~8890 | Changes email format settings |
| `handleCalibrationPost()` | ~9897 | Adds calibration data entries |
| `handleCalibrationDelete()` | ~10014 | Deletes/resets tank calibration |
| `handleLocationRequestPost()` | ~10267 | Sends GPS location request to client |

**Impact:** Any device on the LAN can modify alert recipients, reset calibration, request GPS locations, and trigger serial log retrieval without authentication.

**Additionally:** `handleConfigPost()` (line ~5568) validates PIN via `pinMatches()` directly, bypassing the rate-limiting and lockout logic in `requireValidPin()`. This allows unlimited brute-force attempts against the config endpoint specifically.

---

### C3. Viewer Watchdog Is Dead Code — Wrong Macro Name

**File:** `TankAlarm-112025-Viewer-BluesOpta.ino`, lines 195 and 219  
**Status:** NEW

**Evidence:**
```cpp
#ifdef WATCHDOG_AVAILABLE    // ❌ Wrong macro
  mbedWatchdog.start(WATCHDOG_TIMEOUT_SECONDS * 1000);
#endif
```

The shared platform header defines `TANKALARM_WATCHDOG_AVAILABLE`, but the viewer checks for `WATCHDOG_AVAILABLE` which is never defined. All watchdog initialization and kicking code is compiled out. The viewer has **zero watchdog protection** against firmware hangs.

**Impact:** A stuck web request, Notecard I2C hang, or infinite loop will require manual power cycling.

---

## 🟠 High

### H1. No `Ethernet.maintain()` in Viewer — DHCP Lease Will Expire

**File:** `TankAlarm-112025-Viewer-BluesOpta.ino`  
**Status:** NEW

The viewer's `loop()` never calls `Ethernet.maintain()`. The server does (line 2208), but the viewer does not. When the DHCP lease expires (typically 24 hours), the viewer will silently lose its IP address and stop serving the web dashboard.

**Impact:** Viewer becomes unreachable after DHCP lease expiry. Requires power cycle to recover.

**Recommended fix:** Add `Ethernet.maintain();` at the top of the viewer's `loop()`.

---

### H2. `readHttpRequest()` O(n²) Body Building

**File:** `TankAlarm-112025-Server-BluesOpta.ino`, HTTP body parsing  
**Status:** NEW

The server's HTTP body parser appends characters one at a time: `body += c`. For the maximum body size of 16,384 bytes, this causes approximately 134 million character copies due to `String` reallocation on each append. On the Opta, this can block the event loop for seconds.

**Impact:** Large POST requests (config pushes, contact lists) cause multi-second blocking, potentially triggering watchdog near-misses and degrading responsiveness.

**Recommended fix:** Pre-allocate with `body.reserve(contentLength)` before the read loop.

---

### H3. Client Pulse Measurement Blocks Main Loop Up to 60 Seconds

**File:** `TankAlarm-112025-Client-BluesOpta.ino`, `readTankSensor()` near line ~2638  
**Status:** NEW

RPM/pulse sensor reading modes (pulse-counting and time-based) contain blocking loops that wait for pulse edges. The configurable sample window can be up to 60 seconds. During this time, no alarms are evaluated, no relay commands are processed, no inbound config is polled, and no other sensors are sampled.

The watchdog is kicked inside the loop (good), but system responsiveness is severely degraded.

**Impact:** In multi-sensor configurations, a non-spinning engine sensor can block the entire client for 60 seconds per sample cycle, delaying alarm detection on all other tanks.

**Recommended fix:** Implement non-blocking pulse counting using a timer interrupt and accumulator, checked asynchronously during each loop iteration.

---

### H4. No Atomic File Writes — Power Loss Causes Corruption

**File:** Server and Client `.ino` files  
**Status:** Partially noted in prior reviews, still unfixed

File save functions (`saveConfig()`, `saveContactsConfig()`, `saveEmailFormat()`, `saveHistorySettings()`, `saveCalibrationData()`, etc.) open files with `fopen("w")` which truncates immediately. If power is lost during the write, the file is corrupted or empty. Only `saveConfig()` in the server partially handles this by removing a corrupt partial file. All other save functions do not.

**Impact:** Power loss during any configuration or data save loses that file permanently. For `server_config.json`, this means full server reset to defaults.

**Recommended fix:** Write to a `.tmp` file, then `rename()` (atomic on POSIX-compliant filesystems like LittleFS).

---

### H5. Auto-DFU Executes Without User Confirmation

**File:** Server `.ino` `loop()` near line ~2360; Client `.ino` near ~2048  
**Status:** NEW

Both server and client's `loop()` automatically enable DFU mode when `gDfuUpdateAvailable` is true. Any firmware pushed to Notehub will auto-install on the next DFU status check (hourly). A bad firmware push could brick all devices in the fleet simultaneously with no rollback mechanism.

**Impact:** Fleet-wide bricking risk from a bad firmware publish.

**Recommended fix:** Require explicit API trigger or physical button confirmation before enabling DFU. Add a `dfu_auto_enable` config flag (default: false).

---

## 🟡 Medium

### M1. ArduinoJson v6/v7 API Mixing

**File:** Server `.ino`  
**Status:** NEW

The server mixes `DynamicJsonDocument(size)` (ArduinoJson v6 API, requires manual capacity estimation) with `JsonDocument` (v7 API, auto-sizing). Examples:

- `handleContactsPost()`: `DynamicJsonDocument doc(capacity)` — v6
- `handleLocationRequestPost()`: `JsonDocument doc;` — v7  
- `sendTankJson()`: `JsonDocument doc;` — v7
- `loadHistorySettings()`: `DynamicJsonDocument doc(1024)` — v6

**Impact:** v6 `DynamicJsonDocument` with wrong capacity silently truncates data. Inconsistency makes it hard to reason about memory usage.

**Recommended fix:** Standardize on ArduinoJson v7's `JsonDocument` (auto-sizing) throughout.

---

### M2. `activateLocalAlarm()` Maps Tank Index Directly to Relay Pin

**File:** `TankAlarm-112025-Client-BluesOpta.ino`, ~line 3384  
**Status:** NEW

`activateLocalAlarm(idx, ...)` calls `getRelayPin(idx)`, mapping tank array index directly to relay pin via `LED_D0 + relayIndex`. With `MAX_TANKS = 8` but only `MAX_RELAYS = 4`, tanks 4–7 silently fail or access invalid pins. The per-tank `relayMask` configuration exists but is apparently not used by `activateLocalAlarm()`.

**Impact:** Alarm relay activation works only for the first 4 tanks; tanks 5–8 silently don't trigger relay alarms.

---

### M3. Viewer Configuration Is Entirely Hardcoded

**File:** `TankAlarm-112025-Viewer-BluesOpta.ino`  
**Status:** NEW

`VIEWER_CONFIG_PATH` (`"/viewer_config.json"`) is defined at line 55, but **no code reads or writes any config file**. The `gConfig` struct is initialized with compile-time defaults and never updated. Changing the viewer's IP address, MAC, product UID, or display name requires recompilation.

Meanwhile, unused default variables (`gDefaultMacAddress`, `gDefaultStaticIp`, etc.) are defined but never referenced.

---

### M4. Client `config_ack.qo` Declared But Never Sent

**File:** Common header and Client `.ino`  
**Status:** NEW

`CONFIG_ACK_OUTBOX_FILE` and `CONFIG_ACK_INBOX_FILE` are defined in `TankAlarm_Common.h`, and the server has `config_ack.qi` polling logic. However, the client never actually sends a config acknowledgement after applying updates via `applyConfigUpdate()`. The server has no reliable way to confirm config delivery.

**Impact:** The server's config dispatch tracking (`pendingDispatch`, `lastAckStatus`, `lastAckEpoch`) can never reach a confirmed state.

---

### M5. `respondHtml()` Triple-Copies HTML Responses in Server

**File:** `TankAlarm-112025-Server-BluesOpta.ino`, near ~5030  
**Status:** Previously noted in CODE_REVIEW_SUMMARY_02202026 as resolved, but residual pattern remains

For HTML pages under 32KB, the server copies PROGMEM content to a `String`, builds a second `String` with the loading overlay injected, then assigns the result. This transiently requires ~3x the page size in RAM. For a 30KB HTML page, that's ~90KB of heap.

---

### M6. Broad Unauthenticated Read Surface

**File:** `TankAlarm-112025-Server-BluesOpta.ino`  
**Status:** CONFIRMED — Previously flagged, still present

All GET API endpoints are served without authentication:
- `/api/tanks` — tank levels, alarm states
- `/api/contacts` — phone numbers and email addresses
- `/api/clients` — device UIDs, firmware versions, GPS coordinates
- `/api/serial-logs` — debug serial output
- `/api/location` — GPS/cell-tower location data
- `/api/notecard/status` — Notecard status and signal info
- `/api/dfu/status` — firmware version and update availability

**Impact:** Any LAN device can enumerate the entire fleet, read contact PII, and determine physical locations of all tank sites.

---

### M7. Server Static IP/Gateway/DNS Hardcoded

**File:** `TankAlarm-112025-Server-BluesOpta.ino`, near ~840  
**Status:** NEW

`gStaticIp(192.168.1.200)`, `gStaticGateway(192.168.1.1)`, `gStaticDns(8.8.8.8)` are compiled in. They are not part of `ServerConfig` and cannot be changed without reflashing.

---

### M8. Duplicate Sensor Conversion Logic (DRY Violation)

**File:** `TankAlarm-112025-Server-BluesOpta.ino`  
**Status:** NEW

`handleTelemetry()`, `handleAlarm()`, and `handleDaily()` each contain nearly identical ~40-line sensor type conversion blocks (mA→level, voltage→level, etc.). Changes to conversion logic must be replicated in three places.

---

## 🔵 Low

### L1. Default Admin PIN Is "1234"

**File:** Server `.ino`, ~line 830  
**Status:** Previously noted, still present

The default PIN allows trivial access. If a user never changes it (common in field deployments), all authenticated endpoints are essentially open.

**Recommended fix:** Ship with empty PIN and force PIN setup on first web access.

---

### L2. Redundant Local Notefile Defines in Server

**File:** `TankAlarm-112025-Server-BluesOpta.ino`, ~lines 202, 210  
**Status:** CONFIRMED — Previously flagged as L1

Local `#define` placeholders for `SERIAL_REQUEST_FILE` and `LOCATION_REQUEST_FILE` duplicate shared definitions in `TankAlarm_Common.h`.

---

### L3. Viewer Dead Code

**File:** `TankAlarm-112025-Viewer-BluesOpta.ino`  
**Status:** NEW

- `respondHtml()` function is defined but never called (dashboard uses `sendDashboard()` directly from PROGMEM)
- `statusBadge()` JavaScript function is defined but never invoked in `renderTankRows()`
- `gDefaultMacAddress` and `gDefaultStaticIp/Gateway/Dns` variables are defined but unused
- "24hr Change" column in dashboard always displays `--` with no data source

---

### L4. `sendSerialLogs()` Potential J* Memory Leak

**File:** `TankAlarm-112025-Client-BluesOpta.ino`, ~line 5140  
**Status:** NEW

If `JCreateArray()` for `logsArray` fails after `body` and `req` are created, the `req` (from `notecard.newRequest()`) is not cleaned up. The `body` is deleted, but `req` leaks.

---

### L5. Viewer Missing HTTP 405 Responses

**File:** `TankAlarm-112025-Viewer-BluesOpta.ino`  
**Status:** NEW

POST/PUT/DELETE requests to valid paths receive 404 (Not Found) instead of 405 (Method Not Allowed). Minor HTTP compliance issue.

---

### L6. `readNotecardVinVoltage()` Duplicates `pollBatteryVoltage()`

**File:** `TankAlarm-112025-Client-BluesOpta.ino`  
**Status:** NEW

Both functions call `card.voltage`. In `sendDailyReport()`, `readNotecardVinVoltage()` is called even when battery monitoring is enabled, causing a redundant Notecard I2C transaction. Could reuse `gBatteryData.voltage`.

---

### L7. Chart.js CDN Dependency in Historical Data Page

**File:** `TankAlarm-112025-Server-BluesOpta.ino`, embedded `HISTORICAL_DATA_HTML`  
**Status:** NEW

The historical data page loads Chart.js from a CDN (`cdn.jsdelivr.net`), requiring the viewing device to have internet access. In air-gapped LAN deployments, the charts page won't render.

---

## Architecture Notes

### Positive Patterns

1. **Common library design** — Shared constants, notefile names, platform abstractions, and utility functions are cleanly factored into `TankAlarm_Common.h` and sub-headers. The `#ifndef` guard pattern allows per-project overrides.

2. **Power conservation state machine** — The 4-state system (NORMAL → ECO → LOW_POWER → CRITICAL_HIBERNATE) with hysteresis, debounce, and step-wise recovery is well-engineered for solar deployments.

3. **Notecard communication architecture** — The Route Relay pattern (single `command.qo` with `_type` and `_target` metadata) is clean, and the notefile naming conventions are well-documented.

4. **Defensive string handling** — Consistent use of `strlcpy()` throughout with a polyfill for non-Mbed platforms. No `strcpy()` or `sprintf()` without size bounds.

5. **Offline note buffering** — The client's `bufferNoteForRetry()` / `flushBufferedNotes()` / `pruneNoteBufferIfNeeded()` system provides graceful degradation when cellular connectivity is lost.

6. **Sensor validation** — Range checks, stuck-sensor detection (10 consecutive identical readings), and consecutive failure counting with alarm state transitions are robust.

7. **Hash table for tank lookup** — Server's djb2 hash + linear probing with bounds-checked array access is correct and efficient for the 64-record space.

### Areas for Improvement

1. **Monolithic file sizes** — Server (10,344 lines) and Client (5,488 lines) should be refactored. Natural module boundaries: HTTP handlers, Notecard communication, sensor reading, alarm evaluation, relay control, persistence, FTP, NWS weather, calibration math.

2. **No heap monitoring** — Neither the server nor client logs or checks free heap. On a 1MB SRAM device with frequent `malloc`/`free` and `String` operations, fragmentation-related crashes could develop over weeks of continuous operation with no diagnostic telemetry.

3. **No unit test infrastructure** — The calibration math (Cramer's rule regression, R² calculation) and alarm state machine logic are complex enough to warrant offline testing. Consider extracting pure logic into testable C++ files.

4. **CSS cache invalidation** — `serveCss()` sets `Cache-Control: max-age=31536000` (1 year). After a firmware update, browsers will serve stale CSS until their cache expires or is manually cleared.

---

## Summary of Recommendations by Priority

### P0 — Fix Before Next Deployment

| # | Finding | Component | Action |
|---|---------|-----------|--------|
| C1 | `serverFleet` brace bug | Client | Fix misplaced braces in `applyConfigUpdate()` |
| C3 | Watchdog dead code | Viewer | Change `WATCHDOG_AVAILABLE` → `TANKALARM_WATCHDOG_AVAILABLE` |
| H1 | Missing `Ethernet.maintain()` | Viewer | Add to `loop()` |

### P1 — Fix Before Untrusted Network Deployment

| # | Finding | Component | Action |
|---|---------|-----------|--------|
| C2 | Missing auth on endpoints | Server | Add `requireValidPin()` to all mutation handlers; route `handleConfigPost` through `requireValidPin()` |
| M6 | Unauthenticated read surface | Server | Add optional auth gate for sensitive GET endpoints |
| L1 | Default PIN "1234" | Server | Ship with empty PIN, force first-time setup |

### P2 — Reliability Improvements

| # | Finding | Component | Action |
|---|---------|-----------|--------|
| H2 | O(n²) HTTP body parsing | Server | `body.reserve(contentLength)` |
| H3 | Blocking pulse measurement | Client | Non-blocking ISR-based pulse counting |
| H4 | Non-atomic file writes | Server/Client | Write-to-tmp + rename pattern |
| H5 | Auto-DFU without confirmation | Server/Client | Config flag to require explicit trigger |
| M5 | Triple-copy HTML responses | Server | Stream directly or single-copy injection |

### P3 — Code Quality

| # | Finding | Component | Action |
|---|---------|-----------|--------|
| M1 | ArduinoJson v6/v7 mixing | Server | Standardize on v7 `JsonDocument` |
| M2 | Tank-to-relay index mapping | Client | Use `relayMask` instead of direct index |
| M3 | Hardcoded viewer config | Viewer | Implement config file loader or document as intentional |
| M4 | Config ACK never sent | Client | Implement `config_ack.qo` send after `applyConfigUpdate()` |
| M7 | Hardcoded static IP | Server | Move to `ServerConfig` |
| M8 | Duplicate conversion logic | Server | Extract to shared function |
| L2–L7 | Dead code, leaks, minor | Various | Cleanup pass |

---

## Comparison with Prior Reviews

| Prior Finding | Status |
|---------------|--------|
| C1 (Feb 19): Missing PIN on endpoints | **Still open** — confirmed in 6 handlers |
| H1 (Feb 19): PIN in localStorage | **Still open** — no session token mechanism |
| H2 (Feb 19): Rate-limit bypass via `pinMatches()` | **Still open** — `handleConfigPost` bypasses `requireValidPin()` |
| (Feb 6): Out-of-bounds hash table | **Fixed** — bounds check present |
| (Feb 6): Timing attack PIN comparison | **Fixed** — constant-time `pinMatches()` in place |
| (Feb 6): No rate limiting on auth | **Fixed** — `requireValidPin()` has exponential backoff + lockout |
| (Feb 20): Heap fragmentation in HTTP responses | **Partially fixed** — chunked sending implemented, but triple-copy pattern in `respondHtml()` remains |
| (Feb 20): Filesystem init hard-halts | **Fixed** — graceful degradation implemented |
| (Feb 20): Ethernet init hard-halts | **Fixed** — graceful degradation implemented |

---

*End of review. Next recommended review cycle: After C1 fix is deployed and auth hardening is complete.*
