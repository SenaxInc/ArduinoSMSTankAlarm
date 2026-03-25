# Logic Review: Decision Pathways and Foundations

Date: 2026-03-24  
Author: GitHub Copilot (GPT-5.3-Codex) v1
Scope: Server, Client, Viewer, and shared battery/time utility decision logic

## Review Method
This review focused on decision-making and control-flow foundations rather than style. The audit emphasized:
- Branch conditions and fallback behavior
- Security decisions and trust boundaries
- Timing/scheduling semantics and resilience
- Feedback-loop and oscillation risks
- Assumption validation on external inputs

Primary files reviewed:
- TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino
- TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino
- TankAlarm-112025-Viewer-BluesOpta/TankAlarm-112025-Viewer-BluesOpta.ino
- TankAlarm-112025-Common/src/TankAlarm_Battery.h
- TankAlarm-112025-Common/src/TankAlarm_Utils.h

## Executive Verdict
The codebase shows strong effort in local defensive coding (constant-time PIN compare, stale-alert suppression, per-tank SMS time windows, watchdog-safe sleeping). However, there are high-impact logic weaknesses in security boundary decisions and several medium-impact validation/scheduling assumptions that can produce unsafe behavior or silent data quality degradation.

## Findings (Ordered by Severity)

### Critical 1: Authentication is client-side by convention, not server-enforced as a global policy
Evidence:
- Routing accepts many privileged or sensitive GET pages/APIs without any server-side session/token gate in the dispatcher:
  - TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:4681-4883
- UI pages rely on localStorage checks in browser scripts, but server handlers still return data regardless of browser token state.

Why this is a logic flaw:
- The trust decision is made in the browser, not at the API boundary. Any caller on the LAN can bypass UI logic and call API endpoints directly.

Impact:
- Unauthorized read access to operational and potentially sensitive data.
- Undermines all front-end login assumptions.

Recommendation:
- Introduce a server-side auth gate in `handleWebRequests()` for all protected routes.
- Require either signed session cookie/token or mandatory PIN for sensitive GET routes.
- Treat client-side localStorage checks as UX only, never as authorization.

---

### Critical 2: Sensitive data APIs are exposed without PIN authorization
Evidence:
- Raw client config fetch does not require PIN:
  - TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:7204-7227
- Location endpoint returns cached GPS coordinates without PIN:
  - TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:10282-10345

Why this is a logic flaw:
- The decision model treats these as read-only convenience APIs, but they expose high-value data (configuration internals and geolocation).

Impact:
- Data exfiltration risk on any reachable network segment.

Recommendation:
- Add `requireValidPin(...)` or authenticated session validation to both handlers.
- Optionally split “public summary” and “sensitive detail” APIs.

---

### High 1: Global auth failure counters create a lockout feedback loop across all users
Evidence:
- Global counters (`gAuthFailureCount`, `gNextAllowedAuthTime`) are shared and updated for all attempts:
  - TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:909-966
- Login path also consumes the same global limiter:
  - TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:4622-4658

Why this is a logic flaw:
- A single noisy or malicious source can lock out all legitimate operators (cross-user coupling).

Impact:
- Operational denial-of-service on administration workflows.

Recommendation:
- Track rate-limits per source (IP/subnet or per-session key).
- Keep a separate global circuit-breaker only for extreme abuse, with shorter TTL.

---

### High 2: Staging-period control endpoints allow state changes before PIN setup
Evidence:
- `handlePausePost` and `handleRelayClearPost` only require PIN if one is already valid/configured:
  - TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:5905-5945

Why this is a logic flaw:
- During commissioning (PIN unset), control decisions are effectively open on the LAN.

Impact:
- Unauthorized pause/relay-clear in the highest-risk period (initial deployment).

Recommendation:
- Enforce a first-boot setup mode that blocks all control endpoints until PIN is set.
- Return explicit “PIN setup required” error for every privileged route.

---

### High 3: Orphaned `.tmp` recovery assumes file completeness without integrity proof
Evidence:
- Recovery promotes `.tmp` to live file if target missing, with no checksum/version validation:
  - TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:2368-2411
  - TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino:1119-1155

Why this is a logic flaw:
- The branch “tmp exists + target missing => safe promote” assumes write completed, which may be false under crash/power-loss timing.

Impact:
- Corrupt partial config can become authoritative state.

Recommendation:
- Add write footer/CRC/version and validate before promotion.
- If validation fails, keep safe defaults and emit a recovery fault record.

---

### Medium 1: Viewer schedule accepts remote cadence inputs without range constraints
Evidence:
- `gSourceRefreshSeconds` and `gSourceBaseHour` are loaded from summary payload without clamping:
  - TankAlarm-112025-Viewer-BluesOpta/TankAlarm-112025-Viewer-BluesOpta.ino:601-613
- Scheduling uses these values directly:
  - TankAlarm-112025-Viewer-BluesOpta/TankAlarm-112025-Viewer-BluesOpta.ino:288-292

Why this is a logic flaw:
- If malformed or adversarial summary sets extreme values, scheduling can become too aggressive (flood) or ineffective (stale forever).

Impact:
- Timing instability and potential network load spikes.

Recommendation:
- Clamp `refreshSeconds` to a safe envelope (for example 300..86400).
- Clamp `baseHour` to 0..23.
- Fall back to defaults on any invalid source value.

---

### Medium 2: Voltage source loss forces immediate NORMAL state without transition semantics
Evidence:
- In `updatePowerState()`, if effective voltage is unavailable, state is hard-set to NORMAL and returned:
  - TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino:4278-4283

Why this is a logic flaw:
- “No measurement” is treated as “healthy power.” This conflates unknown with good state.

Impact:
- False recovery behavior, possible oscillation/chatter when telemetry intermittently drops.
- Missed/incorrect state-change notifications.

Recommendation:
- Introduce an explicit UNKNOWN power state or retain last known state under sensor unavailability with timeout logic.
- Emit a dedicated telemetry event for power-sensor-loss.

---

### Medium 3: Calibration accepts unbounded verified level values
Evidence:
- `verifiedLevelInches` is required but not range-validated:
  - TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:9888-9900

Why this is a logic flaw:
- Human-entry or API errors can inject nonphysical values and contaminate regression basis.

Impact:
- Distorted slope/offset estimation and degraded level prediction quality.

Recommendation:
- Validate against plausible physical bounds (for example >= 0 and <= configured tank max + margin).
- Reject or quarantine outlier entries.

---

### Medium 4: Calibration temperature values are accepted without sanity bounds
Evidence:
- Temperature is fetched and persisted for regression, but no explicit bounds check before save/use:
  - TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:9934-9969
  - Regression uses provided temps directly in model fit:
  - TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:9300-9435

Why this is a logic flaw:
- Upstream/weather parse anomalies can become high-leverage regression points.

Impact:
- Corrupted temperature coefficients and weaker predictive stability.

Recommendation:
- Enforce hard bounds (for example -50F..140F) and optional site-specific plausible ranges.
- Exclude outliers from fitting and log rejection reason.

---

### Medium 5: `publishNote` hard cap may silently drop near-limit payloads
Evidence:
- Fixed local serialization buffer size is 1024 bytes:
  - TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino:4535-4540
- Daily report logic allows near-1KB payload construction (`DAILY_NOTE_PAYLOAD_LIMIT + 48`):
  - TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino:585, 4436-4444

Why this is a logic flaw:
- Capacity checks occur in multiple places with different limits; edge payloads can fail serialization and return silently.

Impact:
- Non-obvious data loss when payload shape grows (extra fields, future schema changes).

Recommendation:
- Replace fixed buffer with `measureJson()`-driven allocation or bounded chunking strategy.
- Log explicit error when serialization is truncated or skipped.

## Feedback-Loop and Assumption Risks Summary
- Global auth lockout loop can be externally induced and affects all users.
- Power-state unknown-data branch maps to NORMAL, which can create false recovery loops.
- Viewer cadence depends on unvalidated remote schedule metadata.
- Atomic recovery assumes write completeness without proof.

## What Is Well-Founded
- Constant-time PIN comparison is correctly implemented:
  - TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:857-877
- Server stale-client alert suppression uses `staleAlertSent` state correctly:
  - TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:7667-7685
- Per-tank SMS rate limiting cleans one-hour windows and enforces min interval:
  - TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:6882-6935
- Client power-state hysteresis with debounce is structurally robust when voltage is valid:
  - TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino:4271-4345
- Watchdog-safe chunked sleeping in long low-power periods is sound:
  - TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino:1030-1081

## Suggested Prioritization
1. Implement server-side authorization gate for protected routes and sensitive GET APIs.
2. Separate auth throttling by source identity to remove global lockout coupling.
3. Harden persistence recovery with checksum/version validation.
4. Add input clamps and plausibility checks for scheduler and calibration pathways.
5. Remove fixed 1KB publish bottleneck for future-safe payload handling.
