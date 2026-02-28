# Future Work — TankAlarm Firmware

**Generated:** June 7, 2025  
**Source:** CODE_REVIEW_06072025_COMPREHENSIVE.md (§5, §6, §15 Priorities 3–5) + existing FUTURE_3.x.x documents  
**Status:** All Priority 1 (Critical) and Priority 2 (Moderate) bugs have been fixed. Items below are improvements, not correctness issues.

---

## Table of Contents

1. [Low-Severity Bugs](#1-low-severity-bugs)
2. [Memory & Performance](#2-memory--performance)
3. [Architecture & Maintainability](#3-architecture--maintainability)
4. [Security Hardening](#4-security-hardening)
5. [I2C Subsystem Improvements](#5-i2c-subsystem-improvements)
6. [Version & Build](#6-version--build)
7. [Summary Table](#7-summary-table)

---

## 1. Low-Severity Bugs

These are correctness issues that are unlikely to cause field failures but should be cleaned up.

### 1.1 Float Equality for Digital Alarm Threshold

**File:** Client `evaluateAlarms()`  
**Effort:** 10 min

```cpp
// BEFORE:
cfg.lowAlarmThreshold == DIGITAL_SENSOR_NOT_ACTIVATED_VALUE

// AFTER:
fabsf(cfg.lowAlarmThreshold - DIGITAL_SENSOR_NOT_ACTIVATED_VALUE) < 0.01f
```

Float equality is technically unsafe. While the current values are assigned from integer constants and will compare equal on IEEE 754, this is fragile if thresholds ever come from serialized JSON (which may introduce rounding).

### 1.2 Repeated `pinMode` on Every Digital Sensor Read

**File:** Client `readTankSensor()`  
**Effort:** 15 min

`pinMode(pin, INPUT_PULLUP)` is called on every sample for digital sensors. Move to `setup()` or `reinitializeHardware()` during initialization. The call is idempotent but wastes cycles and obscures intent.

### 1.3 Magic Fallback Pin `(2 + idx)`

**File:** Client `readTankSensor()`  
**Effort:** 15 min

When `primaryPin` is unconfigured, falls back to `(2 + idx)`. This undocumented behavior could map to unexpected GPIO pins on different hardware variants. Options:
- Return an error/sentinel value instead of guessing
- Document the mapping in `TankAlarm_Config.h`
- Add a compile-time `static_assert` or runtime pin validation

### 1.4 I2C Error Counter Reset Only on Daily Report

**File:** Client `sendDailyReport()`  
**Effort:** 20 min

I2C error counters are reset only when the daily report completes successfully. If the report fails (network issue, power loss), counters accumulate beyond 24 hours, making the "24h error count" label misleading.

**Fix options:**
- Reset counters unconditionally at the daily-report interval (even on send failure), persisting the last-reported value
- Add an epoch-based "last reset" timestamp and include the actual window duration in the report

---

## 2. Memory & Performance

### 2.1 Replace String Concatenation in Server JSON Builders

**Effort:** 2–4 hours

Several REST API handlers build JSON using `String +=` in loops, causing O(n²) heap allocations:
- `handleTransmissionLogGet()`
- `handleNotecardStatusGet()`
- `handleDfuStatusGet()`
- `handleLocationGet()`

**Fix:** Replace with `JsonDocument` + `serializeJson()`, or write directly to `EthernetClient` using chunked output.

### 2.2 Stream Large JSON Responses to EthernetClient

**Effort:** 2–4 hours

`sendClientDataJson()` builds a massive `JsonDocument` containing all configs + tank records, then serializes to a `String`, doubling memory usage. Use ArduinoJson's `serializeJson(doc, client)` to stream directly to the `EthernetClient` socket.

### 2.3 Cap FTP Connections in Year-Over-Year API

**Effort:** 1 hour

`handleHistoryYearOverYear()` can make up to 60 FTP connections (12 months × 5 years) in a single request, blocking the main loop for minutes. Add:
- A maximum year span (e.g., 3 years)
- Watchdog kicks between FTP fetches
- A progress timeout that aborts if total duration exceeds 60 seconds

### 2.4 Document Non-Reentrancy of Static-Buffer Functions

**Effort:** 30 min

Client `publishNote()` uses a 1KB static buffer, making it non-reentrant. Safe in Arduino's single-threaded `loop()`, but the Opta runs Mbed RTOS. Add `// NOT THREAD-SAFE` comments to all static-buffer functions.

### 2.5 Reduce NWS API Memory Allocation

**Effort:** 30 min

`nwsFetchAverageTemperature()` allocates 16KB for the HTTP response. On a fragmented heap, `malloc()` silently returns `nullptr` and the function returns `TEMPERATURE_UNAVAILABLE`. Consider:
- Streaming the JSON parse (ArduinoJson `DeserializationOption::Filter`)
- Reducing the buffer to 8KB with a content-length guard

### 2.6 Server 16KB Static Buffer in `loadArchivedMonth()`

**Effort:** Info only

Permanently consumes 16KB of BSS. Safe on Mbed OS, but worth tracking in the RAM budget. If RAM becomes tight, convert to dynamic allocation with fallback.

---

## 3. Architecture & Maintainability

### 3.1 Split Server into Subsystem Files

**Effort:** 2–3 days  
**Impact:** High — 12,224 lines in a single file

Proposed split:
| File | Contents |
|------|----------|
| `TankAlarmServer_Core.ino` | `setup()`, `loop()`, globals, config I/O |
| `TankAlarmServer_Web.h` | HTTP request handling, REST API dispatch |
| `TankAlarmServer_FTP.h` | FTP client (connect, store, retrieve, PASV) |
| `TankAlarmServer_NWS.h` | Weather API integration |
| `TankAlarmServer_History.h` | Hot/warm/cold tier storage, pruning, rollup |
| `TankAlarmServer_Calibration.h` | Linear regression, temperature compensation |
| `TankAlarmServer_Alerts.h` | Contact routing, SMS dispatch, alarm logic |

Use header-only pattern (matching existing `TankAlarm_I2C.h` convention) to avoid Arduino IDE multi-file linking issues.

### 3.2 Split Client into Subsystem Files

**Effort:** 1–2 days  
**Impact:** High — 7,374 lines in a single file

Proposed split:
| File | Contents |
|------|----------|
| `TankAlarmClient_Core.ino` | `setup()`, `loop()`, config, globals |
| `TankAlarmClient_Sensors.h` | Sensor reading (digital, analog, current loop, pulse) |
| `TankAlarmClient_Alarms.h` | Alarm evaluation, debouncing, rate limiting |
| `TankAlarmClient_Relays.h` | Relay control, activation tracking |
| `TankAlarmClient_Power.h` | Power state machine, solar-only mode |
| `TankAlarmClient_Notecard.h` | Publishing, polling, modem stall detection |

### 3.3 Extract Filesystem Abstraction Layer

**Effort:** 4–8 hours

`#if defined(ARDUINO_OPTA)` blocks are duplicated ~20 times in the Server for file I/O. Extract into a shared header:

```cpp
// TankAlarm_FileSystem.h
static inline bool ta_readFile(const char *path, char *buf, size_t bufSize, size_t &bytesRead);
static inline bool ta_writeFileAtomic(const char *path, const char *data, size_t len);
static inline long ta_fileSize(const char *path);
```

This would eliminate ~200+ lines of platform-conditional code.

### 3.4 Extract Shared Telemetry Processing

**Effort:** 2–4 hours

`handleTelemetry()` and `handleDaily()` in the Server contain near-identical sensor type detection, mA/voltage conversion, and snapshot recording logic. Extract into a shared `processTelemetryPayload()` function.

### 3.5 Add Unit Tests for Core Algorithms

**Effort:** 2–3 days

The following algorithms are complex enough to warrant automated testing:
- Alarm debouncing state machine
- Rate limiting (per-type, per-tank, global)
- Power state machine transitions with hysteresis
- Ring buffer wrap-around and pruning
- Config schema migration

Consider a host-side test harness (e.g., PlatformIO native test) that mocks hardware dependencies.

### 3.6 Move `gUnloadDebounceCount[]` into MonitorRuntime

**Effort:** 30 min

`gUnloadDebounceCount[]` is a separate file-scope static array. Move into the `MonitorRuntime` struct for consistency with other per-tank runtime state.

### 3.7 Viewer MAC Address Derivation from Notecard UID

**Effort:** 1 hour

`gConfig.macAddress` is hardcoded. If multiple Viewers are deployed on the same LAN, MAC collisions will cause network issues. Derive the MAC from the Notecard device UID (e.g., hash the UID and set the locally-administered bit).

---

## 4. Security Hardening

### 4.1 Upgrade FTP to SFTP/FTPS

**Effort:** Significant (library dependency)

FTP credentials are stored in plaintext in config JSON on flash, and transmitted in cleartext. This is the highest-severity security item. Options:
- FTPS: Requires TLS library (e.g., BearSSL) — significant flash/RAM overhead on Opta
- SFTP: No mature Arduino library exists
- Alternative: Use Blues Notecard web routes to proxy file uploads through Notehub, eliminating FTP entirely

### 4.2 Increase PIN from 4 to 6 Digits

**Effort:** 1 hour

Currently 10,000 combinations. With rate limiting, brute-force takes ~8 hours worst case. Increasing to 6 digits (1,000,000 combinations) raises this to ~33 days. Changes needed:
- `MAX_PIN_LENGTH` constant
- Validation in config load/save
- Dashboard PIN input field

### 4.3 Implement Per-IP Rate Limiting

**Effort:** 2–4 hours

Authentication rate limiting uses a global counter. One attacker's failed attempts lock out all legitimate users. Fix:
- Track last-failed-IP and failed-count per IP (small ring buffer of 8-16 entries)
- Apply exponential backoff per-IP rather than globally

### 4.4 Server-Side HTML Escaping

**Effort:** 2–4 hours

HTML escaping is currently done in JavaScript on the client side. If an attacker can inject content into tank names, site names, or alert messages via Notecard, the JavaScript escaping may be bypassed. Add server-side escaping when building HTML responses:

```cpp
static String htmlEscape(const String &input) {
  String out;
  out.reserve(input.length());
  for (size_t i = 0; i < input.length(); i++) {
    char c = input.charAt(i);
    switch (c) {
      case '<': out += "&lt;"; break;
      case '>': out += "&gt;"; break;
      case '&': out += "&amp;"; break;
      case '"': out += "&quot;"; break;
      default:  out += c; break;
    }
  }
  return out;
}
```

---

## 5. I2C Subsystem Improvements

These items are derived from the existing detailed analysis documents in `CODE REVIEW/FUTURE_3.x.x_*.md`. See those documents for full implementation specs.

| # | Item | Document | Effort | Priority |
|---|------|----------|--------|----------|
| 5.1 | Notecard `begin()` idempotency audit | FUTURE_3.1.1 | 1–2 hours | Medium |
| 5.2 | Extract I2C header (already done — verify) | FUTURE_3.1.2 | Done | — |
| 5.3 | Server/Viewer I2C hardening | FUTURE_3.1.3 | 2–4 hours | Medium |
| 5.4 | I2C transaction timing instrumentation | FUTURE_3.2.1 | 2–3 hours | Low |
| 5.5 | Current-loop value caching | FUTURE_3.2.2 | 1–2 hours | Low |
| 5.6 | Notecard health check backoff tuning | FUTURE_3.2.3 | 1 hour | Low |
| 5.7 | I2C recovery event logging to Notecard | FUTURE_3.2.4 | 1–2 hours | Medium |
| 5.8 | Move startup scan into health check | FUTURE_3.2.5 | 1 hour | Low |
| 5.9 | I2C mutex guard (RTOS safety) | FUTURE_3.3.1 | 2–4 hours | Medium |
| 5.10 | I2C address conflict detection | FUTURE_3.3.2 | 1–2 hours | Low |
| 5.11 | ~~Wire library timeout~~ | ~~FUTURE_3.3.3~~ | ~~Done~~ | ✅ Fixed (§14.4) |
| 5.12 | Flash-backed I2C error log | FUTURE_3.3.4 | 2–4 hours | Low |
| 5.13 | Opta Blueprint library disposition | FUTURE_3.3.5 | Decision | Low |
| 5.14 | Integration test suite | FUTURE_3.3.6 | 3–5 days | Medium |

---

## 6. Version & Build

### 6.1 Fix Version Mismatch

**Effort:** 5 min

Client reports firmware version **1.1.3**, while Server and Viewer report **1.1.2**. The shared `FIRMWARE_VERSION` in `TankAlarm_Common.h` shows 1.1.3 — check whether Server/Viewer `.ino` files have stale local `#define FIRMWARE_VERSION` overrides.

### 6.2 Add CI/CD Pipeline

**Effort:** 1 day

No automated compilation verification exists. A GitHub Actions workflow using `arduino-cli compile` would catch:
- Syntax errors
- Missing includes
- Platform-specific build failures (Mbed vs AVR)
- Linker errors from header-only implementation conflicts

### 6.3 Add DHCP Lease Renewal Logging (Viewer)

**Effort:** 15 min

The Viewer has no DHCP diagnostic logging. Add a log line when `Ethernet.maintain()` renews or fails to renew the DHCP lease.

---

## 7. Summary Table

| Priority | Category | Items | Total Effort |
|----------|----------|-------|--------------|
| **Low bugs** | Correctness | 4 items (§1.1–1.4) | ~1 hour |
| **Memory/Perf** | Performance | 6 items (§2.1–2.6) | ~8–14 hours |
| **Architecture** | Maintainability | 7 items (§3.1–3.7) | ~7–14 days |
| **Security** | Hardening | 4 items (§4.1–4.4) | ~1–3 days |
| **I2C** | Subsystem | 12 remaining items (§5.1–5.14) | ~2–4 weeks |
| **Version/Build** | DevOps | 3 items (§6.1–6.3) | ~1 day |

### Recommended Next Steps

1. **Low-hanging fruit (1 day):** Fix low bugs §1.1–1.4, version mismatch §6.1, DHCP logging §6.3
2. **Memory wins (2 days):** §2.1 String→JsonDocument, §2.2 streaming JSON, §2.3 FTP cap
3. **Architecture (1 sprint):** §3.3 filesystem abstraction, §3.4 telemetry extraction, §3.6 struct cleanup
4. **I2C hardening (1 sprint):** §5.9 mutex guard, §5.7 event logging, §5.14 integration tests
5. **Major refactoring (planned):** §3.1 + §3.2 server/client file splits
6. **Security (when feasible):** §4.2 PIN length, §4.3 per-IP rate limiting, §4.4 HTML escaping
