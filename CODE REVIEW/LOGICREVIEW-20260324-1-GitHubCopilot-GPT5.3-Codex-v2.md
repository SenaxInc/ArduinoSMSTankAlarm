# Logic Review: Decision-Making Functions (Post-Pull Re-Run)

Date: 2026-03-24
Author: GitHub Copilot (GPT-5.3-Codex) v2
Scope: Post-pull re-review of decision logic in Server, Client, Viewer, and shared scheduling/power foundations

## Method
This re-review was performed from source evidence in the current pulled state, with focus on:
- Security boundary decisions (auth/session/PIN/rate limiting)
- State transition logic (power, stale detection, recovery)
- Scheduling/cadence decisions
- Persistence and crash-recovery assumptions
- Input validation for calibration and control paths

Primary files reviewed:
- TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino
- TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino
- TankAlarm-112025-Viewer-BluesOpta/TankAlarm-112025-Viewer-BluesOpta.ino
- TankAlarm-112025-Common/src/TankAlarm_Utils.h

## Executive Verdict
The latest pull materially improved authorization posture by adding server-side API session middleware and session-aware admin checks. Remaining risks are now concentrated in resilience and data-quality decisions: global auth lockout coupling, atomic recovery assumptions, unbounded viewer cadence intake, and calibration-input validation gaps.

## Findings (Ordered by Severity)

### High 1: Global auth backoff/lockout is shared across all users/sources
Evidence:
- Global counters and global next-allowed timestamp are used for all auth failures: `gAuthFailureCount`, `gNextAllowedAuthTime`, `gLastAuthFailureTime`.
  - TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:1170-1220
- Rate limiter is consulted by protected auth paths (`requireValidPin`, `requireExplicitPinMatch`, login flow):
  - TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:1222-1266
  - TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:5931-5980

Why this is a logic flaw:
- One noisy/malicious source can lock out legitimate operators because throttling state is globally coupled.

Impact:
- Administrative denial-of-service risk during incident response.

Recommendation:
- Rate-limit per source key (IP/session/client fingerprint) and keep a separate short-lived global circuit breaker only for extreme abuse.

---

### High 2: Atomic `.tmp` recovery can promote incomplete writes without integrity validation
Evidence:
- On boot, if target is missing and `.tmp` exists, code renames `.tmp` to live file directly.
  - Client: TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino:1865-1913
- No CRC/version/footer validation is performed in this recovery branch prior to promotion.

Why this is a logic flaw:
- The branch assumes that rename interruption happened after a complete tmp write; power-loss timing can violate this assumption.

Impact:
- Partial/corrupt configuration or queue files may become authoritative state.

Recommendation:
- Add an integrity marker (CRC + version + complete-write footer) and validate before rename promotion; otherwise quarantine and revert to safe defaults.

---

### Medium 1: Viewer accepts remote cadence metadata without upper/lower bounds
Evidence:
- Viewer ingests cadence fields directly from summary payload (`rs`/`refreshSeconds`, `bh`/`baseHour`), only defaulting when `refreshSeconds == 0`.
  - TankAlarm-112025-Viewer-BluesOpta/TankAlarm-112025-Viewer-BluesOpta.ino:821-842
- Scheduler uses these values directly.
  - TankAlarm-112025-Viewer-BluesOpta/TankAlarm-112025-Viewer-BluesOpta.ino:414-419

Why this is a logic flaw:
- Extreme or malformed values can drive pathological fetch cadence (too fast or effectively never) and degrade data freshness/load behavior.

Impact:
- Potential polling flood or stale dashboard behavior.

Recommendation:
- Clamp `refreshSeconds` and `baseHour` to safe ranges (for example, `300..86400`, `0..23`) and fall back on defaults when out-of-range.

---

### Medium 2: Calibration API accepts unbounded `verifiedLevelInches`
Evidence:
- API requires presence of `verifiedLevelInches` but does not validate physical range before saving.
  - TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:12916-12924
  - TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:12972-12973
- Regression pipeline later consumes logged points.
  - TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:12140-12445

Why this is a logic flaw:
- Human/API entry mistakes (negative or implausibly large values) can poison learned calibration.

Impact:
- Distorted slope/offset coefficients and degraded prediction quality.

Recommendation:
- Validate against monitor-specific physical bounds (>= 0 and <= configured max + margin), reject or quarantine outliers, and log the rejection reason.

---

### Medium 3: Config ACK is sent immediately after in-memory apply, before persistence outcome is known
Evidence:
- Config note path: `applyConfigUpdate(doc)` then immediate `sendConfigAck(true, ...)`.
  - TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino:3403-3405
- Persistence occurs later in loop via `persistConfigIfDirty()`.
  - TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino:1718
  - TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino:3714-3738

Why this is a logic flaw:
- ACK semantics currently mean "applied in RAM" not "durably committed." If storage is unavailable, server may record success that does not survive reboot.

Impact:
- Control-plane observability mismatch and potential config reversion surprises.

Recommendation:
- Include persistence status in ACK (`applied_volatile` vs `applied_persisted`), or defer success ACK until persistence completes.

---

### Medium 4: Fixed 1024-byte publish buffer is a hard drop point for payload growth
Evidence:
- `publishNote` serializes into static `char buffer[1024]` and aborts on overflow.
  - TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino:6136-6143

Why this is a logic flaw:
- Schema evolution and optional fields can exceed fixed capacity; failure path drops outbound message.

Impact:
- Silent telemetry/alert loss at edge sizes.

Recommendation:
- Precompute size with `measureJson()` and allocate bounded dynamic buffer (or chunk/trim strategy) with explicit error telemetry.

## Confirmed Improvements in the Pulled Version
1. Server-side session middleware now gates `/api/*` routes (excluding login/session-check).
   - TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:6016-6025
2. `requireValidPin` is now session-aware and no longer relies on repeated PIN entry after session validation.
   - TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:1222-1244
3. Explicit PIN match path is separated for sensitive PIN-change semantics.
   - TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:1247-1266

These changes close major auth-boundary concerns identified in the earlier cycle.

## What Is Well-Founded
- Constant-time session token comparison is implemented.
  - TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:778-789
- API middleware is clear and centralized in request dispatch.
  - TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:6016-6025
- Power-state transitions include hysteresis and debounce, reducing chatter risk.
  - TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino:5650-5752

## Prioritized Remediation Sequence
1. Decouple auth rate limiting by source to remove cross-user lockout behavior.
2. Add integrity validation to `.tmp` recovery before promotion.
3. Clamp viewer cadence inputs and add defensive defaults.
4. Harden calibration input validation (physical bounds + outlier handling).
5. Align config ACK semantics with persistence outcome.
6. Replace fixed publish buffer with measured/bounded serialization.

## Conclusion
Post-pull architecture is directionally stronger, especially in authorization logic. The highest remaining decision-quality risks are not in route protection but in shared-state throttling, persistence assumptions, and unchecked operational inputs that can silently degrade reliability over time.
