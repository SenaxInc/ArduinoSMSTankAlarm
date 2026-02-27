# Watchdog Starvation Audit — 2026-02-26

## Executive Summary

The watchdog is configured at 30 seconds and kicked **once at the top of `loop()`** in both Client and Server. Any single path from the `loop()` kick back to the next iteration's kick that takes >30s will trigger a hardware reset.

Key finding: **Both codebases are well-defended in many areas** (FTP backup/restore has per-file watchdog kicks, `safeSleep()` kicks in chunks, `processNotefile` has a 10-note cap), but **several paths accumulate multiple sequential `requestAndResponse` calls without intermediate kicks**, creating theoretical starvation windows.

---

## Timing Assumptions

| Operation | Typical Time | Worst Case (modem busy / I2C contention) |
|---|---|---|
| `requestAndResponse` (single) | 200–500ms | 5–10s (modem sync in progress) |
| `sendRequest` (fire-and-forget) | 50–200ms | 2–5s |
| FTP file transfer (Ethernet) | 200ms–2s | 10s (large file, slow server) |
| `delay(2)` × 8 (analog sampling) | 16ms | 16ms |

---

## CLIENT: Risky Functions (sorted by risk)

### 🔴 CRITICAL RISK

#### 1. `sampleTanks()` → multi-tank alarm storm path
- **Line:** [L3896](TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino#L3896)
- **Called from:** `loop()` at L1540
- **Notecard calls:** Per tank: `sendTelemetry()` → `publishNote()` (1× `requestAndResponse`) and `sendAlarm()` → `publishNote()` (1× `requestAndResponse`). With 8 tanks, **up to 16 `requestAndResponse` calls**.
- **Watchdog kicks inside:** `publishNote()` kicks once before its `requestAndResponse` at L5769, but **not between tanks**.
- **Loop:** Iterates up to `MAX_TANKS` (8) times.
- **Worst case:** 8 tanks × (telemetry + alarm) × 5–10s each = **40–80s** ← exceeds 30s
- **Sub-call chain:** `sampleTanks` → `evaluateAlarms` → `sendAlarm` → `publishNote` → `requestAndResponse`. On success, `publishNote` also calls `flushBufferedNotes()` which adds more calls (see #2).
- **Mitigation:** Each `publishNote` kicks before its own `requestAndResponse`, so the window between kicks is ~1 R&R per tank. The real danger is if multiple tanks trigger alarms AND telemetry in the same cycle AND the Notecard is slow.

#### 2. `flushBufferedNotes()` — unbounded loop
- **Line:** [L5832](TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino#L5832)
- **Called from:** `publishNote()` on success (L5789), and `checkNotecardHealth()` on recovery (L2870)
- **Notecard calls:** 1× `sendRequest` per buffered line. Buffer can hold up to `NOTE_BUFFER_MAX_BYTES` (16384 bytes). At ~200 bytes/note, that's ~80 notes.
- **Watchdog kicks inside:** ❌ **NONE**
- **Loop:** `while (fgets(...))` — reads every line in the buffer file. **No cap on iterations.**
- **Worst case:** After extended offline period, 80 buffered notes × 2–5s each = **160–400s** ← **CRITICAL**
- **This is the highest-risk function in the entire Client.**

#### 3. `sendDailyReport()` — multi-part with alarm check
- **Line:** [L5543](TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino#L5543)
- **Called from:** `loop()` at L1711
- **Notecard calls:** 
  - `readNotecardVinVoltage()` → 1× `requestAndResponse` (L6777)
  - Per part: `publishNote()` → 1× `requestAndResponse` (L5776)
  - I2C error alarm: `publishNote()` → 1× `requestAndResponse`
  - Each successful `publishNote` also calls `flushBufferedNotes()` (see #2)
  - **Total: 3+ `requestAndResponse` without counting flush**
- **Watchdog kicks inside:** Only inside `publishNote` before its R&R.
- **Loop:** `while (tankCursor < eligibleCount)` — up to 8 iterations with multi-part splitting.
- **Worst case:** VIN read + 3 parts × 5–10s + flush = **20–40s** ← borderline

### 🟡 MODERATE RISK

#### 4. `pollForLocationRequests()` — 3 sequential Notecard calls
- **Line:** [L6706](TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino#L6706)
- **Called from:** `loop()` at L1582
- **Notecard calls:**
  - `note.get` → 1× `requestAndResponse` (L6716)
  - `fetchNotecardLocation()` → 1× `requestAndResponse` (L6867)
  - `note.add` → 1× `sendRequest` (L6754)
- **Watchdog kicks inside:** ❌ **NONE**
- **Total: 2 `requestAndResponse` + 1 `sendRequest` = ~3 blocking calls**
- **Worst case:** 3 × 5–10s = **15–30s** ← borderline

#### 5. `pollForSerialRequests()` — unbounded `while(true)` loop
- **Line:** [L6571](TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino#L6571)
- **Called from:** `loop()` at L1577
- **Notecard calls:** Per iteration: 1× `requestAndResponse` (L6582). Inside the loop body: `sendSerialAck()` × 2 (2× `sendRequest`) + `sendSerialLogs()` (1× `sendRequest`).
- **Watchdog kicks inside:** ❌ **NONE**
- **Loop:** `while(true)` — breaks when no more notes, but if many serial requests are queued, it processes all of them.
- **Worst case:** Multiple serial requests × (R&R + 3 sendRequests) × 5s = **20–60s**

#### 6. `pollForConfigUpdates()` — recovery chain hidden call
- **Line:** [L3050](TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino#L3050)
- **Called from:** `loop()` at L1567
- **Notecard calls:**
  - `checkNotecardHealth()` (fallthrough) → 1× `requestAndResponse` → on success `flushBufferedNotes()` (see #2)
  - `note.get` → 1× `requestAndResponse` (L3070)
  - `sendConfigAck()` → 1× `sendRequest` (L3045)
- **Watchdog kicks inside:** ❌ **NONE** (beyond what `publishNote` provides)
- **Worst case:** If notecard was down and recovers, triggers `checkNotecardHealth` → `flushBufferedNotes` → **same CRITICAL risk as #2**

#### 7. `pollForRelayCommands()` — recovery chain
- **Line:** [L6199](TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino#L6199)
- **Called from:** `loop()` at L1572
- **Notecard calls:** 1× `requestAndResponse` (L6218), plus possible `checkNotecardHealth()` fallthrough on failure.
- **Watchdog kicks inside:** ❌ **NONE**
- **Worst case:** Single path: 10–15s. With recovery chain: same as #6.

#### 8. `checkNotecardHealth()` — recovery cascade
- **Line:** [L2811](TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino#L2811)
- **Called from:** `loop()` at L1414
- **Notecard calls:** 
  - `card.wireless` → 1× `requestAndResponse` (L2829)
  - On recovery: `tankalarm_ensureNotecardBinding()` + `flushBufferedNotes()` (see #2)
  - I2C recovery path: `recoverI2CBus()` + `tankalarm_ensureNotecardBinding()`
- **Watchdog kicks inside:** ❌ **NONE**
- **Worst case:** R&R + full flush = **same CRITICAL risk as #2**

### 🟢 LOW RISK

#### 9. `checkForFirmwareUpdate()` — single R&R
- **Line:** [L2927](TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino#L2927)
- **Notecard calls:** 1× `requestAndResponse` (L2934)
- **Watchdog kicks inside:** None needed — single call.
- **Worst case:** 5–10s ← safe

#### 10. `pollBatteryVoltage()` — single R&R
- **Line:** [L4709](TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino#L4709)
- **Notecard calls:** 1× `requestAndResponse` (L4719)
- **Worst case:** 5–10s ← safe

#### 11. `sendHealthTelemetry()` — single publishNote
- **Line:** [L6966](TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino#L6966)
- **Notecard calls:** 1× via `publishNote` → `requestAndResponse`
- **Worst case:** 5–10s + possible flush ← moderate if flush is large

#### 12. `configureNotecardHubMode()` — fire-and-forget
- **Line:** [L2724](TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino#L2724)
- **Notecard calls:** 3× `sendRequest` (fire-and-forget, no R&R)
- **Worst case:** 3 × 2–5s = 6–15s ← safe

### ✅ Protected

#### `safeSleep()` — properly chunked
- **Line:** [L6932](TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino#L6932)
- Sleeps in 15s (half watchdog timeout) chunks, kicking between each. ✅ Safe.

#### `publishNote()` — kicks before R&R
- **Line:** [L5769](TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino#L5769)
- Has explicit `mbedWatchdog.kick()` before `requestAndResponse`. ✅ But then calls `flushBufferedNotes` on success with no kick.

---

## SERVER: Risky Functions (sorted by risk)

### 🔴 CRITICAL RISK

#### 1. `pollNotecard()` → `processNotefile()` × 9 notefiles
- **Line:** [L7263](TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino#L7263)
- **Called from:** `loop()` at L2518
- **Notecard calls:** `processNotefile` is called for **9 different notefiles** sequentially. Each invocation loops up to `MAX_NOTES_PER_FILE_PER_POLL` (10) times, each iteration doing 1× `requestAndResponse` (L7290). That's potentially **90 `requestAndResponse` calls**.
- **Watchdog kicks inside:** `processNotefile` calls `safeSleep(1)` after each note (L7339), which kicks the watchdog via chunk logic. ✅ **Each note triggers a kick.**
- **Handler callbacks:** `handleAlarm` → `sendSmsAlert()` → 1× `requestAndResponse` (L8151). So alarm processing adds **1 extra R&R per alarm note**.
- **Worst case:** 9 files × 10 notes × (R&R + handler R&R) × 5s = enormous, BUT `safeSleep(1)` kicks after each note.
- **Actual risk:** The `safeSleep(1)` mitigates this well. **The danger is inside handler callbacks** — `handleAlarm` can call `sendSmsAlert` (R&R) without an intermediate kick. Window: 2× R&R = 10–20s between last `safeSleep` kick and next.
- **Risk level reduced to MODERATE** thanks to per-note kick.

#### 2. `sendConfigViaNotecard()` includes `purgePendingConfigNotes()`
- **Line:** [L7138](TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino#L7138)
- **Called from:** `dispatchPendingConfigs()` (loop L2526) and web API handlers
- **Notecard calls in `purgePendingConfigNotes()` (L7058):**
  - `note.changes` → 1× `requestAndResponse` (L7068)
  - Up to 20× `note.delete` → 20× `requestAndResponse` (L7112)
  - Then in `sendConfigViaNotecard()`: 1× `requestAndResponse` (L7181)
  - **Total: up to 22 `requestAndResponse` calls**
- **Watchdog kicks inside:** ❌ **NONE**
- **Worst case:** 22 × 5–10s = **110–220s** ← **CRITICAL**

#### 3. `dispatchPendingConfigs()` — loops over all clients
- **Line:** [L7247](TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino#L7247)
- **Called from:** `loop()` at L2526 (every 60 minutes)
- **Notecard calls:** Per pending client: `sendConfigViaNotecard()` (see #2 above) = up to 22 R&R. With multiple pending clients, this multiplies.
- **Watchdog kicks inside:** ❌ **NONE**
- **Loop:** Iterates over all `gClientConfigCount` entries (max ~8–16).
- **Worst case:** 8 clients × 22 R&R × 5s = **880s** ← **EXTREME**

#### 4. `checkStaleClients()` — loops with SMS per stale client
- **Line:** [L8814](TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino#L8814)
- **Called from:** `loop()` at L2641 (every hour)
- **Notecard calls:** Per stale client: `sendSmsAlert()` → 1× `requestAndResponse` (L8151)
- **Watchdog kicks inside:** ❌ **NONE**
- **Loop:** Up to `gClientMetadataCount` entries.
- **Worst case:** If 10+ clients go stale simultaneously: 10 × R&R × 5–10s = **50–100s**

### 🟡 MODERATE RISK

#### 5. `sendDailyEmail()` — single R&R but large payload
- **Line:** [L8170](TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino#L8170)
- **Called from:** `loop()` at L2558
- **Notecard calls:** 1× `requestAndResponse` (L8267)
- **Watchdog kicks inside:** ❌ **NONE**
- **Worst case:** 5–10s ← safe individually, but stacks with adjacent calls

#### 6. `publishViewerSummary()` — single sendRequest
- **Line:** [L8286](TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino#L8286)
- **Called from:** `loop()` at L2563
- **Notecard calls:** 1× `sendRequest` (L8358)
- **Worst case:** 2–5s ← safe

#### 7. `handleWebRequests()` → API handlers with Notecard calls
- **Line:** [L5627](TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino#L5627)
- **Called from:** `loop()` at L2470
- **Notecard calls:** Web API handlers like `handleNotecardStatusGet` (1× R&R at L11570), config dispatch (see #2), and FTP operations.
- **Worst case:** If user triggers config "retry all" from web UI → same risk as #3.

#### 8. History maintenance block in `loop()`
- **Line:** [L2574](TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino#L2574) (hourly block)
- **Called from:** inline in `loop()`
- **Operations:** `pruneHotTierIfNeeded()`, `rollupDailySummaries()`, `pruneDailySummaryFiles()`, `saveHotTierSnapshot()`, `archiveMonthToFtp()`. The FTP archive can trigger a full FTP session (connect, transfer, disconnect).
- **Watchdog kicks inside:** FTP operations kick internally. The preceding LittleFS operations are fast.
- **Worst case:** Most of the risk is in `archiveMonthToFtp` which connects via FTP. The FTP functions kick watchdog.

### 🟢 LOW RISK

#### 9. `checkForFirmwareUpdate()` — single R&R
- **Line:** [L5317](TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino#L5317)
- **Notecard calls:** 1× `requestAndResponse` (L5324)
- **Worst case:** 5–10s ← safe

#### 10. `checkServerVoltage()` — single R&R
- **Line:** [L5278](TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino#L5278)
- **Notecard calls:** 1× `requestAndResponse` (L5282)
- **Worst case:** 5–10s ← safe

#### 11. `ensureTimeSync()` — single R&R  
- **Line:** [L5241](TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino#L5241)
- **Notecard calls:** 1× `requestAndResponse` (L5252)
- **Worst case:** 5–10s ← safe

### ✅ Protected

#### `performFtpBackupDetailed()` / `performFtpRestoreDetailed()`
- **Lines:** [L3938](TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino#L3938), [L4039](TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino#L4039)
- Kicks watchdog at start, between each file, and at end. ✅ Well-protected.

#### `safeSleep(ms)` — server version
- **Line:** [L2285](TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino#L2285)
- Uses `delay()` in chunks of half the watchdog timeout, kicking between. ✅ Safe.

#### `processNotefile()` — per-note sleep
- **Line:** [L7278](TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino#L7278)
- Calls `safeSleep(1)` after each note processed. ✅ Safe (but handlers can still block).

#### Notecard health check in Server `loop()` — single R&R
- **Line:** [L2480](TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino#L2480)
- Single `requestAndResponse` with I2C recovery. ✅ Safe.

---

## Cumulative loop() Path Analysis

### Client: Worst-case single loop() iteration

Even though individual functions are time-gated, **within a single loop() iteration, multiple gated functions fire if their timers align**. The worst case is boot/recovery:

| Step | Function | Max R&R calls | Kick? |
|---|---|---|---|
| 1 | `mbedWatchdog.kick()` | 0 | ✅ |
| 2 | `checkNotecardHealth()` | 1 + flush (80) | ❌ in flush |
| 3 | `sampleTanks()` | 16 | Partial |
| 4 | `pollForConfigUpdates()` | 1 + ack | ❌ |
| 5 | `pollForRelayCommands()` | 1 | ❌ |
| 6 | `pollForSerialRequests()` | N × 4 | ❌ |
| 7 | `pollForLocationRequests()` | 3 | ❌ |
| 8 | `checkForFirmwareUpdate()` | 1 | ❌ |
| 9 | `sendHealthTelemetry()` | 1 + flush | ❌ in flush |
| 10 | `sendDailyReport()` | 3+ + flush | Partial |
| 11 | `safeSleep()` | 0 | ✅ |

**Total potential R&R in one loop: 100+** (if everything fires at once after recovery with full buffer).

### Server: Worst-case single loop() iteration

| Step | Function | Max R&R calls | Kick? |
|---|---|---|---|
| 1 | `mbedWatchdog.kick()` | 0 | ✅ |
| 2 | `handleWebRequests()` | 22 (config retry) | ❌ |
| 3 | Notecard health | 1 | ❌ |
| 4 | `pollNotecard()` | 90 + handler R&R | ✅ per note |
| 5 | `dispatchPendingConfigs()` | 8 × 22 = 176 | ❌ |
| 6 | `ensureTimeSync()` | 1 | ❌ |
| 7 | `sendDailyEmail()` | 1 | ❌ |
| 8 | `publishViewerSummary()` | 1 (sendRequest) | ❌ |
| 9 | `checkStaleClients()` | 10 | ❌ |
| 10 | FTP backup/restore | N (Ethernet) | ✅ |
| 11 | `checkForFirmwareUpdate()` | 1 | ❌ |
| 12 | `checkServerVoltage()` | 1 | ❌ |

---

## Recommended Fixes (Priority Order)

### P0 — Must Fix (Proven >30s paths)

1. **Client `flushBufferedNotes()`:** Add `mbedWatchdog.kick()` inside the `while(fgets)` loop, at least every N notes (e.g., every 5). Also add a cap on notes-per-flush (e.g., 10) and return, letting the next `loop()` iteration continue.

2. **Server `purgePendingConfigNotes()`:** Add `mbedWatchdog.kick()` inside the delete loop (L7107–L7125).

3. **Server `dispatchPendingConfigs()`:** Add `mbedWatchdog.kick()` at the top of each iteration of the client loop (L7248–L7261).

4. **Server `checkStaleClients()`:** Add `mbedWatchdog.kick()` before each `sendSmsAlert()` call inside the loop.

### P1 — Should Fix (Borderline 15–30s paths)

5. **Client `pollForSerialRequests()`:** Add a notes-per-call cap (like `MAX_NOTES_PER_FILE_PER_POLL`) and `mbedWatchdog.kick()` per iteration of the `while(true)` loop.

6. **Client `pollForLocationRequests()`:** Add `mbedWatchdog.kick()` before the `fetchNotecardLocation()` call.

7. **Client `sampleTanks()`:** Add `mbedWatchdog.kick()` at the start of each tank iteration in the for loop.

8. **Client `sendDailyReport()`:** Add `mbedWatchdog.kick()` at the start of the `while(tankCursor)` loop.

### P2 — Nice to Have (Defense in depth)

9. **Server `handleAlarm()`:** Add `mbedWatchdog.kick()` before `sendSmsAlert()` call.

10. **Server web API handlers** that dispatch configs: Add kick before `sendConfigViaNotecard()`.

11. **Client `pollForConfigUpdates()`:** Add kick before the `requestAndResponse` call.

12. All functions that call `checkNotecardHealth()` as a fallthrough: Add kick before the call.

---

## Audit Methodology

- Searched all `requestAndResponse`, `sendRequest`, `mbedWatchdog.kick()`, and `delay()` patterns in both .ino files
- Traced the entire `loop()` call graph for both Client (L1392–L1735) and Server (L2434–L2667)
- Identified all functions reachable from `loop()` and their sub-calls
- Counted blocking I2C calls per function and checked for intermediate watchdog kicks
- Estimated worst-case timing using 5–10s per `requestAndResponse` (modem-busy scenario)
