# Next Implementation Priorities — February 26, 2026

**Status:** Priorities 0–2 completed. Next session starts at Priority 3.

---

## Step 0: Compilation Verification ✅ COMPLETED

All 4 sketches compiled clean before feature work:

| Sketch | Flash | RAM |
|--------|-------|-----|
| Client | 278,712 (14%) | 73,296 (13%) |
| Server | 644,004 (32%) | 265,664 (50%) |
| Viewer | 250,080 (12%) | 76,176 (14%) |
| I2C Utility | 166,752 (8%) | 63,376 (12%) |

---

## Priority 1: 3.2.4 — I2C Recovery Event Logging ✅ COMPLETED

**Implementation summary:**
- Added `I2CRecoveryTrigger` enum to `TankAlarm_I2C.h` (5 trigger types)
- Added `DIAG_OUTBOX_FILE "diag.qo"` to `TankAlarm_Common.h`
- Created `logI2CRecoveryEvent()` in Client — rate-limited (60s), publishes to `diag.qo` with trigger, count, i2c_errs, timestamp
- Updated all 4 Client recovery call sites with appropriate trigger values
- Added Serial.print logging in Server and Viewer (Notecard unavailable during recovery)

**Post-implementation compilation:**

| Sketch | Flash | RAM |
|--------|-------|-----|
| Client | 279,232 (14%) | 73,304 (13%) |
| Server | 644,052 (32%) | 265,664 (50%) |
| Viewer | 250,128 (12%) | 76,176 (14%) |
| I2C Utility | 166,752 (8%) | 63,376 (12%) |

---

## Priority 2: 3.2.5 — Startup Scan in Health Telemetry ✅ COMPLETED

**Implementation summary:**
- Added `unexpectedCount` field to `I2CScanResult` struct in `TankAlarm_I2C.h`
- Updated `tankalarm_scanI2CBus()` to count and return unexpected device count
- Added 5 Client globals: `gStartupNotecardFound`, `gStartupCurrentLoopFound`, `gStartupScanRetries`, `gStartupUnexpectedDevices`, `gStartupScanReported`
- Updated Client `setup()` to store scan results instead of discarding them
- Added startup scan fields to first `sendHealthTelemetry()` report: `scan_nc`, `scan_cl`, `scan_retries`, `scan_unexpected`

**Post-implementation compilation:** All 4 sketches — same sizes as above, exit code 0.

---

## Priority 3: 3.2.1 — I2C Transaction Timing Telemetry

**Effort:** 4–6 hours | **Risk:** Low | **Planning doc:** `FUTURE_3.2.1_I2C_TRANSACTION_TIMING.md`

**Why third:** Provides early warning of bus degradation (slow transactions) before failures occur. Larger effort than 3.2.4/3.2.5 but high diagnostic value. Benefits from the shared `TankAlarm_I2C.h` header already in place.

---

## Priority 4: 3.2.3 — Notecard Health Check Backoff ✅ COMPLETED

**Implementation summary:**
- Added `NOTECARD_HEALTH_CHECK_BASE_INTERVAL_MS` (5 min) and `NOTECARD_HEALTH_CHECK_MAX_INTERVAL_MS` (80 min) to `TankAlarm_Config.h`
- Client: exponential backoff in `loop()` health check — doubles interval on each failure, caps at 80 min, resets on recovery. Power-state-aware floor (10 min in ECO, 20 min in LOW_POWER+)
- Server/Viewer: same exponential backoff (no power-state floor — grid-powered)
- Net effect: sustained Notecard outage → 288 checks/day reduced to ~21 (93% fewer), bus recoveries reduced from ~5 to ~1

**Post-implementation compilation:**

| Sketch | Flash | RAM | Exit |
|--------|-------|-----|------|
| Client | 279,448 (14%) | 73,312 (14%) | 0 |
| Server | 644,236 (32%) | 265,664 (50%) | 0 |
| Viewer | 250,320 (12%) | 76,176 (14%) | 0 |
| I2C Utility | 166,752 (8%) | 63,376 (12%) | 0 |

---

## ─── Remaining Items (Triaged) ───

### Deferred to Next Review: 3.2.1 — I2C Transaction Timing Telemetry

**Effort:** 4–6 hours | **Risk:** Low | **Planning doc:** `FUTURE_3.2.1_I2C_TRANSACTION_TIMING.md`

Wait for field deployment data from the new diagnostics (3.2.4 recovery logging, 3.2.5 startup scan, 3.2.3 backoff) before investing in timing telemetry. Revisit once we have a deployment cycle showing whether bus degradation is actually occurring.

### Dropped: 3.2.2 — Current Loop Read Caching

The planning doc itself notes this isn't currently an issue (4 reads per 30-min cycle). Caching adds complexity for zero measurable benefit. **Remove from backlog** unless sensor count grows significantly.

### Dropped: 3.3.1 — I2C Mutex Guard

Only needed for dual-core (M4) concurrent I2C access, which is not planned. Speculative future-proofing with zero current benefit. **Remove from backlog** until dual-core becomes a real requirement.

### Dropped: 3.3.2 — I2C Address Conflict Detection

Superseded by 3.2.5 (startup scan in Notehub). Unexpected devices now show up in health telemetry. Remaining edge cases (two devices at one address) are installation errors caught during commissioning. **Remove from backlog.**

### Dropped: 3.3.4 — Flash-Backed I2C Error Log

Superseded by 3.2.4 (recovery events published to Notehub in real-time). The only remaining gap is data loss across watchdog reset, but the next boot's health note reports fresh state. 6–10 hour effort with flash endurance risk for marginal value. **Remove from backlog.**

### Deferred Indefinitely: 3.3.3 — Wire Library Timeout Configuration

Medium risk — Notecard clock stretching sits close to timeout boundary. Needs hardware testing and 3.2.1 baseline data first. **Defer until timing telemetry shows the timeout is actually a problem.**

### Deferred Indefinitely: 3.3.5 — Opta Blueprint Directory Disposition

Housekeeping only. Convert vendored directory to proper git submodule someday. Zero urgency.

### Deferred Indefinitely: 3.3.6 — Integration Test Suite

16–40 hour investment. Park for v2.0 or when the team grows.

### Deferred: Notecard Templates (FUTURE_OPTIMIZATION_TEMPLATES)

Data models are still evolving (new fields added this session). Not ready until schemas stabilize post-v1.x.

---

## Also Open: Issue #247 Remaining Items

From `CODE_REVIEW_ISSUE247_02262026.md`, these items were recommended but not yet implemented:

| Item | Status |
|------|--------|
| Category 4: Auto relay de-energize on CRITICAL_HIBERNATE | Needs verification — is current relay behavior intentional? |
| Category 5: Modular file split | Deferred to v1.3.0+ — document module map only |
| Category 6c: `DEBUG_PRINTLN` macros | Conditional on existing debug flag convention |

## delay() Audit ✅ COMPLETED

All 11 bare `delay()` calls in Client audited. All are safe:
- 4× tiny (1-2ms) in sensor sampling loops with watchdog kicks
- 1× intentional watchdog-trigger (`while(true) { delay(100); }`)
- 2× in `setup()` before watchdog starts
- 1× 10ms I2C bus settle
- 1× 50ms button debounce (bounded by press duration)
- 1× inside `safeSleep()` itself (the fallback path)
Server and Viewer each have 1 `delay()` — inside their own `safeSleep()`. No action needed.

---

*Created February 26, 2026. Updated after 3.2.3, 3.2.4, 3.2.5 implementation and backlog triage.*
