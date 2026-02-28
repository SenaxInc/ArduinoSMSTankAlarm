# Comprehensive Code Review — June 7, 2025

**Firmware Version:** 1.1.3 (Client) / 1.1.2 (Server, Viewer)  
**Reviewer:** GitHub Copilot (Claude Opus 4.6)  
**Scope:** Full codebase — Common library (10 files), Client (7,374 lines), Server (12,224 lines), Viewer (893 lines), I2C Utility (659 lines)

---

## Table of Contents

1. [Executive Summary](#1-executive-summary)
2. [Architecture Overview](#2-architecture-overview)
3. [Bugs — Critical](#3-bugs--critical)
4. [Bugs — Moderate](#4-bugs--moderate)
5. [Bugs — Low](#5-bugs--low)
6. [Memory & Performance Concerns](#6-memory--performance-concerns)
7. [Common Library Review](#7-common-library-review)
8. [Client Firmware Review](#8-client-firmware-review)
9. [Server Firmware Review](#9-server-firmware-review)
10. [Viewer Firmware Review](#10-viewer-firmware-review)
11. [I2C Utility Review](#11-i2c-utility-review)
12. [Security Observations](#12-security-observations)
13. [Code Quality & Maintainability](#13-code-quality--maintainability)
14. [Fixes Applied During This Review](#14-fixes-applied-during-this-review)
15. [Recommendations — Prioritized](#15-recommendations--prioritized)

---

## 1. Executive Summary

This is a **mature, industrial-grade IoT tank monitoring system** built on Arduino Opta + Blues Wireless Notecard. The codebase demonstrates deep domain expertise in embedded systems, power management, and cellular IoT communications.

**Overall Assessment: Strong — with targeted fixes needed.**

| Category | Rating | Notes |
|----------|--------|-------|
| Architecture | ★★★★☆ | Clean separation of concerns; monolithic files are the main weakness |
| Error Handling | ★★★★★ | Exceptional — every Notecard API call checked, watchdog integration, I2C recovery |
| Power Management | ★★★★★ | 4-state conservation machine with hysteresis, solar-only mode, sunset protocol |
| Data Integrity | ★★★★½ | Atomic writes, dirty flags, note buffering; minor ring buffer issue in server |
| Memory Safety | ★★★★☆ | strlcpy throughout; 2 critical bugs (dangling pointer, wrong index), String usage in server |
| Maintainability | ★★★☆☆ | 12K-line server and 7K-line client in single files; significant code duplication |
| Security | ★★★☆☆ | Acceptable for LAN-only; FTP plaintext credentials, 4-digit PIN |
| Test Coverage | ★★☆☆☆ | No automated test suite; relies on field testing |

**Critical bugs found: 2** (both fixed: relay deactivation §14.1, dangling pointer §14.2)  
**Moderate bugs found: 5** (all fixed: §14.3-§14.8; §4.3 was false positive per §14.9)  
**Low-severity issues: 4+**

---

## 2. Architecture Overview

```
                    ┌─────────────┐
                    │  Notehub    │
                    │  (Cloud)    │
                    └──────┬──────┘
                           │ Routes
          ┌────────────────┼────────────────┐
          ▼                ▼                ▼
   ┌─────────────┐  ┌───────────┐  ┌──────────────┐
   │  Client(s)  │  │  Server   │  │   Viewer     │
   │  (Sensors)  │  │  (Hub)    │  │  (Kiosk)     │
   │  Opta+NC+   │  │ Opta+NC+  │  │  Opta+NC+   │
   │  A0602+RS485│  │ Ethernet  │  │  Ethernet    │
   └─────────────┘  └───────────┘  └──────────────┘
```

**Communication Protocol:** Blues Notecard via .qo/.qi notefile pairs. 17 notefile types defined in TankAlarm_Common.h, covering telemetry, alarms, config, relay commands, serial logs, location, DFU, diagnostics, and viewer summaries.

**Fleet Routing:** Client → Notehub Route → Server → Notehub Route → Viewer. Cross-device delivery via Notehub Routes — no device: prefix needed in note.add calls. ServerToClientRelay, ClientToServerRelay, ServerToViewerRelay routes documented in common header.

---

## 3. Bugs — Critical

### 3.1 Relay Deactivation Uses Tank Index Instead of Relay Index

**File:** Client sketch  
**Location:** `sendAlarm()` — relay deactivation on alarm clear  
**Severity:** Critical — relays may not deactivate properly

When an alarm clears, the code clears relay state using the tank index instead of iterating the relay bitmask:

```cpp
// BUG: Uses tank index (idx), not relay indexes from mask
gRelayActivationTime[idx] = 0;
gRelayActiveForTank[idx] = false;
gRelayActiveMaskForTank[idx] = 0;
```

The activation path correctly iterates the bitmask:
```cpp
for (uint8_t r = 0; r < MAX_RELAYS; r++) {
    if (cfg.relayMask & (1 << r)) {
        setRelayState(r + 1, true);
        gRelayActivationTime[r] = millis();
    }
}
```

**Impact:** If tank B (idx=1) has relay mask 0x05 (relays 1 and 3), clearing the alarm resets `gRelayActivationTime[1]` (relay 2's timer) instead of relays 1 and 3. Relay momentary timeouts won't trigger correctly.

**Fix:**
```cpp
for (uint8_t r = 0; r < MAX_RELAYS; r++) {
    if (gRelayActiveMaskForTank[idx] & (1 << r)) {
        gRelayActivationTime[r] = 0;
    }
}
gRelayActiveForTank[idx] = false;
gRelayActiveMaskForTank[idx] = 0;
```

---

### 3.2 Dangling Pointer in Battery Voltage Monitoring

**File:** Client sketch  
**Location:** `pollBatteryVoltage()`  
**Severity:** Critical — undefined behavior on subsequent access

```cpp
data.mode = JGetString(rsp, "mode");  // pointer into rsp's memory
// ...later...
notecard.deleteResponse(rsp);  // memory freed — data.mode is now a dangling pointer
```

If `BatteryData::mode` is a `const char*`, any subsequent access (e.g., in health telemetry or daily report) reads freed memory.

**Fix:** Copy the string into a fixed buffer:
```cpp
const char *modeStr = JGetString(rsp, "mode");
strlcpy(data.modeStr, modeStr ? modeStr : "", sizeof(data.modeStr));
```

---

## 4. Bugs — Moderate

### 4.1 Dead Code: Battery Failure Time-Based Decay

**File:** Client sketch, `updatePowerState()`

The 24-hour decay logic for `gSolarOnlyBatFailCount` is unreachable:
```cpp
if (gSolarOnlyBatFailCount > 0) {
    gSolarOnlyBatFailCount = 0;           // Unconditionally resets to 0
}
// Further down — can never be true:
if (gSolarOnlyBatFailCount > 0 && ...) { // Dead code
    gSolarOnlyBatFailCount = 0;
}
```

The `SOLAR_BAT_FAIL_DECAY_MS` constant and its time-based decay logic have no effect. The counter immediately resets when not in CRITICAL state, rather than gradually decaying over 24 hours.

**Fix:** Replace the unconditional reset with time-based decay:
```cpp
if (gSolarOnlyBatFailCount > 0 &&
    (now - gSolarOnlyBatFailLastIncrMillis) > SOLAR_BAT_FAIL_DECAY_MS) {
    gSolarOnlyBatFailCount = 0;
}
```

---

### 4.2 Battery Recovery Alert Only Fires for LOW State

**File:** Client sketch, `checkBatteryAlerts()`

Recovery alert only triggers when `gLastBatteryAlert == BATTERY_ALERT_LOW`. Recovery from CRITICAL or HIGH produces no notification.

**Fix:** Check all non-NONE alert states:
```cpp
if (gLastBatteryAlert != BATTERY_ALERT_NONE && cfg.alertOnRecovery) {
```

---

### 4.3 Server Ring Buffer Pruning Inconsistency

**File:** Server sketch, `pruneHotTierIfNeeded()`

Modifies `snapshotCount` without adjusting `writeIndex`. After pruning, the ring buffer's internal state (writeIndex vs snapshotCount) becomes inconsistent — new writes will overwrite data at wrong positions.

**Fix:** Either compact the buffer (shift entries) or set `writeIndex = snapshotCount % MAX_HOURLY_HISTORY_PER_TANK` after pruning.

---

### 4.4 Server `fread()` Return Value Unchecked

**File:** Server sketch, `rollupDailySummaries()` and `loadHistorySettings()`

After `malloc()`, `fread()` return values are not verified. A short read (e.g., from filesystem corruption) would produce silently corrupted JSON.

**Fix:** Verify `bytesRead == expectedSize` before processing.

---

### 4.5 safeSleep() Non-Mbed Path Missing Watchdog Kick

**File:** Client sketch, `safeSleep()`

The non-Mbed fallback uses plain `delay(ms)` without watchdog management. On non-Mbed STM32 platforms with watchdog enabled, a long sleep call would trigger a hardware reset.

**Fix:** Apply the same chunked-sleep-with-kick pattern to all watchdog-enabled platforms.

---

## 5. Bugs — Low

### 5.1 Float Equality for Digital Alarm Threshold

**File:** Client sketch, `evaluateAlarms()`

```cpp
cfg.lowAlarmThreshold == DIGITAL_SENSOR_NOT_ACTIVATED_VALUE
```
Float equality comparison. Should use epsilon comparison or `fabsf(a - b) < 0.01f`.

### 5.2 Repeated `pinMode` on Every Digital Sensor Read

**File:** Client sketch, `readTankSensor()`

`pinMode(pin, INPUT_PULLUP)` called on every sample for digital sensors. Should be done once in `setup()`.

### 5.3 Magic Fallback Pin `(2 + idx)`

**File:** Client sketch, `readTankSensor()`

If `primaryPin` is unconfigured, falls back to `(2 + idx)`. This undocumented behavior could map to unexpected GPIO pins on different hardware variants.

### 5.4 I2C Error Counter Reset Only on Daily Report

**File:** Client sketch, `sendDailyReport()`

I2C error counters are reset only when the daily report completes successfully. If the report fails (network issue, power loss), counters accumulate beyond 24 hours, making the "24h error count" label misleading.

---

## 6. Memory & Performance Concerns

### 6.1 Server: String Concatenation in JSON Builders

Several server REST API handlers build JSON responses using `String +=` in loops:
- `handleTransmissionLogGet()` — O(n²) heap allocations
- `handleNotecardStatusGet()` 
- `handleDfuStatusGet()`
- `handleLocationGet()`

**Impact:** Heap fragmentation on a 512KB RAM MCU. With 50+ log entries, repeated reallocation can exhaust memory.

**Fix:** Use `JsonDocument` + `serializeJson()` or write directly to `EthernetClient`.

### 6.2 Server: 16KB Static Buffer in `loadArchivedMonth()`

Permanently consumes 16KB of BSS. Safe on Mbed OS (BSS is not stack-allocated), but worth noting for RAM budget.

### 6.3 Server: 16KB malloc in NWS API

`nwsFetchAverageTemperature()` dynamically allocates 16KB for HTTP response. On a fragmented heap, this silently returns `TEMPERATURE_UNAVAILABLE`.

### 6.4 Server: `sendClientDataJson()` Doubles Memory

Builds a massive `JsonDocument` containing all configs + all tank records, then serializes to `String`, doubling memory usage. Consider streaming serialization directly to `EthernetClient`.

### 6.5 Server: Year-Over-Year API Blocks Main Loop

`handleHistoryYearOverYear()` can make up to 60 FTP connections (12 months × 5 years) in a single API call, blocking the main loop for minutes.

### 6.6 Client: `publishNote()` Static Buffer

The 1KB static buffer makes `publishNote()` non-reentrant. Safe in Arduino's single-threaded context, but the Opta runs Mbed RTOS — document this constraint.

---

## 7. Common Library Review

**10 header/source files, well-organized shared utilities.**

| File | Purpose | Quality |
|------|---------|---------|
| TankAlarm_Common.h | Notefile names, constants, fleet routing docs | Excellent — very thorough |
| TankAlarm_Config.h | Default intervals, thresholds, all configurable | Excellent — well-commented |
| TankAlarm_I2C.h | Bus recovery (SCL toggling), scanning, current loop | Excellent — robust recovery |
| TankAlarm_Notecard.h | Time sync, idempotent binding, epoch calculation | Clean, with audit notes |
| TankAlarm_Utils.h | Unit conversion, strlcpy polyfill, roundTo | Clean utility functions |
| TankAlarm_Platform.h | Watchdog wrapper, POSIX I/O, atomic writes | Critical infrastructure, well-done |
| TankAlarm_Solar.h/cpp | SunSaver MPPT Modbus, alert priority chain | Well-structured, register map documented |
| TankAlarm_Battery.h | Battery types, SOC thresholds, Vin divider | Comprehensive type system |
| TankAlarm_Diagnostics.h | Free RAM, health snapshots | Simple and effective |

**Strengths:**
- Platform-agnostic abstractions (AVR, STM32, Mbed OS)
- Atomic file writes (write-to-temp-then-rename) prevent data loss
- `strlcpy` used consistently — no buffer overflows observed
- I2C bus recovery with 16 SCL clock cycles + STOP condition
- Comprehensive unit conversion system (PSI, bar, kPa, mbar, m, cm, ft, in)

**Minor Issues:**
- `TankAlarm_I2C.h` contains implementation code (not just declarations). This is fine for Arduino's compilation model but could cause issues with traditional C++ linker if included in multiple translation units.
- `TankAlarm_Solar.cpp` has static 128-byte buffers for fault/alarm descriptions — consider reducing for RAM-constrained builds.

---

## 8. Client Firmware Review

**7,374 lines — the field sensor node.**

### Architecture Highlights

- **4 sensor types:** Digital (float switch), Analog (voltage), Current Loop (4-20mA via A0602), Pulse (hall effect RPM/flow)
- **Non-blocking pulse sampler:** Cooperative state machine that polls in 50ms bursts, keeping `loop()` responsive even during 60-second RPM measurements
- **Power conservation:** 4-state machine (NORMAL → ECO → LOW_POWER → CRITICAL_HIBERNATE) with hysteresis, debounce, and remote-tunable thresholds
- **Solar-only mode:** Startup debounce, sensor voltage gating, sunset protocol, battery failure fallback
- **Note buffering:** Failed publishes are written to flash and retried when Notecard recovers
- **I2C recovery cascade:** Sensor-only recovery (with exponential backoff) → Dual-failure bus recovery → Watchdog forced reset
- **Remote configuration:** Full config push from server with ACK tracking and schema versioning

### Positive Patterns

1. **Alarm rate limiting** — Triple-layered: per-type minimum interval, per-tank hourly sliding window, and global hourly cap
2. **Modem stall detection** — If `card.wireless` responds but no notes sent in 4+ hours, issue `card.restart`
3. **Boot telemetry optimization** — Solar clients skip boot telemetry to avoid wasting power on brownout reboots
4. **Atomic interrupt guards** — `noInterrupts()`/`interrupts()` wrappers for volatile pulse counters with clear documentation of ARM atomicity
5. **Config schema versioning** — Forward/backward compatible config loading with safe defaults from `memset(0)`
6. **Orphaned .tmp file recovery** — Boot-time check for interrupted atomic writes

### Areas for Improvement

1. **File size** — 7,374 lines in a single .ino is challenging to navigate. Extract sensor reading, alarm evaluation, and relay control into separate files.
2. **MonitorConfig struct** — ~50 fields, ~200 bytes per instance. Consider splitting into type-specific sub-configs to reduce memory for simple installations.
3. **`gUnloadDebounceCount[]`** — Lives as a separate file-scope static array rather than inside `MonitorRuntime`. Inconsistent with other per-tank state.

---

## 9. Server Firmware Review

**12,224 lines — the central hub.**

### Architecture Highlights

- **~30 REST API endpoints** served over Ethernet with embedded PROGMEM HTML/JS dashboards
- **O(1) tank record lookup** via djb2 hash table with linear probing
- **Tiered historical storage:** Hot tier (RAM ring buffers, persisted to flash) → Warm tier (daily summaries on LittleFS) → Cold tier (FTP archives)
- **Calibration learning system** — Multiple linear regression with optional NWS temperature compensation
- **Multi-part daily reports** — Part-loss detection with bitmask tracking
- **Contact-based alert routing** — Modern contacts system with alarm associations, SMS rate limiting, falling back to legacy phone numbers
- **Config dispatch pipeline** — Auto-retry with exponential backoff, stale config purging, ACK tracking

### Positive Patterns

1. **Hash table for tank lookup** — O(1) with `static_assert` validating table size at compile time
2. **LRU eviction** — Non-alarming tanks evicted first when at MAX_TANK_RECORDS capacity
3. **Constant-time PIN comparison** — Prevents timing attacks on admin authentication
4. **Authentication rate limiting** — Exponential backoff (1s, 2s, 4s, 8s, 16s, then 30s lockout)
5. **Dirty flag batching** — Flash writes deferred and coalesced: `gConfigDirty`, `gTankRegistryDirty`, `gClientMetadataDirty`
6. **Watchdog integration** — Kicked before every long operation (FTP, Notecard, file I/O)
7. **`doc.overflowed()` checks** — ArduinoJson overflow detection before sending responses
8. **Missed alarm detection** — Cross-references daily report alarms with server-side state (catches alarms missed if server was down when alarm note was sent)

### Areas for Improvement

1. **File size** — 12,224 lines in a single .ino is the single biggest maintainability concern. Subsystems (FTP client, NWS API, calibration, historical storage, web server) should each be in separate files.
2. **Duplicated telemetry processing** — `handleTelemetry()` and `handleDaily()` contain near-identical sensor type detection, mA/voltage conversion, and snapshot recording logic.
3. **Platform I/O duplication** — `#if defined(ARDUINO_OPTA)` blocks duplicated ~20 times for file I/O. A thin filesystem abstraction layer would eliminate ~200+ lines.
4. **Auth is global, not per-IP** — Rate limiting uses a global counter. One attacker's failed attempts lock out all users.

---

## 10. Viewer Firmware Review

**893 lines — read-only kiosk display.**

Well-structured and appropriately simple for its role. Fetches viewer_summary.qi from Notecard, renders a lightweight dashboard over Ethernet.

**Strengths:**
- Minimal attack surface — only GET /api/tanks and GET / endpoints
- Body size cap (1KB) prevents memory exhaustion
- Notecard health check with exponential backoff matches client pattern
- PROGMEM HTML with chunked streaming (128-byte buffer)
- Proper `strlcpy` usage throughout

**Minor Issues:**
- `gConfig.macAddress` is hardcoded to `{ 0x02, 0x00, 0x01, 0x11, 0x20, 0x25 }`. If multiple viewers are deployed on the same LAN, MAC address collisions will cause network issues. Consider deriving from Notecard device UID.
- Missing DHCP lease renewal logging for diagnostic purposes.

---

## 11. I2C Utility Review

**659 lines — diagnostic/recovery tool.**

Well-designed interactive serial menu for field technicians. Provides I2C bus scanning, Notecard diagnostics, hub configuration, I2C address reset, outbound note clearing, and factory restore.

**Strengths:**
- Confirmation prompt for destructive operations (`card.restore` requires typing "RESTORE")
- Auto-detect probes every valid I2C address (0x08–0x77)
- Lists all known TankAlarm outbound notefiles for clearing
- Clean separation of attach/diagnose/configure operations

**Minor Issues:**
- `safeSleep()` is a plain `delay()` — no watchdog present (acceptable for utility tool)
- `PRODUCT_UID` and `DEVICE_FLEET` are empty by default. The `hub.set` command properly rejects empty UIDs, but the error message only says "PRODUCT_UID is empty" without instructions on where to set it.

---

## 12. Security Observations

| Concern | Severity | Component | Status |
|---------|----------|-----------|--------|
| FTP plaintext credentials | High | Server | Stored in config JSON on flash; SFTP planned |
| 4-digit PIN authentication | Medium | Server | Rate-limited but 10,000 combinations; consider 6-digit |
| No HTTPS on Ethernet | Medium | Server/Viewer | Acceptable for LAN-only deployment |
| Auth rate limiting not per-IP | Low | Server | Global counter — one attacker blocks all users |
| No input sanitization on webUI | Low | Server | HTML escaping done in JavaScript, not server-side |
| Notecard API keys in code | Info | All | Product UID is not a secret; acceptable |

**Positive Security Practices:**
- Constant-time PIN comparison prevents timing attacks
- Rate limiting with exponential backoff
- Viewer is read-only (no mutation endpoints)
- Config PIN required for all write operations
- `isValidClientUid()` rejects non-`dev:` prefixed UIDs (prevents phantom entries)
- Buffer sizes consistently enforced with `strlcpy`

---

## 13. Code Quality & Maintainability

### Strengths

1. **Consistent coding style** — Naming conventions, indentation, and commenting are uniform throughout
2. **Exhaustive comments** — Complex algorithms (pulse sampler state machine, power conservation, I2C recovery cascade) are well-documented with rationale
3. **Defensive programming** — Null checks, bounds checks, and fallback defaults on every external input
4. **Forward/backward compatible config** — Schema versioning with `memset(0)` + field-by-field loading means new fields get safe defaults
5. **Audit trail** — Previous code review recommendations tracked across multiple documents in CODE REVIEW/

### Weaknesses

1. **Monolithic files** — 12K and 7K line single-file sketches are the primary maintainability concern
2. **Code duplication** — File I/O platform abstraction (~200+ lines), telemetry processing (~100 lines), sensor type branching (~50 lines across 3 functions)
3. **No automated tests** — Complex logic (alarm debouncing, rate limiting, ring buffer management, power state machine) would benefit from unit tests
4. **No CI/CD pipeline** — No automated compilation verification or lint checks
5. **Arduino String usage in server** — While mostly avoided, several REST handlers use `String +=` concatenation which fragments the heap

### Metrics

| Component | Lines | Functions | Globals | Structs |
|-----------|-------|-----------|---------|---------|
| Common Library | ~1,800 | ~40 | ~10 | ~15 |
| Client | 7,374 | ~80 | ~60 | ~10 |
| Server | 12,224 | ~130+ | ~50 | ~25 |
| Viewer | 893 | ~15 | ~15 | ~3 |
| I2C Utility | 659 | ~15 | ~5 | ~0 |
| **Total** | **~22,950** | **~280** | **~140** | **~53** |

---

## 14. Fixes Applied During This Review

The following bugs were fixed in-code during this review session.

### 14.1 [CRITICAL] Relay Deactivation Index Error — FIXED

**File:** Client `sendAlarm()` (line ~4502)  
**Problem:** Relay deactivation used `gRelayActivationTime[idx] = 0` where `idx` is the *tank* index, not the relay index. Only relay 0 was ever cleared.  
**Fix:** Iterate the `gRelayActiveMaskForTank[idx]` bitmask to clear `gRelayActivationTime[]` for each relay bit that was set.

### 14.2 [CRITICAL] Dangling Pointer in Battery Monitoring — FIXED

**Files:** `TankAlarm_Battery.h` + Client `pollBatteryVoltage()` (line ~4861)  
**Problem:** `BatteryData.mode` was a `const char*` pointing into Notecard JSON response memory. The pointer became dangling after `notecard.deleteResponse()`.  
**Fix:** Changed `mode` to `char mode[16]` fixed buffer in the struct. Changed assignment to `strlcpy(data.mode, modeStr, sizeof(data.mode))` to copy the string before the response is freed.

### 14.3 [MODERATE] WDT Starvation in HTTP Body Parsing — FIXED

**Files:** Server `readHttpRequest()` (line ~6000) + Viewer `readHttpRequest()` (line ~540)  
**Problem:** The HTTP body-reading loop had **no timeout** and **no watchdog kick**:
```cpp
// BEFORE — could block indefinitely without kicking WDT:
while (readBytes < contentLength && client.connected()) {
    while (client.available() && readBytes < contentLength) { ... }
}
```
If a client sent a `Content-Length` header indicating N bytes but stalled while keeping the connection open, this loop would spin forever, starving the 30-second hardware watchdog.  
**Fix:** Added a 5-second `millis()` timeout guard (matching the header loop) and `safeSleep(1)` between read attempts to yield CPU and kick the watchdog:
```cpp
// AFTER:
unsigned long bodyStart = millis();
while (readBytes < contentLength && client.connected() && millis() - bodyStart < 5000UL) {
    while (client.available() && readBytes < contentLength) { ... }
    if (readBytes < contentLength) {
        safeSleep(1);  // Yield CPU + kick watchdog
    }
}
```

### 14.4 [MODERATE] I2C Blocking on Physical Wire Disconnection — FIXED

**Files:** `TankAlarm_Config.h`, `TankAlarm_I2C.h`, Client, Server, Viewer `.ino` files  
**Problem:** No `Wire.setTimeout()` was ever called. The Wire library's `endTransmission()` and `requestFrom()` can block indefinitely if SDA/SCL lines are physically disconnected mid-transaction (a realistic field scenario with screw terminals, vibration, or rodent damage). The default timeout on Mbed OS may be unset or infinite depending on the core version.  
**Fix:**
1. Added `I2C_WIRE_TIMEOUT_MS` (25 ms) constant to `TankAlarm_Config.h`
2. Added `Wire.setTimeout(I2C_WIRE_TIMEOUT_MS)` after every `Wire.begin()` call:
   - Client `setup()` and `reinitializeHardware()`
   - Server `setup()`
   - Viewer `setup()`
   - `tankalarm_recoverI2CBus()` in `TankAlarm_I2C.h` (reinitializes Wire after recovery)

**Tuning note:** 25 ms is conservative. The A0602 ADC response typically takes ~400 µs for a 2-byte read. If Notecard I2C transactions need more time (e.g., during cellular modem activity), increase to 50 ms. The constant is `#ifndef`-guarded for per-sketch override.

### 14.5 [MODERATE] Battery Failure Decay Dead Code — FIXED

**File:** Client `updatePowerState()` (line ~5651)  
**Problem:** Unconditional `gSolarOnlyBatFailCount = 0` executed whenever the battery exits CRITICAL state, making the subsequent time-based 24-hour decay code unreachable (dead code). This defeated the intended design: a battery oscillating between CRITICAL and LOW_POWER within 24 hours should accumulate failure counts toward the threshold, while a battery that stays non-CRITICAL for 24+ hours should decay.  
**Fix:** Removed the unconditional reset. Now only the 24-hour time-based decay remains, correctly allowing rapid CRITICAL oscillations to accumulate toward the failure threshold while preventing slow accumulation over weeks of borderline voltage.

### 14.6 [MODERATE] Battery Recovery Alert Only Fires for LOW State — FIXED

**File:** Client `checkBatteryAlerts()` (line ~4941)  
**Problem:** Recovery alert was restricted to `gLastBatteryAlert == BATTERY_ALERT_LOW`. Recovery from CRITICAL or HIGH states produced no notification, so operators would never know the battery had recovered from those states.  
**Fix:** Removed the `== BATTERY_ALERT_LOW` restriction. Recovery alert now fires for any previous non-NONE alert state (the outer condition `gLastBatteryAlert != BATTERY_ALERT_NONE` already ensures this).

### 14.7 [MODERATE] `fread()` Return Value Unchecked — FIXED

**File:** Server sketch — 3 functions  
**Problem:** After `malloc()` + `fread()`, the return value of `fread()` was silently ignored at 3 locations. A short read (e.g., from filesystem corruption, interrupted write, or flash wear) would produce truncated data fed into `deserializeJson()`, causing silent data corruption or undefined behavior.  
**Locations fixed:**
1. `loadDailySummaryMonth()` — daily summary JSON  
2. `rollupDailySummaries()` — existing month file merge  
3. `loadHotTierSnapshot()` — hot tier ring buffer restore  

**Fix:** Added `bytesRead != sz` check after each `fread()`. On short read: `free(buf)` and return failure. The `loadHistorySettings()` function already had this check — the fix brings the other 3 sites to the same standard.

### 14.8 [MODERATE] Non-Mbed `safeSleep()` Missing Watchdog Kick — FIXED

**File:** Client `safeSleep()` (line ~7299)  
**Problem:** The non-Mbed fallback path used plain `delay(ms)` without watchdog management. On non-Mbed STM32 platforms with IWatchdog enabled, a sleep longer than 30 seconds would trigger a hardware reset.  
**Fix:** Applied the same chunked-sleep pattern already used in Server and Viewer: split sleep into chunks of `WATCHDOG_TIMEOUT_SECONDS / 2`, calling `IWatchdog.reload()` between chunks. Guard by `#ifdef TANKALARM_WATCHDOG_AVAILABLE` so it compiles cleanly on platforms without a watchdog.

### 14.9 [REVIEW NOTE] Ring Buffer Pruning (§4.3) — NOT A BUG

**File:** Server `pruneHotTierIfNeeded()`  
**Original concern:** `snapshotCount` modified without adjusting `writeIndex`, potentially corrupting ring buffer state.  
**Analysis:** After thorough trace through all code paths, this is a **false positive**. Since telemetry timestamps are monotonically increasing, all pruned entries are contiguous at the start of the logical sequence. Reducing `snapshotCount` correctly shifts the logical start forward while `writeIndex` remains valid for the next write. A clarifying comment was added to the code to explain this invariant.

---

## 15. Recommendations — Prioritized

### Priority 1: Fix Critical Bugs (Immediate) — ✅ ALL COMPLETE

| # | Issue | Location | Status |
|---|-------|----------|--------|
| 1.1 | Fix relay deactivation index (§3.1) | Client `sendAlarm()` | ✅ Fixed (§14.1) |
| 1.2 | Fix dangling pointer in battery monitoring (§3.2) | Client `pollBatteryVoltage()` | ✅ Fixed (§14.2) |
| 1.3 | ~~Fix server ring buffer pruning (§4.3)~~ | Server `pruneHotTierIfNeeded()` | ✅ Not a bug (§14.9) |

### Priority 2: Fix Moderate Bugs (This Sprint) — ✅ ALL COMPLETE

| # | Issue | Location | Status |
|---|-------|----------|--------|
| 2.1 | Fix battery failure decay dead code (§4.1) | Client `updatePowerState()` | ✅ Fixed (§14.5) |
| 2.2 | Fix battery recovery alert scope (§4.2) | Client `checkBatteryAlerts()` | ✅ Fixed (§14.6) |
| 2.3 | Add fread() return value checks (§4.4) | Server (multiple functions) | ✅ Fixed (§14.7) |
| 2.4 | Fix non-Mbed safeSleep() watchdog (§4.5) | Client `safeSleep()` | ✅ Fixed (§14.8) |
| 2.5 | Fix WDT starvation in HTTP body parsing | Server + Viewer `readHttpRequest()` | ✅ Fixed (§14.3) |
| 2.6 | Fix I2C blocking on wire disconnection | All sketches + TankAlarm_I2C.h | ✅ Fixed (§14.4) |

### Priority 3: Memory & Performance (Next Sprint)

| # | Issue | Effort |
|---|-------|--------|
| 3.1 | Replace String concatenation in server JSON builders with JsonDocument | 2-4 hours |
| 3.2 | Stream large JSON responses directly to EthernetClient | 2-4 hours |
| 3.3 | Cap FTP connections in year-over-year API endpoint | 1 hour |
| 3.4 | Document non-reentrancy of static-buffer functions | 30 min |

### Priority 4: Architecture & Maintainability (Planned)

| # | Improvement | Effort |
|---|-------------|--------|
| 4.1 | Split server into subsystem files (FTP, NWS, calibration, history, webserver) | 2-3 days |
| 4.2 | Split client into subsystem files (sensors, alarms, relays, power, notecard) | 1-2 days |
| 4.3 | Extract filesystem abstraction layer (eliminate 200+ lines of POSIX/LittleFS duplication) | 4-8 hours |
| 4.4 | Extract shared telemetry processing from handleTelemetry/handleDaily | 2-4 hours |
| 4.5 | Add unit tests for core algorithms (alarm debouncing, rate limiting, power state machine) | 2-3 days | 
| 4.6 | Move `gUnloadDebounceCount[]` into MonitorRuntime struct | 30 min |
| 4.7 | Viewer MAC address derivation from Notecard UID to prevent collisions | 1 hour |

### Priority 5: Security Hardening (When Feasible)

| # | Improvement | Effort |
|---|-------------|--------|
| 5.1 | Upgrade FTP to SFTP/FTPS for credential protection | Significant (library dependency) |
| 5.2 | Increase PIN from 4 to 6 digits | 1 hour |
| 5.3 | Implement per-IP rate limiting (requires IP tracking buffer) | 2-4 hours |
| 5.4 | Add server-side HTML escaping for dashboard values | 2-4 hours |

---

## Appendix A: Notable Design Decisions

These are architectural choices that appear intentional and well-considered:

1. **Single-file .ino sketches** — Arduino IDE convention; splitting requires careful management of include order and forward declarations
2. **Static global state** — Appropriate for single-threaded embedded; avoids heap allocation/fragmentation
3. **Fire-and-forget Notecard commands** — `hub.set` during boot may fail silently; `checkNotecardHealth()` in loop serves as retry mechanism
4. **Modem stall detection** — 4-hour threshold for notes-not-sent triggers `card.restart`. Practical workaround for real cellular modem issues
5. **Solar-only battery failure fallback** — Automatically enters solar-only behaviors when battery consistently reads CRITICAL, preventing infinite reboot loops on failing batteries
6. **Config schema versioning** — `configSchemaVersion` field enables safe migration from older firmware without losing existing field values
7. **Multi-part daily reports** — Splits large reports at payload boundary (960 bytes) with part numbering and "more" flag; server reassembles with bitmask tracking
8. **Boot telemetry skip for solar clients** — Avoids wasting power on brownout boot loops; monitors send telemetry in normal loop instead

## Appendix B: Version Mismatch

Client reports firmware version **1.1.3**, while Server and Viewer report **1.1.2**. Verify whether the server/viewer need a version bump — the `FIRMWARE_VERSION` constant in `TankAlarm_Common.h` shows 1.1.3, suggesting the server/viewer `.ino` files have stale local version defines.
