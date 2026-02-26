# Code Review — February 26, 2026

**Repository:** SenaxInc/ArduinoSMSTankAlarm  
**Branch:** master  
**Base Commit:** `1b3c3f3` (Add diagnostics helpers and safeSleep)  
**Firmware Version:** 1.1.3  

---

## Issues Addressed

- **Issue #251** — Arduino compilation failure (overloaded function ambiguity)
- **Issue #250** — Follow-up hardening improvements (5 workstreams)
- **Issue #247** — I2C reliability improvements (8 fixes from comprehensive analysis)

---

## Part 1: Changes Made

### 1.1 — Fix #251: Compilation Error (Overloaded Function Ambiguity)

**File:** `TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino`

**Problem:** `getPressureConversionFactor` and `getDistanceConversionFactor` had overloads for both `enum class` types and `const char*`, creating ambiguous call resolution during Arduino compilation.

**Fix:** Renamed the `const char*` overloads to `getPressureConversionFactorByName()` and `getDistanceConversionFactorByName()`. Updated all 6 call sites. Kept the `constexpr` enum-class overloads unchanged. Added missing forward declarations for `checkAlarmRateLimit()` and `readVinDividerVoltage()`.

---

### 1.2 — Issue #250 Workstream 1: Atomic Flash Writes

**File:** `TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino`

**Problem:** `saveSolarStateToFlash()` used direct `fopen("w")` which can corrupt data on power loss.

**Fix:** Changed to use `tankalarm_posix_write_file_atomic()` (write-to-temp-then-rename pattern) already available in the shared library.

---

### 1.3 — Issue #250 Workstream 2: Shared Diagnostics

**New File:** `TankAlarm-112025-Common/src/TankAlarm_Diagnostics.h` (93 lines)

**Problem:** `freeRam()` and `printHeapStats()` were duplicated across all 4 sketches with identical logic.

**Fix:** Created `TankAlarm_Diagnostics.h` providing:
- `tankalarm_freeRam()` — platform-agnostic heap measurement
- `tankalarm_printHeapStats()` — formatted heap statistics output
- `TankAlarmHealthSnapshot` struct — standardized health data container
- `tankalarm_collectHealthSnapshot()` — snapshot collector for telemetry

Updated all 4 sketches (Client, Server, Viewer, I2C Utility) to delegate to the shared implementations. Added `#include "TankAlarm_Diagnostics.h"` to `TankAlarm_Common.h`.

**Modified Files:**
- `TankAlarm-112025-Common/src/TankAlarm_Common.h` — added include and `HEALTH_OUTBOX_FILE` notefile definition
- `TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino` — delegated `freeRam()`/heap stats
- `TankAlarm-112025-Viewer-BluesOpta/TankAlarm-112025-Viewer-BluesOpta.ino` — delegated `freeRam()`/heap stats
- `TankAlarm-112025-I2C_Utility/TankAlarm-112025-I2C_Utility.ino` — added `#include <TankAlarm_Common.h>`, delegated diagnostics

---

### 1.4 — Issue #250 Workstream 3: Health Telemetry

**File:** `TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino`

Added optional health telemetry behind `TANKALARM_HEALTH_TELEMETRY_ENABLED` feature flag:
- `gHeapMinFreeBytes` low-watermark tracker
- `gNotecardCommErrorCount` / `gStorageWriteErrorCount` cumulative counters
- `sendHealthTelemetry()` function publishing to `HEALTH_OUTBOX_FILE` notefile
- Telemetry fields: heap_free, heap_min_free, uptime_s, power_state, battery_v, notecard_errors, storage_errors, storage_ok, fw_version, solar_enabled, monitors, i2c_cl_err, i2c_bus_recover

---

### 1.5 — Issue #250 Workstreams 4 & 5: Documentation

**New Files:**
- `CODE REVIEW/POWER_STATE_TEST_COVERAGE.md` — 7 reproducible test scenarios for power state machine edge cases
- `CODE REVIEW/MODULARIZATION_DESIGN_NOTE.md` — module map and 7-phase extraction plan for the monolithic sketches

---

### 1.6 — Issue #247: I2C Reliability Fixes (8 Fixes)

**Analysis File:** `CODE REVIEW/I2C_COMPREHENSIVE_ANALYSIS_02262026.md` (647 lines)

All fixes applied to `TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino`:

#### Fix 1 — I2C Bus Recovery Function (CRITICAL)

Added `recoverI2CBus()` (~55 lines) that:
- **DFU Guard:** Returns early if `gDfuInProgress` is true to avoid disrupting firmware updates
- **Watchdog Kick:** Kicks the watchdog at entry to maximize the time budget for recovery
- Deinitializes `Wire` to release I2C pins
- Toggles SCL as GPIO (16 clock pulses) to free any slave holding SDA low
- Generates a STOP condition (SDA low→high while SCL high)
- Reinitializes `Wire` at 100 kHz default
- Increments `gI2cBusRecoveryCount` for diagnostics

Uses `PIN_WIRE_SCL` / `PIN_WIRE_SDA` on Opta, with fallback to `SCL`/`SDA` for other boards.

#### Fix 2 — Retry Logic for Current Loop Reads (HIGH)

Rewrote `readCurrentLoopMilliamps()` with:
- `I2C_CURRENT_LOOP_MAX_RETRIES` (default 3) retry attempts with 2ms inter-retry delay
- Error logging only on final attempt failure (avoids log spam)
- Drain of partial Wire buffer data on short read failures
- Increments `gCurrentLoopI2cErrors` on each exhausted-retries event

#### Fix 3 — I2C Error Counters (MEDIUM)

Added two global counters:
- `gCurrentLoopI2cErrors` — cumulative A0602 current loop I2C failures
- `gI2cBusRecoveryCount` — number of times bus recovery was invoked

Both are included in health telemetry as `i2c_cl_err` and `i2c_bus_recover`.

#### Fix 4 — I2C Recovery in Notecard Failure Path (HIGH)

Enhanced `checkNotecardHealth()`:
- After `I2C_NOTECARD_RECOVERY_THRESHOLD` (default 10) consecutive failures, calls `recoverI2CBus()` + `notecard.begin(NOTECARD_I2C_ADDRESS)` to attempt I2C-level recovery
- On Notecard recovery, re-calls `notecard.begin()` to properly re-attach the NoteI2c singleton

#### Fix 5 — Wire Reinit in `reinitializeHardware()` (LOW)

Added to the start of `reinitializeHardware()`:
```cpp
Wire.end();
delay(10);
Wire.begin();
```
Followed by `notecard.begin(NOTECARD_I2C_ADDRESS)` after sensor reconfiguration, ensuring I2C bus state is clean after configuration changes.

#### Fix 6 — Prolonged I2C Failure Detection (MEDIUM)

Added zombie-state detection in `loop()`:
- Monitors whether **both** Notecard (`!gNotecardAvailable`) AND any current-loop sensors (`consecutiveFailures >= SENSOR_FAILURE_THRESHOLD`) are failing simultaneously
- At `I2C_DUAL_FAIL_RECOVERY_LOOPS` (default 30) consecutive loop iterations: attempts `recoverI2CBus()` + Notecard reconnect
- At `I2C_DUAL_FAIL_RESET_LOOPS` (default 120) consecutive loop iterations: prints FATAL message and stops kicking the watchdog, triggering a hardware reset within 30 seconds
- Counter resets to 0 whenever either bus recovers

#### Fix 7 — I2C Error Code Logging (LOW)

`Wire.endTransmission()` return codes are now logged with the I2C address and channel number:
- `1` = data too long for transmit buffer
- `2` = received NACK on transmit of address
- `3` = received NACK on transmit of data
- `4` = other error
- `5` = timeout

#### Fix 8 — `Wire.available()` Guard (LOW)

Added `Wire.available() < 2` check before `Wire.read()` calls in `readCurrentLoopMilliamps()`, with buffer drain on underrun. Prevents reading garbage data if the Wire library reports success but the buffer is short.

---

### 1.7 — Workspace Cleanup

Moved **55 files** from root directory to `RecycleBin/`:
- 24 `tmpclaude-*-cwd` temporary directories
- 13 HTML files (search results pages from root and `Arduino_Opta_Blueprint/`)
- 17 search/temp scripts (`.ps1`, `.py`, `.cs`)
- 7 text/data files (`.txt`, `.json`)
- 1 zip archive (`TankAlarm-112025-Common.zip`)
- 1 marker file (`_codeql_detected_source_root`)

---

### 1.8 — Follow-Up I2C Hardening (7 Additional Improvements)

Implemented all feasible improvements from the initial code review's future suggestions:

#### 1.8.1 — Configurable I2C Recovery Parameters

**File:** `TankAlarm-112025-Common/src/TankAlarm_Config.h`

All hardcoded I2C magic numbers moved to `#ifndef`-guarded compile-time defines:

| Define | Default | Purpose |
|--------|---------|--------|
| `I2C_NOTECARD_RECOVERY_THRESHOLD` | 10 | Notecard failures before bus recovery |
| `I2C_DUAL_FAIL_RECOVERY_LOOPS` | 30 | Loop iterations (dual fail) before recovery attempt |
| `I2C_DUAL_FAIL_RESET_LOOPS` | 120 | Loop iterations (dual fail) before forced watchdog reset |
| `I2C_SENSOR_ONLY_RECOVERY_THRESHOLD` | 10 | Current-loop-only failures before bus recovery |
| `I2C_CURRENT_LOOP_MAX_RETRIES` | 3 | Retries per channel in `readCurrentLoopMilliamps()` |

All can be overridden per-sketch via `#define` before including the header.

#### 1.8.2 — DFU Guard in I2C Recovery

**File:** `TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino`

`recoverI2CBus()` now returns early with a log message when `gDfuInProgress == true`, preventing I2C bus disruption during firmware OTA updates.

#### 1.8.3 — Watchdog Kick in I2C Recovery

**File:** `TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino`

`recoverI2CBus()` now kicks the watchdog at entry to maximize the time budget before the recovery sequence runs.

#### 1.8.4 — I2C Bus Scan on Startup

**File:** `TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino`

Added a diagnostic I2C bus scan in `setup()` after `Wire.begin()` that:
- Probes both expected devices (Notecard @ 0x17, A0602 @ 0x64) and logs OK / NOT FOUND with error code
- Scans the full valid I2C address range (0x08–0x77) for unexpected devices
- Logs results to Serial for field diagnostics

#### 1.8.5 — Sensor-Only I2C Recovery Trigger

**File:** `TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino`

Added a new detection block in `loop()` that monitors for current-loop-only failures (Notecard still healthy). When **all** configured current-loop sensors have failed for `I2C_SENSOR_ONLY_RECOVERY_THRESHOLD` (default 10) consecutive loop iterations:
- Calls `recoverI2CBus()` to attempt A0602 recovery
- Resets all current-loop sensor failure counters to give them a fresh chance
- Independent of the dual-failure detection (which handles Notecard + sensors both down)

#### 1.8.6 — Periodic I2C Error Counter Reset

**File:** `TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino`

`gCurrentLoopI2cErrors` and `gI2cBusRecoveryCount` are now reset to 0 at the end of `sendDailyReport()`, so health telemetry reflects the last 24-hour window rather than lifetime totals.

#### 1.8.7 — RecycleBin and Temp Files in .gitignore

**File:** `.gitignore`

Added `RecycleBin/` and `tmpclaude-*-cwd/` patterns to `.gitignore` to keep the repo lean.

### 1.9 — Second Follow-Up: Scan Retry, Backoff, and Error Alerting (3 Improvements)

Implemented 3 additional I2C reliability improvements from section 3.2 suggestions.

#### 1.9.1 — Startup Bus Scan Failure Escalation

**Files:** `TankAlarm-112025-Client-BluesOpta.ino`, `TankAlarm_Config.h`

The I2C bus scan in `setup()` now retries when expected devices (Notecard, A0602) are not found. Controlled by two new defines:
- `I2C_STARTUP_SCAN_RETRIES` (default 3) — number of scan attempts
- `I2C_STARTUP_SCAN_RETRY_DELAY_MS` (default 2000) — delay between retries

If devices are still missing after all retries, a `WARNING` is logged and boot continues. This catches loose wiring during installation without blocking startup permanently.

#### 1.9.2 — Sensor-Only Recovery with Exponential Backoff

**Files:** `TankAlarm-112025-Client-BluesOpta.ino`, `TankAlarm_Config.h`

The sensor-only I2C recovery now uses exponential backoff. After each `recoverI2CBus()` attempt for current-loop-only failures, the effective threshold doubles (10 → 20 → 40 → 80). Capped at `I2C_SENSOR_RECOVERY_MAX_BACKOFF` (default 8×). The backoff resets to 1× when sensors recover successfully.

Changes:
- Counter widened from `uint8_t` to `uint16_t` to accommodate larger thresholds
- Added `static uint8_t sensorRecoveryBackoff` variable
- Comparison changed from `==` to `>=` against `threshold × backoff`
- Log message now reports current backoff multiplier

#### 1.9.3 — I2C Error Rate Alerting

**Files:** `TankAlarm-112025-Client-BluesOpta.ino`, `TankAlarm_Config.h`

Added a check in `sendDailyReport()` that publishes an alarm note when the 24-hour I2C error count exceeds `I2C_ERROR_ALERT_THRESHOLD` (default 50). The alarm is sent *before* the daily counter reset so the error count is accurate.

Alarm note format (via `ALARM_FILE`):
```json
{
  "c": "<deviceUID>",
  "s": "<siteName>",
  "y": "i2c-error-rate",
  "errs": <errorCount>,
  "recs": <recoveryCount>,
  "t": <epoch>
}
```

This alerts operators to degrading I2C connections before they cause measurement gaps.

---

### 1.10 — High-Priority Improvements: `notecard.begin()` Audit, Shared I2C Header, Server/Viewer Hardening

**Implements:** 3.1.1, 3.1.2, 3.1.3 from Part 3

#### 1.10.1 — `notecard.begin()` Idempotency Audit (3.1.1)

**Finding:** Audited the Blues `note-arduino` library source at `C:\Users\Mike\Documents\Arduino\libraries\Blues_Wireless_Notecard\src\`. The key discovery:

- `Notecard::begin(uint32_t addr)` calls `make_note_i2c(Wire)`
- `make_note_i2c()` uses a **singleton pattern** with a `if (!note_i2c)` guard — it only allocates `NoteI2c_Arduino` once
- Subsequent `begin()` calls re-register I2C function-pointer callbacks (`NoteSetFnI2CDefault`) and re-set address/MTU, but do **NOT** allocate new memory
- **Result: No memory leak. Safe to call repeatedly.**

**Changes:**
- Created `tankalarm_ensureNotecardBinding()` helper in `TankAlarm_Notecard.h` — documents the audit finding and provides a semantic wrapper
- Replaced all 6 direct `notecard.begin(NOTECARD_I2C_ADDRESS)` calls in the Client sketch with `tankalarm_ensureNotecardBinding(notecard)` for clarity of intent
- Moved `NOTECARD_FAILURE_THRESHOLD` (was `#define` in Client only) to `TankAlarm_Config.h` with `#ifndef` guard for fleet-wide use

#### 1.10.2 — Extract I2C Operations into `TankAlarm_I2C.h` (3.1.2)

**New File:** `TankAlarm-112025-Common/src/TankAlarm_I2C.h` (~260 lines)

Extracted three core I2C functions into a shared header-only module, following the same `static inline` pattern as `TankAlarm_Diagnostics.h`:

| Function | Purpose | Parameters |
|----------|---------|------------|
| `tankalarm_recoverI2CBus()` | SCL toggle bus recovery with DFU guard | `bool dfuInProgress`, `void (*kickWatchdog)()` |
| `tankalarm_scanI2CBus()` | Startup device scan with retry + unexpected device detection | Address/name arrays, count → `I2CScanResult` struct |
| `tankalarm_readCurrentLoopMilliamps()` | A0602 current-loop read with retry and error counting | `int16_t channel`, `uint8_t i2cAddr` |

Design decisions:
- **DFU guard** passed as parameter (Client-specific, others pass `false`)
- **Watchdog kick** passed as function pointer (each sketch provides its own, or `nullptr`)
- **Error counters** declared `extern` in header, defined in each sketch
- Added `#include "TankAlarm_I2C.h"` to `TankAlarm_Common.h` master include list

Client refactoring:
- `recoverI2CBus()` → thin wrapper calling `tankalarm_recoverI2CBus(gDfuInProgress, kickWd)` with lambda for watchdog
- `readCurrentLoopMilliamps()` → thin wrapper calling `tankalarm_readCurrentLoopMilliamps(channel, i2cAddr)` with config address
- Startup bus scan → replaced 35-line inline block with `tankalarm_scanI2CBus()`
- Error counters changed from `static` to non-static for extern linkage
- **Net reduction:** ~100 lines removed from Client

I2C Utility refactoring:
- Replaced local `scanI2CBus()` implementation with shared `tankalarm_scanI2CBus()`
- Added `gCurrentLoopI2cErrors` and `gI2cBusRecoveryCount` extern definitions

#### 1.10.3 — Server I2C Hardening (3.1.3)

**File:** `TankAlarm-112025-Server-BluesOpta.ino` (+60 lines)

Added to the Server sketch:
- **I2C error counters:** `gCurrentLoopI2cErrors`, `gI2cBusRecoveryCount` (extern linkage), `gNotecardAvailable`, `gNotecardFailureCount`, `gLastSuccessfulNotecardComm`
- **Startup bus scan:** After `Wire.begin()`, scans for Notecard at 0x17 only (no A0602 on Server)
- **Notecard health check:** Every 5 minutes when `gNotecardAvailable` is false, probes `card.version`, attempts `tankalarm_recoverI2CBus()` after threshold failures, re-binds with `tankalarm_ensureNotecardBinding()`
- **Failure tracking in `processNotefile()`:** Tracks null request/response failures, auto-transitions to offline mode after `NOTECARD_FAILURE_THRESHOLD`, auto-recovers on success

#### 1.10.4 — Viewer I2C Hardening (3.1.3)

**File:** `TankAlarm-112025-Viewer-BluesOpta.ino` (+45 lines)

Same hardening pattern as Server:
- **I2C error counters** + Notecard availability tracking globals
- **Startup bus scan** for Notecard only
- **Health check in `loop()`** — 5-minute interval when offline, recovery with bus toggle + re-binding
- **Failure tracking in `fetchViewerSummary()`** — the Viewer's primary Notecard interaction now tracks failures and auto-transitions to offline mode

---

## Part 2: Files Modified Summary

| File | Type | Lines Changed |
|------|------|---------------|
| `TankAlarm-112025-Client-BluesOpta.ino` | Modified | +470 / -~200 |
| `TankAlarm-112025-Common/src/TankAlarm_Common.h` | Modified | +7 |
| `TankAlarm-112025-Common/src/TankAlarm_Config.h` | Modified | +52 |
| `TankAlarm-112025-Common/src/TankAlarm_Diagnostics.h` | **New** | +93 |
| `TankAlarm-112025-Common/src/TankAlarm_I2C.h` | **New** | +260 |
| `TankAlarm-112025-Common/src/TankAlarm_Notecard.h` | Modified | +35 |
| `TankAlarm-112025-Server-BluesOpta.ino` | Modified | +84 / -24 |
| `TankAlarm-112025-Viewer-BluesOpta.ino` | Modified | +69 / -24 |
| `TankAlarm-112025-I2C_Utility.ino` | Modified | +24 / -22 |
| `.gitignore` | Modified | +6 |
| `CODE REVIEW/I2C_COMPREHENSIVE_ANALYSIS_02262026.md` | **New** | +647 |
| `CODE REVIEW/POWER_STATE_TEST_COVERAGE.md` | **New** | documentation |
| `CODE REVIEW/MODULARIZATION_DESIGN_NOTE.md` | **New** | documentation |
| `CODE REVIEW/FUTURE_3.1.1_*.md` through `FUTURE_3.3.6_*.md` | **New** | 14 planning documents |
| `CODE REVIEW/CODE_REVIEW_02262026.md` | **New** | this file |
| 55 files | Moved to RecycleBin | cleanup |

---

## Part 3: Future Improvements

### 3.1 — High Priority — ✅ ALL IMPLEMENTED (Section 1.10)

#### 3.1.1 — ✅ Notecard `notecard.begin()` Idempotency Audit
Audited and documented. The Blues note-arduino library uses a singleton pattern — no memory leak. Created `tankalarm_ensureNotecardBinding()` helper and refactored all 6 Client call sites. Moved `NOTECARD_FAILURE_THRESHOLD` to shared config.

#### 3.1.2 — ✅ Extract I2C Operations into `TankAlarm_I2C.h`
Created `TankAlarm-112025-Common/src/TankAlarm_I2C.h` (~260 lines) with `tankalarm_recoverI2CBus()`, `tankalarm_scanI2CBus()`, and `tankalarm_readCurrentLoopMilliamps()`. Refactored Client and I2C Utility to use them. Added to `TankAlarm_Common.h` master include.

#### 3.1.3 — ✅ Server/Viewer I2C Hardening
Added startup bus scan, Notecard health check (5-min interval), failure tracking with auto-offline transition, and bus recovery to both Server and Viewer sketches. Both now use shared `TankAlarm_I2C.h` and `TankAlarm_Notecard.h` functions.

---

### 3.2 — Medium Priority

#### 3.2.1 — I2C Transaction Timing Telemetry
Instrument `readCurrentLoopMilliamps()` and Notecard calls with timing (`micros()`) to report average and max I2C transaction durations in health telemetry. This would provide early warning of bus degradation before outright failures.

#### 3.2.2 — Current Loop Read Caching / Rate Limiting
Multiple calls to `readCurrentLoopMilliamps()` for the same channel within a single `sampleTanks()` iteration could be cached. While not currently an issue (each tank reads its own channel), future multi-sensor configurations could benefit.

#### 3.2.3 — Notecard Health Check Backoff
The `checkNotecardHealth()` function runs every loop iteration when `gNotecardAvailable` is false. Consider adding a backoff timer so reconnection attempts are spaced further apart (e.g., 30s → 60s → 120s), reducing I2C bus traffic during prolonged Notecard outages.

#### 3.2.4 — I2C Recovery Event Logging via Notecard
When `recoverI2CBus()` is called, publish a lightweight diagnostic note (separate from the alarm) so the fleet dashboard can track recovery frequency per device over time. This complements the daily error counter reset (1.8.6) by providing event-level granularity.

#### 3.2.5 — Startup Scan Results in Health Telemetry
Include the startup I2C bus scan results (found/missing devices, retry count used) in the first health report after boot. This would help diagnose field units that boot successfully but with missing peripherals.

---

### 3.3 — Low Priority

#### 3.3.1 — I2C Mutex / Guard for Future Multi-threaded Use
The Opta's STM32H747 is dual-core (M7 + M4). If future firmware uses both cores, I2C bus access will need a mutex. Adding a no-op `I2CGuard` RAII wrapper now would make the transition easier.

#### 3.3.2 — I2C Address Conflict Detection
Add runtime detection for I2C address conflicts. If the Notecard (0x17) and A0602 (0x64) are ever misconfigured to the same address, detect this during the startup bus scan and log a critical warning.

#### 3.3.3 — Wire Library Timeout Configuration
Investigate whether Mbed OS's Wire implementation exposes a configurable timeout (the default is ~25ms). For faster failure detection, a shorter timeout could be beneficial, but needs testing with the Notecard's clock-stretching behavior.

#### 3.3.4 — Flash-Backed I2C Error Log
Persist critical I2C errors (bus recovery events, prolonged failures) to flash so they survive resets. This would allow post-mortem analysis of field units that experienced watchdog resets. The daily counter reset (1.8.6) makes this more relevant — lifetime data is now only available if persisted.

#### 3.3.5 — `Arduino_Opta_Blueprint` Directory Disposition
The `Arduino_Opta_Blueprint/` directory in the repo root appears to be a reference copy of the Arduino Opta expansion library. Decide whether to keep it as a vendored dependency, move it to a `lib/` or `vendor/` directory, or remove it and reference via the Arduino Library Manager.

#### 3.3.6 — Comprehensive Integration Test Suite
The `POWER_STATE_TEST_COVERAGE.md` defines 7 manual test scenarios. These could be formalized into a hardware-in-the-loop test framework using the I2C Utility sketch as a test harness, with automated pass/fail reporting via Notecard.

---

## Part 4: Risk Assessment

| Risk | Likelihood | Impact | Mitigation |
|------|-----------|--------|------------|
| `recoverI2CBus()` GPIO pin constants wrong for Opta variant | Low | High — bus won't recover | Uses `PIN_WIRE_SCL`/`PIN_WIRE_SDA` from board variant; verify on actual hardware |
| `notecard.begin()` called multiple times leaks memory | Low | Medium — eventual OOM | Audit Blues library source; test with heap tracking |
| Prolonged I2C fail detection triggers false positive | Low | High — unnecessary reset | Requires BOTH Notecard AND current loop failure; `I2C_DUAL_FAIL_RESET_LOOPS` iterations = many minutes |
| I2C recovery during Notecard transaction corrupts state | Very Low | Medium — garbled response | Recovery only triggers from health check path, not mid-transaction; DFU guard prevents during OTA |
| 2ms retry delay in `readCurrentLoopMilliamps()` affects timing | Very Low | Low — 6ms max per channel | 6ms × 8 channels = 48ms worst case, well within sample interval |
| Sensor-only recovery fires repeatedly if A0602 is broken | Very Low | Low — excessive log output | Exponential backoff implemented (1.9.2); max interval = base × 8 |
| Startup bus scan retries delay boot | Very Low | Low — max ~6s delay | Only retries when expected devices missing; `I2C_STARTUP_SCAN_RETRIES` × `I2C_STARTUP_SCAN_RETRY_DELAY_MS` |
| I2C error alert false positive | Low | Low — spurious alarm | Threshold of 50 errors/day is well above normal noise; adjustable via `I2C_ERROR_ALERT_THRESHOLD` |
