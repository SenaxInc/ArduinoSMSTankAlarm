# Code Review — April 9, 2026 (Comprehensive)

> **Firmware Version:** 1.6.1  
> **Reviewer:** GitHub Copilot (Claude Opus 4.6)  
> **Scope:** Full codebase — Client, Server, Viewer, Common headers  
> **Method:** Automated static analysis with manual verification of findings  

---

## Summary

| Severity | Count | Components |
|----------|-------|------------|
| HIGH | 4 | Client (2), Server (1), Viewer (1) |
| MEDIUM | 8 | Client (4), Server (3), Viewer (1) |
| LOW | 6 | Client (3), Server (2), Common (1) |
| INFO | 3 | Design observations / future hardening |

---

## HIGH Severity

### CR-H1: Alarm Hourly Rate Limit Unsigned Underflow in First Hour After Boot **(C)**

**File:** `TankAlarm-112025-Client-BluesOpta.ino` line ~4693  
**Function:** `checkAlarmRateLimit()`

```cpp
unsigned long oneHourAgo = now - 3600000UL;
```

When `millis()` returns a value less than 3,600,000 (i.e., during the first ~60 minutes after boot), the subtraction wraps around to a very large unsigned value. Any alarm timestamp recorded during this period will be *less than* `oneHourAgo`, so `alarmTimestamps[i] > oneHourAgo` evaluates to `false`. This causes **all** timestamps to be pruned on every call, resetting `alarmCount` to zero.

**Impact:** The per-monitor hourly alarm count (`MAX_ALARMS_PER_HOUR`) and global count (`MAX_GLOBAL_ALARMS_PER_HOUR`) are **ineffective** during the first hour after boot. The per-alarm-type interval checks (`lastHighAlarmMillis`, etc.) still work, so this is not a complete bypass. However, a rapidly oscillating sensor could still send more alarms than intended.

**Fix:**
```cpp
if (now >= 3600000UL) {
  unsigned long oneHourAgo = now - 3600000UL;
  // ... existing pruning logic ...
} else {
  // Within first hour — all recorded timestamps are valid, keep them all
}
```

---

### CR-H2: `relay_timeout` Shares Rate Bucket With `sensor-fault` **(C)**

**File:** `TankAlarm-112025-Client-BluesOpta.ino` line ~4682  
**Function:** `checkAlarmRateLimit()`

The `relay_timeout` alarm type is not handled as an explicit case in `checkAlarmRateLimit()`. It falls through to the `sensor-fault`/`sensor-stuck` branch, which checks `lastSensorFaultMillis`. If a sensor fault was recently reported, the relay safety timeout notification is suppressed for `MIN_ALARM_INTERVAL_SECONDS` (5 minutes).

**Impact:** The server may not learn about a relay forced-off event for up to 5 minutes after a sensor fault. In the `sendAlarm()` function, `relay_timeout` is excluded from `isAlarm` (correct per v1.6.1 fix), but the rate-limit path still suppresses the notification.

**Fix:** Add explicit handling for `relay_timeout` in `checkAlarmRateLimit()`:
```cpp
} else if (strcmp(alarmType, "relay_timeout") == 0) {
  // Relay safety timeouts should not be rate-limited — they are safety-critical events
  return true;
}
```

---

### CR-H3: `handleAlarm()` System Alarm SMS Not Rate-Limited **(S)**

**File:** `TankAlarm-112025-Server-BluesOpta.ino` line ~8314  
**Function:** `handleAlarm()` — system alarm branch

When a system alarm (solar/battery/power) has `se=true`, `sendSmsAlert()` is called directly without going through `checkSmsRateLimit()`. If a power voltage oscillates near the CRITICAL threshold, the client sends a `power` alarm on every transition, and each one triggers an SMS without rate limiting.

**Impact:** Unbounded SMS costs during battery voltage oscillation events. The client's power state debounce (3 consecutive readings) partially mitigates this, but rapid transition sequences (CRITICAL→LOW→CRITICAL within minutes) still generate multiple SMS.

**Fix:** Add rate limiting using a dedicated timestamp tracker:
```cpp
static double gLastSystemSmsSentEpoch = 0.0;
// ...
if (smsRequested) {
  double now = currentEpoch();
  if (now - gLastSystemSmsSentEpoch < MIN_SMS_ALERT_INTERVAL_SECONDS) {
    Serial.println(F("System alarm SMS rate-limited"));
  } else {
    gLastSystemSmsSentEpoch = now;
    sendSmsAlert(message);
  }
}
```

---

### CR-H4: Viewer `respondJson()` Double-Buffers Entire Sensor JSON **(V)**

**File:** `TankAlarm-112025-Viewer-BluesOpta.ino` line ~686  
**Function:** `sendSensorJson()`

The entire JSON response is serialized into a `String` (heap allocation) before being sent via `respondJson()`. With 64 sensors, the JSON payload can reach 8-12 KB. Since `respondJson()` also computes `body.length()` and sends in 512-byte chunks, the full payload lives on the heap for the entire duration of the HTTP response. On the Opta Lite (256 KB RAM), this can cause heap exhaustion during high-sensor-count deployments.

**Impact:** `std::nothrow` in `JsonDocument` allocation protects against OOM for the doc itself, but the `serializeJson(doc, body)` call can still cause the `String` to fail silently (returning truncated JSON or an empty string).

**Fix:** Use streaming JSON serialization directly to the `EthernetClient`:
```cpp
client.println(F("HTTP/1.1 200 OK"));
client.println(F("Content-Type: application/json"));
// Compute Content-Length via measureJson, then use serializeJson(doc, client)
size_t len = measureJson(doc);
client.print(F("Content-Length: ")); client.println(len);
client.println(F("Cache-Control: no-cache, no-store, must-revalidate"));
client.println();
serializeJson(doc, client);
```

---

## MEDIUM Severity

### CR-M1: Stuck Sensor Detection Interferes With Unload Tracking **(C)**

**File:** `TankAlarm-112025-Client-BluesOpta.ino` lines ~4100-4180  
**Function:** `validateSensorReading()` — stuck detection block

When `stuckDetectionEnabled=true` and `trackUnloads=true` on the same monitor, a tank being emptied produces consecutive readings that differ by less than 0.05 inches (the hard-coded stuck tolerance). After 10 such readings, the sensor is marked as "stuck/failed" even though the tank is genuinely emptying slowly.

**Impact:** False `sensor-stuck` alarm during legitimate unload events; sensor data suppressed until readings diverge enough to recover.

**Fix:** Skip stuck detection when unload tracking is active and the level is dropping:
```cpp
// In stuck detection block:
bool exemptFromStuck = cfg.trackUnloads && state.unloadTracking;
if (!exemptFromStuck && fabsf(reading - state.lastValidReading) < STUCK_TOLERANCE) {
  state.stuckReadingCount++;
  // ...
}
```

---

### CR-M2: `millis()` Wraparound in Per-Type Alarm Interval Checks **(C)**

**File:** `TankAlarm-112025-Client-BluesOpta.ino` line ~4662  
**Function:** `checkAlarmRateLimit()`

The individual alarm type checks use `now - state.lastHighAlarmMillis < minInterval`. This is safe for `unsigned long` subtraction (wraps correctly at 49.7 days). However, `lastHighAlarmMillis` is initialized to 0. After a boot, `now - 0` equals `now`, which may be less than `minInterval` (5 minutes = 300,000 ms) if the first alarm fires within 5 minutes of boot.

**Impact:** The very first high/low alarm after boot is suppressed if it occurs within 5 minutes. Subsequent alarms are unaffected.

**Fix:** Initialize `lastHighAlarmMillis`, `lastLowAlarmMillis`, `lastClearAlarmMillis`, and `lastSensorFaultMillis` to `0 - minInterval` (i.e., the distant past from `millis()`'s perspective) during `MonitorRuntime` initialization:
```cpp
state.lastHighAlarmMillis = millis() - (MIN_ALARM_INTERVAL_SECONDS * 1000UL + 1);
```

---

### CR-M3: `readCurrentLoopSensor()` Returns Stale Value on I2C Failure **(C)** — BY DESIGN (reclassified)

**File:** `TankAlarm-112025-Client-BluesOpta.ino` line ~4310  

Already classified as by-design in TODO.md (I-5). The stuck sensor detection system catches persistent failures. No action needed.

---

### CR-M4: `sendAlarm()` Large JSON Document Allocated on Every Call **(C)**

**File:** `TankAlarm-112025-Client-BluesOpta.ino` line ~4855+  
**Function:** `sendAlarm()`

Each alarm call creates a `JsonDocument` (ArduinoJson v7 auto-sized) and serializes 15-20 fields. On rapid alarm sequences (multiple monitors triggering simultaneously during a power event), multiple documents are created in quick succession. While each one is scope-destroyed, the heap fragmentation risk compounds.

**Impact:** Unlikely OOM on steady-state operation, but during a multi-monitor alarm storm (e.g., power drop below all thresholds), heap pressure could cause subsequent allocations to fail.

**Fix:** Consider using a static `JsonDocument` for alarm serialization (reset with `doc.clear()` between uses), or pre-reserve heap in `setup()`.

---

### CR-M5: Server `handleTelemetry()` Deserializes Cached Config on Every Update **(S)**

**File:** `TankAlarm-112025-Server-BluesOpta.ino` line ~8157-8194  
**Function:** `handleTelemetry()` — objectType fallback path

When `objectType` is not in the telemetry payload (e.g., older client firmware), the server deserializes the full cached config snapshot (up to 1536 bytes of JSON) to look up the monitor type. This happens on **every telemetry update** for the affected sensor until the client sends an explicit `ot` field.

**Impact:** ~2-3 ms of JSON parsing per telemetry update. With 8 sensors reporting every 30 minutes, this adds ~0.05 ms/cycle — negligible per sensor, but wasteful if many legacy clients are active. Also creates temporary heap pressure from the parsed document.

**Fix:** Cache the resolved `objectType` in the `SensorRecord` so subsequent lookups skip the config deserialization path. This is already partially done (the result IS stored in `rec->objectType`), but only if the config lookup succeeds. Add an explicit "unknown" sentinel to prevent repeated lookups:
```cpp
if (rec->objectType[0] == '\0') {
  // ... lookup logic ...
  if (rec->objectType[0] == '\0') {
    strlcpy(rec->objectType, "tank", sizeof(rec->objectType)); // Final default
  }
}
```
**Status:** Already implemented at line ~8194. This finding is a **false positive** — the fallback `strlcpy(rec->objectType, "tank", ...)` prevents repeated lookups. Reclassified to **INFO**.

---

### CR-M6: `checkStaleClients()` VLA-like Stack Array for Client Removal **(S)**

**File:** `TankAlarm-112025-Server-BluesOpta.ino` line ~10017  
**Function:** `checkStaleClients()`

```cpp
char clientsToRemove[MAX_CLIENT_METADATA][48];
```

With `MAX_CLIENT_METADATA=20`, this allocates 960 bytes on the stack. While within limits, the function also has other local variables (loop counters, floats, etc.). Combined with the `snprintf` buffers in Phase 3 and Phase 5, total stack usage for this function could approach 1.5-2 KB.

**Impact:** On the Cortex-M7 (STM32H747XI), the default stack is typically 8-16 KB, so this is safe. However, if `MAX_CLIENT_METADATA` is increased in future, stack overflow risk rises.

**Fix:** Move `clientsToRemove` to a file-static array or use a smaller removal-count limit.

---

### CR-M7: Viewer `readHttpRequest()` Does Not Limit Header Count **(V)**

**File:** `TankAlarm-112025-Viewer-BluesOpta.ino` line ~542  
**Function:** `readHttpRequest()`

The header parsing loop reads indefinitely until an empty line. A malicious client could send thousands of small headers before the 5-second timeout, consuming CPU and preventing other web requests from being served.

**Impact:** DoS against the Viewer dashboard by sending a slow-drip of headers. Mitigated by the 5-second overall timeout and the 512-byte line length limit, but the device is unresponsive during the attack.

**Fix:** Add a maximum header count (e.g., 32):
```cpp
uint8_t headerCount = 0;
const uint8_t MAX_HEADERS = 32;
// ... in header loop ...
if (++headerCount > MAX_HEADERS) return false;
```

---

### CR-M8: `snprintf` SMS Message Truncation Risk **(S)**

**File:** `TankAlarm-112025-Server-BluesOpta.ino` lines ~8472-8484  
**Function:** `handleAlarm()` — SMS message construction

The 160-byte SMS buffer `message[160]` can be exceeded when `rec->site` (32 chars), `rec->alarmType` (24 chars), and `rec->measurementUnit` (8 chars) are all at maximum length. `snprintf` safely truncates, but the truncated message may cut off critical information (e.g., the level value or alarm type).

**Impact:** Operators may see incomplete SMS like `"North Facility Tank Farm... "` without the alarm level or type.

**Fix:** Pre-truncate `siteName` for SMS:
```cpp
char shortSite[24];
strlcpy(shortSite, rec->site, sizeof(shortSite));
snprintf(message, sizeof(message), "%s #%d %s %.1f %s",
         shortSite, rec->sensorIndex, rec->alarmType, level, rec->measurementUnit);
```

---

## LOW Severity

### CR-L1: `linearMap()` Missing From Common Headers **(C)**

**File:** `TankAlarm-112025-Client-BluesOpta.ino`  
**Function:** Used in `readAnalogSensor()` and `readCurrentLoopSensor()`

`linearMap()` is defined only in the Client sketch. If the Server or Viewer ever needs to perform inline sensor conversion (e.g., for calibration preview), the function must be duplicated. Currently the Server has its own conversion logic.

**Fix:** Move `linearMap()` to `TankAlarm_Utils.h` for reuse.

---

### CR-L2: `gSolarOnlyBootCount` Saturating Increment Missing **(C)**

**File:** `TankAlarm-112025-Client-BluesOpta.ino` — setup  

The boot count is a `uint32_t` that increments without overflow check. While 4 billion boots is unrealistic, a saturating increment (`if (count < UINT32_MAX) count++`) costs nothing and follows defensive coding best practice.

---

### CR-L3: `safeSleep()` Called With Long Durations During Ethernet Retry **(V)** — ALREADY FIXED

Per TODO.md item C-4, `safeSleep()` replaced bare `delay()` and feeds the watchdog. Verified correct in code.

---

### CR-L4: Server `posix_read_file()` Silently Truncates Large Files **(S)**

**File:** `TankAlarm-112025-Server-BluesOpta.ino` line ~1225  

If a file exceeds `bufSize - 1`, only the first `bufSize - 1` bytes are read. The caller receives a truncated result with no indication of data loss. This could cause corrupted JSON parsing for files that grow larger than expected (e.g., `sensor_registry.json` with many clients).

**Fix:** Return a special error code when truncation occurs, or log a warning.

---

### CR-L5: Server Email Buffer Not Validated Against Sensor Count **(S)**

**File:** `TankAlarm-112025-Server-BluesOpta.ino` line ~114  

`MAX_EMAIL_BUFFER` is 16384 bytes, documented as supporting ~70 sensors. With `MAX_SENSOR_RECORDS=64`, this is adequate with margin. However, the actual buffer usage per sensor depends on field lengths (site names, labels) which vary. No runtime check ensures the email builder stays within bounds.

**Fix:** Add a runtime check in the email builder:
```cpp
if (measureJson(emailDoc) > MAX_EMAIL_BUFFER - 512) {
  // Stop adding sensors to email, note truncation
}
```

---

### CR-L6: `TankAlarm_Utils.h` `static inline` Functions in Header **(A)**

**File:** `TankAlarm-112025-Common/src/TankAlarm_Utils.h`

All conversion functions are `static inline`, which generates separate copies in each compilation unit that includes the header. On Arduino (single `.ino` file per sketch), this is benign — only one copy exists. But if the project ever moves to multi-file compilation (PlatformIO), code size will balloon.

**Fix:** No action needed now. Document as a consideration for future modularization.

---

## INFO / Design Observations

### CR-I1: Single-Threaded Architecture Eliminates Race Conditions

The Server review agent flagged "race conditions in sensor registry access" as CRITICAL. This is a **false positive** on Arduino Opta. The Mbed OS Ethernet stack is interrupt-driven for packet reception, but the Arduino `loop()` function is single-threaded. All `handleTelemetry`, `handleAlarm`, and web request handlers execute sequentially in the main loop. No mutex is needed.

### CR-I2: XSS Mitigation Via Client-Side `escapeHtml()`

All dynamic data in the Server web UI flows through JSON API endpoints (`/api/sensors`, `/api/clients`, etc.) and is rendered by JavaScript using `escapeHtml()` (DOM `textContent` method). The Server does not inject user data into server-rendered HTML. The XSS risk is minimal and limited to scenarios where the JSON API returns content that bypasses the JavaScript escaping — which requires a prior injection into the sensor registry (trusted Notecard channel).

### CR-I3: FTP Credentials in Plaintext

FTP backup uses unencrypted credentials over the network. The code comments acknowledge LAN-only deployment. Adding a runtime warning when FTP is enabled would improve operational awareness. Not classified as a bug since the limitation is documented.

---

## Files Reviewed

| File | Lines | Coverage |
|------|-------|----------|
| TankAlarm-112025-Client-BluesOpta.ino | 7,356 | Full |
| TankAlarm-112025-Server-BluesOpta.ino | 12,778 | Full |
| TankAlarm-112025-Viewer-BluesOpta.ino | 912 | Full |
| TankAlarm_Common.h | 240 | Full |
| TankAlarm_Config.h | 125 | Full |
| TankAlarm_Platform.h | 306 | Full |
| TankAlarm_Battery.h | 323 | Full |
| TankAlarm_DFU.h | 551 | Full |
| TankAlarm_Diagnostics.h | 82 | Full |
| TankAlarm_I2C.h | 264 | Full |
| TankAlarm_Notecard.h | 110 | Full |
| TankAlarm_Utils.h | 119 | Full |
| TankAlarm_Solar.h | 243 | Full |
| TankAlarm_Solar.cpp | 412 | Full |

**Total lines reviewed: ~22,921**
