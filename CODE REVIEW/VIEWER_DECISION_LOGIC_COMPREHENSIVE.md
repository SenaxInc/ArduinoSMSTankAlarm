# Comprehensive Decision Logic Analysis: Viewer Sketch + Server/Client Cross-Reference

**File:** `TankAlarm-112025-Viewer-BluesOpta/TankAlarm-112025-Viewer-BluesOpta.ino`  
**Lines:** 997 total  
**Version:** 1.1.9  
**Date:** March 24, 2026

---

## TABLE OF CONTENTS

1. [Viewer: Constants & Thresholds](#1-viewer-constants--thresholds)
2. [Viewer: setup() — Initialization Sequence](#2-viewer-setup--initialization-sequence)
3. [Viewer: loop() — Main Decision Loop](#3-viewer-loop--main-decision-loop)
4. [Viewer: safeSleep() — Watchdog-Safe Delay](#4-viewer-safesleep--watchdog-safe-delay)
5. [Viewer: initializeNotecard()](#5-viewer-initializenotecard)
6. [Viewer: ensureTimeSync() / currentEpoch()](#6-viewer-ensuretimesync--currentepoch)
7. [Viewer: deriveMacFromUid()](#7-viewer-derivemacfromuid)
8. [Viewer: scheduleNextSummaryFetch()](#8-viewer-schedulenextsummaryfetch)
9. [Viewer: initializeEthernet()](#9-viewer-initializeethernet)
10. [Viewer: handleWebRequests()](#10-viewer-handlewebrequests)
11. [Viewer: readHttpRequest()](#11-viewer-readhttprequest)
12. [Viewer: respondJson() / respondStatus() / sendDashboard()](#12-viewer-response-helpers)
13. [Viewer: sendSensorJson()](#13-viewer-sendsensorjson)
14. [Viewer: fetchViewerSummary()](#14-viewer-fetchviewersummary)
15. [Viewer: handleViewerSummary()](#15-viewer-handleviewersummary)
16. [Viewer: checkForFirmwareUpdate()](#16-viewer-checkforfirmwareupdate)
17. [Viewer: enableDfuMode()](#17-viewer-enabledfumode)
18. [Viewer: Notecard I2C Health Check (in loop)](#18-viewer-notecard-i2c-health-check)
19. [Viewer: JavaScript Client-Side Logic](#19-viewer-javascript-client-side-logic)
20. [Server Cross-Reference: Stale Client Detection](#20-server-stale-client-detection)
21. [Server Cross-Reference: Daily Email Scheduling](#21-server-daily-email-scheduling)
22. [Server Cross-Reference: Calibration Learning Regression](#22-server-calibration-learning-regression)
23. [Client Cross-Reference: Power State Hysteresis](#23-client-power-state-hysteresis)
24. [Client Cross-Reference: Sensor Failure / Stuck Detection](#24-client-sensor-failure--stuck-detection)
25. [Summary of Edge Cases & Potential Issues](#25-summary-of-edge-cases--potential-issues)

---

## 1. Viewer: Constants & Thresholds

| Constant | Value | Purpose |
|----------|-------|---------|
| `VIEWER_SUMMARY_FILE` | `"viewer_summary.qi"` | Notecard inbound notefile for summary data |
| `VIEWER_CONFIG_PATH` | `"/viewer_config.json"` | Local config file path |
| `VIEWER_NAME` | `"Tank Alarm Viewer"` | Display name |
| `WEB_REFRESH_SECONDS` | `21600` (6 hours) | Browser auto-refresh interval |
| `SUMMARY_FETCH_INTERVAL_SECONDS` | `21600UL` (6 hours) | Default Notecard fetch interval |
| `SUMMARY_FETCH_BASE_HOUR` | `6` | Base hour alignment for fetch scheduling |
| `MAX_HTTP_BODY_BYTES` | `1024` | Maximum request body; caps to prevent memory exhaustion |
| `MAX_SENSOR_RECORDS` | *(from Common.h)* | Cap on sensor records in memory |
| `DFU_CHECK_INTERVAL_MS` | *(from Common.h)* | Hourly firmware update check |
| `NOTECARD_FAILURE_THRESHOLD` | *(from Common.h)* | Consecutive Notecard failures before marking unavailable |
| `I2C_NOTECARD_RECOVERY_THRESHOLD` | *(from Common.h)* | Failures before triggering I2C bus recovery |
| `NOTECARD_HEALTH_CHECK_BASE_INTERVAL_MS` | *(from Common.h)* | Initial health check interval |
| `NOTECARD_HEALTH_CHECK_MAX_INTERVAL_MS` | *(from Common.h)* | Max backoff for health checks |
| `I2C_WIRE_TIMEOUT_MS` | *(from Common.h)* | Wire.setTimeout value |
| `WATCHDOG_TIMEOUT_SECONDS` | *(from Common.h)* | Hardware watchdog timeout |

---

## 2. Viewer: setup() — Initialization Sequence
**Lines:** 226–284

### Execution Order (strictly sequential):
1. `Serial.begin(115200)` — wait up to 2000ms for serial
2. `Wire.begin()` + `Wire.setTimeout(I2C_WIRE_TIMEOUT_MS)`:  Guard against indefinite I2C bus hang
3. **I2C bus scan** — Verify Notecard is at `NOTECARD_I2C_ADDRESS` via `tankalarm_scanI2CBus()`
4. `initializeNotecard()` — Configure Notecard hub.set
5. `ensureTimeSync()` — Get epoch from Notecard
6. `deriveMacFromUid()` — Generate deterministic MAC from Notecard UID
7. `fetchViewerSummary()` — **Drain any queued summaries** before starting web server
8. `scheduleNextSummaryFetch()` — Calculate next aligned fetch epoch
9. `initializeEthernet()` — DHCP or static IP
10. `gWebServer.begin()` — Start HTTP listener
11. **Watchdog enable** — Platform-conditional (Mbed vs STM32)
12. Print heap stats

### Decisions:
- **Serial wait**: `while (!Serial && millis() < 2000)` — Gives USB 2s to connect, doesn't block headless forever
- **Watchdog platform**: `#if defined(ARDUINO_OPTA) || defined(ARDUINO_ARCH_MBED)` selects MbedWatchdogHelper vs IWatchdog
- **"Drain before serve"**: `fetchViewerSummary()` is called before `gWebServer.begin()` — ensures first HTTP client sees data, not an empty dashboard

### Edge Cases:
- If Notecard is absent from I2C scan, setup continues anyway — `initializeNotecard()` will fail silently
- If time sync fails (`gLastSyncedEpoch == 0`), `currentEpoch()` returns 0.0 — all epoch-based scheduling is disabled until sync succeeds
- No config file loading (`VIEWER_CONFIG_PATH`) happens in setup — the static IP/DHCP toggle comes from compile-time `gConfig` defaults only. Config hot-loading appears unimplemented.

---

## 3. Viewer: loop() — Main Decision Loop
**Lines:** 286–371

### Per-iteration sequence:
1. **Watchdog kick** — Platform-conditional
2. `Ethernet.maintain()` — DHCP lease renewal
3. `handleWebRequests()` — Process one HTTP request if available
4. `ensureTimeSync()` — Periodic time resync (delegated to shared library)
5. **Notecard I2C health check** (see §18) — Only runs when `!gNotecardAvailable` with exponential backoff
6. **Summary fetch trigger**: `if (gNextSummaryFetchEpoch > 0.0 && currentEpoch() >= gNextSummaryFetchEpoch)` — Fetch + reschedule
7. **DFU check**: `if (!gDfuInProgress && (currentMillis - gLastDfuCheckMillis >= DFU_CHECK_INTERVAL_MS))` — Check for firmware update

### Decisions:
| Condition | Action |
|-----------|--------|
| `gNextSummaryFetchEpoch > 0.0 && currentEpoch() >= gNextSummaryFetchEpoch` | Fetch viewer summary, reschedule |
| `!gDfuInProgress && elapsed >= DFU_CHECK_INTERVAL_MS` | Check DFU status |
| `!gNotecardAvailable && elapsed > ncHealthInterval` | Run I2C health probe with backoff |

### Edge Cases:
- **No yield/delay in main loop**: The loop is tight — no `delay()` or `safeSleep()` between iterations. On Opta this is fine (RTOS thread), but CPU usage is 100% of this thread.
- **Single-client web server**: `gWebServer.available()` returns at most one client per loop iteration. If the summary fetch or DFU check is slow (blocking I2C), HTTP clients are starved.
- **millis() rollover**: The DFU check uses `currentMillis - gLastDfuCheckMillis` — unsigned subtraction handles rollover correctly.
- **Epoch 0.0 guard**: Summary fetch is gated on `gNextSummaryFetchEpoch > 0.0`, so if time sync never succeeds, no fetches occur. This is correct but means a Viewer with no cellular connection and no prior sync is permanently dormant.

---

## 4. Viewer: safeSleep() — Watchdog-Safe Delay
**Lines:** 194–217

### Logic:
```
if ms == 0: return immediately
if watchdog available: chunk = (WATCHDOG_TIMEOUT * 1000) / 2
else: chunk = ms (no chunking needed)
while remaining > 0:
    delay(min(chunk, remaining))
    kick watchdog
    remaining -= chunk
```

### Decision: Chunk size = half the watchdog timeout
- Ensures at least one watchdog kick per chunk
- **Identical pattern to Server and Client** — shared via convention, not shared code

### Edge Cases:
- If `WATCHDOG_TIMEOUT_SECONDS` is 0 or very small, `maxChunk` could be 0 → infinite loop. In practice, watchdog timeout is always ≥16s.

---

## 5. Viewer: initializeNotecard()
**Lines:** 373–425

### Decisions:
1. **Debug mode**: `#ifdef DEBUG_MODE` → `notecard.setDebugOutputStream(Serial)`
2. **hub.set**: Configures product UID from `gConfig.productUid`, mode = `"continuous"`, fleet = `"tankalarm-viewer"`
3. **Error checking**: Inspects `err` field in hub.set response — warns but continues if it fails
4. **No response**: Prints warning, continues. No retry.
5. **UID retrieval**: Uses `hub.get` (NOT `card.uuid` which doesn't exist) to get `"device"` field

### Edge Cases:
- If `gConfig.productUid` is empty string (DEFAULT_VIEWER_PRODUCT_UID not set), `hub.set` will fail with "product required"
- If `hub.get` returns no device UID, `gViewerUid` remains `{0}` — `deriveMacFromUid()` will skip MAC derivation and default MAC is used

---

## 6. Viewer: ensureTimeSync() / currentEpoch()
**Lines:** 427–433

### Implementation:
Both delegate to shared library functions:
- `tankalarm_ensureTimeSync(notecard, gLastSyncedEpoch, gLastSyncMillis)` — calls `card.time` on Notecard
- `tankalarm_currentEpoch(gLastSyncedEpoch, gLastSyncMillis)` — returns `gLastSyncedEpoch + (millis() - gLastSyncMillis) / 1000.0`

**Identical to Server pattern.** Server's `currentEpoch()` (line 5558) uses the same formula:
```cpp
uint32_t delta = (uint32_t)(millis() - gLastSyncMillis);  // Handles millis() rollover
return gLastSyncedEpoch + (double)delta / 1000.0;
```

### Edge Cases:
- **millis() drift**: Between Notecard syncs, the MCU crystal determines time accuracy. Opta crystal has ~50ppm drift → ~4.3s/day. Syncing every 6 hours limits drift to ~1s.
- **Cast to uint32_t**: The `(uint32_t)(millis() - gLastSyncMillis)` handles the 49.7-day millis() rollover correctly for gaps under 49.7 days.
- **Initial state**: `gLastSyncedEpoch == 0.0` → returns 0.0 → all epoch comparisons disabled.

---

## 7. Viewer: deriveMacFromUid()
**Lines:** 443–467

### Logic:
```
if gViewerUid is empty: return (keep default MAC)
hash = DJB2(gViewerUid)
MAC[0] = 0x02 (locally administered, unicast)
MAC[1] = 0x00
MAC[2..5] = hash bytes (big-endian extraction)
```

### Decision: DJB2 hash for determinism
- Same UID always produces same MAC — avoids DHCP lease churn across reboots
- `0x02` in byte 0 = locally administered bit set, unicast bit clear

### Edge Cases:
- **Hash collision**: Two different UIDs could produce the same last 4 bytes. Probability is ~1/2^32 per pair. For a fleet of <100 viewers, collision risk is negligible.
- **Called before Ethernet init**: Correct — MAC must be set before `Ethernet.begin()`

---

## 8. Viewer: scheduleNextSummaryFetch()
**Lines:** 435–441

### Logic:
```
epoch = currentEpoch()
interval = gSourceRefreshSeconds > 0 ? gSourceRefreshSeconds : SUMMARY_FETCH_INTERVAL_SECONDS
baseHour = gSourceBaseHour
gNextSummaryFetchEpoch = computeNextAlignedEpoch(epoch, baseHour, interval)
```

### Decision: Aligned scheduling
- Uses `tankalarm_computeNextAlignedEpoch()` from shared library — same as Server viewer summary scheduling
- Base hour defaults to 6 AM, interval defaults to 6 hours → fetches at 6:00, 12:00, 18:00, 0:00

### Dynamic reconfiguration:
- `gSourceRefreshSeconds` and `gSourceBaseHour` are updated from the Server's summary JSON (see §15)
- If the Server changes its cadence, the Viewer adopts it on next summary

### Edge Cases:
- If `currentEpoch()` returns 0.0, `computeNextAlignedEpoch()` behavior depends on shared library — likely returns 0.0 or a value near epoch 0 (1970). The loop guard `gNextSummaryFetchEpoch > 0.0` should protect against immediate triggering.
- If the Server sends `refreshSeconds: 0`, fallback to default 21600s is applied.

---

## 9. Viewer: initializeEthernet()
**Lines:** 469–512

### Decision tree:
```
if gConfig.useStaticIp:
    Ethernet.begin(mac, staticIp, dns, gateway, subnet)
else:
    Ethernet.begin(mac)  // DHCP

if status == 0 (FAILED):
    Retry up to 3 times:
        retryDelay = attempt * 5000ms  (5s, 10s, 15s)
        delay(retryDelay)
        retry same mode (static or DHCP)
    if all retries fail:
        print error, continue anyway
```

### Edge Cases:
- **No watchdog kick during retry delays**: `delay(retryDelay)` is a raw delay, not `safeSleep()`. With retry delays of 5-15s, and watchdog of ~16s, the 15s delay could be close to a watchdog reset.
  - **POTENTIAL ISSUE**: The third retry uses `delay(15000)` — if watchdog timeout is 16s, this leaves only 1s margin. If any other delay occurs before the retry, watchdog WILL fire. Should use `safeSleep()` instead.
- **Continues without Ethernet**: If all retries fail, the code proceeds. Web server calls will just never accept connections. Summary fetches via Notecard still work.
- **Static fallback**: There is no automatic fallback from DHCP failure to static IP.

---

## 10. Viewer: handleWebRequests()
**Lines:** 514–548

### Decision tree:
```
client = gWebServer.available()
if !client: return

if !readHttpRequest(): respond 400, stop
if bodyTooLarge: respond 413, stop

if GET / : sendDashboard()
elif GET /api/sensors: sendSensorJson()
else: respond 404

safeSleep(1)
client.stop()
```

### Security decisions:
- **Read-only**: Only GET `/` and GET `/api/sensors` are served. No POST, PUT, DELETE endpoints.
- **No authentication**: Any client on the network can access the dashboard. By design — "kiosk mode."
- **Body cap**: Even though Viewer only accepts GET, it still caps request body to 1024 bytes as defense-in-depth.

### Edge Cases:
- **No keep-alive**: Every request opens/closes connection. The `safeSleep(1)` before `client.stop()` gives the client 1ms to read the response.
- **Path matching is exact**: `/api/sensors?foo=bar` would NOT match `/api/sensors` → 404. Query parameters are not stripped.
- **No `Host` header validation**: Any Host header is accepted.

---

## 11. Viewer: readHttpRequest()
**Lines:** 550–635

### Decision logic:
```
Parse until blank line (double newline):
    First line: extract METHOD SP PATH SP HTTP/version
    Subsequent lines: extract Content-Length header

if Content-Length > MAX_HTTP_BODY_BYTES:
    bodyTooLarge = true
    Read only MAX_HTTP_BODY_BYTES

Timeout: 5000ms for headers, 5000ms for body
Line length cap: 512 chars per header line
```

### Decisions:
| Condition | Action |
|-----------|--------|
| `line.length() > 512` | return false (→ 400 Bad Request) |
| No space in first line | return false |
| Content-Length > 1024 | Set bodyTooLarge, cap read |
| Connection timeout (5s) | Stop reading, return what we have |

### Edge Cases:
- **Slowloris vulnerability**: The 5s timeout protects against slow header attacks, but a client sending one byte per second can hold the connection for ~5s × 512 chars = 2560 seconds. However, since the Viewer is single-threaded and processes one client at a time, a slowloris would block ALL HTTP for up to 5s per phase.
- **`\r` stripping**: Only `\r` is stripped, `\n` is the line delimiter. A client sending only `\n` (no `\r`) is handled correctly.
- **Case-insensitive Content-Length**: `headerKey.equalsIgnoreCase("Content-Length")` — correct.
- **String concatenation**: `body += c` in a loop → O(n²) for large bodies. Capped at 1024 bytes so it's acceptable.
- **Body read continues past bodyTooLarge**: Once `readBytes >= MAX_HTTP_BODY_BYTES`, the loop breaks. Remaining body data stays in the TCP buffer and will contaminate the next connection.

---

## 12. Viewer: Response Helpers
**Lines:** 637–711

### respondJson() (L637–656):
- Sends HTTP 200, Content-Type: application/json
- Sets `Cache-Control: no-cache, no-store, must-revalidate`
- Chunked write in 512-byte chunks to avoid memory issues

### respondStatus() (L658–682):
- Maps status codes: 200, 400, 404, 413, 500
- Falls through to "Error" for unknown status codes

### sendDashboard() (L684–711):
- Reads PROGMEM HTML in 128-byte chunks via `pgm_read_byte_near()`
- Sets `Content-Length` from `strlen_P()` — correct for PROGMEM strings

### Edge Cases:
- **No Connection: close header**: HTTP/1.1 defaults to keep-alive, but `client.stop()` is called in `handleWebRequests()` after every response. Edge case: if client expects keep-alive, it may wait before rendering.

---

## 13. Viewer: sendSensorJson()
**Lines:** 713–768

### Decision logic:
- Allocates `JsonDocument` on heap via `new (std::nothrow)`
- If allocation fails → respond 500 "Out of Memory"
- Populates metadata fields: `vn`, `vi`, `sn`, `si`, `ge`, `lf`, `nf`, `rs`, `bh`, `sf`, `rc`, `ls`
- Iterates `gSensorRecords[0..gSensorRecordCount-1]`:
  - Always includes: `c`, `s`, `n`, `k`, `l`, `a`, `at`, `u`
  - Conditional: `un` (if > 0), `v` (if > 0.0f), `ot`/`st`/`mu` (if non-empty), `d` (if `hasChange24h`)

### Field selection logic:
```
if userNumber > 0: include "un"
if vinVoltage > 0.0f: include "v"
if objectType[0] != '\0': include "ot"
if sensorType[0] != '\0': include "st"
if measurementUnit[0] != '\0': include "mu"
if hasChange24h: include "d"
```

### Edge Cases:
- **`serializeJson()` to String**: The `String body` grows dynamically. For many sensors (MAX_SENSOR_RECORDS), this could OOM. The heap-allocated JsonDocument helps, but the serialized String is also on heap.
- **No overflow check**: Unlike Server's `doc.overflowed()` check, Viewer doesn't check for ArduinoJson document overflow.

---

## 14. Viewer: fetchViewerSummary()
**Lines:** 770–838

### Decision logic (drain loop):
```
while true:
    kick watchdog
    if ++notesProcessed > 20: break (safety cap)
    
    req = note.get(file=VIEWER_SUMMARY_FILE, delete=true)
    if !req: increment failure count, check threshold, return
    
    rsp = requestAndResponse(req)
    if !rsp: increment failure count, check threshold, return
    
    // Reset failure tracking on any response
    if (!gNotecardAvailable): recover
    gNotecardFailureCount = 0
    
    body = rsp["body"]
    if !body: break  // No more notes (normal exit)
    
    Parse JSON body → handleViewerSummary()
    Delete response, loop for next note
```

### Key decisions:
| Condition | Action |
|-----------|--------|
| `notesProcessed > 20` | Cap drain, defer remaining to next cycle |
| `!req` (allocation fail) | Increment failure count, mark Notecard unavailable after threshold |
| `!rsp` (no response) | Same failure tracking |
| Any valid response | Reset `gNotecardAvailable = true`, reset failure count |
| `!body` in response | Break — no more queued notes (normal termination) |
| Parse error | Print error, skip this note, continue loop |
| OOM during JsonDocument alloc | Print error, skip note, continue |

### Edge Cases:
- **`delete: true` on note.get**: Each note is consumed after reading. If `handleViewerSummary()` crashes or OOMs AFTER the note.get returns but BEFORE the data is processed, that summary is lost.
- **Cap of 20 notes**: If summaries queue up faster than they're consumed (e.g., Viewer was offline for weeks), it takes multiple cycles to drain. At 6h intervals, 20 notes = 5 days of queue. If queue is deeper, it takes multiple 6h cycles → significant lag.
- **Failure counter vs. note loop**: If the first note.get fails (Notecard issue), the function returns immediately. It doesn't retry within the same call.

---

## 15. Viewer: handleViewerSummary()
**Lines:** 840–903

### Decision logic for metadata parsing:
```
serverName = doc["sn"] | doc["serverName"] | "Tank Alarm Server"    // Short key, legacy key, or default
serverUid = doc["si"] | doc["serverUid"] | ""

refreshSeconds: prefer "rs" key, fallback to "refreshSeconds"
    if 0 → use SUMMARY_FETCH_INTERVAL_SECONDS default

baseHour: prefer "bh" key, fallback to "baseHour"

generatedEpoch: prefer "ge", fallback "generatedEpoch", fallback note epoch, fallback currentEpoch()

sensors array: parse up to MAX_SENSOR_RECORDS
```

### Per-sensor parsing:
```
memset(&rec, 0, sizeof(SensorRecord))  // Zero-initialize
clientUid = item["c"] | ""
site = item["s"] | ""
label = item["n"] | "Tank"            // Default label = "Tank"
sensorIndex = item["k"] as uint8_t, or gSensorRecordCount (fallback)
userNumber = item["un"] or 0
levelInches = item["l"] as float
alarmActive = item["a"] as bool
alarmType = item["at"] | (alarmActive ? "alarm" : "clear")   // Dynamic default
lastUpdateEpoch = item["u"] as double
vinVoltage = item["v"] as float
objectType/sensorType/measurementUnit = optional string fields
change24h: if item["d"] is not null → set hasChange24h = true
```

### Edge Cases:
- **`gSensorRecordCount` reset**: Set to 0 at top of function → complete replacement of sensor data. If parsing fails mid-array, partial data is served.
- **Missing keys**: ArduinoJson `|` operator returns default values. `item["l"].as<float>()` returns 0.0f if missing — a tank reporting 0 inches is indistinguishable from a missing reading.
- **`alarmType` default based on `alarmActive`**: Smart — but if `alarmActive` is wrong (corrupted JSON), the alarm type follows the corruption.
- **No schema version check**: The Viewer doesn't validate `schemaVersion`. If the Server upgrades its summary format, the Viewer may silently misparse new fields.
- **Truncation safety**: `strlcpy()` is used throughout — no buffer overflows. But silently truncated UIDs could cause mismatches.

---

## 16. Viewer: checkForFirmwareUpdate()
**Lines:** 907–960

### Decision tree:
```
req = dfu.status
if !req or !rsp: print error, return
if responseError: print error, return

mode = rsp["mode"]
body = rsp["body"] 

if mode == "downloading" || "download-pending":
    gDfuInProgress = true, return

if mode == "ready" && body has content:
    updateAvailable = true
    
if updateAvailable:
    gDfuUpdateAvailable = true
    strlcpy(gDfuVersion, body, 32)
    #ifdef DFU_AUTO_ENABLE: enableDfuMode()
    #else: print "available but disabled"
else:
    gDfuUpdateAvailable = false
```

### Edge Cases:
- **DFU auto-enable is compile-time**: `#ifdef DFU_AUTO_ENABLE` — there's no runtime toggle. This is probably intentional for safety (avoid accidental remote updates) but means changing DFU policy requires recompilation.
- **Body as version string**: `body = JGetString(rsp, "body")` — the Notecard DFU body field contains firmware version info. If it's very long, `strlcpy` to 32-char buffer truncates.

---

## 17. Viewer: enableDfuMode()
**Lines:** 962–997

### Decision tree:
```
if !gDfuUpdateAvailable: return early
req = dfu.status (with "on": true)
if !req or !rsp: print error, return
if responseError: print error, return
else: gDfuInProgress = true
```

### Edge Cases:
- **Uses dfu.status not dfu.mode**: The code sends `dfu.status` with `"on": true` to enable DFU. This matches the Blues Notecard API (dfu.status controls the DFU state machine).
- **No rollback**: Once DFU is enabled, the Notecard downloads firmware and resets the host MCU. There's no undo.
- **No watchdog consideration**: During DFU download, the main loop continues running (DFU is Notecard-side). But the watchdog is still active, which is correct.

---

## 18. Viewer: Notecard I2C Health Check (in loop)
**Lines:** 308–360

### State machine:
```
static lastNcHealthCheck = 0
static ncHealthInterval = NOTECARD_HEALTH_CHECK_BASE_INTERVAL_MS

if (!gNotecardAvailable && elapsed > ncHealthInterval):
    Send card.version request
    
    if response received:
        gNotecardAvailable = true
        gNotecardFailureCount = 0
        tankalarm_ensureNotecardBinding(notecard)
        ncHealthInterval = NOTECARD_HEALTH_CHECK_BASE_INTERVAL_MS  // Reset backoff
        
    else (no response):
        gNotecardFailureCount++
        if >= I2C_NOTECARD_RECOVERY_THRESHOLD:
            tankalarm_recoverI2CBus()  // Clock-toggle recovery
            tankalarm_ensureNotecardBinding(notecard)
            gNotecardFailureCount = 0
        
        // Exponential backoff
        ncHealthInterval *= 2
        if > MAX: cap at MAX
```

### Decision: Exponential backoff
- Starts at BASE interval (probably ~30s-60s)
- Doubles on each failure up to MAX (probably ~15-30min)
- Resets to BASE on recovery

### Edge Cases:
- **Static locals**: `lastNcHealthCheck` and `ncHealthInterval` are function-scoped statics — they survive across loop iterations but not across reboots. After reboot, backoff resets. This is correct behavior.
- **I2C recovery**: `tankalarm_recoverI2CBus()` does clock-toggle recovery (standard I2C bus recovery). Passes a lambda to kick the watchdog during recovery.
- **`gDfuInProgress` check in recovery**: Recovery skips if DFU is in progress (the Notecard is busy downloading firmware).
- **Pattern identical to Server/Client**: All three devices use the same health check with exponential backoff. Shared via convention.

---

## 19. Viewer: JavaScript Client-Side Logic
**Lines:** embedded in `VIEWER_DASHBOARD_HTML` (PROGMEM, lines ~130–195)

### Stale data detection (client-side):
```javascript
const staleThresholdMs = 93600000;  // 26 hours in ms
const isStale = lastUpdate && ((now - (lastUpdate * 1000)) > staleThresholdMs);
```

- If sensor data is >26 hours old: adds ⚠️ warning, sets opacity to 0.6, adds tooltip

### Formatting logic:
```javascript
formatFeetInches(inches):
    if typeof != number || !isFinite || < 0: return '--'
    feet = floor(inches / 12)
    remaining = inches - (feet * 12)
    return `${feet}' ${remaining.toFixed(1)}"`

format24hChange(t):
    d = t.d
    if undefined/null/not-number/not-finite: return '--'
    return sign prefix + d.toFixed(1) + '"'

formatEpoch(epoch):
    if !epoch: return '--'
    new Date(epoch * 1000)
    if isNaN: return '--'
    return date.toLocaleString()
```

### XSS protection:
```javascript
escapeHtml(value, fallback=''):
    maps: & < > " ' → HTML entities
```
- Applied to all user-sourced strings (site name, tank label, alarm type)

### Auto-refresh:
```javascript
setInterval(() => fetchSensors(), REFRESH_SECONDS * 1000)
```
- `REFRESH_SECONDS` is compiled-in (21600 = 6 hours)

### Edge Cases:
- **Stale threshold mismatch**: Client-side uses 26 hours (93600000ms). Server uses 49 hours for stale alerting. Viewer will show stale warning for data between 26-49 hours that the Server considers fresh. This is actually correct — it's a UI-level early warning.
- **Timezone handling**: `toLocaleString()` uses the browser's timezone. If the Viewer kiosk is in a different timezone than the Server, timestamps show in local time (correct for the viewer's location).
- **Negative inches**: `formatFeetInches` returns `--` for negative values. A slight below-zero reading (e.g., -0.1 from sensor drift) shows as `--` rather than error.

---

## 20. Server Cross-Reference: Stale Client Detection

**File:** `TankAlarm-112025-Server-BluesOpta.ino`  
**Function:** `checkStaleClients()` — Lines 9934–10050+

### Constants:
| Constant | Value | Purpose |
|----------|-------|---------|
| `STALE_CLIENT_THRESHOLD_SECONDS` | 176400 (49h) | Alert threshold: no data for 49 hours |
| `STALE_CHECK_INTERVAL_MS` | 3600000 (1h) | How often to run stale check |
| `ORPHAN_SENSOR_PRUNE_SECONDS` | 259200 (72h/3 days) | Threshold for orphaned sensor pruning |
| `STALE_CLIENT_PRUNE_SECONDS` | 604800 (7 days) | Auto-remove fully stale clients |
| `MIN_ARCHIVE_AGE_SECONDS` | 2592000 (30 days) | Minimum age to archive before removal |

### 5-Phase algorithm:
1. **Dedup**: `deduplicateSensorRecordsLinear()` — catch duplicate records
2. **Per-client analysis**: Count total sensors, stale sensors (>72h), find latest update epoch
3. **Orphan pruning**: If `staleSensors > 0 && staleSensors < totalSensors` → prune orphans
4. **Stale SMS alerting**: If `offlineSeconds >= 49h` && `!meta.staleAlertSent` → send SMS, set flag
5. **Auto-removal**: If `offlineSeconds >= 7 days` → defer removal, archive to FTP, remove data

### Edge Cases & Issues:
- **49-hour window rationale**: The comment says "one missed daily before flagging stale, avoiding false positives." This assumes clients report daily. If a client reports every 6 hours, 49 hours is ~8 missed reports before alert.
- **Orphan pruning vs. actual slow sensors**: A sensor that naturally reports infrequently (e.g., a gas pressure sensor checked daily) could be pruned as "orphan" if it crosses the 72h threshold while other sensors from the same client are fresh.
- **Auto-removal side effects**: When a fully stale client is removed, its calibration data, alarm history, and sensor records are all deleted. `archiveClientToFtp()` attempts to back up first, but if FTP fails, data is lost permanently.
- **Deferred removal**: Client removal is deferred to avoid modifying the metadata array during iteration — correct pattern.
- **No stale recovery notification**: When `meta.staleAlertSent` is cleared (client reports again), there's no "client recovered" SMS. Only a serial log message.

---

## 21. Server Cross-Reference: Daily Email Scheduling

**Function:** `scheduleNextDailyEmail()` — Lines 5563–5571  
**Function:** `sendDailyEmail()` — Lines 9137–9280  
**Trigger in loop():** Line 2772–2774

### Scheduling algorithm:
```cpp
dayStart = floor(epoch / 86400.0) * 86400.0   // UTC midnight
scheduled = dayStart + dailyHour * 3600 + dailyMinute * 60
if scheduled <= epoch:
    scheduled += 86400.0  // Push to tomorrow
gNextDailyEmailEpoch = scheduled
```

### Email sending logic:
1. Build email recipient list from contacts config (`dailyReportRecipients`)
2. Fallback to legacy `gConfig.dailyEmail` field if no contacts configured
3. If no recipients → return silently
4. **Rate limit**: `if (now - gLastDailyEmailSentEpoch) < 3600s` → reject (minimum 1 hour between daily emails)
5. Build JSON payload with all sensor records
6. Check `doc.overflowed()` → abort if JSON too big
7. Serialize to static 16KB buffer (avoids stack overflow on Mbed OS 4-8KB stack)
8. `note.add` to `email.qo` with `sync: true`
9. Kick watchdog before Notecard transaction

### Edge Cases & Issues:
- **UTC-based scheduling**: `floor(epoch / 86400.0) * 86400.0` gives UTC midnight. `dailyHour` is treated as UTC hour. If user sets "6 AM" expecting local time but the Server's epoch is UTC, the email goes out at 6:00 UTC (which could be 1:00 AM EST or 11:00 PM PST the previous day).
  - **POTENTIAL ISSUE**: No timezone awareness. The web UI lets users set a time, but the Server interprets it as UTC. Unless the user accounts for this, emails arrive at unexpected times.
- **Rate limit prevents double-send**: The 1-hour minimum prevents issues if the scheduler fires twice (e.g., due to clock adjustment).
- **Static buffer reuse**: `static char buffer[MAX_EMAIL_BUFFER]` — shared across calls. If `sendDailyEmail()` were somehow called concurrently (impossible on single-threaded MCU), data corruption would occur.
- **Watchdog kick before I2C**: The note.add transaction can be slow with many sensors. The comment explicitly calls out that JSON construction + I2C can exceed 30s.
- **Schema stamp**: `stampSchemaVersion(body)` adds a version field for forward compatibility.
- **No retry on failure**: If `note.add` fails, the email is simply logged as error and not reattempted until the next daily cycle.

---

## 22. Server Cross-Reference: Calibration Learning Regression

**Function:** `recalculateCalibration()` — Lines 12133–12500+  
**Data structures:** Lines 445–497  
**Constants:** Lines 269–308

### Constants:
| Constant | Value | Purpose |
|----------|-------|---------|
| `MAX_CALIBRATION_ENTRIES` | 100 | Max data points per sensor |
| `MAX_CALIBRATION_SENSORS` | 20 | Max sensors with calibration |
| `TEMP_REFERENCE_F` | 70.0 | Reference temp for normalization |
| `MIN_TEMP_ENTRIES_FOR_COMPENSATION` | 5 | Min temp-enabled points for temp regression |
| Temperature range required | 10°F | Min temp variation for temp compensation |

### Algorithm (two-path regression):

**Path 1: Multiple linear regression (with temperature)**  
Requires: ≥5 entries with temp data AND ≥10°F temperature variation
```
Model: level = b0 + b1 * sensorReading + b2 * (temperature - 70°F)
Solver: Normal equations via Cramer's rule (3×3 system)
Fallback: If determinant < 0.0001 (singular matrix) → fall back to simple regression
R² calculation: 1 - (SSresid / SStotal), clamped to [0, 1]
```

**Path 2: Simple linear regression (no temperature)**  
Requires: ≥2 data points
```
Model: level = slope * sensorReading + offset
Solver: Standard OLS formulas
R² calculation: (SSxy²) / (SSx × SSy), clamped to [0, 1]
Fallback: If denominator < 0.0001 → give up (hasLearnedCalibration = false)
```

### Data validation:
- Only includes points where `4.0 ≤ sensorReading ≤ 20.0` (4-20mA range)
- Only includes points where `verifiedLevel ≥ 0.0`
- Temperature validity: `-50°F < temperature < 150°F` AND not `TEMPERATURE_UNAVAILABLE`

### Edge Cases & Issues:
- **Static points array**: `static CalibrationPoint points[MAX_CALIBRATION_ENTRIES]` — 100 entries × (4 floats + 1 bool) = ~2KB. Static allocation is fine, but means only one `recalculateCalibration()` can run at a time (correct for single-threaded).
- **Cramer's rule numerical stability**: For 3×3 systems, Cramer's rule is acceptable. But with data points clustered in a narrow range, the coefficient matrix can be ill-conditioned even when `det > 0.0001`. The singular check (`fabs(det) < 0.0001f`) may miss near-singular cases.
  - **POTENTIAL ISSUE**: With real-world sensor data where readings are closely spaced (e.g., tank always at 80-90% full), the matrix could be poorly conditioned and produce wildly inaccurate slope/tempCoef values even with a "good" R².
- **R² calculation difference**: Simple regression uses the correlation-based formula `r² = SSxy² / (SSx × SSy)`, while multiple regression uses `1 - SSresid/SStotal`. Both are standard and correct for their respective models.
- **Temperature normalization**: Subtracting 70°F improves numerical stability by keeping X2 values small. This is good practice.
- **Only applies to 4-20mA sensors**: The validation filter `4.0 ≤ sensorReading ≤ 20.0` means this calibration system only works for current-loop sensors, not digital, analog voltage, or RPM sensors. This is by design.
- **File I/O during regression**: The function reads the calibration log file from filesystem during calculation. If the file is corrupted or has parse errors, individual lines are skipped but processing continues.
- **Duplicate code**: The file reading logic is duplicated nearly identically for POSIX (`fopen/fgets`) and LittleFS (`File.open/readStringUntil`). This is a maintenance burden but functionally correct.
- **Tab-delimited parsing**: Uses `indexOf('\t')` for field separation. If notes contain tabs, parsing breaks for that entry (silently skipped).

---

## 23. Client Cross-Reference: Power State Hysteresis

**Function:** `updatePowerState()` — Lines 5632–5830  
**Constants:** Lines 260–365

### State machine (4 states):
| State | Entry Voltage | Exit Voltage | Hysteresis Gap | Sleep Interval |
|-------|--------------|-------------|----------------|----------------|
| NORMAL | — | — | — | 100ms |
| ECO | <12.0V | ≥12.4V | 0.4V | 5s |
| LOW_POWER | <11.8V | ≥12.3V | 0.5V | 30s |
| CRITICAL_HIBERNATE | <11.5V | ≥12.2V | 0.7V | 5min |

### Transition rules:
- **Degradation**: Can jump multiple levels down (NORMAL → CRITICAL if voltage drops fast)
- **Recovery**: Steps up ONE level at a time (CRITICAL → LOW_POWER → ECO → NORMAL)
- **Debounce**: Requires `POWER_STATE_DEBOUNCE_COUNT` (3) consecutive readings at proposed new state before transition

### Additional behaviors:
- **On entering CRITICAL**: All relays de-energized (saves ~100mA per relay coil)
- **Power state change notification**: Sends telemetry to server on every transition
- **Periodic logging**: Every 30 minutes when not NORMAL
- **Transition log rate limiting**: Minimum 5 minutes between transition log entries
- **Remote-tunable thresholds**: `gConfig.powerEcoEnterV` etc. override compile-time defaults if > 0.0f
- **Battery failure fallback**: After `batteryFailureThreshold` consecutive CRITICAL readings → enable solar-only behaviors
- **Battery failure decay**: If 24 hours pass without CRITICAL reading, fail counter resets to 0 (prevents slow accumulation over weeks)
- **Solar-only recovery**: When battery recovers to ECO or better, deactivate solar-only fallback

### Edge Cases & Issues:
- **Asymmetric transitions**: Degradation can skip levels (NORMAL → CRITICAL) but recovery must step through each level. This is conservative and correct — prevents oscillation on recovery.
- **Debounce resets on state match**: `if (proposed != gPowerState) gPowerStateDebounce++; else gPowerStateDebounce = 0;` — if voltage oscillates between two states, the debounce counter resets every other reading. With high-frequency oscillation, no transition ever occurs. This is the *desired* behavior (the hysteresis band should prevent it unless thresholds are poorly chosen).
- **`getEffectiveBatteryVoltage()` returns 0**: If no battery monitoring is active, forces NORMAL state. Good — prevents false power conservation on devices without battery monitoring.
- **Gap between ECO entry (12.0V) and LOW_POWER entry (11.8V)**: Only 0.2V. A battery under load can drop 0.2V quickly, meaning a device might transition through ECO to LOW_POWER in rapid succession (though debounce would require 3 × 2 = 6 readings).
- **Multiplier stacking**: ECO uses 2x outbound multiplier, LOW uses 4x. These are applied to the base outbound interval. If base is 6h, LOW_POWER means 24h between syncs — the Server's stale threshold of 49h allows two missed reports before alert.

---

## 24. Client Cross-Reference: Sensor Failure / Stuck Detection

**Function:** `validateSensorReading()` — Lines 3825–3950  
**Constants:**
| Constant | Value | Purpose |
|----------|-------|---------|
| `SENSOR_STUCK_THRESHOLD` | 10 | Consecutive identical readings to flag stuck |
| `SENSOR_FAILURE_THRESHOLD` | *(from Common)* | Consecutive out-of-range readings to flag failure |
| Stuck epsilon | 0.05f | `fabs(reading - lastValid) < 0.05` means "same" |

### Decision tree:
```
1. Calculate valid range based on sensor type:
   - Current loop ultrasonic: maxValid = sensorMountHeight * 1.1
   - Current loop pressure: maxValid = (rangeMax * convFactor + mountHeight) * 1.1
   - Digital: [-0.5, 1.5]
   - Other (RPM etc): maxValid = highAlarmThreshold * 2.0
   - minValid = -maxValid * 0.1 (allow slight negative for drift)

2. Range check:
   if reading < minValid || reading > maxValid:
       consecutiveFailures++
       if >= SENSOR_FAILURE_THRESHOLD:
           sensorFailed = true
           Publish "sensor-fault" alarm (rate limited)
       return false

3. Stuck detection (if enabled):
   if cfg.stuckDetectionEnabled && hasLastValidReading && |reading - lastValid| < 0.05:
       stuckReadingCount++
       if >= SENSOR_STUCK_THRESHOLD (10):
           sensorFailed = true
           Publish "sensor-stuck" alarm (rate limited)
       return false
   else:
       stuckReadingCount = 0  // Different reading → reset counter

4. Valid reading:
   consecutiveFailures = 0
   if was previously failed:
       sensorFailed = false
       Publish "sensor-recovered" notification (no rate limit)
   lastValidReading = reading
   hasLastValidReading = true
   return true
```

### Edge Cases & Issues:
- **Stuck detection epsilon (0.05)**: For a 4-20mA sensor with 10-bit ADC, 0.05 inches change is approximately 0.001 mA. Real-world ADC noise on an Opta should be >0.05 for most sensors, so true stuck readings would be caught. BUT if a tank is completely stationary (e.g., not in use), the level genuinely doesn't change. The 0.05f epsilon is very tight.
  - **POTENTIAL ISSUE**: A stable, legitimately unchanged tank level will trigger stuck detection after 10 readings. The `stuckDetectionEnabled` flag exists to disable this per-sensor, but it defaults to `true`. Operators must know to disable it for stable tanks.
- **Recovery notification not rate limited**: The comment says "no rate limit on recovery." If a sensor oscillates between failed and recovered, it generates a recovery notification on every good reading after failure. This could flood the server.
- **`stuckReadingCount` only resets on different reading**: If stuck detection fires (count ≥ 10) and `return false`, the next call with the same reading increments `stuckReadingCount` to 11, 12, etc. — but `sensorFailed` is already true so it just publishes another alarm (rate limited) and returns false. The counter never wraps or overflows because it's `uint8_t` and would stop at 255.
- **Negative minValid**: `minValid = -maxValid * 0.1f` — for a tank with mount height 100", this is -11". A legitimate sensor error producing -5" would be considered valid. This is intentional to handle slight calibration offsets.
- **Digital sensor range [-0.5, 1.5]**: Digital sensors return 0.0 or 1.0. Allowing ±0.5 around these values handles float imprecision.
- **No separate stuck vs. range-fail state**: Once `sensorFailed = true`, it doesn't distinguish the cause. Recovery clears it regardless. The alarm type is published as different events ("sensor-fault" vs "sensor-stuck"), so the Server can distinguish.

---

## 25. Summary of Edge Cases & Potential Issues

### Viewer Issues:
| # | Severity | Location | Issue |
|---|----------|----------|-------|
| V1 | **Medium** | `initializeEthernet()` L499 | Uses raw `delay()` instead of `safeSleep()` during Ethernet retries — 15s delay on third retry risks watchdog reset |
| V2 | Low | `handleWebRequests()` L540 | Exact path matching — `/api/sensors?param=val` returns 404 |
| V3 | Low | `sendSensorJson()` L713 | No `doc.overflowed()` check after populating JsonDocument |
| V4 | Low | `fetchViewerSummary()` L770 | `delete: true` on note.get means lost data if processing fails after read |
| V5 | Info | `handleViewerSummary()` L840 | Missing level (0.0f) is indistinguishable from an actual zero reading |
| V6 | Info | setup() | No config file loading implemented — all config is compile-time only |
| V7 | Low | `readHttpRequest()` L635 | Remaining body data stays in TCP buffer after bodyTooLarge, could affect next connection |
| V8 | Info | loop() | No delay/yield in main loop — 100% CPU utilization on its thread |

### Server Issues (from analyzed sections):
| # | Severity | Location | Issue |
|---|----------|----------|-------|
| S1 | **Medium** | `scheduleNextDailyEmail()` | UTC-based scheduling with no timezone awareness — users likely expect local time |
| S2 | Low | `checkStaleClients()` | No "client recovered" SMS after stale alert clears |
| S3 | Low | `recalculateCalibration()` | Cramer's rule singular check (0.0001 threshold) may miss near-singular matrices with clustered data |
| S4 | Info | `recalculateCalibration()` | Tab-separated calibration log — notes containing tabs break parsing |
| S5 | Info | `sendDailyEmail()` | No retry on mail send failure until next daily cycle |

### Client Issues (from analyzed sections):
| # | Severity | Location | Issue |
|---|----------|----------|-------|
| C1 | **Medium** | `validateSensorReading()` | Stuck detection defaults to enabled — stable/idle tanks will false-positive after 10 readings |
| C2 | Low | `validateSensorReading()` | Recovery notifications not rate-limited — oscillating sensor could flood server |
| C3 | Info | `updatePowerState()` | Only 0.2V gap between ECO (12.0V) and LOW_POWER (11.8V) entry thresholds |

### Cross-System Issues:
| # | Severity | Issue |
|---|----------|-------|
| X1 | Info | Viewer stale threshold (26h JS) vs Server stale threshold (49h) — deliberate but worth documenting |
| X2 | Info | Client LOW_POWER uses 24h outbound interval vs Server 49h stale threshold — leaves ~25h margin for 2 missed reports |
| X3 | Info | Shared patterns (safeSleep, time sync, I2C health check) are implemented by convention, not code-sharing — risk of drift if one is updated and others aren't |
