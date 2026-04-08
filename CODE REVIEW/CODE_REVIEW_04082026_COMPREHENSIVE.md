# Code Review — April 8, 2026 (Comprehensive)

**Firmware Version:** 1.4.0 (Common), 1.3.0 (Client/Viewer), 1.2.1 (Server)  
**Reviewed By:** GitHub Copilot (Claude Opus 4.6)  
**Scope:** Full codebase — Common library, Client, Server, Viewer, I2C Utility  
**Focus:** Bugs, logic errors, safety pitfalls, security, and improvement opportunities

---

## Table of Contents

1. [Critical Issues](#1-critical-issues)
2. [High Severity Issues](#2-high-severity-issues)
3. [Medium Severity Issues](#3-medium-severity-issues)
4. [Low Severity Issues](#4-low-severity-issues)
5. [Logic Review](#5-logic-review)
6. [Security Review](#6-security-review)
7. [Improvement Recommendations](#7-improvement-recommendations)
8. [Positive Observations](#8-positive-observations)

---

## 1. Critical Issues

### CRIT-01: IAP DFU Flash Erase Before Full Download — Partial Write Bricks Device

**File:** `TankAlarm-112025-Common/src/TankAlarm_DFU.h`  
**Location:** `tankalarm_performIapUpdate()`, flash erase section

The IAP firmware update erases the *entire application flash region* before beginning the chunk-by-chunk download from the Notecard:

```
Step 1: flash.erase(eraseAddr, eraseSize)    ← Erases all application sectors
Step 2: Loop: dfu.get → base64 decode → flash.program()  ← Downloads and writes chunks
```

If any failure occurs during Step 2 (I2C bus failure, Notecard timeout, power loss, base64 decode error, flash program failure), the application flash is left partially written or blank. The failure path at `iap_restore_hub:` restores the Notecard hub mode but **cannot restore erased flash**. The STM32H747 bootloader (below `appStart` at 0x08040000) survives, but the application code is corrupted. The device becomes unrecoverable without JTAG/SWD.

**Recommendation:** Implement A/B partitioning or write to a staging area first, verify integrity, then swap. Alternatively, write firmware to external flash/Notecard storage first, verify CRC, then erase+program application flash in one pass. At minimum, download all chunks to RAM/external storage before touching application flash.

### CRIT-02: No Firmware Integrity Verification After Flash Write

**File:** `TankAlarm-112025-Common/src/TankAlarm_DFU.h`  
**Location:** After the firmware write loop, before `NVIC_SystemReset()`

After writing all firmware chunks to flash, the code immediately reboots via `NVIC_SystemReset()` without verifying a CRC, SHA hash, or MD5 of the written firmware against an expected checksum. A single bit-flip during flash programming (electrical noise, marginal flash cell, incomplete erase) would go undetected, potentially causing undefined behavior or a crash loop.

**Recommendation:** The Notecard `dfu.status` response provides metadata that should include a checksum. After writing, read back the flash and verify against the expected hash before rebooting. If verification fails, restore hub mode and report the error.

---

## 2. High Severity Issues

### HIGH-01: `publishNote()` Static Buffer Truncation Causes Silent Data Loss

**File:** `TankAlarm-112025-Client-BluesOpta.ino`  
**Location:** `publishNote()` function

```cpp
static char buffer[1024];
size_t len = serializeJson(doc, buffer, sizeof(buffer));
```

Daily reports with many monitors or large payloads could exceed the 1KB buffer. Although `DAILY_NOTE_PAYLOAD_LIMIT` (960 bytes) exists and the outer split logic attempts to chunk large reports, if the split logic ever miscalculates or a non-daily note exceeds 1KB, the function prints a warning and **silently drops** the note. No retry, no persistence, no error escalation.

**Recommendation:** Increase the buffer to 2KB or dynamically allocate. When truncation is detected, handle gracefully (e.g., queue for retry, split payload, or send a truncation indicator note).

### HIGH-02: Viewer `fetchViewerSummary()` Double note.get Race Condition

**File:** `TankAlarm-112025-Viewer-BluesOpta.ino`  
**Location:** `fetchViewerSummary()`

The viewer uses a two-step approach:
1. `note.get` (peek, no delete) — process the note
2. `note.get` with `delete:true` — remove the processed note

If a new summary note arrives between steps 1 and 2, the delete call removes the **newly arrived** note instead of the one that was processed, causing data loss.

**Recommendation:** Use `note.get delete:true` in a single call and accept the trade-off (if processing fails, the note is already deleted). Alternatively, use each note's unique ID to ensure the delete targets the correct note.

### HIGH-03: Server `processNotefile()` Peek-Then-Delete TOCTOU Window

**File:** `TankAlarm-112025-Server-BluesOpta.ino`  
**Location:** `processNotefile()` pattern

Similar to HIGH-02: `note.get` (peek, no delete) → process → `note.get delete:true`. A crash between processing and deleting causes **duplicate processing** on next boot. This could result in duplicate SMS alerts, duplicate alarm log entries, or duplicate telemetry snapshots.

**Recommendation:** Add an idempotency key (e.g., note epoch or unique ID) to the processing pipeline. Track which notes have been processed in persistent storage and skip already-seen notes.

### HIGH-04: Config Generator "Load From Cloud" API Schema Mismatch

**File:** `TankAlarm-112025-Server-BluesOpta.ino`  
**Location:** Config Generator embedded JavaScript — `loadFromCloudBtn` click handler (~line 1520)

*(Cross-referenced from Copilot code review 04/08/2026)*

The "Load From Cloud" button fetches `/api/clients?summary=1` and then renders the client list using `d.clients.map(...)`. However, the API response schema uses `d.cs` (compact key), not `d.clients`. The `populateRelayTargets()` function on the same page correctly uses `d.cs`:

```javascript
// BUG: Load From Cloud handler uses wrong key
els.clientList.innerHTML = d.clients.map(c => `...`).join('');  // ← d.clients is undefined

// CORRECT: populateRelayTargets uses the actual API schema
d.cs.forEach(c => { ... });
```

**Impact:** Clicking "Load From Cloud" in the Config Generator throws a `TypeError: Cannot read properties of undefined (reading 'map')`. The client selection modal shows "Error loading clients" instead of the client list. This completely breaks the cloud-load workflow.

**Fix:** Change `d.clients` to `d.cs` in the click handler, and update property accesses to match the compact schema (`c.c` for client UID, `c.n` for label, `c.s` for site).

### HIGH-05: Undefined `rec` Variable in Embedded UI JavaScript — Multiple Pages Broken

**File:** `TankAlarm-112025-Server-BluesOpta.ino`  
**Location:** Calibration HTML (`populateSensorDropdowns()`) and Historical Data HTML (`renderLevelChart()`, `exportData()`, `renderSites()`)

*(Cross-referenced from Copilot code review and logic review 04/08/2026)*

Multiple embedded JavaScript functions reference a variable named `rec` that was never declared. The actual loop variable is `tank` (or `sensors[i]`, etc.), suggesting a copy-paste error from a refactoring where a `rec` iterator was renamed to `tank` but not all references were updated.

**Calibration page** — `populateSensorDropdowns()`:
```javascript
uniqueTanks.forEach((tank, key) => {
    option.textContent = `${typeTag}${rec.site} - ${rec.label}...`;  // ← rec is undefined
});
```

**Historical page** — `renderLevelChart()`:
```javascript
datasets = sensors.slice(0,8).map((tank, i) => ({
    label: `${rec.site} - ${rec.label}`,  // ← rec is undefined
    ...
}));
```

**Historical page** — `exportData()`:
```javascript
csv += `"${rec.site}","${rec.label}",...`;  // ← rec is undefined
```

**Historical page** — `renderSites()`:
```javascript
tankCard.innerHTML = `...<div class="tank-name">${rec.label}</div>...`;  // ← rec is undefined
```

**Impact:** On the calibration page, the sensor dropdown renders `undefined - undefined` for every sensor, making it impossible to select a sensor for calibration. On the historical page, chart labels show undefined, CSV export produces corrupted data, and site cards display broken content. These are runtime `ReferenceError` exceptions in some browsers.

**Fix:** Replace all `rec.` references with the correct loop variable (`tank.`) in each affected function.

### HIGH-06: Month-over-Month Compare Aggregates Entire Hot Tier Without Month Filtering

**File:** `TankAlarm-112025-Server-BluesOpta.ino`  
**Location:** `handleHistoryCompare()` (~line 11054) — hot tier aggregation loop

*(Cross-referenced from Copilot logic review 04/08/2026)*

When either the current or previous month falls within the hot tier (in-memory circular buffer), `handleHistoryCompare()` iterates **all snapshots** in the buffer without checking whether each snapshot's timestamp falls within the requested month boundaries:

```cpp
if (currInHotTier) {
    for (uint16_t j = 0; j < hist.snapshotCount; j++) {
        // Iterates ALL snapshots — no timestamp filter for currYear/currMonth
        TelemetrySnapshot &snap = hist.snapshots[idx];
        currSum += snap.level;  // Includes data from other months
        currCount++;
    }
}
```

If hot tier retention is 90 days and the user requests April data, March and February data are included in the averages.

**Impact:** The month-over-month comparison page shows statistically incorrect min/max/avg values for any month served from the hot tier. The error magnitude grows with larger hot tier retention settings.

**Fix:** Add a timestamp filter inside the loop: convert `currYear`/`currMonth` to epoch boundaries and skip snapshots outside the range.

### HIGH-07: Year-over-Year Current Year Stats Aggregate Entire Hot Tier Without Year Filter

**File:** `TankAlarm-112025-Server-BluesOpta.ino`  
**Location:** `handleHistoryYearOverYear()` (~line 11268) — current year hot tier loop

*(Cross-referenced from Copilot logic review 04/08/2026)*

Same pattern as HIGH-06: the "current year" stats computation iterates all hot tier snapshots without filtering by year:

```cpp
for (uint16_t j = 0; j < hist.snapshotCount; j++) {
    TelemetrySnapshot &snap = hist.snapshots[idx];
    yearSum += snap.level;  // Includes data from previous year if hot tier spans Dec→Jan
    yearCount++;
}
```

The same pattern also affects the **single-sensor detail branch** (~line 11381), which labels its output as the current month but includes all hot tier data without month filtering.

**Impact:** Year-over-year comparison shows inflated or deflated current-year statistics when hot tier data spans a year boundary (e.g., December data included in January's year aggregate). The single-sensor view is also inaccurate.

**Fix:** Check each snapshot's epoch against the current year (or current month for the single-sensor branch) boundaries before including it in the aggregation.

---

## 3. Medium Severity Issues

### MED-01: millis() Overflow in Server Auth Rate Limiting — Permanent Lockout

**File:** `TankAlarm-112025-Server-BluesOpta.ino`  
**Location:** Auth rate-limit logic

```cpp
if (now < gNextAllowedAuthTime) {
    // Lock out
}
```

`gNextAllowedAuthTime` is computed as `now + delayMs`. After the 49.7-day `millis()` overflow, if `now` wraps to a small value but `gNextAllowedAuthTime` still holds a large pre-overflow value, the condition `now < gNextAllowedAuthTime` evaluates to `true` — **permanently locking out authentication** until reboot.

**Fix:**
```cpp
// Replace absolute future timestamp with elapsed-time check:
if (gAuthLockoutActive && (now - gAuthLockoutStartMillis) < AUTH_LOCKOUT_DURATION) {
    // Still locked out
}
```

### MED-02: Relay Stuck ON in MANUAL_RESET Mode if Notecard Offline

**File:** `TankAlarm-112025-Client-BluesOpta.ino`  
**Location:** Relay control / alarm processing

In `RELAY_MODE_MANUAL_RESET`, relays stay ON until a server-initiated reset command arrives via `pollForRelayCommands()`. If the Notecard loses connectivity (cellular outage, Notehub down), the relay command will never arrive. The relay remains ON indefinitely.

The physical clear button provides a local override, but only if configured (`clearButtonPin >= 0`). Default is disabled (`clearButtonPin = -1`).

**Recommendation:** Add a configurable maximum ON duration for MANUAL_RESET relays (e.g., `maxManualResetHours`). If the relay has been ON longer than the limit without acknowledgment, auto-turn-off and log an alarm. Default could be 24-72 hours.

### MED-03: Shared Clear Debounce Counter Between High/Low Alarm Paths

**File:** `TankAlarm-112025-Client-BluesOpta.ino`  
**Location:** Alarm evaluation logic

Both high and low alarm evaluation paths reset `state.clearDebounceCount = 0` when their respective alarm condition is true. If a reading oscillates between the high and low alarm zones within the hysteresis band (edge case), the clear debounce counter is continuously reset, potentially **preventing alarm clear** indefinitely.

**Recommendation:** Use separate debounce counters for high-clear and low-clear, or ensure the hysteresis band is wide enough to prevent this oscillation pattern.

### MED-04: Non-Atomic Flash Writes on Non-POSIX STM32duino Platforms

**File:** `TankAlarm-112025-Common/src/TankAlarm_Platform.h`  
**Location:** STM32duino path (non-Mbed)

The `saveConfigToFlash()` path for STM32duino (non-Mbed) uses `LittleFS.open()` + write + close without the temp-file-then-rename pattern. A power loss during write could corrupt the config file. Only the Mbed/Opta POSIX path uses atomic writes.

**Note:** The `tankalarm_littlefs_write_file_atomic()` function exists in `TankAlarm_Platform.h` but callers need to use it consistently. Verify all config save paths use the atomic variant.

### MED-05: gRpmLastPinState[] Double Initialization — Phantom First Pulse

**File:** `TankAlarm-112025-Client-BluesOpta.ino`  
**Location:** `setup()` and `startPulseSample()`

In `setup()`, `gRpmLastPinState[i] = HIGH` is set for all monitors. But in `startPulseSample()`, `ctx.lastPinState = digitalRead(pin)` reads the actual state. The accumulated mode path uses `gRpmLastPinState[idx]` from `setup()`, not the actual pin state. If the pin happens to start LOW, the first `detectPulseEdge(HALL_EFFECT_UNIPOLAR, HIGH, LOW)` returns true — a phantom pulse.

**Fix:** Initialize `gRpmLastPinState[i] = digitalRead(pin)` in setup after `pinMode()` has been called.

### MED-06: Manual Relay Commands Bypass Momentary-Timeout Bookkeeping

**File:** `TankAlarm-112025-Client-BluesOpta.ino`  
**Location:** `processRelayCommand()` — manual relay activation path

*(Cross-referenced from Copilot code review and logic review 04/08/2026)*

When an alarm triggers a relay, the alarm path correctly manages momentary timeouts (setting the relay start time, scheduling auto-off, updating shared state for the clear debounce logic). However, when a manual relay command arrives via `processRelayCommand()`, it activates the relay via `setRelayState()` without updating any of the momentary-timeout bookkeeping fields.

This means:
- A manual relay command in MOMENTARY mode turns the relay ON but the auto-off timer is never started — the relay stays ON indefinitely (contradicting MOMENTARY semantics)
- A manual command while an alarm-driven relay is counting down resets the physical state but the timer continues running from the alarm's start time, leading to premature auto-off

**Note:** This extends LOW-03 (which only noted the `"duration"` parameter being dead code) to the broader inconsistency between alarm-driven and manual relay activation paths.

**Recommendation:** Route manual relay commands through the same bookkeeping logic as alarm-driven activation, or provide separate momentary handling for manual commands.

### MED-07: FTP Response Assembly Uses Unbounded `strcat()`

**File:** `TankAlarm-112025-Server-BluesOpta.ino`  
**Location:** FTP response parsing (~line 3475)

*(Cross-referenced from Copilot code review 04/08/2026)*

The FTP response reader uses `strcat()` to assemble multi-line FTP reply messages:

```cpp
if (needed <= maxLen) {
    strcat(message, line);
    strcat(message, "\n");
}
```

While the preceding bounds check (`if (needed <= maxLen)`) prevents the most obvious overflow, `strcat()` itself performs no bounds checking and requires scanning to find the NUL terminator on every call ($O(n)$ per append). If a future code change modifies the bounds check or the `maxLen` calculation, the `strcat()` calls become directly exploitable buffer overflows.

**Recommendation:** Replace `strcat()` with `strncat()` or a cursor-based approach (`memcpy` at a tracked write offset) that provides inherent bounds safety without relying on an external guard.

### MED-08: First-Login PIN Setup Accepts Non-Digit Characters

**File:** `TankAlarm-112025-Server-BluesOpta.ino`  
**Location:** `handleLoginPost()` (~line 5960) — fresh-install PIN setup branch

*(Cross-referenced from Copilot code review 04/08/2026)*

When no PIN is configured (`!isValidPin(gConfig.configPin)`), the first-login handler accepts any 4-character string as the admin PIN:

```cpp
if (pin && strlen(pin) == 4) {
    strlcpy(gConfig.configPin, pin, sizeof(gConfig.configPin));  // accepts "abcd", "!@#$", etc.
```

However, the login HTML form specifies `pattern="\\d{4}" title="4-digit PIN"`, and the existing `isValidPin()` function validates that all characters are digits. The server-side check only verifies length, not content. A user with JavaScript disabled or using `curl` could set a non-numeric PIN, which `isValidPin()` would then reject on subsequent logins — permanently locking out the device.

**Fix:** Replace `strlen(pin) == 4` with `isValidPin(pin)` in the first-login branch to enforce digit-only pins server-side.

### MED-09: Historical Page Injects Unescaped Site Names Into innerHTML — Stored XSS

**File:** `TankAlarm-112025-Server-BluesOpta.ino`  
**Location:** Historical Data HTML — `renderSites()` function

*(Cross-referenced from Copilot code review 04/08/2026)*

The `renderSites()` function injects site names directly into `innerHTML` without escaping:

```javascript
siteCard.innerHTML = `<div class="site-header">
    <h3>${siteName}</h3>  ...`;  // ← siteName not escaped
```

The `escapeHtml()` helper function exists on the same page but is not used for site name injection in the site cards. An attacker who can set a site name containing `<script>` tags (via the Config Generator or API) could execute JavaScript in any admin's browser session on the LAN dashboard.

**Impact:** Stored XSS — the malicious site name persists in server state and executes every time any authenticated user views the historical page.

**Fix:** Use `escapeHtml(siteName)` in all `innerHTML` assignments in `renderSites()`.

### MED-10: Auth Middleware Bypass Is Prefix-Based — Future Routes at Risk

**File:** `TankAlarm-112025-Server-BluesOpta.ino`  
**Location:** Session validation middleware (~line 6030)

*(Cross-referenced from Copilot logic review 04/08/2026)*

The auth middleware exempts routes using `startsWith()`:

```cpp
if (path.startsWith("/api/") &&
    !path.startsWith("/api/login") &&
    !path.startsWith("/api/session/check")) {
```

Any future route added under `/api/login*` (e.g., `/api/login-history`, `/api/login-attempts`) or `/api/session/check*` (e.g., `/api/session/check-all`) would automatically bypass authentication. While no such routes exist today, this is a maintenance trap.

**Recommendation:** Use exact-match comparisons (`path == "/api/login"`) or add explicit trailing delimiters (`path.startsWith("/api/login") && (path.length() == 10 || path[10] == '?')`).

### MED-11: Multiple API Handlers Build JSON via Raw String Concatenation Without Escaping

**File:** `TankAlarm-112025-Server-BluesOpta.ino`  
**Location:** `handleTransmissionLogGet()` (~line 13290), `handleLocationGet()` (~line 13592), and ~8 other `respondJson(client, responseStr)` call sites

*(Cross-referenced from Copilot code review 04/08/2026)*

At least 10 API handlers build JSON responses by concatenating `String` objects directly into a JSON template without escaping special characters:

```cpp
responseStr += "\"site\":\"" + String(e.siteName) + "\",";
responseStr += "\"detail\":\"" + String(e.detail) + "\"";
```

If any dynamic value (site name, detail text, client UID, NWS grid office code) contains a quote (`"`), backslash (`\`), or control character, the resulting JSON is malformed. API consumers (the embedded JavaScript pages) will throw a `SyntaxError` on `JSON.parse()`, breaking the entire page.

In contrast, most other API handlers in the same codebase correctly use `JsonDocument` + `serializeJson()`, which auto-escapes.

**Impact:** A site name like `O'Brien "East"` would corrupt the transmission log API response, breaking the log viewer page for all users. Since site names are operator-configured, this is a latent data-dependent failure.

**Recommendation:** Replace raw `String` concatenation with `JsonDocument` construction for all API response handlers, consistent with the pattern used elsewhere in the codebase.

---

## 4. Low Severity Issues

### LOW-01: millis() Absolute Comparison in Daily Report Fallback

**File:** `TankAlarm-112025-Client-BluesOpta.ino`  
**Location:** Daily report scheduling logic

```cpp
if (!reportDue && currentEpoch() <= 0.0 && millis() > 24UL * 60UL * 60UL * 1000UL) {
```

This compares `millis()` against an absolute value (86,400,000 ms). After the 49.7-day overflow, `millis()` wraps to 0 and this condition becomes permanently false, disabling the fallback daily report mechanism. Only affects solar-only deployments where Notecard time never syncs and the device runs >49.7 days continuously.

**Fix:** Track startup time and use elapsed-time subtraction instead.

### LOW-02: Server HTTP Header Line Count Not Capped

**File:** `TankAlarm-112025-Server-BluesOpta.ino`  
**Location:** `readHttpRequest()`

Each individual header line is capped at 512 bytes (good), but the total number of header lines is unlimited. A slow attacker could send thousands of short header lines, causing heap fragmentation through repeated Arduino `String` concatenation/destruction.

**Recommendation:** Add a maximum header count (e.g., 50 headers). Reject requests exceeding this.

### LOW-03: `processRelayCommand()` Duration Parameter is Dead Code

**File:** `TankAlarm-112025-Client-BluesOpta.ino`  
**Location:** `processRelayCommand()` 

The handler reads a `"duration"` parameter but there's no implementation to auto-turn-off after the specified duration. A relay command with `"state": true, "duration": 300` turns the relay ON and it never auto-turns off via this path. The comment says "reserved for future use" but could confuse future developers.

**Note:** See also MED-06 for the broader issue of manual relay commands bypassing all momentary-timeout bookkeeping.

**Recommendation:** Either implement the duration feature or remove the parameter reading and document clearly.

### LOW-04: `generateSessionToken()` Entropy Quality

**File:** `TankAlarm-112025-Server-BluesOpta.ino`  
**Location:** `generateSessionToken()`

The session token is derived via `analogRead()` and `micros()` through an LCG. While the 64-bit seed space is large and adequate for its LAN-only deployment, the entropy sources (ADC noise + microsecond timer) are somewhat predictable on a deterministic embedded platform. If the server were ever internet-exposed, this would be cryptographically weak.

**Recommendation:** Document the threat model assumption (LAN-only). If internet exposure is ever planned, use a cryptographic PRNG.

### LOW-05: Hash Table Corruption Edge Case on Full Table

**File:** `TankAlarm-112025-Server-BluesOpta.ino`  
**Location:** `insertSensorIntoHash()`

The function guards `if (recordIndex >= gSensorRecordCount)` but if the hash table fills (all slots used, which the `static_assert` for 2x size makes unlikely), the insert silently fails. The `findSensorByHash()` correctly detects the full-table case by checking `SENSOR_HASH_EMPTY` sentinel.

**Recommendation:** Add a warning log in the `insertSensorIntoHash()` full-table fallthrough path.

### LOW-06: Viewer Has No Authentication

**File:** `TankAlarm-112025-Viewer-BluesOpta.ino`

The viewer serves sensor data to anyone on the LAN without authentication. Since it's explicitly documented as a "read-only kiosk," this is by design, but worth noting for deployments on shared networks.

### LOW-07: `warmTierAvailable` Hard-Coded `true` Regardless of Actual Data

**File:** `TankAlarm-112025-Server-BluesOpta.ino`  
**Location:** `sendHistoryJson()` (~line 11010)

*(Cross-referenced from Copilot logic review 04/08/2026)*

The `dataInfo["warmTierAvailable"]` field is unconditionally set to `true`:

```cpp
dataInfo["warmTierAvailable"] = true;  // LittleFS daily summaries always available
```

This is incorrect on a fresh server with no daily summary files on LittleFS, or if the warm tier has been corrupted/cleared. The historical page data source banner will always display "+ Flash" even when no warm tier data exists, misleading operators about data availability.

**Fix:** Check whether any daily summary files actually exist on LittleFS before setting this flag.

### LOW-08: Staleness Threshold Inconsistency Between Viewer and Server

**File:** `TankAlarm-112025-Viewer-BluesOpta.ino` (JavaScript) and `TankAlarm-112025-Server-BluesOpta.ino`

*(Cross-referenced from Copilot logic review 04/08/2026)*

The viewer's embedded JavaScript marks sensor data as stale after **26 hours** without an update (the yellow "stale" badge on the dashboard). The server marks clients as stale after **49 hours** (for the "offline" indicator and stale-client alerts).

While the two thresholds serve different purposes (viewer=display freshness, server=client liveness), the large gap means a sensor can appear "stale" on the viewer dashboard for 23 hours before the server triggers any alert — confusing operators who see a warning but no corresponding server-side action.

**Recommendation:** Document the rationale for the different thresholds. Consider adding a configurable viewer staleness threshold, or at minimum ensure the two values are defined as named constants near each other with comments explaining the difference.

---

## 5. Logic Review

### 5.1 Sensor Reading Pipeline (Client)

The sensor reading flow is well-structured:

1. **`sampleMonitors()`** — iterates all configured monitors, calls `readMonitorSensor()`
2. **`readMonitorSensor()`** — dispatches to type-specific readers based on `sensorInterface`
3. **Type-specific readers:**
   - `readDigitalSensor()` — handles NO/NC float switches with proper debouncing
   - `readAnalogSensor()` — reads 0-10V analog input, maps to configured range
   - `readCurrentLoopSensor()` — reads 4-20mA via A0602 I2C, with pressure/ultrasonic conversion
   - `readPulseSensor()` — non-blocking state machine for RPM/flow measurement
4. **Alarm evaluation** — uses configurable thresholds with hysteresis+debounce
5. **Telemetry dispatch** — sends via Notecard with rate limiting and offline buffering

**Logic Issues Found:**
- The current loop conversion correctly handles both pressure (bottom-mounted, direct mapping) and ultrasonic (top-mounted, inverted mapping with mount height subtraction)
- Stuck sensor detection correctly counts identical consecutive readings
- Sensor failure detection uses consecutive failure count with recovery debounce
- The non-blocking pulse sampler state machine correctly avoids blocking the main loop

### 5.2 Alarm Processing (Client)

The alarm system is well-designed with multiple layers of protection:

- **Debounce:** ALARM_DEBOUNCE_COUNT (3) consecutive readings required to trigger/clear
- **Hysteresis:** Configurable band prevents alarm/clear oscillation
- **Per-sensor rate limiting:** MAX_ALARMS_PER_HOUR (10) per monitor
- **Global rate limiting:** MAX_GLOBAL_ALARMS_PER_HOUR (30) across all sensors
- **SMS escalation:** Separate from local alarm processing, sent to server for dispatch
- **Relay activation runs before rate limiting** — ensures physical safety response is never skipped even when alarm notes are rate-limited

### 5.3 Power State Machine (Client)

The power conservation state machine is correctly implemented:

- **Entry thresholds** (voltage falling) are lower than **exit thresholds** (voltage rising) — proper hysteresis
- **Debounce count** prevents rapid state toggling
- **CRITICAL_HIBERNATE** correctly forces all relays OFF (safety measure)
- **State recovery** correctly restores UNTIL_CLEAR/MANUAL_RESET relays but skips MOMENTARY relays
- **Sleep durations** scale progressively: 100ms → 5s → 30s → 5min
- **Outbound/inbound sync multipliers** reduce Notecard communication at lower power states

### 5.4 Server-Side Processing

- **Telemetry ingestion** correctly upserts sensor records using hash table for O(1) lookup
- **24-hour change tracking** stores previous level readings for trend analysis
- **Stale client detection** (49h threshold) correctly avoids false positives on missed daily reports
- **Orphan sensor pruning** (72h) correctly handles client reconfiguration by pruning sensors that are stale while the client has other fresh sensors
- **Daily email aggregation** collects all sensors and formats with site grouping
- **SMS rate limiting** per-sensor with configurable maximum alerts per hour

### 5.5 Viewer Logic

- **Time-aligned fetch schedule** correctly uses `computeNextAlignedEpoch()` to sync with server summary cadence
- **MAC address derivation** from Notecard UID ensures unique MACs per viewer without hardcoding
- **Dashboard HTML** is a single PROGMEM constant with embedded JavaScript that fetches `/api/sensors` periodically
- **Stale data highlighting** (26h threshold in JavaScript) correctly warns users about old data

### 5.6 Configuration Pipeline

- **Server generates config** → pushes via `command.qo` → Notecard route delivers to client's `config.qi`
- **Client receives config** → validates → applies → persists to flash → sends ACK via `config_ack.qo`
- **Server receives ACK** → updates config snapshot status → logs to transmission log
- **Schema versioning** (`CONFIG_SCHEMA_VERSION`, `NOTEFILE_SCHEMA_VERSION`) enables forward/backward compatibility detection

### 5.7 I2C Bus Recovery

- **Three-tier escalation:** sensor-only recovery → Notecard failure recovery → dual-failure recovery
- **Exponential backoff** for sensor-only recovery prevents infinite recovery on dead hardware
- **Circuit breaker** (I2C_SENSOR_RECOVERY_MAX_ATTEMPTS = 5) trips after max attempts
- **Bus recovery sequence:** Wire.end() → GPIO toggle SCL 16× → STOP condition → Wire.begin() — standard I2C recovery protocol
- **Post-recovery Notecard rebinding** via `tankalarm_ensureNotecardBinding()` — correctly re-registers I2C callbacks

---

## 6. Security Review

### 6.1 Strengths

| Area | Implementation | Assessment |
|------|---------------|-----------|
| PIN Authentication | 4-8 character configurable PIN | Adequate for LAN |
| Rate Limiting | Exponential backoff after 5 failures (30s lockout) | Good (see MED-01 for overflow bug) |
| Session Tokens | 64-bit entropy, 16-hex-char token | Adequate for LAN |
| Constant-Time Comparison | XOR-based for both PIN and session token | Best practice ✅ |
| Single-Session Enforcement | New login invalidates previous token | Prevents session fixation ✅ |
| Input Validation | `isValidClientUid()`, `isValidMeasurementUnit()`, whitelist validation | Good ✅ |
| HTTP Body Size Caps | 16KB server, 1KB viewer | Prevents memory exhaustion ✅ |
| HTML Escaping | JavaScript `escapeHtml()` on most user-provided data | Good — but see MED-09 for gaps ⚠️ |
| Atomic File Writes | POSIX temp-then-rename on Mbed/Opta | Prevents data loss ✅ |

### 6.2 Weaknesses

| Area | Issue | Risk Level |
|------|-------|-----------|
| No HTTPS | All traffic (including PIN) is plaintext | Acceptable for LAN-only ⚠️ |
| No Transfer-Encoding handling | Theoretical request smuggling if proxied | Low (no proxy expected) |
| Viewer unauthenticated | Read-only dashboard open to LAN | By design |
| Weak PRNG for sessions | ADC noise + timer through LCG | Adequate for LAN |
| Content-Type not enforced | API accepts any Content-Type | Low risk |
| Auth middleware prefix-based | `startsWith` exemptions could accidentally bypass auth for future routes (MED-10) | Medium ⚠️ |
| First-login PIN validation | Server accepts non-digit PINs on first setup (MED-08) | Medium ⚠️ |
| Stored XSS via site names | Unescaped site names in historical page `innerHTML` (MED-09) | Medium ⚠️ |

### 6.3 Recommendation

The security posture is appropriate for the single-LAN, no-internet-facing deployment model. No changes required unless the server is exposed to untrusted networks.

---

## 7. Improvement Recommendations

### 7.1 Architecture

1. **Split monolithic sketch files.** At ~7,000 lines (Client) and ~13,000 lines (Server), these files are difficult to maintain and slow to compile. Consider splitting by concern into separate `.cpp`/`.h` files:
   - Client: `client_telemetry.cpp`, `client_alarms.cpp`, `client_relay.cpp`, `client_config.cpp`
   - Server: `server_notecard.cpp`, `server_http.cpp`, `server_dashboard.cpp`, `server_config.cpp`, `server_alerts.cpp`

2. **Server HTML assets.** The ~6,000 lines of inline HTML/CSS/JS in PROGMEM could be served from LittleFS files or compressed with gzip. This would simplify the code and allow UI updates without firmware recompilation.

3. **Unit testing.** The shared library functions (`tankalarm_roundTo()`, `tankalarm_currentEpoch()`, `tankalarm_computeNextAlignedEpoch()`, hash table operations) are excellent candidates for off-device unit testing using a test framework like PlatformIO + Unity.

### 7.2 Reliability

4. **DFU safety (CRIT-01, CRIT-02).** Implement firmware integrity verification and a safer update strategy. At minimum, verify a CRC after flash write before rebooting.

5. **Note idempotency (HIGH-02, HIGH-03).** Add an idempotency key (epoch or unique ID) to prevent duplicate processing after crashes. Track processed note IDs in persistent storage.

6. **Watchdog starvation during FTP.** FTP operations (backup/restore) involve blocking TCP operations that could take 8+ seconds each. Ensure the watchdog is kicked within FTP send/receive loops, similar to the DFU flow.

7. **Maximum relay ON duration.** Add a configurable safety timeout for MANUAL_RESET relays to prevent indefinite activation during communication outages.

### 7.3 Code Quality

8. **millis() overflow audit.** Fix the two identified absolute-comparison patterns (MED-01, LOW-01) to use subtraction-based elapsed-time checks.

9. **Consistent error propagation.** Several functions return `bool` for success/failure but callers don't always check the return value. Add `[[nodiscard]]` attributes where available.

10. **Static analysis.** Run the codebase through a static analyzer (e.g., `cppcheck`, PVS-Studio, or Clang-Tidy) to catch additional issues. The large monolithic files make manual review challenging.

### 7.4 Data Handling

11. **`publishNote()` buffer size (HIGH-01).** Increase from 1KB to 2KB or use dynamic allocation for edge cases.

12. **Temperature compensation.** The calibration learning system uses multiple linear regression with temperature coefficients — a good approach. Consider adding a minimum R² threshold before applying learned calibrations to prevent poor fits from degrading accuracy.

13. **FTP archive cache.** The `FtpArchiveCache` struct stores only one cached month at a time. If the web UI frequently switches between months, this causes repeated FTP downloads. Consider caching the 2-3 most recently accessed months.

---

## 8. Positive Observations

This codebase demonstrates strong embedded engineering practices:

- **Defensive programming:** Null checks, buffer size guards, `strlcpy()` everywhere, and bounds validation on array indices
- **Well-documented configuration:** Every `#define` is wrapped in `#ifndef`, allowing per-project overrides without modifying shared code
- **Layered error handling:** I2C bus recovery escalation (sensor → Notecard → dual → reset) with exponential backoff and circuit breakers
- **Power conservation:** Proper hysteresis with separate enter/exit thresholds prevents voltage oscillation state toggling
- **Fleet management:** Clean separation of Client/Server/Viewer roles with well-defined Notecard message routing
- **Atomic file operations:** The POSIX temp-then-rename pattern on Mbed/Opta correctly handles power-loss scenarios
- **Boot-time .tmp cleanup:** `tankalarm_posix_cleanup_tmp_files()` recovers from interrupted atomic writes
- **Header-only shared library:** All common functions are `static inline`, avoiding linker issues across multiple sketch targets
- **Non-blocking design:** The pulse sampler state machine, cooperative loop structure, and watchdog-aware sleep all avoid blocking the main loop
- **Constant-time security comparisons:** Both PIN and session token checks use XOR-based comparisons to prevent timing side-channel attacks
- **HTML escaping:** JavaScript `escapeHtml()` function is defined on every page — though see MED-09 for a location where it was missed
- **Schema versioning:** Both config and notefile schemas are versioned for forward compatibility

---

## Summary

| Severity | Count | Key Items |
|----------|-------|-----------|
| **Critical** | 2 | DFU flash erase before download (CRIT-01), no firmware verification (CRIT-02) |
| **High** | 7 | publishNote buffer truncation (HIGH-01), viewer/server note.get race conditions (HIGH-02/03), Config Generator API schema mismatch (HIGH-04), undefined `rec` variable in UI JavaScript (HIGH-05), month-over-month filter bug (HIGH-06), year-over-year filter bug (HIGH-07) |
| **Medium** | 11 | millis overflow lockout (MED-01), relay manual reset stuck (MED-02), clear debounce (MED-03), non-atomic writes (MED-04), phantom pulse (MED-05), manual relay bookkeeping bypass (MED-06), FTP strcat safety (MED-07), PIN first-login validation (MED-08), stored XSS via site names (MED-09), auth prefix bypass risk (MED-10), unescaped JSON concatenation (MED-11) |
| **Low** | 8 | millis fallback (LOW-01), header count (LOW-02), dead code (LOW-03), PRNG quality (LOW-04), hash table edge (LOW-05), viewer auth (LOW-06), warmTierAvailable hardcoded (LOW-07), staleness threshold gap (LOW-08) |

*Items marked with "Cross-referenced from Copilot code/logic review 04/08/2026" were identified by a parallel peer review (GPT-5.3-Codex) and verified against the source code before inclusion.*

The most urgent action items are CRIT-01 and CRIT-02 (DFU safety), HIGH-04 and HIGH-05 (broken UI pages), and MED-01 (auth lockout after millis overflow). HIGH-06 and HIGH-07 should be addressed before relying on month-over-month or year-over-year analytics. All other issues have effective mitigations in place or affect edge cases unlikely to occur in normal operation.

---

## 9. Final Review (Post-Fix Verification - 2026-04-08)

This section validates the implemented fixes in current source and lists updates required in this review document.

### 9.1 Fixes Verified as Implemented

The following previously-open items are now fixed in code and should be marked **Resolved** in this document:

- Server: HIGH-04, HIGH-05, HIGH-06, HIGH-07, MED-01, MED-07, MED-08, MED-11, LOW-02, LOW-05
- Client: MED-05, LOW-01
- Viewer: HIGH-02

### 9.2 Partially Fixed / Keep Open

The following items are improved but not fully resolved and should remain open:

1. **MED-09 (Stored XSS hardening): PARTIAL**
    - Historical rendering now escapes visible fields, but Config Generator still injects untrusted UID into an inline `onclick` JavaScript string. HTML escaping alone is not sufficient for JS-string context.
2. **MED-10 (Auth middleware route exemptions): PARTIAL**
    - Middleware now uses exact path checks, but route dispatch still accepts `startsWith("/api/session/check")`.
    - Requests like `/api/session/check?x=1` can be incorrectly challenged by middleware.
3. **LOW-07 (`warmTierAvailable` semantics): PARTIAL**
    - `warmTierAvailable` now reflects retention configuration (`warmTierRetentionMonths > 0`) but does not prove warm-tier records are actually present for the selected range.
4. **HIGH-01 (Client publish payload limit): PARTIAL**
    - Buffer was increased to 2048 bytes, but oversized payloads are still dropped instead of chunked/compressed or safely rejected with upstream signaling.

### 9.3 Newly Introduced Issues (Need New Entries)

These items were introduced or exposed by the fix patch set and should be added to this review document as new findings:

1. **New High - Alarm SMS intent override**
    - In server alarm handling, client-provided SMS intent (`se` / `smsEnabled`) is overwritten to `true` for sensor alarms, changing behavior from opt-in to forced eligibility.
    - Action: preserve client intent and apply server policy as an additional gate (logical AND), not as an override.

2. **New Medium - CSV export data corruption risk**
    - Historical CSV export uses `escapeHtml()` for CSV cell content, producing HTML entities in downloaded CSV (e.g., `&#39;`) instead of proper CSV escaping.
    - Action: replace with CSV escaping (`"` -> `""`) and leave characters otherwise unchanged.

3. **New Medium - Session-check query mismatch**
    - Middleware exemption is exact-match, while handler dispatch accepts prefix.
    - Action: normalize request path (strip query before middleware check) or exempt with the same predicate used by dispatch.

### 9.4 Required Document Updates

Apply these review-document changes now:

1. Move the resolved items listed in 9.1 out of open findings and into a closed/resolved subsection.
2. Keep MED-09, MED-10, LOW-07, and HIGH-01 open with updated "partial" status and narrower remediation steps.
3. Add the 3 new findings from 9.3 with severities and remediation notes.
4. Update summary counts so resolved items are no longer included in active totals.
