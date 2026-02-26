# Next Implementation Priorities — February 26, 2026

**Prerequisite:** Compile all 4 sketches before any new feature work.

---

## Step 0: Compilation Verification

All 4 sketches have significant uncommitted changes from the high-priority implementation session. Compile each to catch any issues while the changes are fresh:

1. `TankAlarm-112025-Client-BluesOpta.ino`
2. `TankAlarm-112025-Server-BluesOpta.ino`
3. `TankAlarm-112025-Viewer-BluesOpta.ino`
4. `TankAlarm-112025-I2C_Utility.ino`

FQBN: `arduino:mbed_opta:opta`

---

## Priority 1: 3.2.4 — I2C Recovery Event Logging via Notecard

**Effort:** 2–3 hours | **Risk:** Very low | **Planning doc:** `FUTURE_3.2.4_I2C_RECOVERY_EVENT_LOGGING.md`

**Why first:** Recovery events currently vanish into Serial and get aggregated into daily counters. Publishing a lightweight diagnostic note on each `tankalarm_recoverI2CBus()` call gives operators real-time, per-event, fleet-wide visibility in Notehub. Directly builds on the recovery infrastructure completed in 3.1.2/3.1.3.

**Scope:**
- Add a `tankalarm_logRecoveryEvent()` function (shared header or per-sketch)
- Publish a `diag.qo` note with: trigger reason, timestamp, error counters at time of recovery
- Call from each recovery trigger point in Client, Server, and Viewer

---

## Priority 2: 3.2.5 — Startup Scan Results in Health Telemetry

**Effort:** 1–2 hours | **Risk:** Very low | **Planning doc:** `FUTURE_3.2.5_STARTUP_SCAN_IN_HEALTH.md`

**Why second:** Quick add-on. The startup scan already runs and logs to Serial. Capturing results (found/missing devices, retry count) in the first health report after boot helps diagnose field units that boot with missing peripherals.

**Scope:**
- Store `I2CScanResult` from startup scan in a global
- Include scan results in the first health telemetry note post-boot

---

## Priority 3: 3.2.1 — I2C Transaction Timing Telemetry

**Effort:** 4–6 hours | **Risk:** Low | **Planning doc:** `FUTURE_3.2.1_I2C_TRANSACTION_TIMING.md`

**Why third:** Provides early warning of bus degradation (slow transactions) before failures occur. Larger effort than 3.2.4/3.2.5 but high diagnostic value. Benefits from the shared `TankAlarm_I2C.h` header already in place.

---

## Priority 4: 3.2.3 — Notecard Health Check Backoff

**Effort:** 2–3 hours | **Risk:** Low | **Planning doc:** `FUTURE_3.2.3_NOTECARD_HEALTH_CHECK_BACKOFF.md`

**Why fourth:** Reduces I2C bus traffic during prolonged Notecard outages. Currently the health check runs every 5 minutes regardless of how many consecutive failures have occurred. Adding exponential backoff (5min → 10min → 20min) is a refinement rather than a gap-fill.

---

## Priority 5: 3.2.2 — Current Loop Read Caching

**Effort:** 2–3 hours | **Risk:** Low | **Planning doc:** `FUTURE_3.2.2_CURRENT_LOOP_CACHING.md`

**Why last among mediums:** Not currently an issue — each tank reads its own channel once per sample cycle. This is future-proofing for multi-sensor configurations.

---

## Also Open: Issue #247 Remaining Items

From `CODE_REVIEW_ISSUE247_02262026.md`, these items were recommended but not yet implemented:

| Item | Status |
|------|--------|
| Category 4: Auto relay de-energize on CRITICAL_HIBERNATE | Needs verification — is current relay behavior intentional? |
| Category 5: Modular file split | Deferred to v1.3.0+ — document module map only |
| Category 6c: `DEBUG_PRINTLN` macros | Conditional on existing debug flag convention |

---

*Created February 26, 2026. Review after compilation verification.*
