# Viewer Firmware Decision-Logic Extraction — Comprehensive Code Review

**File:** `TankAlarm-112025-Viewer-BluesOpta/TankAlarm-112025-Viewer-BluesOpta.ino`  
**Version:** 1.1.9  
**Date:** 2026-03-24  
**Reviewer scope:** ALL decision-making logic, control flow, timing, error handling, and recovery paths  
**Total lines:** 997

---

## Table of Contents

1. [Architecture Overview](#1-architecture-overview)
2. [Loop Timing & Blocking Analysis](#2-loop-timing--blocking-analysis)
3. [Watchdog Management](#3-watchdog-management)
4. [Notecard Polling & Note Processing](#4-notecard-polling--note-processing)
5. [I2C Bus Recovery Decisions](#5-i2c-bus-recovery-decisions)
6. [Data Freshness / Stale Data Handling](#6-data-freshness--stale-data-handling)
7. [Web Server Responses](#7-web-server-responses)
8. [Time Synchronization Decisions](#8-time-synchronization-decisions)
9. [DFU (Firmware Update) Decisions](#9-dfu-firmware-update-decisions)
10. [Memory Management Decisions](#10-memory-management-decisions)
11. [Schema Version Handling](#11-schema-version-handling)
12. [Error Handling & Recovery Paths](#12-error-handling--recovery-paths)
13. [Battery/Voltage Monitoring](#13-batteryvoltage-monitoring)
14. [Configuration & Initialization Decisions](#14-configuration--initialization-decisions)
15. [Previously Reported Issues — Status Check](#15-previously-reported-issues--status-check)
16. [Summary of Findings](#16-summary-of-findings)

---

## 1. Architecture Overview

The Viewer is a read-only kiosk device. Its sole purpose:
1. Receive **viewer_summary.qi** notes from the Server (routed through Notehub).
2. Parse the summary payload into an in-memory `gSensorRecords[]` array.
3. Serve a web dashboard and `/api/sensors` JSON endpoint over Ethernet.

It has **no sensors**, **no relays**, and **no outbound data notefiles** (aside from DFU and diagnostics). This simplicity means the decision surface is small compared to the Client/Server, but the timing and reliability constraints still matter for a remote unattended device.

**Global state footprint:**
- `gSensorRecords[MAX_SENSOR_RECORDS]` (64 entries × ~168 bytes each ≈ 10.7 KB static)
- `gConfig` (ViewerConfig struct, ~122 bytes)
- Various epoch/millis tracking globals (~80 bytes)
- `VIEWER_DASHBOARD_HTML[]` in PROGMEM (~4+ KB)

---

## 2. Loop Timing & Blocking Analysis

### `loop()` — Lines 282–345

The main loop is **non-blocking by design**. It performs:

```
1. Watchdog kick                           ~0 ms
2. Ethernet.maintain()                      ~0 ms (DHCP lease renewal)
3. handleWebRequests()                      0–10 sec (see below)
4. ensureTimeSync()                         0–~100 ms (I2C, only every 6h)
5. Notecard health check block              0–~100 ms (I2C, only when offline)
6. fetchViewerSummary() (conditional)       0–~2 sec (I2C, up to 20 notes)
7. checkForFirmwareUpdate() (conditional)   0–~100 ms (I2C, hourly)
```

**Assessment:** The loop structure is sound for the Viewer's low-frequency workload. There is no `delay()` in the main loop body itself. All periodic work is millis-gated or epoch-gated.

### `safeSleep()` — Lines 207–227

```cpp
static void safeSleep(unsigned long ms) {
  const unsigned long maxChunk = (WATCHDOG_TIMEOUT_SECONDS * 1000UL) / 2;  // 15 sec
  while (remaining > 0) {
    delay(chunk);
    mbedWatchdog.kick();
    remaining -= chunk;
  }
}
```

**Decision:** Breaks `delay()` into chunks ≤ half the watchdog timeout (15 seconds), kicking the watchdog between chunks.

**Assessment:** ✅ **Sound.** This is the correct pattern for delay-with-watchdog. Every call site in the Viewer passes small values (1–10 ms) except `initializeEthernet()` retries (up to 15 seconds). The 15-second chunk fits within a 30-second watchdog.

### `handleWebRequests()` — Lines 505–543

```cpp
static void handleWebRequests() {
  EthernetClient client = gWebServer.available();
  if (!client) return;
  // ...parse, respond, then:
  safeSleep(1);
  client.stop();
}
```

**Decision:** Non-blocking entry (returns immediately if no client). Processing a single request is bounded by `readHttpRequest()`.

### `readHttpRequest()` — Lines 545–623

```cpp
unsigned long start = millis();
while (client.connected() && millis() - start < 5000UL) {
  if (!client.available()) {
    safeSleep(1);
    continue;
  }
  // ... read character by character
}
```

**Key decisions:**
- **5-second timeout** on header reading
- **5-second timeout** on body reading (separate timer)
- **512-byte line length cap** (returns false → 400 Bad Request if exceeded)
- **1024-byte body cap** (`MAX_HTTP_BODY_BYTES`)

**Assessment:** ⚠️ **Mostly sound, with caveats:**

1. **Worst case blocking: ~10 seconds.** A slow-loris client can cause the loop to block for up to 10 seconds (5s headers + 5s body). With the 30-second watchdog and `safeSleep(1)` kicking it, the watchdog won't fire, but **the Notecard summary fetch schedule check is delayed.** For a 6-hour cadence device this is inconsequential.

2. **Character-by-character reading** with `String` concatenation (`line += c`) is O(n²) due to repeated reallocation. For the small payloads the Viewer handles (GET requests with no body), this is acceptable. The 512-byte line cap bounds it well.

3. **Single-client processing.** Only one client is served per `loop()` iteration. If a second client connects simultaneously, it waits. This is fine for a kiosk — concurrent requests are rare.

4. ⚠️ **The body reading loop has a subtle issue** (lines 610–622): It reads up to `contentLength` bytes, but `contentLength` was already capped to `MAX_HTTP_BODY_BYTES` if too large (line 594). However, `bodyTooLarge` is set inside the read loop if `readBytes >= MAX_HTTP_BODY_BYTES` (line 617), which is redundant — the cap was already applied to `contentLength`. Not a bug, just a belt-and-suspenders pattern.

### Previous `delay()` Issue — Status Check

**Previously reported issue:** "Viewer uses `delay()` instead of non-blocking timing."

**Current status:** ✅ **FIXED.** The main loop uses no bare `delay()`. All delays go through `safeSleep()` which kicks the watchdog. Periodic tasks use millis/epoch comparisons. The only raw `delay()` calls are inside `tankalarm_recoverI2CBus()` (microsecond-level, for I2C bit-banging) and inside `initializeEthernet()` retries (wrapped in `safeSleep` isn't used there — see finding below).

⚠️ **Finding:** `initializeEthernet()` lines 463–470 use `delay(retryDelay)` where `retryDelay` can be up to 15,000 ms. This is during `setup()` (before the watchdog is started), so it's safe. But if Ethernet init were ever moved to a post-watchdog recovery path, this would become a starvation risk.

---

## 3. Watchdog Management

### Watchdog Initialization — Lines 255–273

```cpp
// In setup(), AFTER all initialization:
uint32_t timeoutMs = WATCHDOG_TIMEOUT_SECONDS * 1000;  // 30 seconds
mbedWatchdog.start(timeoutMs);
```

**Decision:** Watchdog is started **last in setup()**, after Notecard init, time sync, Ethernet init, and initial summary fetch.

**Assessment:** ✅ **Sound.** Starting the watchdog early during long setup tasks has historically caused resets. The current placement is correct.

### Watchdog Kick Points

| Location | Context | Safe? |
|---|---|---|
| Top of `loop()` (L284–288) | Every iteration | ✅ |
| `safeSleep()` (L219–223) | Every chunk of delay | ✅ |
| `fetchViewerSummary()` inner loop (L753–757) | Between note.get calls | ✅ |
| I2C recovery callback lambda (L314–316) | Before recovery procedure | ✅ |

**Assessment:** ✅ **Comprehensive.** The four kick points cover all potentially long-running paths. The `fetchViewerSummary()` loop cap of 20 notes prevents unbounded iteration.

### Watchdog Starvation Scenarios

| Scenario | Max blocking time | Starves WDT? |
|---|---|---|
| Slow HTTP client | ~10 sec (2 × 5s timeouts) | No (safeSleep kicks) |
| 20 notes drained in fetchViewerSummary | ~2–4 sec (20 × I2C round-trips) | No (kicked per iteration) |
| I2C bus recovery | ~1–2 ms (16 SCL toggles) | No (pre-kicked) |
| Notecard health check | ~100 ms (single I2C round-trip) | No |
| DFU check | ~100 ms (single I2C round-trip) | No |
| Ethernet DHCP retry in setup | Up to 45 sec (3 × 15s) | N/A (watchdog not started yet) |

**Assessment:** ✅ **No watchdog starvation risks identified.**

---

## 4. Notecard Polling & Note Processing

### Summary Fetch Schedule — Lines 338–341

```cpp
if (gNextSummaryFetchEpoch > 0.0 && currentEpoch() >= gNextSummaryFetchEpoch) {
  fetchViewerSummary();
  scheduleNextSummaryFetch();
}
```

**Decision:** The Viewer fetches summaries at epoch-aligned intervals (default: every 6 hours starting at 6:00 AM). Uses `tankalarm_computeNextAlignedEpoch()` to align to the Server's known cadence.

**Assessment:** ✅ **Sound design.** Aligning to the Server's cadence means the Viewer checks shortly after the Server is expected to have published a new summary.

⚠️ **Edge case — time not yet synced:** If `currentEpoch()` returns 0.0 (never synced), the condition `currentEpoch() >= gNextSummaryFetchEpoch` is false (0.0 < positive epoch), so no fetch happens. `gNextSummaryFetchEpoch` is set in `setup()` via `scheduleNextSummaryFetch()`, which calls `currentEpoch()` internally. If time was never synced at setup time, `computeNextAlignedEpoch()` returns 0.0, making `gNextSummaryFetchEpoch = 0.0`, which makes the `> 0.0` guard fail. **Result: The Viewer will never fetch summaries until time syncs.** This is actually correct behavior — fetching without knowing the current time would lead to immediate fetch-every-loop behavior. Once `ensureTimeSync()` succeeds (called every loop), the next `scheduleNextSummaryFetch()` could fix it. But **`scheduleNextSummaryFetch()` is never re-called after time syncs** unless a fetch succeeds first.

⚠️ **GAP: If time is unavailable at boot and syncs later, `gNextSummaryFetchEpoch` stays 0.0 forever.** The Viewer would never fetch summaries until restarted with cellular time available. **Recommendation:** Re-schedule on first successful time sync, or add a fallback millis-based check.

### `fetchViewerSummary()` — Lines 733–824

```cpp
while (true) {
  // kick watchdog
  if (++notesProcessed > 20) break;   // Safety cap
  
  J *req = notecard.newRequest("note.get");
  JAddStringToObject(req, "file", VIEWER_SUMMARY_FILE);  // "viewer_summary.qi"
  JAddBoolToObject(req, "delete", true);                  // Consume note after reading
  J *rsp = notecard.requestAndResponse(req);
  
  // ... parse body, call handleViewerSummary(), loop for more notes
}
```

**Key decisions:**
1. **Drain loop:** Reads ALL queued notes (up to 20), not just the latest. Each note is deleted after reading (`"delete": true`).
2. **20-note safety cap:** Prevents runaway loop if notes accumulate.
3. **Last-write-wins:** Each note overwrites `gSensorRecords[]` entirely. If 3 summaries queued, only the last one persists.
4. **`note.get` without `"note"` key:** Gets the next available note in the file (FIFO).

**Assessment:** ✅ **Sound for the use case.** The drain-all pattern ensures the Viewer always displays the freshest data. The 20-note cap is generous — at a 6-hour cadence, even a week of queued summaries is only 28 notes.

⚠️ **Concern: The while(true) loop processes notes serially.** Each `note.get` is a blocking I2C round-trip (~50–100 ms). For 20 notes, that's 1–2 seconds. The watchdog kick per iteration prevents starvation. Acceptable.

⚠️ **Concern: `gSensorRecordCount` is set to 0 inside `handleViewerSummary()` (line 868).** If the loop processes note #1 (sets records), then note #2 (overwrites records), and note #2 has a parse error, `handleViewerSummary()` is never called for #2, so records from note #1 remain. This is correct — partial parse failures don't wipe valid data.

### Notecard Failure Tracking in `fetchViewerSummary()` — Lines 768–782

```cpp
if (!req) {
  gNotecardFailureCount++;
  if (gNotecardFailureCount >= NOTECARD_FAILURE_THRESHOLD && gNotecardAvailable) {
    gNotecardAvailable = false;
  }
  return;
}
// ... same pattern for !rsp
```

**Decision:** After `NOTECARD_FAILURE_THRESHOLD` (5) consecutive failures, marks Notecard as unavailable. This triggers the health check backoff system in the main loop.

**Assessment:** ✅ **Sound.** The failure counter is reset on any successful response (line 793–795). The threshold prevents premature offline declarations from transient I2C glitches.

---

## 5. I2C Bus Recovery Decisions

### Health Check with Exponential Backoff — Lines 293–334

```cpp
if (!gNotecardAvailable && (now - lastNcHealthCheck > ncHealthInterval)) {
  // Try card.version as a lightweight probe
  J *hcRsp = notecard.requestAndResponse(hcReq);
  if (hcRsp) {
    // RECOVERED: reset everything
    gNotecardAvailable = true;
    gNotecardFailureCount = 0;
    tankalarm_ensureNotecardBinding(notecard);
    ncHealthInterval = NOTECARD_HEALTH_CHECK_BASE_INTERVAL_MS;  // 5 min
  } else {
    gNotecardFailureCount++;
    if (gNotecardFailureCount >= I2C_NOTECARD_RECOVERY_THRESHOLD) {  // 10 failures
      tankalarm_recoverI2CBus(gDfuInProgress, [](){ mbedWatchdog.kick(); });
      tankalarm_ensureNotecardBinding(notecard);
      gNotecardFailureCount = 0;  // Reset counter after recovery attempt
    }
    // Exponential backoff: 5min → 10min → 20min → 40min → 80min (cap)
    ncHealthInterval *= 2;
    if (ncHealthInterval > NOTECARD_HEALTH_CHECK_MAX_INTERVAL_MS) {
      ncHealthInterval = NOTECARD_HEALTH_CHECK_MAX_INTERVAL_MS;  // 80 min
    }
  }
}
```

**Decision tree:**
1. Only runs when `gNotecardAvailable == false`
2. Probes with `card.version` at increasing intervals
3. After 10 consecutive probe failures → fires I2C bus recovery (SCL bit-bang)
4. After recovery, re-binds Notecard and resets failure counter
5. Backoff: 5 → 10 → 20 → 40 → 80 minutes (capped)

**Assessment:** ✅ **Well-designed.** The exponential backoff prevents hammering a dead bus. The 10-failure threshold before bus recovery means ~50 minutes minimum at base interval before the first recovery attempt.

⚠️ **Subtle interaction:** The failure counter `gNotecardFailureCount` is shared between `fetchViewerSummary()` and the health check block. If `fetchViewerSummary()` hits 5 failures and sets `gNotecardAvailable = false`, the health check inherits `gNotecardFailureCount = 5`. After 5 more health-check failures (total 10), bus recovery fires. This is correct behavior — the two paths feed the same escalation ladder.

### `tankalarm_recoverI2CBus()` — TankAlarm_I2C.h

```cpp
Wire.end();
// GPIO bit-bang: 16 SCL toggles + STOP condition
Wire.begin();
Wire.setTimeout(I2C_WIRE_TIMEOUT_MS);  // 25 ms
gI2cBusRecoveryCount++;
```

**Assessment:** ✅ **Standard I2C recovery procedure.** The DFU guard (`gDfuInProgress`) prevents recovery during firmware transfer. Watchdog is kicked via the lambda callback before the 160μs bit-bang sequence.

---

## 6. Data Freshness / Stale Data Handling

### Server-side (embedded in JSON payload)

The Viewer has **no autonomous staleness detection for sensor data.** It displays whatever the last summary contained. Staleness is purely a **client-side JavaScript concern:**

```javascript
// In VIEWER_DASHBOARD_HTML (line ~170):
const staleThresholdMs = 93600000;  // 26 hours in ms
const isStale = lastUpdate && ((now - (lastUpdate*1000)) > staleThresholdMs);
// If stale: shows ⚠️ icon, reduces opacity to 0.6
```

**Decision:** Sensor rows with `lastUpdateEpoch` older than 26 hours are visually flagged but still displayed.

**Assessment:** ✅ **Appropriate for a read-only mirror.** The Viewer cannot take corrective action — it can only display. The 26-hour threshold accommodates the default 6-hour cadence plus tolerance.

### Summary Freshness

The Viewer tracks:
- `gLastSummaryGeneratedEpoch` — when the Server generated the summary
- `gLastSummaryFetchEpoch` — when the Viewer last successfully consumed a note
- `gNextSummaryFetchEpoch` — when the next fetch is scheduled

All three are exposed in `/api/sensors` JSON for dashboard display.

⚠️ **GAP: No firmware-side staleness alarm.** If the Viewer hasn't received a summary in 24+ hours (e.g., Server down, Notehub route broken), there is no local indication beyond what the JS dashboard shows. The Viewer continues serving stale data indefinitely with no LED flash, no serial warning, and no diagnostic note. For a remote kiosk, this means operators must manually check the dashboard to notice data is stale.

**Recommendation:** Consider logging a serial warning and/or toggling an LED if no summary has been received within `2 × gSourceRefreshSeconds`.

---

## 7. Web Server Responses

### Route Table — `handleWebRequests()` lines 533–539

| Method | Path | Handler | Response |
|---|---|---|---|
| GET | `/` | `sendDashboard()` | Full HTML page from PROGMEM |
| GET | `/api/sensors` | `sendSensorJson()` | JSON with all metadata + sensor array |
| * | * | `respondStatus(404)` | "Not Found" |

**Non-GET methods** receive 404 (not 405 Method Not Allowed). Acceptable for a kiosk.

### Authentication

**There is no authentication.** Anyone on the local network can access the dashboard and JSON API.

**Assessment:** ⚠️ **Known design choice for a read-only kiosk.** The Viewer has no control paths (no POST/PUT/DELETE handlers, no relay control). The exposed data is sensor levels, alarm states, and device UIDs. For a private industrial LAN this is acceptable. For any internet-facing deployment, this would be a concern.

### Caching

```cpp
// Lines 641, 663:
client.println(F("Cache-Control: no-cache, no-store, must-revalidate"));
```

**Decision:** All responses are marked no-cache. This ensures browsers always fetch fresh data.

**Assessment:** ✅ **Correct for a dashboard.** The dashboard JS also auto-refreshes on the `WEB_REFRESH_SECONDS` interval (6 hours by default), but a manual browser refresh will always get current data.

### `sendDashboard()` — Lines 656–672

Sends the PROGMEM HTML page in 128-byte chunks via `pgm_read_byte_near()`.

**Assessment:** ✅ **Memory-efficient.** The 128-byte stack buffer avoids heap allocation.

### `sendSensorJson()` — Lines 674–730

```cpp
std::unique_ptr<JsonDocument> docPtr(new (std::nothrow) JsonDocument());
if (!docPtr) {
  respondStatus(client, 500, "Out of Memory");
  return;
}
// ... build JSON, serialize to String, send
```

**Decision:** Allocates `JsonDocument` on the heap via `nothrow new`, with OOM fallback to 500 error.

**Assessment:** ⚠️ **Mixed.** The OOM check on `JsonDocument` allocation is good. However:

1. `serializeJson(doc, body)` (line 728) serializes the entire JSON into an Arduino `String`. For 64 sensors with all fields, this could be 15–20 KB. The `String` grows via realloc, which can fragment the heap.

2. `respondJson()` (lines 633–650) sends the `String` in 512-byte chunks but still holds the **entire serialized string in memory** while sending. Peak memory = `JsonDocument` + serialized `String`.

3. **No streaming serialization.** ArduinoJson supports streaming directly to the `EthernetClient`, which would avoid the intermediate `String` entirely. However, `respondJson()` needs `Content-Length` first, which requires knowing the serialized size before sending. ArduinoJson's `measureJson()` could compute the length without serializing.

**Recommendation for a future optimization:** Use `measureJson(doc)` to get length, send headers, then `serializeJson(doc, client)` directly. This would halve the peak memory.

### `respondJson()` Chunked Sending — Lines 633–650

```cpp
const size_t chunkSize = 512;
while (remaining > 0) {
  size_t toSend = (remaining < chunkSize) ? remaining : chunkSize;
  client.write((const uint8_t*)body.c_str() + offset, toSend);
  offset += toSend;
  remaining -= toSend;
}
```

**Assessment:** ⚠️ **No watchdog kick in the send loop.** For a 20 KB payload at 512 bytes/chunk, that's ~40 iterations. Each `client.write()` is typically fast (µs–ms level), so total time is well under a second. **Not a starvation risk**, but inconsistent with the caution shown elsewhere.

### HTTP Request Body Limit

```cpp
#define MAX_HTTP_BODY_BYTES 1024
```

**Assessment:** ✅ **Correct.** The Viewer has no POST handlers, so any body is irrelevant. The cap prevents memory exhaustion from malformed requests.

### HTTP Line Length Cap

```cpp
if (line.length() > 512) {
  return false;  // → 400 Bad Request
}
```

**Assessment:** ✅ **Good defense against overly long header lines.**

---

## 8. Time Synchronization Decisions

### `ensureTimeSync()` — Line 397, delegates to `tankalarm_ensureTimeSync()` (TankAlarm_Notecard.h)

```cpp
const unsigned long SYNC_INTERVAL_MS = 6UL * 60UL * 60UL * 1000UL;  // 6 hours

if (!forceSync && lastSyncedEpoch > 0.0 && (millis() - lastSyncMillis) < SYNC_INTERVAL_MS) {
  return;  // No sync needed
}

J *req = notecard.newRequest("card.time");
```

**Decision:** Syncs time from the Notecard every 6 hours, or immediately on first call (never-synced state).

**Assessment:** ✅ **Sound.** The `card.time` call is lightweight. 6-hour re-sync prevents drift from the millis()-based interpolation.

### `currentEpoch()` — Line 401, delegates to `tankalarm_currentEpoch()`

```cpp
if (lastSyncedEpoch <= 0.0) return 0.0;
return lastSyncedEpoch + (double)(millis() - lastSyncMillis) / 1000.0;
```

**Assessment:** ✅ **Sound.** Uses millis() delta from last sync point. millis() wraps at ~49.7 days; the subtraction `millis() - lastSyncMillis` handles wrap correctly on unsigned arithmetic. The 6-hour re-sync means at most ~6 hours of millis-based drift (~0.5 seconds at worst for a crystal oscillator).

### `computeNextAlignedEpoch()` — TankAlarm_Utils.h lines 121–131

```cpp
double aligned = floor(epoch / 86400.0) * 86400.0 + (double)baseHour * 3600.0;
while (aligned <= epoch) {
  aligned += (double)intervalSeconds;
}
return aligned;
```

**Assessment:** ⚠️ **Assumes UTC.** `floor(epoch / 86400.0) * 86400.0` gives the start of the UTC day. The `baseHour` is applied as UTC hours, not local time. If the Server publishes summaries at "6 AM local" but the Viewer computes "6 AM UTC," there's a timezone offset. Whether this is a bug depends on whether the Server also computes in UTC (likely, since Notecard epochs are UTC).

⚠️ **The while loop** can iterate many times if `epoch` is far ahead of the aligned base. For a 6-hour interval starting at hour 6, there are at most 4 iterations per day. For a 1-hour interval, up to 24. Not a performance concern.

---

## 9. DFU (Firmware Update) Decisions

### DFU Check — Lines 343–348

```cpp
if (!gDfuInProgress && (currentMillis - gLastDfuCheckMillis >= DFU_CHECK_INTERVAL_MS)) {
  gLastDfuCheckMillis = currentMillis;
  checkForFirmwareUpdate();
}
```

**Decision:** Checks every `DFU_CHECK_INTERVAL_MS` (1 hour). Skipped if DFU is already in progress.

### `checkForFirmwareUpdate()` — Lines 899–953

**Decision tree:**
1. Query `dfu.status` from Notecard
2. If mode is `"downloading"` or `"download-pending"` → set `gDfuInProgress = true`, return
3. If mode is `"ready"` and `body` has content → firmware available
4. If `DFU_AUTO_ENABLE` is defined → call `enableDfuMode()`
5. Otherwise → log and wait

**Assessment:** ⚠️ **`DFU_AUTO_ENABLE` is never defined in the codebase.** I searched for it:

```
#ifdef DFU_AUTO_ENABLE
```

This means auto-DFU is always disabled for the Viewer. The Viewer detects available updates but never applies them automatically. Updates must be triggered externally (e.g., via Notehub console).

**This is a conscious safety choice** — auto-DFU on an unattended kiosk could brick it if the update is bad. However, it also means the Viewer will **never self-update** unless someone explicitly enables it.

### `enableDfuMode()` — Lines 960–988

```cpp
J *req = notecard.newRequest("dfu.status");
JAddBoolToObject(req, "on", true);
```

**Assessment:** ⚠️ **Uses `dfu.status` with `on: true`, not `dfu.set`.** The Blues Notecard API for initiating DFU is `dfu.status` with `on: true` in older firmware, but newer firmware prefers `card.dfu`. This may need updating depending on Notecard firmware version. Not a correctness bug — the code checks for error responses.

### DFU Guard in I2C Recovery

```cpp
tankalarm_recoverI2CBus(gDfuInProgress, ...);
// Inside recoverI2CBus():
if (dfuInProgress) {
  Serial.println(F("I2C recovery skipped - DFU in progress"));
  return;
}
```

**Assessment:** ✅ **Correct.** Prevents I2C bus reset during firmware transfer, which would corrupt the DFU stream.

---

## 10. Memory Management Decisions

### Static Allocation

| Allocation | Size | Location |
|---|---|---|
| `gSensorRecords[64]` | ~10.7 KB | Global static |
| `gConfig` (ViewerConfig) | ~122 B | Global static |
| `VIEWER_DASHBOARD_HTML` | ~4+ KB | PROGMEM |
| String globals (UIDs, names) | ~200 B | Global static |

### Heap Allocation

| Allocation | Context | Guard |
|---|---|---|
| `JsonDocument` in `sendSensorJson()` | Per HTTP request | `nothrow new` + null check |
| `JsonDocument` in `fetchViewerSummary()` | Per note processed | `nothrow new` + null check |
| `String body` in `sendSensorJson()` | Per HTTP request | Implicit (no guard) |
| `JConvertToJSONString()` in `fetchViewerSummary()` | Per note | `NoteFree()` called |

**Assessment:** ✅ **Generally good.** All `JsonDocument` allocations use `nothrow new` with null checks. The Notecard library's `JConvertToJSONString` / `NoteFree` pattern is correctly followed.

⚠️ **The `String body` in `sendSensorJson()` line 728** grows via Arduino `String` realloc without an explicit size check. For 64 sensors, the JSON could be ~15–20 KB. On the STM32H747XI with ~512 KB SRAM, this is fine, but heap fragmentation over weeks of operation could eventually cause allocation failures. The OOM fallback for `JsonDocument` would catch catastrophic failure, but a `String` realloc failure silently truncates.

### `freeRam()` — Line 234

Delegates to `tankalarm_freeRam()` which uses Mbed's `mbed_stats_heap_get()`. Called in `setup()` via `tankalarm_printHeapStats()` for diagnostic output.

**Assessment:** ✅ **Good for diagnostics.** Not called periodically in the loop, which is fine — the Viewer's allocation pattern is simple enough.

---

## 11. Schema Version Handling

### NOTEFILE_SCHEMA_VERSION

Defined in `TankAlarm_Common.h` as `1`.

**Assessment:** ⚠️ **The Viewer never checks the schema version of incoming summaries.** `handleViewerSummary()` (lines 826–893) does not inspect any `_sv` or `schemaVersion` field. If the Server starts sending schema version 2 with different field names, the Viewer would silently mis-parse data.

The Viewer does handle **dual-name fields** (e.g., `"sn"` or `"serverName"`, `"rs"` or `"refreshSeconds"`), which provides forward/backward compatibility for field renaming. But structural changes (e.g., nested sensor arrays, new required fields) would be silently ignored.

**Recommendation:** Add a schema version check in `handleViewerSummary()`:
```cpp
int sv = doc["_sv"] | 0;
if (sv > NOTEFILE_SCHEMA_VERSION) {
  Serial.println(F("WARNING: Summary schema version newer than firmware supports"));
}
```

---

## 12. Error Handling & Recovery Paths

### Notecard Error Escalation Ladder

```
Normal operation
  ↓ (failure)
gNotecardFailureCount++ (per request)
  ↓ (≥ NOTECARD_FAILURE_THRESHOLD = 5)
gNotecardAvailable = false
  ↓ (health check probes at increasing intervals)
  ↓ (probe failure × 10)
tankalarm_recoverI2CBus()
  ↓ (success → reset to normal)
  ↓ (failure → exponential backoff continues, max 80 min)
```

**Assessment:** ✅ **Well-structured escalation.** The ladder has clear thresholds and doesn't spiral.

### Error Recovery Paths Summary

| Error | Detection | Recovery | Permanent failure mode? |
|---|---|---|---|
| Notecard I2C failure | Request returns null | Health check + bus recovery | Backoff caps at 80 min; never gives up |
| Ethernet failure at boot | `Ethernet.begin() == 0` | 3 retries with increasing delay | Continues without Ethernet (dashboard offline) |
| HTTP parse error | `readHttpRequest()` returns false | 400 Bad Request, client.stop() | No — per-request |
| Body too large | Content-Length > 1024 | 413 Payload Too Large | No — per-request |
| JSON parse failure | `deserializeJson()` error | Log error, skip note, continue loop | No — per-note |
| OOM on JsonDocument | `new (nothrow)` returns nullptr | Log error, skip / return 500 | No — transient |
| DFU status error | `notecard.responseError()` | Log error, return | Checks again in 1 hour |
| Time sync failure | `card.time` returns error | Return, try again next loop | Blocks summary scheduling until time available |
| hub.set failure | Error string in response | Log warning, continue | Viewer may not receive routed notes |

### `hub.set` Failure — Lines 359–373

```cpp
const char *hubErr = JGetString(hubRsp, "err");
if (hubErr && hubErr[0] != '\0') {
  Serial.print(F("WARNING: hub.set failed: "));
  Serial.println(hubErr);
}
```

**Assessment:** ⚠️ **hub.set failure is logged but not retried.** If the ProductUID is wrong or the Notecard can't reach Notehub, the Viewer will never receive routed notes. This is a setup/configuration error, not a runtime transient, so the log-and-continue approach is reasonable. However, there's no indication on the web dashboard that the Notecard configuration failed.

---

## 13. Battery/Voltage Monitoring

The Viewer **does not perform any battery/voltage monitoring of its own hardware.** It is assumed to be mains-powered (Arduino Opta).

However, it **receives and displays** `vinVoltage` (VIN voltage) per sensor record from the Server's summary:

```cpp
// Line 880:
rec.vinVoltage = item["v"].as<float>();

// Line 710 (sendSensorJson):
if (gSensorRecords[i].vinVoltage > 0.0f) {
  obj["v"] = gSensorRecords[i].vinVoltage;
}
```

**Assessment:** ✅ **Correct pass-through.** The Viewer mirrors what the Server sends.

⚠️ **Note:** The dashboard HTML does not display `vinVoltage` anywhere. The data is in the JSON API for programmatic consumers but not rendered in the table. The table columns are: Site, Tank, Level, 24hr Change, Updated. No voltage column exists.

---

## 14. Configuration & Initialization Decisions

### Product UID Configuration — Lines 48–51

```cpp
#ifndef DEFAULT_VIEWER_PRODUCT_UID
#define DEFAULT_VIEWER_PRODUCT_UID ""
#endif
```

**Decision:** Empty by default. Must be set via `ViewerConfig.h` or a config file.

**Assessment:** ⚠️ **If left empty, `hub.set` is called with an empty product UID.** The Notecard will likely return an error, but the Viewer continues anyway. The warning is logged but easy to miss on a deployed device. Consider adding a more prominent boot-time error message (e.g., blinking an LED pattern).

### MAC Address Derivation — Lines 420–442

```cpp
// DJB2 hash of UID string
uint32_t hash = 5381;
for (const char *p = gViewerUid; *p; p++) {
  hash = ((hash << 5) + hash) + (uint8_t)*p;
}
gConfig.macAddress[0] = 0x02;  // Locally administered
gConfig.macAddress[1] = 0x00;
gConfig.macAddress[2..5] = hash bytes;
```

**Decision:** Derives a deterministic MAC from the Notecard device UID. Using the "locally administered" bit (0x02) is correct per IEEE 802.3.

**Assessment:** ✅ **Sound.** Deterministic MAC prevents conflicts on a LAN with multiple Viewers. DJB2 has adequate distribution for this use case.

⚠️ **Edge case:** If `hub.get` fails and `gViewerUid` is empty, `deriveMacFromUid()` returns early, keeping the compile-time default MAC. If two Viewers share the same firmware build and both fail UID retrieval, they'd have the same MAC. In practice, unlikely.

### Ethernet Initialization — Lines 444–502

**Decision tree:**
1. If `gConfig.useStaticIp` → use static IP
2. Else → use DHCP
3. If first attempt fails → retry up to 3 times with 5s/10s/15s delays
4. If all retries fail → log error, continue (dashboard will be inaccessible)

**Assessment:** ✅ **Reasonable.** The increasing delay gives the DHCP server time to respond. The raw `delay()` calls are safe here because the watchdog is not yet started.

⚠️ **After Ethernet failure, the Viewer still enters the main loop.** It will continue attempting Notecard operations (which don't need Ethernet) and maintaining time sync. But `gWebServer.begin()` is called regardless of Ethernet status (line 252). Serving on a dead Ethernet is harmless — `gWebServer.available()` returns no client.

---

## 15. Previously Reported Issues — Status Check

### Issue: "delay() instead of non-blocking timing"
**Status:** ✅ **FIXED.** Main loop is fully non-blocking. All delays wrapped in `safeSleep()`.

### Issue: "Potential watchdog starvation during summary processing"  
**Status:** ✅ **FIXED.** Watchdog kick added per-iteration in `fetchViewerSummary()` loop with a 20-note cap.

### Issue: "No I2C recovery mechanism"  
**Status:** ✅ **IMPLEMENTED.** Full exponential-backoff health check with bus recovery via `tankalarm_recoverI2CBus()`.

---

## 16. Summary of Findings

### Critical Issues
None.

### Important Issues (should address)

| # | Issue | Location | Impact |
|---|---|---|---|
| **I-1** | If time is unavailable at boot and syncs later, `gNextSummaryFetchEpoch` remains 0.0 — summaries are **never fetched** until reboot | `setup()` L246, `loop()` L338 | Viewer displays no data until restarted with cellular |
| **I-2** | No schema version check on incoming summaries — silent mis-parse on schema change | `handleViewerSummary()` L826 | Data integrity risk on firmware version mismatch |

### Minor Issues / Improvements

| # | Issue | Location | Impact |
|---|---|---|---|
| **M-1** | `vinVoltage` data available in API but not rendered on dashboard | HTML template ~L170; `sendSensorJson()` L710 | Operators can't see Client power status without API tool |
| **M-2** | No firmware-side staleness warning — stale data served silently | Entire Viewer | Operators at remote kiosk unaware Server/route is down |
| **M-3** | `sendSensorJson()` serializes to `String` then sends; could use `measureJson()` + streaming to halve peak memory | `sendSensorJson()` L728 | Memory fragmentation over weeks of operation |
| **M-4** | `respondJson()` send loop has no watchdog kick (safe but inconsistent) | `respondJson()` L643–650 | Not a real risk at 512-byte chunks |
| **M-5** | `DFU_AUTO_ENABLE` never defined — Viewer never self-updates | `checkForFirmwareUpdate()` L944 | Manual intervention required for updates |
| **M-6** | Empty `DEFAULT_VIEWER_PRODUCT_UID` passes empty string to `hub.set` — no prominent boot error | L48, `initializeNotecard()` L356 | Misconfigured Viewer hard to troubleshoot in field |

### Design Strengths

1. **Clean separation of concerns.** The Viewer does exactly one thing: receive and display. No control paths, no outbound data.
2. **Robust I2C recovery path.** Exponential backoff with bus recovery is the same battle-tested pattern as the Server/Client.
3. **PROGMEM HTML serving.** Keeps the large HTML template out of SRAM.
4. **OOM-safe JSON handling.** `nothrow new` on all heap-allocated `JsonDocument` objects.
5. **Watchdog discipline.** Every potentially-long path kicks the watchdog. The `safeSleep()` pattern is well-applied.
6. **Drain-all note pattern.** Ensures the Viewer always displays the freshest summary, not a stale queued one.
7. **Deterministic MAC derivation.** Prevents LAN conflicts without requiring per-device configuration.
8. **Non-blocking main loop.** No blocking calls in the steady state path.

---

*End of review.*
