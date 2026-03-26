# TankAlarm Master TODO List

> **Current Version:** 1.1.8 (March 16, 2026)  
> **Last Updated:** June 9, 2026  
> **Purpose:** Comprehensive tracker for all unimplemented changes identified in code reviews and logic reviews. Update after every new review or commit.

---

## Table of Contents

- [Legend](#legend)
- [Critical Bugs — Fix Immediately](#critical-bugs--fix-immediately)
- [High-Priority Issues](#high-priority-issues)
- [Moderate-Priority Issues](#moderate-priority-issues)
- [Minor / Cosmetic Issues](#minor--cosmetic-issues)
- [Common Header Centralization](#common-header-centralization)
- [Future Work — Planned Enhancements](#future-work--planned-enhancements)
- [Deferred / Dropped Items](#deferred--dropped-items)
- [Completed Items (Archive)](#completed-items-archive)
- [Review Sources](#review-sources)

---

## Legend

| Symbol | Meaning |
|--------|---------|
| `[ ]` | Not started |
| `[~]` | In progress |
| `[x]` | Completed |
| `[D]` | Deferred to future release |
| `[X]` | Dropped / Won't fix |
| **(S)** | Server component |
| **(C)** | Client component |
| **(V)** | Viewer component |
| **(A)** | All / Common |

---

## Critical Bugs — Fix Immediately

These items represent data loss, safety, or security risks.

### ~~C-5: UNTIL_CLEAR Relay Clear Unreachable **(C)** — SAFETY~~ ✅ FIXED
- [x] `sendAlarm()` now properly checks `cfg.relayMode == RELAY_MODE_UNTIL_CLEAR` and deactivates when the clearing alarm matches the trigger type. Logic corrected at Client line ~4591.
- **Source:** LOGICREVIEW-20260324 (multiple reviewers)
- **Verified:** March 24, 2026 — code inspection confirms fix in place.

### ~~C-6: Viewer DFU Enable Uses Wrong Notecard Request **(V)** — PROTOCOL~~ ✅ FIXED
- [x] `enableDfuMode()` now correctly sends `card.dfu` with `name:"stm32"` and `on:true` (matching Server pattern) instead of the status-check-only `dfu.status`. Function comment also corrected.
- **Source:** LOGICREVIEW-20260324
- **Fixed:** June 9, 2026 — Viewer `enableDfuMode()` updated.

### ~~C-2: SMS Rate Limiting Fails Open **(S)** — SECURITY/COST~~ ✅ FIXED
- [x] `checkSmsRateLimit()` now returns `false` (deny SMS) when time sync is unavailable (`now <= 0.0`). Prevents unbounded SMS flooding before clock synchronization.
- **Source:** LOGICREVIEW-20260324
- **Fixed:** June 9, 2026 — Server `checkSmsRateLimit()` updated.

### ~~C-3: Daily Email Shown as Local Time, Treated as UTC **(S)** — UX/LOGIC~~ ✅ FIXED
- [x] Web UI label now reads "Daily Email Time (HH:MM, UTC)" to clarify the timezone. Users are informed the time is UTC.
- **Source:** LOGICREVIEW-20260324
- **Fixed:** June 9, 2026 — Server settings HTML updated.

### ~~C-1: Stuck Sensor Detection False Positives **(C)**~~ ✅ FIXED
- [x] `stuckDetectionEnabled` now defaults to `false` (opt-in via config). Prevents false positive stuck-sensor alerts on idle tanks before explicit configuration.
- **Source:** LOGICREVIEW-20260324
- **Fixed:** June 9, 2026 — Client default config updated.

### ~~C-4: Viewer Ethernet Retry Near Watchdog Timeout **(V)** — WATCHDOG~~ ✅ FIXED
- [x] Ethernet retry `delay()` replaced with `safeSleep()` which feeds the watchdog in chunks. Prevents watchdog reset during 15-second retry delays.
- **Source:** LOGICREVIEW-20260324
- **Fixed:** June 9, 2026 — Viewer `initializeEthernet()` updated.

### ~~H4: Non-Atomic File Writes **(S)(C)(V)** — DATA LOSS~~ ✅ FIXED
- [x] All core save functions and archive manifest now use atomic writes. Server `archived_clients.json` updated to write-to-temp-then-rename pattern.
- **Source:** H4_ATOMIC_WRITE_REFACTOR_RECOMMENDATION.md, CODE_REVIEW_FIX_IMPLEMENTATION_02202026.md
- **Fixed:** June 9, 2026 — archive manifest write now atomic.

---

## High-Priority Issues

### ~~I-10: Delete-Before-Accept Message Pattern **(A)** — ERROR HANDLING~~ ✅ FIXED
- [x] All three components now use two-step get-then-delete: `note.get` without `delete:true` (peek), process body, then separate `note.get` with `delete:true` to consume. If the device crashes during processing, the note survives for retry on next boot.
- **Source:** LOGICREVIEW-20260324
- **Fixed:** June 9, 2026 — Client `handleInboundConfig`, Server `processNotefile`, Viewer summary fetch all updated.

### ~~I-2: Viewer Summary Fetch Bootstrap Deadlock **(V)**~~ ✅ FIXED
- [x] `setup()` now calls `ensureTimeSync()` before `scheduleNextSummaryFetch()`, ensuring `gNextSummaryFetchEpoch` is set to a positive value. The bootstrap deadlock no longer occurs.
- **Source:** LOGICREVIEW-20260324
- **Verified:** March 24, 2026 — fix confirmed at Viewer lines ~246-250.

### I-5: Current-Loop Failures Return Previous Value **(C)** — DATA INTEGRITY — BY DESIGN
- [X] `readCurrentLoopSensor()` returns previous value on I2C failure. This is intentional: the stuck sensor detection system catches persistent failures (N identical readings → stuck alert). Single transient I2C failures are smoothed over, which is appropriate for slowly-changing tank levels. The `consecutiveFailures` counter and `communicationOk` flag provide visibility.
- **Source:** LOGICREVIEW-20260324
- **Reclassified:** June 9, 2026 — accepted as design choice; stuck detection is the backstop.

### ~~I-4: Solar Comm Failures Silently Suppressed **(C)**~~ ✅ CLARIFIED
- [x] `SolarManager::poll()` return value documented: returns `false` for both "not due" and "read failed". Callers should check `isCommunicationOk()` for failure state. The `consecutiveErrors` counter and `communicationOk` flag already surface failures internally and trigger alerts after threshold.
- **Source:** LOGICREVIEW-20260324
- **Fixed:** June 9, 2026 — added clarifying comments to `poll()` return paths.

### ~~I-3: Viewer Never Validates Schema Version **(V)** — FORWARD COMPAT~~ ✅ FIXED
- [x] `handleViewerSummary()` now checks the `_sv` field and logs a warning if the schema version is not 1. Future schema changes will produce visible warnings.
- **Source:** LOGICREVIEW-20260324
- **Fixed:** June 9, 2026 — Viewer `handleViewerSummary()` updated.

### ~~I-7: Server Overrides Per-Monitor SMS Intent **(S)(C)**~~ — DESIGN CHOICE
- [x] Server intentionally forces `smsEnabled=true` for sensor alarms (high/low/clear/digital), modulated by server-wide policies (`smsOnHigh`, `smsOnLow`). Non-sensor/diagnostic alarms respect client's `smsEnabled` setting. This is by design — sensor-level alarms are always SMS-eligible.
- **Source:** LOGICREVIEW-20260324
- **Verified:** March 24, 2026 — confirmed intentional at Server line ~8382.

### I-6: Config Retry State Inconsistency **(S)** — STATE MACHINE
- [ ] Auto-retry leaves `pendingDispatch=true` until ACK. Manual retry clears `pendingDispatch=false` immediately on Notecard send success (line ~7187). If a manual note is lost in transit, no auto re-send happens because `pendingDispatch` was already cleared.
- **Source:** LOGICREVIEW-20260324
- **Verified:** March 24, 2026 — still inconsistent.

### ~~I-8: Missing Sensor Metadata in Alarms **(C)(S)**~~ ✅ FIXED
- [x] Alarm payloads now include object type (`ot`), measurement unit (`mu`), and sensor interface type (`st`) fields. Added at Client lines ~4330-4350.
- **Source:** LOGICREVIEW-20260324
- **Verified:** March 24, 2026 — all three metadata fields present in `sendAlarm()`.

### ~~I-9: Unbounded Viewer Cadence Inputs **(V)** — INPUT VALIDATION~~ ✅ FIXED
- [x] `refreshSeconds` clamped to 3600–86400 (1–24 hours). `baseHour` clamped to 0–23 (defaults to 6 if out of range). Prevents pathological fetch frequencies from malformed payloads.
- **Source:** LOGICREVIEW-20260324
- **Fixed:** June 9, 2026 — Viewer `handleViewerSummary()` updated with bounds clamping.

### ~~I2C Bus Recovery Missing **(C)(V)**~~ ✅ FIXED
- [x] `TankAlarm_I2C.h` now provides `tankalarm_recoverI2CBus()` with Wire.end(), 16x SCL-toggle, STOP condition, and Wire.begin() reinit. Used by Client (line ~1490), Server (line ~2703), and Viewer (line ~314) with watchdog integration.
- **Source:** I2C_COMPREHENSIVE_ANALYSIS_02262026.md
- **Verified:** March 24, 2026 — TankAlarm_I2C.h lines ~69-118.

### ~~V-1: Tank Array Overflow **(V)**~~ ✅ FIXED
- [x] Bounds check added: `if (gSensorRecordCount >= MAX_SENSOR_RECORDS) break;` at Viewer line ~856. `MAX_SENSOR_RECORDS` = 64.
- **Source:** CODE_REVIEW_03122026_COMPREHENSIVE.md
- **Verified:** March 24, 2026.

### ~~DEFAULT_PRODUCT_UID Collision **(C)**~~ ✅ FIXED
- [x] Client now uses conditional `#include "ClientConfig.h"` before the `#ifndef DEFAULT_PRODUCT_UID` guard, allowing per-deployment override. ClientConfig.h defines the UID first; fallback only applies if not already defined.
- **Source:** COMMON_HEADER_AUDIT_02192026.md (C5)
- **Verified:** March 24, 2026 — Client lines ~104-109.

### ~~Config ACK Notefile Routing Mismatch **(C)(S)**~~ ✅ FIXED
- [x] Routing now uses separate notefile pair: Client sends ACKs on `config_ack.qo` (CONFIG_ACK_OUTBOX_FILE), Server reads from `config_ack.qi` (CONFIG_ACK_INBOX_FILE). Both constants centralized in TankAlarm_Common.h lines ~139-144.
- **Source:** COMMON_HEADER_AUDIT_02192026.md (D1)
- **Verified:** March 24, 2026.

---

## Moderate-Priority Issues

### M-5: No "Client Recovered" SMS Alert **(S)**
- [ ] Stale client alert is sent at 49h timeout. When the client resumes, no recovery SMS is sent. Operators must check the dashboard manually.
- **Source:** LOGICREVIEW-20260324
- **Verified:** March 24, 2026 — still no recovery SMS.

### ~~M-7: Sensor-Recovered Alarms Blocked from SMS **(S)**~~ ✅ FIXED
- [x] `sensor-recovered` is now routed to SMS with explicit `isRecovery` flag handling at Server lines ~8347-8348.
- **Source:** LOGICREVIEW-20260324
- **Verified:** March 24, 2026.

### ~~M-2: Sensor Recovery Not Rate-Limited **(C)**~~ ✅ FIXED
- [x] Sensor recovery notifications now go through `checkAlarmRateLimit()` instead of bypassing rate limits. Both the normal recovery path and the button-clear recovery path are rate-limited.
- **Source:** LOGICREVIEW-20260324
- **Fixed:** June 9, 2026 — Client recovery paths updated.

### ~~M-3: Sensor Recovery Instantaneous (No Debounce) **(C)**~~ ✅ FIXED
- [x] Recovery now requires 3 consecutive valid readings before declaring sensor recovered (`recoveryCount` field added to `MonitorRuntime`). Prevents flaky sensors from rapidly toggling between failed/recovered states.
- **Source:** LOGICREVIEW-20260324
- **Fixed:** June 9, 2026 — Client `validateReading()` updated with debounce counter.

### M-4: I2C Thresholds Loop-Count-Based **(C)** — PARTIALLY FIXED
- [~] Still uses loop counts (not elapsed time), but now has exponential backoff multiplier (`sensorRecoveryBackoff`) at Client line ~1532. Variance between NORMAL and CRITICAL power states remains (~3,000x).
- **Source:** LOGICREVIEW-20260324, I2C_COMPREHENSIVE_ANALYSIS_02262026.md
- **Fix:** Use elapsed-time thresholds instead of loop counts.
- **Verified:** March 24, 2026 — loop-count-based with backoff.

### ~~M-6: Power Thresholds Accepted Without Validation **(C)**~~ ✅ FIXED
- [x] After receiving power threshold config, hysteresis ordering is validated: exit voltage must be greater than enter voltage for each tier (eco, low, critical). If violated, exit voltage is corrected to enter + 0.2V with a warning logged.
- **Source:** LOGICREVIEW-20260324
- **Fixed:** June 9, 2026 — Client `handleInboundConfig()` updated with cross-validation.

### M-1: Current-Loop No Multi-Sample Averaging **(C)**
- [ ] Analog path uses 8-sample averaging. Current-loop path uses a single sample. Inconsistent noise rejection.
- **Source:** LOGICREVIEW-20260324
- **Verified:** March 24, 2026 — single sample at Client line ~4033.

### ~~M-8: Calibration Singular Matrix Detection Weak **(S)**~~ — ACCEPTABLE
- [x] Uses `fabs(det) < 0.0001f` threshold with fallback to simple regression at Server line ~12376. While the absolute threshold could miss some ill-conditioned matrices, the fallback path provides safety. Acceptable for current use case.
- **Source:** LOGICREVIEW-20260324
- **Verified:** March 24, 2026.

### M-9: 24h Change Tracking Frozen at High Frequency **(S)**
- [ ] Snapshot-based "24h change" calculated as latest minus oldest recent reading, not bucketed by hour. Becomes stale for frequent reporters.
- **Source:** LOGICREVIEW-20260324
- **Verified:** March 24, 2026 — still snapshot-based at Server line ~10914.

### M-10: Manual Config Retry Inconsistency **(S)**
- [ ] Manual path clears `pendingDispatch` immediately on Notecard send success (line ~7206); auto path waits for ACK. If a manually dispatched note is lost in transit, there's no auto re-send.
- **Source:** LOGICREVIEW-20260324
- **Verified:** March 24, 2026 — still inconsistent.

### ~~Config Buffer Mismatch **(S)**~~ ✅ FIXED
- [x] Buffer sizes aligned: payload buffer 1536 bytes (line ~880), dispatch uses 8192-byte static buffer (line ~7851). No mismatch risk.
- **Source:** CODE_REVIEW_03122026_COMPREHENSIVE.md (S-4)
- **Verified:** March 24, 2026.

### ~~Server `respondHtml()` Triple-Copy Memory Pattern **(S)**~~ ✅ FIXED
- [x] Now uses streaming with `sendStringRange()` at Server lines ~6449-6510. Calculates `Content-Length` upfront, then writes chunks instead of building a massive String in memory.
- **Source:** CODE_REVIEW_RECOMMENDATIONS_02262026.md
- **Verified:** March 24, 2026.

### JSON API Builders Using String Concatenation **(S)** — PARTIALLY FIXED
- [~] Uses `+=` incremental concatenation (Server lines ~13228-13232) instead of single massive concat, reducing peak memory. However, still uses `String` class rather than streaming JSON. Heap fragmentation risk remains but is reduced.
- **Source:** CODE_REVIEW_RECOMMENDATIONS_02262026.md
- **Verified:** March 24, 2026.

---

## Minor / Cosmetic Issues

### m-1: Stuck Tolerance Hard-Coded **(C)**
- [ ] `0.05` inches hard-coded threshold, not proportional to sensor range. High-resolution sensors (0–1 PSI) read within normal noise.
- **Source:** LOGICREVIEW-20260324

### m-5: No DFU Rollback on Crash-Loop **(C)**
- [ ] No boot-success flag to detect bad firmware. A bad DFU update causes permanent crash-loop.
- **Source:** LOGICREVIEW-20260324

### m-9: No Heartbeat in CRITICAL_HIBERNATE **(C)**
- [ ] Device sends zero telemetry in deepest sleep state. Server loses visibility entirely.
- **Source:** LOGICREVIEW-20260324

### m-10: VinVoltage Not Rendered on Dashboard **(V)**
- [ ] `VinVoltage` data is present in the JSON API but not displayed in the HTML table. Operators can't see client power status.
- **Source:** LOGICREVIEW-20260324

### ~~m-6: Negative Level Clamping Inconsistent **(S)**~~ ✅ FIXED
- [x] All fallback calculation paths in `convertMilliampsToLevel()` now clamp negative results to 0.0f. Previously only the calibrated path and one fallback had clamping.
- **Source:** LOGICREVIEW-20260324
- **Fixed:** June 9, 2026 — Server fallback returns updated.

### ~~m-7: FTP Archive Manifest Not Atomic **(S)**~~ ✅ FIXED
- [x] Archive manifest write now uses atomic write-to-temp-then-rename pattern (`/fs/archived_clients.json.tmp` → `/fs/archived_clients.json`). Part of H4 completion.
- **Source:** LOGICREVIEW-20260324
- **Fixed:** June 9, 2026 — merged with H4 fix.

### m-8: Solar Alert Reports Stale Data **(C)**
- [ ] Reports battery-critical before communication-failure. Past reading persists after comms go down.
- **Source:** LOGICREVIEW-20260324

### m-3: millis() Rollover at 49.7 Days **(C)**
- [ ] Rate limiter briefly fails at `millis()` rollover. Low risk; debounce catches on next cycle.
- **Source:** LOGICREVIEW-20260324

### m-2: Momentary Relay Duration Precedence Unclear **(C)**
- [ ] Multiple relays in mask with different durations: both deactivate at the minimum. Documented but unintuitive behavior.
- **Source:** LOGICREVIEW-20260324

### ~~Dead `#ifndef` Redefinitions **(S)(C)**~~ ✅ FIXED
- [x] All deprecated local redefinitions have been removed. Server and Client now reference Common.h definitions directly.
- **Source:** COMMON_HEADER_AUDIT_02192026.md (E1)
- **Verified:** March 24, 2026.

---

## Common Header Centralization

Items from the COMMON_HEADER_AUDIT_02192026.md that need cleanup.

### HIGH
- [x] **(A1)** ~~`CONFIG_ACK_OUTBOX_FILE` + `CONFIG_ACK_INBOX_FILE`~~ ✅ Now centralized in TankAlarm_Common.h lines ~139-144.
- [x] **(C5)** ~~`DEFAULT_PRODUCT_UID` collision~~ ✅ Fixed via ClientConfig.h conditional include — see High-Priority Issues above.
- [x] **(D1)** ~~Config ACK notefile routing~~ ✅ Separate .qo/.qi pair centralized — see High-Priority Issues above.

### MEDIUM
- [~] **(C1)** `DEFAULT_SAMPLE_SECONDS` — Client uses alias `#define DEFAULT_SAMPLE_SECONDS DEFAULT_SAMPLE_INTERVAL_SEC` (line ~187). Functionally correct but maintains dual naming.
- [~] **(C2)** `DEFAULT_LEVEL_CHANGE_THRESHOLD_INCHES` — Same alias pattern: `#define DEFAULT_LEVEL_CHANGE_THRESHOLD_INCHES DEFAULT_LEVEL_CHANGE_THRESHOLD` (line ~188).
- [x] **(A2)** ~~`MAX_RELAYS`~~ ✅ Centralized in TankAlarm_Common.h lines ~58-59. Local definitions removed.
- [x] **(A3)** ~~`CLIENT_SERIAL_BUFFER_SIZE`~~ ✅ Centralized in TankAlarm_Common.h lines ~65-66. Local definitions removed.
- [x] **(A7)** ~~`VIEWER_SUMMARY_INTERVAL_SECONDS` / `VIEWER_SUMMARY_BASE_HOUR`~~ ✅ Now centralized in TankAlarm_Common.h. Server local definitions removed; Viewer local aliases now reference Common.h constants.

### LOW
- [x] **(E1)** ~~Dead `#ifndef` redefinitions~~ ✅ All removed.
- [ ] **(E2)** Platform defines (`POSIX_FS_PREFIX` etc.) — TankAlarm_Platform.h uses `TANKALARM_POSIX_FS_PREFIX`, but Server/Client maintain local `POSIX_FS_PREFIX`. Parallel naming conventions coexist.
- [ ] **(E3)** Debug macros — Client has full `#ifdef DEBUG_MODE` macros (lines ~85-95); Server/Viewer only use inline `#ifdef` without no-op macro stubs.
- [x] **(A4)** ~~`DFU_CHECK_INTERVAL_MS`~~ ✅ Centralized in TankAlarm_Common.h lines ~238-239. Redundant local definitions removed.
- [ ] **(A6)** `MAX_TANKS_PER_CLIENT` — still Client-local only, not in Common.h.

---

## Future Work — Planned Enhancements

### Tier 1 — Foundation (P1, prerequisite for later work)

- [x] **3.1.1 — Notecard `begin()` Idempotency Audit** ✅ COMPLETED  
  `tankalarm_ensureNotecardBinding()` wrapper created in TankAlarm_Notecard.h (line ~108). Used across 20+ callsites in all components.

- [x] **3.1.2 — Extract I2C Operations to Shared Header** ✅ COMPLETED  
  TankAlarm_I2C.h created (~300 lines) with `tankalarm_recoverI2CBus()`, `tankalarm_scanI2CBus()`, and `tankalarm_readCurrentLoopMilliamps()`. All 4 sketches now use shared functions.

- [x] **3.1.3 — Server/Viewer I2C Hardening** ✅ COMPLETED  
  Server and Viewer both use shared I2C recovery + bus scan with watchdog integration. Error counters and exponential backoff implemented.

### Tier 2 — Diagnostics & Reliability (P2)

- [ ] **3.2.1 — I2C Transaction Timing Telemetry** (4–6 hrs)  
  Instrument I2C calls to report min/avg/max timing data. Deferred pending field data collection.

- [x] **3.2.3 — Notecard Health Check Backoff** — COMPLETED
- [x] **3.2.4 — I2C Recovery Event Logging** — COMPLETED
- [x] **3.2.5 — Startup Scan in Health Telemetry** — COMPLETED

### Tier 3 — Advanced / Deferred to v2.0+

- [D] **3.3.1 — I2C Mutex Guard** — DROPPED (dual-core not planned)
- [D] **3.3.2 — I2C Address Conflict Detection** — DROPPED (superseded by 3.2.5)
- [D] **3.3.3 — Wire Library Timeout Configuration** — DEFERRED (pending 3.2.1 baseline data)
- [D] **3.3.4 — Flash-Backed I2C Log** — DROPPED (superseded by 3.2.4)
- [D] **3.3.5 — Opta Blueprint Disposition** — DEFERRED housekeeping
- [D] **3.3.6 — Integration Test Suite** — DEFERRED to v2.0 (16–40 hrs estimated)
- [D] **Notecard Optimization Templates** — DEFERRED (schemas still evolving)

### Other Future Enhancements

- [ ] **Pump Off Control Implementation** — Review and implement pump-off control options per PUMP_OFF_CONTROL_OPTIONS_03202026.md.
- [ ] **Modularization** — Break monolithic `.ino` files into modular compilation units. Blocked by Arduino IDE constraints. Consider PlatformIO migration.
- [ ] **Power State Machine Test Automation** — Execute the 7 documented test scenarios (POWER_STATE_TEST_COVERAGE.md). Currently test procedures exist, but execution status is unknown.

---

## Deferred / Dropped Items

| Item | Reason | Source |
|------|--------|--------|
| 3.2.2 Current Loop Caching | DROPPED — No current need | FUTURE_3.2.2 |
| 3.3.1 I2C Mutex Guard | DROPPED — Dual-core not planned | FUTURE_3.3.1 |
| 3.3.2 Address Conflict Detection | DROPPED — Superseded by 3.2.5 | FUTURE_3.3.2 |
| 3.3.4 Flash-Backed I2C Log | DROPPED — Superseded by 3.2.4 | FUTURE_3.3.4 |
| Notecard Templates | DEFERRED — Schemas still evolving | FUTURE_OPTIMIZATION_TEMPLATES |
| Integration Test Suite | DEFERRED to v2.0 — 16-40 hrs | FUTURE_3.3.6 |
| Monolithic file refactor | DEFERRED — Arduino IDE constraint | MODULARIZATION_DESIGN_NOTE |

---

## Completed Items (Archive)

Items moved here after implementation. Include version number and date.

### Completed in Code Review Pass (June 9, 2026)
- [x] **C-6:** Viewer DFU — `dfu.status` → `card.dfu` with `name:"stm32"` (Viewer)
- [x] **C-2:** SMS rate limit — fails closed (deny) when no time sync (Server)
- [x] **C-3:** Daily email time label — added "(UTC)" indicator (Server)
- [x] **C-1:** Stuck sensor detection — default changed to disabled/opt-in (Client)
- [x] **C-4:** Ethernet retry — `delay()` → `safeSleep()` for watchdog safety (Viewer)
- [x] **H4:** Archive manifest — atomic write-to-temp-then-rename (Server)
- [x] **I-10:** Delete-before-parse — two-step get-then-delete in all 3 components
- [x] **I-3:** Schema version — `_sv` field check added to Viewer summary handler
- [x] **I-9:** Viewer cadence — `refreshSeconds` clamped 3600–86400, `baseHour` clamped 0–23
- [x] **I-4:** Solar poll — return semantics documented; `isCommunicationOk()` for failure state
- [x] **I-5:** Current-loop stale data — reclassified as by-design (stuck detection is backstop)
- [x] **m-6:** Negative level clamping — all fallback paths now clamp to ≥ 0.0f (Server)
- [x] **m-7:** Archive manifest — merged with H4 atomic write fix (Server)
- [x] **M-2:** Sensor recovery rate limiting — now goes through `checkAlarmRateLimit()` (Client)
- [x] **M-3:** Sensor recovery debounce — requires 3 consecutive good readings (Client)
- [x] **M-6:** Power threshold validation — hysteresis cross-check added (Client)
- [x] **A7:** Viewer summary constants — centralized in TankAlarm_Common.h; Server/Viewer deduplicated

### Completed in v1.1.8 (March 16, 2026)
- [x] Save all dirty data before DFU/reboot
- [x] Deduplicate daily summaries on reboot
- [x] Persist SMS rate-limit state across power cycles
- [x] Expand FTP backup to 9 files
- [x] Warm tier fallback in FTP monthly archives
- [x] Wire `ftpSyncHour` into maintenance check
- [x] 3-layer stale sensor auto-pruning
- [x] FTP archival system with API + UI

### Completed in v1.1.7 (March 15, 2026)
- [x] Sensor-centric naming refactor (`tankNumber` → `sensorIndex`)
- [x] `userNumber` field added
- [x] Backward-compatibility dual-key JSON removed
- [x] `saveConfigToFlash()` serialization fix
- [x] `serializeConfig()` correct `sensorIndex` emission

### Completed in v1.1.6 (March 13, 2026)
- [x] **S-1:** Remove PIN from browser localStorage
- [x] **S-2:** Remove session tokens from URL query strings (header-based auth)
- [x] **S-3:** Require PIN enrollment on fresh install
- [x] **C-1:** Relay command target-UID verification
- [x] Single-session login with server-generated tokens + constant-time comparison
- [x] **S-4:** Config dispatch/cache buffer partial fix
- [x] DFU auto-recovery
- [x] Tank registry deduplication
- [x] Per-monitor stuck sensor detection (configurable toggle)
- [x] Notefile schema versioning (`_sv` field)
- [x] Boot-time `.tmp` orphan cleanup
- [x] Viewer metadata enrichment + deterministic MAC + Ethernet retry
- [x] 5s relay command cooldown
- [x] I2C sensor recovery max attempts cap
- [x] Solar sunset state persistence

### Completed in v1.1.5 (March 10, 2026)
- [x] Measurement unit & object type metadata in payloads
- [x] Orphaned tank record pruning
- [x] Auto-DFU re-enabled in client main loop
- [x] External antenna documentation

### Verified Fixed (March 24, 2026 audit)
- [x] **C-5:** UNTIL_CLEAR relay clear logic (Client)
- [x] **I-2:** Viewer summary fetch bootstrap deadlock (Viewer)
- [x] **I-7:** Server SMS override — confirmed as intentional design choice
- [x] **I-8:** Sensor metadata (`ot`, `mu`, `st`) now in alarm payloads (Client)
- [x] **I2C Bus Recovery:** `tankalarm_recoverI2CBus()` in shared TankAlarm_I2C.h
- [x] **V-1:** Tank array bounds check (Viewer)
- [x] **DEFAULT_PRODUCT_UID:** ClientConfig.h conditional include fix
- [x] **Config ACK Routing:** Separate `config_ack.qo`/`.qi` pair centralized
- [x] **M-7:** Sensor-recovered alarms now SMS-eligible (Server)
- [x] **M-8:** Calibration singular matrix with fallback — acceptable
- [x] **Config Buffer:** Dispatch/cache buffer sizes aligned (Server)
- [x] **respondHtml():** Now streaming with `sendStringRange()` (Server)
- [x] **H4 (partial):** Core save functions use atomic writes; archive manifest still non-atomic
- [x] Common Header — **A1:** CONFIG_ACK files centralized
- [x] Common Header — **A2:** MAX_RELAYS centralized
- [x] Common Header — **A3:** CLIENT_SERIAL_BUFFER_SIZE centralized
- [x] Common Header — **A4:** DFU_CHECK_INTERVAL_MS centralized
- [x] Common Header — **E1:** Dead #ifndef redefinitions removed

### Completed (FUTURE work items)
- [x] **3.1.1** — Notecard `begin()` Idempotency Audit
- [x] **3.1.2** — Extract I2C Operations to Shared Header (TankAlarm_I2C.h)
- [x] **3.1.3** — Server/Viewer I2C Hardening
- [x] **3.2.3** — Notecard Health Check Backoff
- [x] **3.2.4** — I2C Recovery Event Logging
- [x] **3.2.5** — Startup Scan in Health Telemetry

---

## Review Sources

This TODO was compiled from the following documents, sorted by date:

| Date | Document | Type |
|------|----------|------|
| 2026-03-24 | LOGICREVIEW-20260324-* (9 files) | Logic review — multiple AI reviewers |
| 2026-03-24 | CODE_REVIEW_VIEWER_DECISION_LOGIC_03242026.md | Viewer decision logic review |
| 2026-03-21 | CODE_REVIEW_PUMP_OFF_CONTROL_03212026.md | Pump off control review |
| 2026-03-20 | PUMP_OFF_CONTROL_OPTIONS_03202026.md | Design options |
| 2026-03-16 | V1.1.8_RELEASE_NOTES.md | Release notes |
| 2026-03-15 | V1.1.7_RELEASE_NOTES.md | Release notes |
| 2026-03-13 | V1.1.6_RELEASE_NOTES.md | Release notes |
| 2026-03-12 | CODE_REVIEW_03122026_*.md (3 files) | Comprehensive code review |
| 2026-03-10 | V1.1.5_RELEASE_NOTES.md | Release notes |
| 2026-02-26 | NEXT_PRIORITIES_02262026.md | Priority roadmap |
| 2026-02-26 | CODE_REVIEW_RECOMMENDATIONS_02262026.md | Recommendations |
| 2026-02-26 | I2C_COMPREHENSIVE_ANALYSIS_02262026.md | I2C deep dive |
| 2026-02-26 | WATCHDOG_STARVATION_AUDIT_02262026.md | Watchdog audit |
| 2026-02-26 | BUGFIX_WATCHDOG_STARVATION_02262026.md | Watchdog bugfix |
| 2026-02-19 | COMMON_HEADER_AUDIT_02192026.md | Common header review |
| 2026-02-19 | CODE_REVIEW_02192026_COMPREHENSIVE*.md | Comprehensive review |
| 2026-02-20 | CODE_REVIEW_FIX_IMPLEMENTATION_02202026.md | Fix implementation tracking |
| 2026-02-06 | SECURITY_FIXES_02062026.md | Security hardening |
| Various | FUTURE_3.x.x_*.md (15 files) | Future work items |
| Various | H4_ATOMIC_WRITE_REFACTOR_RECOMMENDATION.md | Architecture recommendation |

---

## Maintenance Notes

- **Update frequency:** After every code review, logic review, or release commit.
- **Workflow:** When an item is completed, move it to the [Completed Items](#completed-items-archive) section with the version number and date. Do not delete items — archive them.
- **New findings:** Add new items under the appropriate severity section. Include source document, affected component(s), and a clear description of the issue and fix.
- **Priority reassessment:** Re-evaluate priorities after each release. Items may be promoted or demoted based on field experience.
