# Comprehensive Code & Logic Review — TankAlarm System v1.2.1

**Date:** April 2, 2026  
**Reviewer:** GitHub Copilot (Claude Opus 4.6)  
**Scope:** Server, Client, Viewer, I2C Utility, and Common Library  
**Firmware Version:** 1.2.1  
**Previous Review:** CODE_REVIEW_03122026_COMPREHENSIVE.md (March 12, 2026, v1.1.4)

---

## Table of Contents

1. [Executive Summary](#1-executive-summary)
2. [Architecture Overview](#2-architecture-overview)
3. [Common Library Review](#3-common-library-review)
4. [Client Review](#4-client-review)
5. [Server Review](#5-server-review)
6. [Viewer Review](#6-viewer-review)
7. [I2C Utility Review](#7-i2c-utility-review)
8. [Logic Review](#8-logic-review)
9. [Security Assessment](#9-security-assessment)
10. [Reliability & Error Handling](#10-reliability--error-handling)
11. [Memory & Resource Analysis](#11-memory--resource-analysis)
12. [Prioritized Findings](#12-prioritized-findings)
13. [Recommendations Summary](#13-recommendations-summary)

---

## 1. Executive Summary

### Verdict: **STRONG — Production-hardened system with minor improvements available**

The TankAlarm v1.2.1 system is a mature, well-engineered IoT monitoring platform. Since the v1.1.4 review (March 2026), the codebase has been updated to v1.2.1 and continues to demonstrate strong defensive programming practices. The system uses Arduino Opta hardware with Blues Notecard cellular connectivity to monitor industrial tanks, engines, pumps, gas pressure, and flow via 4-20mA current loops, analog sensors, digital float switches, and pulse/RPM sensors.

This review identifies **3 critical**, **8 high**, **16 medium**, and **12 low** severity findings. The critical items include session token entropy, relay control gated behind alarm rate limiting, and UNTIL_CLEAR relay clearing logic. Two critical and several high findings were surfaced during peer cross-review (Appendix A) and validated against source.

### Key Strengths
- **Atomic file persistence** via write-to-temp-then-rename across all critical writes
- **Bounded memory** with ring buffers, hash tables (O(1) sensor lookup), and static allocations
- **Safe string handling** exclusively (`strlcpy`, `snprintf` — no raw `strcpy`/`sprintf`)
- **Power conservation state machine** with hysteresis and debounce (client)
- **Watchdog-safe** patterns with kicks before every blocking I2C operation
- **I2C bus recovery** escalation chain (toggle SCL → reinit Wire → watchdog reset)
- **Tiered historical storage** (RAM hot → LittleFS warm → FTP cold)
- **Constant-time session comparison** preventing timing side channels
- **Config dispatch ACK tracking** with auto-retry
- **Solar-only and battery-failure fallback** modes
- **Notefile schema versioning** for forward compatibility
- **Comprehensive rate limiting** on alarms (per-sensor, global, SMS)

### Key Risks
- Session token PRNG uses LCG seeded from ADC/timing — predictable under analysis
- **Remote relay activation is blocked when alarm rate limit is hit** — safety-critical relay control unreachable
- **UNTIL_CLEAR relay mode can never auto-clear for HIGH/LOW triggers** — relay stuck ON forever
- Server overrides client SMS intent — ignores per-monitor `enableAlarmSms` flag
- Unload notification SMS/email flags never serialized by client — server reads missing fields
- Viewer has no authentication — any LAN user can see sensor data
- FTP credentials stored/transmitted in plaintext (documented, inherent limitation)
- `String` class usage in HTTP handlers risks heap fragmentation on long-running server
- No HTTPS — all web traffic including PIN auth is plaintext over LAN

---

## 2. Architecture Overview

```
┌─────────────┐     Blues Notecard      ┌──────────────┐
│   Client    │ ─── (.qo → Route) ───> │    Server    │
│ (Opta+A0602)│ <── (Route → .qi) ──── │  (Opta Lite) │
│  Sensors    │     Cellular/WiFi       │  Web UI/API  │
└─────────────┘                         └──────┬───────┘
                                               │ .qo Route
                                               ▼
                                        ┌──────────────┐
                                        │   Viewer     │
                                        │  (Opta Lite) │
                                        │  Read-only   │
                                        └──────────────┘
```

**Communication Flow:**
- Client → Server: `telemetry.qo`, `alarm.qo`, `daily.qo`, `unload.qo`, `config_ack.qo`, `serial_log.qo`, `location_response.qo`, `diag.qo`, `health.qo`
- Server → Client: `command.qo` (consolidated, with `_type` field) → Routes to `config.qi`, `relay.qi`, `serial_request.qi`, `location_request.qi`
- Server → Viewer: `viewer_summary.qo` → `viewer_summary.qi`

**Storage Tiers:**
- **Hot:** RAM ring buffers (90 snapshots/sensor)
- **Warm:** LittleFS daily summaries (3 months)
- **Cold:** FTP monthly archives (unlimited)

---

## 3. Common Library Review

### TankAlarm_Common.h
**Status: Good**

Well-organized central header with `#ifndef` guards allowing compile-time override of every constant. The notefile naming documentation is excellent — clearly explains the Blues .qo/.qi conventions and Route Relay wiring.

**Observations:**
- All notefiles correctly follow `.qo` (outbound) and `.qi` (inbound) naming
- `NOTEFILE_SCHEMA_VERSION` provides forward compatibility for payload changes
- All constants are `#ifndef`-guarded for per-project customization

### TankAlarm_Platform.h
**Status: Good**

Clean platform abstraction layer. The `tankalarm_posix_write_file_atomic()` function correctly implements the write-to-temp-then-rename pattern for crash safety.

- **Finding (LOW-1):** `tankalarm_posix_file_exists()` opens and closes a file just to check existence. On LittleFS this is benign, but `access()` or `stat()` would be more efficient if available on the platform.

### TankAlarm_Utils.h
**Status: Good**

- `strlcpy` polyfill is correct — null-checks inputs, handles zero-size buffers
- `tankalarm_roundTo()` uses `pow(10, decimals)` which is fine for the 1-3 decimal range used
- `tankalarm_computeNextAlignedEpoch()` has proper `epoch <= 0.0` and `intervalSeconds == 0` guards

### TankAlarm_I2C.h
**Status: Good**

- `tankalarm_recoverI2CBus()` correctly deinitializes Wire, bit-bangs 16 SCL clocks, generates STOP condition, and reinitializes Wire
- DFU-in-progress guard correctly skips recovery to avoid corrupting firmware transfer
- `Wire.setTimeout()` is set after every `Wire.begin()` call — prevents indefinite blocking
- `tankalarm_readCurrentLoopMilliamps()` has retry logic (3 attempts), drains partial data from Wire buffer on short reads, and checks `Wire.available()` before reading

**Finding (MED-1):** In `tankalarm_recoverI2CBus()`, the SDA pin is configured as `INPUT` (floating), then later driven `LOW` as `OUTPUT` for the STOP condition. On Opta hardware this is correct, but if there's a strong external pull-up, the brief floating period could cause a glitch. Consider using `INPUT_PULLUP` instead of bare `INPUT`.

### TankAlarm_Notecard.h
**Status: Good**

- `tankalarm_ensureTimeSync()` correctly rate-limits to every 6 hours
- `tankalarm_currentEpoch()` adds `millis()` delta for sub-second accuracy between syncs
- `tankalarm_ensureNotecardBinding()` is documented with audit notes confirming no memory leak on repeated calls

**Finding (MED-2):** `tankalarm_currentEpoch()` is vulnerable to `millis()` overflow (every ~49.7 days). The expression `millis() - lastSyncMillis` handles unsigned wraparound correctly, but the resulting `delta` could be very large if the sync hasn't happened in 49+ days. This is mitigated by the 6-hour resync interval, so impact is low.

### TankAlarm_Battery.h
**Status: Good**

- Comprehensive battery type support (Lead-Acid, LiFePO4, LiPo, Custom)
- VinMonitorConfig with voltage divider math is correct: `Vbat = Vadc / (R2/(R1+R2))`
- `vinDividerRatio()` has division-by-zero protection
- BatteryData `mode` field was correctly fixed from dangling `const char*` to fixed buffer (`char mode[16]`) per bugfix note

### TankAlarm_Solar.h / TankAlarm_Solar.cpp
**Status: Good**

- Modbus register addresses match SunSaver MPPT documentation
- Scaling formulas (voltage, current) are correctly implemented
- `readRegisters()` chains reads with `success` flag — cascading failure correctly aborts remaining reads
- `consecutiveErrors >= SOLAR_COMM_FAILURE_THRESHOLD` prevents false communication failure alerts from transient errors

**Finding (LOW-2):** `SolarManager::readRegisters()` makes 11 separate Modbus `requestFrom()` calls. Most SunSaver MPPT registers are contiguous — a bulk read of registers 17-20 and 44-48 would reduce communication overhead and improve reliability.

### TankAlarm_Diagnostics.h
**Status: Good**

- `tankalarm_freeRam()` correctly uses `mbed_stats_heap_get()` on Opta and returns 0 on unsupported platforms
- `TankAlarmHealthSnapshot` provides useful diagnostic telemetry

---

## 4. Client Review

### Overall Assessment: **Strong**

The client firmware demonstrates excellent defensive programming for an embedded system operating in harsh field conditions with unreliable power and connectivity.

### 4.1 Sensor Reading Logic

**Status: Correct**

- `readCurrentLoopSensor()`: Linear mapping from 4-20mA is correct for both pressure (bottom-mount) and ultrasonic (top-mount) sensors. The ultrasonic formula `level = mountHeight - distance` correctly inverts the reading.
- `readAnalogSensor()`: Proper 8-sample averaging reduces noise. The 12-bit ADC resolution (`analogReadResolution(12)`) matches the `/4095.0f` divisor.
- `readDigitalSensor()`: NO/NC switch mode correctly inverts the logic (`NO: LOW=activated`, `NC: HIGH=activated`).
- `readPulseSensor()`: Non-blocking state machine for RPM measurement is well-designed.

**Finding (MED-3):** `readCurrentLoopSensor()` returns the previous reading (`gMonitorState[idx].currentInches`) when `milliamps < 0.0f` (read failure). This is a reasonable stale-data strategy, but the caller has no way to know the reading is stale. If the I2C bus is permanently down, the sensor will perpetually report the last known level. The `sensorFailed` flag in `MonitorRuntime` mitigates this after `SENSOR_FAILURE_THRESHOLD` consecutive failures.

### 4.2 Alarm Logic

**Status: Correct with minor optimization opportunity**

- **Debounce**: Requires `ALARM_DEBOUNCE_COUNT` (3) consecutive threshold violations before triggering — prevents transient noise from causing alarms.
- **Hysteresis**: Alarm clears only when value moves `hysteresisValue` (default 2.0) past the threshold — prevents oscillation.
- **Rate limiting**: Both per-sensor (`MAX_ALARMS_PER_HOUR = 10`) and global (`MAX_GLOBAL_ALARMS_PER_HOUR = 30`) limits prevent alarm storms.
- **Stuck sensor detection**: After `SENSOR_STUCK_THRESHOLD` (10) identical readings, sensor is flagged as failed.

**Finding (MED-4):** The `MonitorRuntime.alarmTimestamps[]` array is sized at `MAX_ALARMS_PER_HOUR` (10 entries) and uses `millis()` for timestamps. If the system has been running for 49+ days without a restart, `millis()` wraps around. The rate limiting uses `(now - alarmTimestamps[i]) < 3600000UL` which handles unsigned wraparound correctly. No action needed.

**Finding (CRITICAL-2): Remote relay activation is gated behind the alarm rate limit.** *(Cross-ref: GPT-5.4 Logic Review)* In `sendAlarm()` (line ~4717), `checkAlarmRateLimit()` returns false when the hourly cap is reached, and `sendAlarm()` returns immediately — **before** the relay control logic at line ~4808. While `activateLocalAlarm()` is correctly called before the rate limit check, remote relay commands (`triggerRemoteRelays()`) are never issued. This means a verified alarm that should activate a pump, valve, or equipment on a remote client will silently fail whenever rate limiting is active. **Fix:** Move relay logic (lines 4795-4845) to execute before the rate limit return, or separate relay actuation from Notecard message transmission.

**Finding (MED-12): Global alarm suppression consumes per-monitor quota.** *(Cross-ref: GPT-5.4 Logic Review)* In `checkAlarmRateLimit()` (line ~4625), the per-monitor timestamp is added to `alarmTimestamps[]` **before** the global `MAX_GLOBAL_ALARMS_PER_HOUR` check at line ~4639. If the global cap is hit, the function returns `false`, but the per-monitor quota has already been consumed. This means global suppression accelerates per-monitor rate exhaustion. **Fix:** Move the per-monitor timestamp insertion to after the global check passes.

### 4.3 Relay Control

**Status: Good with one concern**

- Three relay modes: `MOMENTARY` (auto-off after duration), `UNTIL_CLEAR` (auto-off when alarm clears), `MANUAL_RESET` (requires server command)
- Per-relay momentary durations are independently configurable
- `RELAY_COMMAND_COOLDOWN_MS` (5s) prevents rapid toggling from stale/replayed commands

**Finding (HIGH-1): Relay commands from the server do not verify the target client UID.** If a Notehub Route is misconfigured, relay commands could be delivered to the wrong client device, potentially actuating pumps or valves on the wrong site. The client should validate that `doc["target"]` matches its own device UID before acting on relay commands.

**Finding (CRITICAL-3): UNTIL_CLEAR relays can never auto-clear for HIGH or LOW triggers.** *(Cross-ref: GPT-5.4 Logic Review)* In `sendAlarm()` (lines 4819-4826), when alarm clears (`alarmType = "clear"`), the code checks: `strcmp(alarmType, "high") == 0` and `strcmp(alarmType, "low") == 0`. Since `alarmType` is `"clear"` at this point, both comparisons always evaluate to `false`. Only `RELAY_TRIGGER_ANY` clears successfully. For HIGH-only or LOW-only triggers, the relay remains ON forever — requiring manual reset or a momentary timeout if misconfigured. **Fix:** When clearing, don't match against `alarmType` (which is always `"clear"`). Instead, unconditionally set `shouldClear = true` when the relay is active for this monitor, or track the original trigger type.

**Finding (MED-15): Critical-hibernate relay shutdown not restored on recovery.** *(Cross-ref: GPT-5.4 Logic Review)* When `CRITICAL_HIBERNATE` power state forces all relays OFF, the recovery path back to `LOW_POWER` or `ECO` does not recompute active relay state. Any relays that were legitimately ON (e.g., UNTIL_CLEAR holding a pump on) remain OFF after battery recovery.

**Finding (MED-16): Multi-relay remote actions drain one Notecard queue entry per poll.** *(Cross-ref: GPT-5.4 Code Review)* `triggerRemoteRelays()` emits one `note.add` per bit in the relay mask (up to 4 notes). The receiving side drains one note per poll cycle via `note.get`. If 4 relays are triggered simultaneously, it takes 4 poll cycles to process all commands, introducing latency proportional to the relay count.

### 4.4 Power Conservation State Machine

**Status: Excellent**

The 4-state machine (NORMAL → ECO → LOW_POWER → CRITICAL_HIBERNATE) is well-designed:
- **Entry/exit hysteresis**: 0.4-0.7V gap between enter and exit thresholds prevents oscillation
- **Debounce**: `POWER_STATE_DEBOUNCE_COUNT` (3) consecutive readings required before state change
- **Progressive degradation**: Each state increases sleep intervals and reduces sync frequency
- **CRITICAL_HIBERNATE**: Relays are forced OFF and only essential monitoring continues
- **Remote-tunable thresholds**: Server can adjust voltage trip points without firmware update

### 4.5 Solar-Only Mode

**Status: Good**

The solar-only mode correctly handles the unique challenges of battery-less solar installations:
- Startup debounce requires stable voltage for `startupDebounceSec` before reading sensors
- Sunset protocol detects declining voltage and saves state before power loss
- Opportunistic daily reports are sent ASAP after boot if overdue
- State persistence to LittleFS survives power cycles

### 4.6 Telemetry & Daily Reports

**Status: Good**

- Multi-part daily reports correctly handle large sensor arrays by splitting across multiple notes
- VIN voltage is correctly sourced from the best available: Vin divider → Notecard card.voltage → SunSaver MPPT
- Telemetry includes firmware version for server-side version tracking

### 4.7 Configuration Management

**Status: Good**

- Config is persisted to LittleFS via atomic write
- Schema version detection allows detecting stale configs
- Config ACK sent back to server after applying updates
- `hardwareChanged` flag correctly triggers Notecard hub.set reconfiguration

**Finding (MED-5):** `applyConfigUpdate()` applies field-by-field updates from partial JSON payloads. If a field is missing from the update, the previous value is preserved. This is correct for incremental updates, but there's no mechanism to explicitly reset a field to its default value (e.g., removing a relay target). Consider adding support for explicit `null` values to clear fields.

### 4.8 I2C Bus Recovery (Client-Specific)

**Status: Good**

The client has a sophisticated 3-tier I2C failure detection:
1. **Notecard-only failures**: After `I2C_NOTECARD_RECOVERY_THRESHOLD` (10) failures → bus recovery
2. **Sensor-only failures**: After `I2C_SENSOR_ONLY_RECOVERY_THRESHOLD` (10) with exponential backoff → bus recovery, circuit breaker after `I2C_SENSOR_RECOVERY_MAX_ATTEMPTS` (5)
3. **Dual failures** (Notecard + sensors): After `I2C_DUAL_FAIL_RECOVERY_LOOPS` (30) → bus recovery; after `I2C_DUAL_FAIL_RESET_LOOPS` (120) → watchdog reset

### 4.9 Note Buffering

**Status: Good**

When Notecard is offline, notes are buffered to LittleFS (`/pending_notes.log`) up to `NOTE_BUFFER_MAX_BYTES` (16KB) with `NOTE_BUFFER_MIN_HEADROOM` (2KB) guard against filling storage. Notes are replayed when the Notecard recovers.

---

## 5. Server Review

### Overall Assessment: **Strong**

The server is the most complex component (~20K lines) handling telemetry ingestion, web UI, SMS/email dispatching, calibration learning, tiered storage, and fleet management.

### 5.1 Notecard Polling

**Status: Good**

- `processNotefile()` uses peek-then-delete pattern for crash safety — notes are only deleted after successful processing
- `MAX_NOTES_PER_FILE_PER_POLL` (10) prevents long blocking drains
- Notecard failure tracking with threshold-based online/offline transitions
- Battery and health check backoff with exponential intervals up to `NOTECARD_HEALTH_CHECK_MAX_INTERVAL_MS`

### 5.2 Telemetry Handling

**Status: Good with one finding**

- `handleTelemetry()` correctly uses `upsertSensorRecord()` for O(1) hash table lookup
- 24-hour change tracking uses a 22-hour window to roll previous readings — handles timing jitter
- Object type fallback correctly tries: doc field → cached client config → "tank" default
- Raw sensor readings (mA, voltage) are stored for server-side recalculation with calibration

**Finding (MED-6):** `handleTelemetry()` calls `convertMaToLevelWithTemp()` for current-loop sensors, which uses the calibration learning system. If the calibration data is stale or incorrect, the server's calculated level will diverge from the client's level. The client should also send its own calculated level (not just raw mA) as a cross-check.

### 5.3 Alarm Handling

**Status: Good**

- System alarms (solar/battery/power) correctly stored on `ClientMetadata` instead of creating phantom sensor records
- SMS rate limiting via `checkSmsRateLimit()` prevents spam
- Server-side SMS policy flags (`smsOnHigh`, `smsOnLow`, `smsOnClear`) give operators control
- Diagnostic alarm types (sensor-fault, sensor-stuck, sensor-recovered) are correctly filtered from SMS

**Finding (LOW-3):** In `handleAlarm()`, the SMS message for digital sensors uses `%d` format for `rec->userNumber` (which is `uint8_t`), and for `rec->sensorIndex` (also `uint8_t`). The `snprintf` call could produce mangled messages if the format string interpolation doesn't match the argument types. The `%d` format works for `uint8_t` due to implicit integer promotion, but `%u` would be more semantically correct.

**Finding (HIGH-7): Server overrides client per-monitor SMS intent.** *(Cross-ref: GPT-5.4 Code Review)* In `handleAlarm()` (lines 8412-8434), the server force-sets `smsEnabled = true` for high, low, clear, and digital alarm types, regardless of the client's `se` (SMS escalation) flag. The client's `MonitorConfig.enableAlarmSms` setting only controls whether `doc["se"] = true` is included in the alarm note. The server ignores this field and applies its own policy flags (`smsOnHigh`, `smsOnLow`, etc.) unconditionally. While server-side policy override may be intentional, it silently nullifies per-monitor SMS opt-out configurations.

**Finding (HIGH-8): Unload notification flags not serialized by client.** *(Cross-ref: GPT-5.4 Code Review)* In `handleUnload()` on the server (line ~8848), the code reads `doc["sms"]` and `doc["email"]` from the unload note payload. However, the client's `sendUnloadEvent()` (line ~4983) never includes these fields in the JSON. As a result, unload events never trigger SMS or email notifications regardless of configuration.

### 5.4 Daily Email

**Status: Good**

- Contacts system with named recipients and role-based routing
- Static `char buffer[MAX_EMAIL_BUFFER]` avoids stack overflow on Mbed OS (4-8KB stack)
- Watchdog kicked before long JSON serialization + I2C transaction
- Duplicate email prevention via `MIN_DAILY_EMAIL_INTERVAL_SECONDS` rate limit
- Document overflow check (`doc.overflowed()`) prevents sending truncated data

### 5.5 Web API Security

**Status: Functional with improvements needed**

- Sessions validated via `X-Session` header on all `/api/` routes except login/session-check
- Single active session enforced (new login invalidates previous)
- Constant-time comparison prevents timing attacks
- Auth lockout after `AUTH_MAX_FAILURES` (5) attempts for `AUTH_LOCKOUT_DURATION` (30 seconds)
- HTTP body size capped per route

**Finding (CRITICAL-1): Session token PRNG is deterministic.** The `generateSessionToken()` function uses a 64-bit LCG (Linear Congruential Generator) seeded from `micros()`, `millis()`, and 4x `analogRead()`. While the ADC noise adds some entropy, the LCG is algebraically invertible — given one output, an attacker can derive the full internal state and predict all future tokens. An attacker on the same LAN who observes one token (e.g., via network sniffing since there's no HTTPS) could predict the next token. **Mitigation**: Use hardware RNG (`HAL_RNG_GenerateRandomNumber()` available on STM32H747) or accumulate more entropy over time.

**Finding (HIGH-2): No CSRF protection on state-changing POST endpoints.** The server validates the session token via `X-Session` header, which provides implicit CSRF protection against simple form submissions (forms can't set custom headers). However, XMLHttpRequest from malicious pages on the same LAN could set custom headers if CORS is not enforced. The server does not send CORS headers, which means browsers will block cross-origin requests — but only if the response includes proper CORS denial. **Recommendation**: Explicitly send `Access-Control-Allow-Origin` headers limited to the server's own origin.

**Finding (HIGH-3): Viewer has no authentication.** The Viewer's web UI (`/` and `/api/sensors`) is completely unauthenticated. Any device on the LAN can see all sensor data, site names, client UIDs, and alarm states. While the Viewer is described as "read-only," the exposed data (site names, UIDs, levels) could be sensitive in industrial settings.

**Finding (HIGH-6): PIN first-login accepts non-digit characters.** *(Cross-ref: Copilot Code Review)* In the PIN login handler (line ~5950), the first-login path validates only `strlen(pin) == 4` without calling `isValidPin()` to verify all characters are digits. An operator could set a PIN like `"ab!@"` on first use, which might cause issues with the numeric keypad input on the dashboard.

**Finding (MED-13): Manual JSON string building without escaping in web endpoints.** *(Cross-ref: Copilot Code Review)* Several web response handlers (line ~13294+) build JSON via `String` concatenation instead of using ArduinoJson's serializer. If any interpolated value contains a double-quote or backslash, the resulting JSON will be malformed. Most values come from controlled sources, but site names and sensor labels are user-configured.

**Finding (MED-14): Global auth lockout enables DoS from any LAN user.** *(Cross-ref: Copilot Code Review)* The `AUTH_MAX_FAILURES` (5) / `AUTH_LOCKOUT_DURATION` (30s) mechanism is global — not per-IP. Any device on the LAN can trigger lockout for all users by sending 5 bad login attempts, blocking the legitimate operator for 30 seconds. Repeated attacks could effectively deny access indefinitely.

### 5.6 Stale Client Detection

**Status: Good**

- 49-hour threshold correctly allows one missed daily report before flagging
- Per-sensor orphan pruning: when some sensors update but others don't (post-reconfiguration), stale sensors are auto-pruned after 72 hours
- Full client auto-removal after 7 days of inactivity prevents ghost entries
- FTP archiving before removal preserves historical data for clients active >30 days
- `deduplicateSensorRecordsLinear()` called proactively during stale checks catches any duplicate records

### 5.7 Calibration Learning System

**Status: Good**

- Multiple linear regression with temperature compensation
- R² goodness-of-fit metric for quality assessment
- Minimum entry count required before enabling learned calibration
- Temperature normalization around 70°F reference for numerical stability
- NWS API integration for weather data during calibration

**Finding (MED-7):** The NWS API calls use HTTP (port 80), not HTTPS. The NWS API may redirect HTTP to HTTPS, which the Arduino Ethernet library cannot follow. If NWS enforces HTTPS-only, temperature data will silently fail, and calibration temperature compensation will be disabled. This is a known limitation documented in the code.

### 5.8 Tiered History

**Status: Good**

- Hot tier: 90-entry ring buffer per sensor in RAM
- Warm tier: Daily summary files on LittleFS (min/max/avg/samples, 3-month retention)
- Cold tier: FTP monthly archives (unlimited)
- Hot tier persisted to LittleFS on reboot for continuity

### 5.9 Config Dispatch

**Status: Good**

- Configs sent via consolidated `command.qo` with `_type` routing
- ACK tracking with timestamps enables retry and status display
- Config snapshots cached for comparison and resend
- Orphan sensor pruning triggered on config ACK when sensor count changes

---

## 6. Viewer Review

### Overall Assessment: **Good — Intentionally minimal**

The Viewer is a read-only kiosk that mirrors the server's dashboard via periodic Notecard summary fetches.

### 6.1 Summary Fetching

**Status: Good**

- Summary fetch scheduled at aligned intervals (default: every 6 hours starting 6 AM)
- Fetches all queued notes in a loop with a safety cap of 20 per call
- Peek-then-delete pattern for crash safety
- Server metadata (name, UID, cadence) propagated to Viewer UI

### 6.2 Web UI

**Status: Good**

- Dashboard HTML is stored in PROGMEM to save RAM
- Chunked PROGMEM reads (128 bytes) prevent stack overflow
- `escapeHtml()` in JavaScript prevents XSS when rendering sensor data
- `MAX_HTTP_BODY_BYTES` (1KB) limits incoming request body size
- Stale data detection (>26 hours) with visual warning (dimmed row + ⚠️)

**Finding (MED-8):** The Viewer allocates `JsonDocument` on the heap via `new (std::nothrow)` in `sendSensorJson()`. If heap is exhausted, a 500 error is returned. This is correct, but the `serializeJson(doc, body)` call serializes into a `String`, which also uses heap. If the sensor count is large, the combined allocation could fail. Consider streaming JSON directly to the `EthernetClient` instead.

### 6.3 MAC Address Derivation

**Status: Good**

- DJB2 hash of device UID produces deterministic MAC address
- Byte 0 = 0x02 (locally administered, unicast) is correct per IEEE 802
- Avoids MAC conflicts when multiple Viewers share a network

### 6.4 Notecard Health

**Status: Good**

- Exponential backoff health checks (5 min → 80 min max)
- I2C bus recovery triggered after `I2C_NOTECARD_RECOVERY_THRESHOLD` failures
- Notecard binding re-established after bus recovery

---

## 7. I2C Utility Review

### Overall Assessment: **Good — Clean diagnostic tool**

The I2C Utility provides a serial-based menu for diagnosing and recovering Notecard issues. It's intentionally simple and self-contained.

- Input validation on hex address parsing (`0x08-0x77` range check)
- `readLine()` with timeout prevents indefinite blocking
- All Notecard API calls have response error checking
- Required `extern` variables (`gCurrentLoopI2cErrors`, `gI2cBusRecoveryCount`) are properly defined

---

## 8. Logic Review

### 8.1 Time Synchronization Logic

**Correctness: Good**

The time system uses Notecard `card.time` as the source of truth, syncing every 6 hours. Between syncs, `millis()` delta provides sub-second drift. The `millis()` wraparound at ~49.7 days is handled correctly by unsigned subtraction semantics.

**Finding (MED-9):** If the Notecard has no cellular connectivity and never syncs time, `lastSyncedEpoch` stays at 0.0, and `currentEpoch()` returns 0.0. The daily report scheduler, summary scheduler, and stale detection all correctly check for `epoch > 0.0` before acting. However, the 22-hour 24h-change tracking window uses `epoch > 0.0` as an initialization guard but doesn't handle the case where time was never available — a client that never syncs time will never compute 24-hour changes, which is acceptable behavior.

### 8.2 Notefile Message Ordering

**Correctness: Good**

Blues Notecard delivers notes in FIFO order. The server processes notes one at a time with peek-then-delete. If the server crashes mid-processing, the undeleted note will be reprocessed on restart. `handleTelemetry()` and `handleAlarm()` are idempotent for the same note (upsert pattern), so reprocessing is safe.

**Finding (MED-10):** `processNotefile()` uses `note.get` without specifying a note ID, which returns the oldest note. After processing, it calls `note.get` again with `"delete": true` to consume it. If another note arrives between the peek and delete calls, the **new** note could be deleted instead of the processed one. On Blues Notecard, `note.get` with `delete: true` returns AND deletes the oldest note atomically, so the consumed note should be the same one returned by the peek. However, if the Notecard internally reorders, this could cause a note to be skipped. **Risk**: Very low — Blues documentation confirms FIFO ordering.

### 8.3 Alarm State Machine Logic

**Correctness: Good**

The client-side alarm state machine follows this flow:
1. Sensor reading crosses threshold → increment debounce counter
2. Counter reaches `ALARM_DEBOUNCE_COUNT` (3) → alarm triggers, send alarm note
3. Sensor reading returns within threshold + hysteresis → increment clear counter
4. Clear counter reaches `ALARM_DEBOUNCE_COUNT` → alarm clears, send clear note

The hysteresis band prevents oscillation at the threshold boundary. The debounce count prevents transient spikes from triggering alarms.

**Finding (LOW-4):** If a sensor transitions from high alarm directly to low alarm (e.g., rapid level drop past both thresholds), the clear debounce for the high alarm and the trigger debounce for the low alarm run concurrently. The high alarm will clear after 3 samples at or below the high threshold - hysteresis, at which point the low alarm debounce is also counting. This means the low alarm will trigger 3 samples after crossing the low threshold, which is correct behavior. No issue found.

### 8.4 Relay Forwarding Logic

**Correctness: Good with caveat**

When a client alarm triggers relays on a **remote** client:
1. Client sends `relay_forward.qo` with target UID and relay mask
2. Route delivers to server's `relay_forward.qi`
3. Server validates and re-issues via `command.qo` with `_type: relay`
4. Route delivers to target client's `relay.qi`
5. Target client actuates relays

The server acts as a trusted relay, which prevents clients from directly commanding each other. The `RELAY_COMMAND_COOLDOWN_MS` (5s) on the target client prevents rapid toggling.

**Finding (HIGH-4):** The relay forwarding path has no authorization check — any client can request relay actuation on any other client, as long as it knows the target UID. The server should validate that the requesting client's config actually lists the target as a `relayTargetClient`. This prevents a compromised or misconfigured client from toggling relays on arbitrary devices.

### 8.5 Power State Transition Logic

**Correctness: Excellent**

The hysteresis-based power state machine is well-designed:
- NORMAL → ECO: below 12.0V (enter), above 12.4V (exit) — 0.4V hysteresis
- ECO → LOW_POWER: below 11.8V (enter), above 12.3V (exit) — 0.5V hysteresis
- LOW_POWER → CRITICAL: below 11.5V (enter), above 12.2V (exit) — 0.7V hysteresis

The increasing hysteresis at lower voltages is appropriate because battery voltage recovery is slower when deeply discharged.

### 8.6 Daily Report Multi-Part Logic

**Correctness: Good**

Daily reports split across multiple notes when the sensor count exceeds what fits in a single JSON payload. Each part includes a part number (`p`) and a more-parts flag (`m`). The server tracks received parts via a bitmask and considers the report complete when the final part (with `m=false`) is received.

**Finding (LOW-5):** The `dailyPartsReceived` bitmask is `uint8_t`, limiting tracking to 8 parts. With `MAX_MONITORS = 8` sensors per client, each part carrying 2-3 monitors, 3-4 parts max is expected. This is sufficient for current configuration but would need expansion if `MAX_MONITORS` increases significantly.

### 8.7 Sensor Hash Table Logic

**Correctness: Good**

- DJB2 hash with linear probing
- Table size (128) validated via `static_assert` to be ≥ 2× `MAX_SENSOR_RECORDS` (64)
- Power-of-2 size enables efficient modulo via bitwise AND
- `SENSOR_HASH_EMPTY` (0xFF) sentinel correctly distinguishes empty slots from valid indices (max 63)

### 8.8 Unload Detection Logic

**Correctness: Good**

The fill-and-empty tank tracking follows:
1. Track peak level as tank fills
2. When level drops below `unloadDropPercent` (50%) of peak → trigger unload event
3. Debounce with `UNLOAD_DEBOUNCE_COUNT` (3) consecutive low readings
4. Log unload with peak and empty levels, send SMS/email if configured
5. Reset peak tracking for next fill cycle

Minimum peak height (`UNLOAD_MIN_PEAK_HEIGHT = 12 inches`) prevents false triggers from noise.

### 8.9 Solar-Only Sunset Protocol Logic

**Correctness: Good**

1. Monitor Vin voltage continuously
2. When Vin drops below `sunsetVoltage` → start sunset timer
3. If Vin stays below for `sunsetConfirmSec` (120s) → save state to LittleFS
4. On power loss, state is preserved; on next boot, state is restored
5. Opportunistic reporting: if >20 hours since last report, send immediately after boot

The fallback from battery mode to solar-only mode on consecutive critical battery readings is a smart design decision.

### 8.10 Historical Analytics Logic (MoM / YoY)

**Correctness: Flawed — hot-tier filtering issues**

*(Findings surfaced during peer cross-review and validated against source)*

**Finding (MED-17): MoM hot-tier stats aggregate ALL snapshots regardless of month.** *(Cross-ref: Copilot Logic Review)* In `handleHistoryMonthOverMonth()` (line ~11107), when `currInHotTier` is true, the loop iterates all `hist.snapshotCount` ring buffer entries without filtering by month. The hot tier retains ~90 hourly snapshots (~3.75 days), so in practice the contamination window is small (only a few snapshots from the previous month at month boundaries). However, the stats labeled "current month" may include late readings from the previous month. Similarly, the `prevInHotTier` path (line ~11157) has the same issue.

**Finding (LOW-10): YoY hot-tier current-year stats ignore calendar boundaries.** *(Cross-ref: Copilot Logic Review)* In `handleHistoryYearOverYear()` (line ~11280), current-year stats from the hot tier iterate all snapshots without year filtering. This only matters in the first few days of January when the ring buffer may contain December readings. Impact is minimal.

**Finding (LOW-11): YoY fallback year hardcoded to 2025.** *(Cross-ref: Copilot Logic Review)* At line ~11241: `int currentYear = nowTm ? nowTm->tm_year + 1900 : 2025;` — the fallback is benign (only triggers when time sync has never occurred), but should use a compile-time constant or omit YoY results when time is unavailable.

**Finding (LOW-12): Debug mutation endpoint active in production.** *(Cross-ref: Copilot Code Review)* The server routes `/api/debug/sensors` with a `dedup` action that can mutate the sensor registry. This should be gated behind a `#ifdef DEBUG` guard or removed from production builds.

---

## 9. Security Assessment

### 9.1 Authentication
| Aspect | Status | Notes |
|--------|--------|-------|
| PIN authentication | ✅ Good | PIN stored in config, not hardcoded |
| Brute-force protection | ✅ Good | 5 attempts, 30s lockout |
| Session tokens | ⚠️ Weak PRNG | LCG is algebraically invertible (CRITICAL-1) |
| Constant-time comparison | ✅ Good | Prevents timing side channels |
| Session invalidation | ✅ Good | New login invalidates previous token |
| Viewer auth | ❌ None | No password required (HIGH-3) |

### 9.2 Input Validation
| Aspect | Status | Notes |
|--------|--------|-------|
| HTTP body size limits | ✅ Good | Per-route size caps |
| String copy safety | ✅ Good | `strlcpy`/`snprintf` everywhere |
| JSON deserialization | ✅ Good | ArduinoJson handles malformed input |
| I2C address validation | ✅ Good | Range-checked against 0x08-0x77 |
| Measurement unit validation | ✅ Good | `isValidMeasurementUnit()` whitelist |

### 9.3 Data Protection
| Aspect | Status | Notes |
|--------|--------|-------|
| Transport encryption | ❌ No HTTPS | Hardware limitation (no TLS library for Opta) |
| FTP credentials | ⚠️ Plaintext | Inherent FTP limitation, documented |
| Cellular security | ✅ Good | Blues Notecard handles TLS to Notehub |
| Flash persistence | ✅ Good | Atomic writes prevent corruption |

### 9.4 Authorization
| Aspect | Status | Notes |
|--------|--------|-------|
| Relay command source validation | ⚠️ Missing | No client UID check (HIGH-1) |
| Relay forward authorization | ⚠️ Missing | No config cross-check (HIGH-4) |
| API route protection | ✅ Good | Session required for all /api/ except login |
| Viewer read-only enforcement | ✅ Good | Only GET / and /api/sensors routes exist |

---

## 10. Reliability & Error Handling

### 10.1 Watchdog Management
**Status: Excellent**

All blocking operations are preceded by watchdog kicks:
- Before I2C bus scans
- Before Notecard request/response calls
- During long JSON serialization
- Inside `safeSleep()` chunked delay
- Before daily email transmission

The `WATCHDOG_TIMEOUT_SECONDS` (30s) is conservative enough to allow recovery from most I2C hangs.

### 10.2 Notecard Offline Mode
**Status: Good**

Client correctly buffers notes to LittleFS when Notecard is unavailable and replays them on recovery. Server correctly marks Notecard as offline after threshold and uses exponential backoff health checks.

### 10.3 Flash Storage
**Status: Good**

- Atomic write pattern prevents corruption on power loss
- `.tmp` file cleanup on boot handles interrupted writes
- `isStorageAvailable()` checked before any file operation
- Periodic save intervals prevent excessive flash wear

### 10.4 Network Resilience
**Status: Good**

- DHCP with retry logic (3 attempts, increasing delays)
- Link state monitoring with reconnection
- Web request timeout (5 seconds)
- Response chunking for large payloads

**Finding (HIGH-5):** The server's `handleWebRequests()` reads HTTP requests character-by-character into `String` objects. Under sustained traffic (e.g., multiple browser tabs auto-refreshing), the repeated `String` concatenation causes heap fragmentation. Over days/weeks of uptime, this could lead to allocation failures. **Mitigation**: Use fixed-size `char[]` buffers for HTTP parsing, similar to how the daily email buffer is handled.

---

## 11. Memory & Resource Analysis

### 11.1 Static RAM Allocation (Server)

| Component | Size (bytes) | Count | Total |
|-----------|-------------|-------|-------|
| SensorRecord | ~224 | 64 | ~14,336 |
| ClientMetadata | ~208 | 20 | ~4,160 |
| SensorHourlyHistory | ~840 | 20 | ~16,800 |
| AlarmLogEntry | ~152 | 50 | ~7,600 |
| TransmissionLogEntry | ~184 | 50 | ~9,200 |
| UnloadLogEntry | ~172 | 50 | ~8,600 |
| SensorCalibration | ~136 | 20 | ~2,720 |
| ServerSerialBuffer | ~10,080 | 1 | ~10,080 |
| ClientSerialBuffer | ~5,088 | 5 | ~25,440 |
| Hash table | 128 | 1 | 128 |
| Daily email buffer | 16,384 | 1 | 16,384 |
| Embedded HTML/CSS | varies | ~10 pages | ~60,000+ |
| **Estimated Total** | | | **~175KB+** |

The Arduino Opta has 1MB SRAM, so this is well within limits (~17.5% utilization). However, the embedded HTML pages (stored in PROGMEM or const arrays) and ArduinoJson dynamic allocations add significant heap pressure.

### 11.2 Static RAM Allocation (Client)

| Component | Size (bytes) | Count | Total |
|-----------|-------------|-------|-------|
| ClientConfig | ~1,200 | 1 | ~1,200 |
| MonitorRuntime | ~128 | 8 | ~1,024 |
| RPM state arrays | ~32 | 8 | ~256 |
| Serial log buffer | ~5,000 | 1 | ~5,000 |
| **Estimated Total** | | | **~7.5KB** |

The client's memory footprint is modest, leaving ample room for stack and heap.

### 11.3 Flash Storage Usage

Each device uses LittleFS on the internal flash. Key files:
- Server: config (~1KB), sensor registry (~10KB), client metadata (~5KB), calibration data (~5KB), history (~50KB+)
- Client: config (~2KB), note buffer (~16KB max), solar state (~200B)
- Viewer: config (~500B)

The Opta has 2MB internal flash, and LittleFS typically uses 512KB-1MB of that. Current usage is well within limits.

---

## 12. Prioritized Findings

### Critical (3)

| ID | Component | Finding | Impact |
|----|-----------|---------|--------|
| CRITICAL-1 | Server | Session token PRNG uses invertible LCG — tokens predictable if one is observed | Authentication bypass on LAN |
| CRITICAL-2 | Client | Remote relay activation gated behind alarm rate limit — `sendAlarm()` returns before relay logic | Safety-critical relay control unreachable during rate limiting |
| CRITICAL-3 | Client | UNTIL_CLEAR relay mode checks `alarmType == "high"/"low"` but alarmType is always `"clear"` | Relay stuck ON forever for HIGH/LOW triggers |

### High (8)

| ID | Component | Finding | Impact |
|----|-----------|---------|--------|
| HIGH-1 | Client | Relay commands execute without target UID verification | Wrong device could actuate relays |
| HIGH-2 | Server | No explicit CORS headers — relies on browser default same-origin behavior | Potential CSRF via XMLHttpRequest |
| HIGH-3 | Viewer | No authentication on web UI | Sensor data exposed to LAN |
| HIGH-4 | Server | Relay forwarding path has no authorization check | Client impersonation could trigger relays |
| HIGH-5 | Server | `String` concatenation in HTTP parser causes heap fragmentation | Server memory exhaustion over time |
| HIGH-6 | Server | PIN first-login accepts non-digit characters — `strlen(pin) == 4` without `isValidPin()` | Malformed PIN could lock out operator |
| HIGH-7 | Server | Server force-sets `smsEnabled = true` for alarm types, overriding client `se` flag | Per-monitor SMS opt-out silently ignored |
| HIGH-8 | Client/Server | Client `sendUnloadEvent()` never includes `sms`/`email` fields; server reads them | Unload SMS/email notifications never fire |

### Medium (16)

| ID | Component | Finding | Impact |
|----|-----------|---------|--------|
| MED-1 | Common | I2C recovery SDA briefly floating during recovery | Potential I2C glitch (theoretical) |
| MED-2 | Common | `currentEpoch()` doesn't explicitly handle 49-day millis wraparound | Mitigated by 6h resync |
| MED-3 | Client | Sensor read failure returns stale data silently | Mask prolonged sensor outage |
| MED-4 | Client | Alarm rate-limiting timestamps use millis() | Correct due to unsigned math |
| MED-5 | Client | No mechanism to explicitly clear config fields to defaults | Cannot remove relay targets |
| MED-6 | Server | Server recalculates level from raw mA — may diverge from client | Inconsistent level display |
| MED-7 | Server | NWS API uses HTTP — may fail if NWS enforces HTTPS | Temperature compensation disabled |
| MED-8 | Viewer | JSON→String serialization uses heap — could OOM with many sensors | Viewer 500 error |
| MED-9 | All | No time available = no scheduled operations | Acceptable degradation |
| MED-10 | Server | Peek-then-delete note processing assumes FIFO ordering | Very low risk of note skip |
| MED-11 | Server | Embedded HTML pages in PROGMEM/const consume significant flash | Future scalability concern |
| MED-12 | Client | Global alarm suppression consumes per-monitor quota — timestamp added before global check | Per-monitor rate budget drained by global cap |
| MED-13 | Server | Manual JSON string building via String concatenation without escaping | Malformed JSON if values contain quotes |
| MED-14 | Server | Global auth lockout (5 attempts/30s) is not per-IP — any LAN device can lock out all users | DoS against operator login |
| MED-15 | Client | Critical-hibernate relay shutdown not restored on battery recovery | UNTIL_CLEAR relays left OFF after recovery |
| MED-16 | Client | Multi-relay remote actions emit one note per bit — receiver drains one per poll | Relay activation latency scales with relay count |

### Low (12)

| ID | Component | Finding | Impact |
|----|-----------|---------|--------|
| LOW-1 | Common | `file_exists()` uses fopen/fclose instead of stat() | Minor inefficiency |
| LOW-2 | Common | SolarManager does individual register reads instead of bulk | Communication overhead |
| LOW-3 | Server | SMS format string uses %d for uint8_t | Correct via promotion, not ideal |
| LOW-4 | Client | High-to-low alarm transition concurrent debounce | Correct behavior |
| LOW-5 | Server | Daily report bitmask limited to 8 parts | Sufficient for current config |
| LOW-6 | Server | Multiple embedded HTML pages increase binary size | No runtime impact |
| LOW-7 | All | Debug output via `Serial.print()` even in production | Minor power waste |
| LOW-8 | I2C Utility | `safeSleep()` uses `delay()` without watchdog | OK — utility has no watchdog |
| LOW-9 | Viewer | `respondStatus()` missing 401 case in switch | Falls through to "Error" label |
| LOW-10 | Server | YoY hot-tier current-year stats don't filter by calendar year | Minor stat contamination at year boundary |
| LOW-11 | Server | YoY fallback year hardcoded to 2025 when time unavailable | Cosmetic — only triggers without time sync |
| LOW-12 | Server | `/api/debug/sensors` dedup action active in production | Debug mutation endpoint reachable |

---

## 13. Recommendations Summary

### Immediate (3)

1. **CRITICAL-2**: Decouple relay actuation from rate-limited alarm transmission. Move relay logic before the rate limit return in `sendAlarm()`, or extract relay control into a separate function called unconditionally:
   ```cpp
   activateLocalAlarm(idx, isAlarm);
   handleRelayControl(idx, alarmType, isAlarm, cfg);  // NEW: before rate limit
   if (!checkAlarmRateLimit(idx, alarmType)) {
     return;  // Only blocks Notecard transmission
   }
   ```

2. **CRITICAL-3**: Fix UNTIL_CLEAR relay clearing logic. When `alarmType == "clear"`, unconditionally clear all active relays for the monitor instead of matching against the trigger type:
   ```cpp
   } else {
     // Alarm cleared — deactivate relay if mode is UNTIL_CLEAR
     if (cfg.relayMode == RELAY_MODE_UNTIL_CLEAR && gRelayActiveForMonitor[idx]) {
       shouldDeactivateRelay = true;  // Always clear on alarm clear
     }
   }
   ```

3. **CRITICAL-1**: Replace LCG-based token generation with STM32 hardware RNG:
   ```cpp
   #include "stm32h7xx_hal.h"
   RNG_HandleTypeDef hrng;
   hrng.Instance = RNG;
   HAL_RNG_Init(&hrng);
   uint32_t random;
   HAL_RNG_GenerateRandomNumber(&hrng, &random);
   ```

### Short-Term (6)

4. **HIGH-1**: Add client UID verification on incoming relay commands:
   ```cpp
   const char *target = doc["target"] | "";
   if (strlen(target) > 0 && strcmp(target, gDeviceUID) != 0) {
     Serial.println(F("Relay command not for this device, ignoring"));
     return;
   }
   ```

5. **HIGH-4**: Validate relay forward requests against the requesting client's config before re-issuing.

6. **HIGH-5**: Replace `String` concatenation in `readHttpRequest()` with fixed-size `char[]` buffers.

7. **HIGH-6**: Call `isValidPin()` in the first-login path to ensure PIN contains only digits.

8. **HIGH-7**: Respect the client's `se` flag in `handleAlarm()` — only set `smsEnabled = true` when BOTH the server policy AND client `se` flag agree.

9. **HIGH-8**: Add `doc["sms"] = cfg.unloadSmsEnabled` and `doc["email"] = cfg.unloadEmailEnabled` to `sendUnloadEvent()`.

### Medium-Term (6)

10. **HIGH-2**: Add explicit CORS denial headers to all responses.

11. **HIGH-3**: Add optional PIN authentication to Viewer (can be compile-time disabled for truly public kiosks).

12. **MED-5**: Support `null` JSON values in config updates to reset fields to defaults.

13. **MED-8**: Stream JSON directly to `EthernetClient` in Viewer instead of serializing to `String`.

14. **MED-12**: Move per-monitor timestamp insertion in `checkAlarmRateLimit()` to after the global check passes.

15. **MED-14**: Consider per-IP lockout tracking (limited to 4-8 tracked IPs given RAM constraints) or at least increase lockout threshold.

### Deferred / Acknowledged (8)

16. **MED-6**: Client already sends levels; server recalculation is a feature for calibration accuracy. Document the dual-path behavior.

17. **MED-7**: NWS HTTP limitation is inherent to Arduino Ethernet. Document fallback behavior.

18. **MED-11**: HTML size is a trade-off for self-contained deployment. No external dependencies is a feature.

19. **MED-13**: Migrate manual JSON string building to ArduinoJson — lower priority since most interpolated values are from controlled sources.

20. **MED-15**: On power state recovery from CRITICAL_HIBERNATE, recompute relay state from current alarm status.

21. **MED-17**: Add month/year filtering to hot-tier aggregation loops in MoM and YoY endpoints.

22. **LOW-2**: Bulk Modbus reads would improve solar monitoring efficiency but require testing with MRC-1 adapter latency.

23. No HTTPS is an accepted limitation of the Arduino Opta Ethernet stack.

---

## Appendix A: Peer Review Cross-Reference

The following findings were surfaced by peer AI reviewers and validated against source code by this review:

| Finding ID | Source Review | Original Finding | Validation |
|-----------|-------------|-----------------|------------|
| CRITICAL-2 | GPT-5.4 Logic Review | Remote relay gated behind rate limit | ✅ Confirmed — `sendAlarm()` line 4717 returns before relay logic at line 4808 |
| CRITICAL-3 | GPT-5.4 Logic Review | UNTIL_CLEAR relays can't auto-clear for HIGH/LOW | ✅ Confirmed — `strcmp(alarmType, "high")` always false when alarmType is `"clear"` |
| HIGH-6 | Copilot Code Review | PIN first-login accepts non-digit characters | ✅ Confirmed — line 5950 uses `strlen(pin) == 4` without `isValidPin()` |
| HIGH-7 | GPT-5.4 Code Review | Server overrides client SMS intent | ✅ Confirmed — lines 8412-8434 force `smsEnabled = true` |
| HIGH-8 | GPT-5.4 Code Review | Unload notification flags not serialized | ✅ Confirmed — `sendUnloadEvent()` line 4983 omits `sms`/`email` fields |
| MED-12 | GPT-5.4 Logic Review | Global alarm suppression consumes per-monitor quota | ✅ Confirmed — timestamp added at line 4625 before global check at line 4639 |
| MED-13 | Copilot Code Review | Manual JSON string building without escaping | ✅ Confirmed — lines 13294+ use String concatenation |
| MED-14 | Copilot Code Review | Global auth lockout enables DoS | ✅ Confirmed — lockout is global, not per-IP |
| MED-15 | GPT-5.4 Logic Review | Critical-hibernate relay shutdown not restored | ✅ Confirmed — recovery path does not recompute relay state |
| MED-16 | GPT-5.4 Code Review | Multi-relay remote actions drain one note per poll | ✅ Confirmed — `triggerRemoteRelays()` emits per-bit notes |
| MED-17 | Copilot Logic Review | MoM hot-tier stats aggregate ALL snapshots | ✅ Confirmed — no month filtering in hot tier loop |
| LOW-10 | Copilot Logic Review | YoY hot-tier paths ignore calendar boundaries | ✅ Confirmed — no year filtering in hot tier loop |
| LOW-11 | Copilot Logic Review | YoY fallback year hardcoded to 2025 | ✅ Confirmed — line 11241 fallback |
| LOW-12 | Copilot Code Review | Debug mutation endpoint active in production | ✅ Confirmed — `/api/debug/sensors` dedup action routed |

**Peer Reviews Evaluated:**
1. `CODE_REVIEW_04022026_COPILOT.md` — Copilot Code Review
2. `CODE_REVIEW_04022026_SERVER_CLIENT_VIEWER_COPILOT.md` — GPT-5.4 Code Review
3. `LOGIC_REVIEW_04022026_COPILOT.md` — Copilot Logic Review
4. `LOGICREVIEW-20260402-GitHubCopilot-GPT54.md` — GPT-5.4 Logic Review

---

## Review History

| Date | Version | Reviewer | Notes |
|------|---------|----------|-------|
| Sep 2025 | 0.x | Internal | Initial deployment review |
| Nov 2025 | 1.0 | Multiple | Pre-release review suite |
| Dec 2025 | 1.0 | Internal | V1.0 release review |
| Jan 2026 | 1.0.x | AI | Post-release hardening |
| Feb 2026 | 1.1.4 | Multiple AI | Comprehensive cross-review (14 fixes applied) |
| Mar 2026 | 1.1.4 | Copilot | Consolidated review with AI cross-references |
| **Apr 2026** | **1.2.1** | **Copilot (Claude Opus 4.6)** | **This review — code + logic review with peer cross-validation** |
