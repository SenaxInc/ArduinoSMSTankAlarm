# Future Work - Undone Items

Date: April 9, 2026
Updated: April 9, 2026 (post-v1.6.2 verification pass)
Source: CODE REVIEW/TODO.md (open, in-progress, and deferred entries)
Scope: Items not marked complete as of v1.6.2

---

## Summary

- Open items: 14
- In-progress items: 3
- Deferred items: 6
- Resolved (reclassified this pass): 1
- Dropped items (tracked for record only): 3

---

## Priority 1 - Active Reliability and Operations

### In Progress

1. M-4: I2C thresholds are still loop-count based (Client)
- Status: [~]
- Why pending: Exponential backoff exists, but elapsed-time thresholds are not implemented.
- Target outcome: Convert recovery windows to time-based checks to normalize behavior across power states.
- Ref: Client line ~1532 (`sensorRecoveryBackoff`).

### Open

1. 3.2.1: I2C transaction timing telemetry
- Status: [ ]
- Effort: 4-6 hours
- Target outcome: Report min/avg/max I2C timing to support field diagnostics and timeout tuning.
- Note: Prerequisite for closing M-4 (provides field baseline) and unblocks 3.3.3 (Wire timeout config).

2. m-9: No heartbeat in CRITICAL_HIBERNATE
- Status: [ ]
- Target outcome: Add low-cadence heartbeat or explicit hibernate-state signaling to server.
- Verified: Client lines 1625, 1662 skip all sampling and inbound polling in this state. Only solar charger poll (line 1702) runs. Server has zero visibility.

3. m-8: Solar alert can report stale data
- Status: [ ]
- Target outcome: Prefer communication-failure state over stale battery-critical values when comms are down.
- Verified: Alert logic does not check `isCommunicationOk()` before reporting battery state.

4. m-5: No DFU rollback on crash-loop
- Status: [ ]
- Target outcome: Implement boot-success flag; detect crash-loop after bad DFU update and revert or alert.
- Verified: No boot-success flag or crash-loop detection exists in codebase.

### Resolved This Pass

1. ~~m-3: millis rollover edge case in rate limiter~~
- Status: ✅ RESOLVED — no code change needed
- Reason: `checkAlarmRateLimit()` uses unsigned `(now - lastTimestamp)` comparisons (Client line ~4721). Unsigned subtraction wraps correctly at 49.7 days on all 32-bit targets. The v1.6.2 `if (now >= 3600000UL)` guard (line ~4751) protects the hourly pruning window specifically; the main rate-limit comparisons were already rollover-safe.
- Reclassified: April 9, 2026 — verified via code inspection.

---

## Priority 2 - Architecture and Maintainability

### In Progress

1. C1: DEFAULT_SAMPLE_SECONDS alias still retained in Client
- Status: [~]
- Target outcome: Remove dual naming and use one canonical constant path.
- Verified: Alias at Client line 187; referenced at lines 2043-2044, 2542-2543.

2. C2: DEFAULT_LEVEL_CHANGE_THRESHOLD_INCHES alias still retained in Client
- Status: [~]
- Target outcome: Remove alias layer and keep one authoritative symbol.
- Verified: Alias at Client line 188; referenced in config initialization paths.

### Open

1. E2: Platform define naming split (POSIX_FS_PREFIX variants)
- Status: [ ]
- Target outcome: Standardize platform macro naming across Server and Client.
- Verified: `TANKALARM_POSIX_FS_PREFIX` in TankAlarm_Platform.h line 52, but Server (line 68) and Client (line 58) still define local `POSIX_FS_PREFIX`.

2. E3: Debug macro model inconsistent across sketches
- Status: [ ]
- Target outcome: Use unified debug macro stubs/no-op wrappers across Server, Client, Viewer.
- Verified: Client has full macro stubs (lines 83-95: DEBUG_BEGIN, DEBUG_PRINT, etc.). Server (line 58) and Viewer (line 37) only use inline `#ifdef DEBUG_MODE` with no-op stubs.

3. A6: MAX_TANKS_PER_CLIENT not centralized in Common header
- Status: [ ]
- Target outcome: Move constant to Common header for consistency.
- Verified: Still Client-local only; not in TankAlarm_Common.h.

4. m-11: linearMap not centralized
- Status: [ ]
- Target outcome: Move utility to shared header (for reuse and single-source maintenance).
- Verified: Defined at Client line 4124 only; used at lines 4322, 4385, 4397.

5. m-12: posix_read_file truncates silently on oversized files
- Status: [ ]
- Target outcome: Return explicit error or warning on truncation.
- Verified: Server line 1041-1043 — truncates to `bufSize - 1` with no return code or log indicating data loss.

6. M-11 follow-up: Config Generator JS does not render dispatch metadata
- Status: [ ]
- Target outcome: `fetchClientConfig()` JS (Server line ~1510) should extract `c.dispatch` and populate `#configStatus` div (line ~1445).
- Context: API-side `GET /api/client?uid=` returns `dispatch` object with `pending`, `attempts`, `lastDispatchEpoch`, `lastAckEpoch`, `lastAckStatus`, `configVersion` (implemented in v1.6.2). JS ignores it — only extracts `c.config`.

7. Other enhancement: Modularization of monolithic ino files
- Status: [ ]
- Target outcome: Split major sketches into maintainable units; evaluate PlatformIO workflow if needed.

---

## Priority 3 - Product Behavior and UX

1. m-1: Stuck tolerance hard-coded at 0.05 inches
- Status: [ ]
- Target outcome: Make tolerance configurable or proportional to sensor range/noise model.

2. m-2: Momentary relay duration precedence is unintuitive with multi-relay masks
- Status: [ ]
- Target outcome: Clarify and/or redesign precedence rules when multiple relay durations differ.

3. Other enhancement: Pump off control implementation
- Status: [ ]
- Target outcome: Implement selected pump-off option set from the design review.

4. Other enhancement: Power state machine test automation
- Status: [ ]
- Target outcome: Execute and automate the documented power-state test scenarios.

---

## Deferred (Planned Later)

1. JSON API builders String-concatenation migration (Server)
- Status: [D]
- Deferred reason: Larger refactor; current risk reduced but not eliminated.

2. 3.3.3: Wire library timeout configuration
- Status: [D]
- Deferred reason: Waiting for 3.2.1 field baseline data.

3. 3.3.5: Opta blueprint disposition housekeeping
- Status: [D]

4. 3.3.6: Integration test suite
- Status: [D]
- Deferred target: v2.0

5. Notecard optimization templates
- Status: [D]
- Deferred reason: Schemas still evolving.

6. M-17: Delayed relay commands can actuate after alarm clear
- Status: [D]
- Deferred reason: Requires cross-device protocol update — epoch timestamp on relay commands + freshness validation on receiving Client. Relay safety timeout is the current backstop.
- Ref: LOGIC_REVIEW_04092026.md (LR-5)

---

## Dropped (Record Only)

These are intentionally not planned unless strategy changes.

1. 3.3.1: I2C mutex guard (dual-core path not planned)
2. 3.3.2: I2C address conflict detection (superseded)
3. 3.3.4: Flash-backed I2C log (superseded)

---

## Suggested Next Execution Slice

1. Implement 3.2.1 timing telemetry and use results to close M-4 with elapsed-time thresholds.
2. Close low-risk codebase consistency items: C1, C2, E2, E3, A6, m-11.
3. Implement m-12 truncation signaling and m-9 heartbeat visibility.
4. Wire up M-11 Config Generator JS to render dispatch/ACK status.
5. Reassess M-17 with a protocol-versioned relay command timestamp design.
