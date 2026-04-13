# Logic Review - April 13, 2026 (GitHub Copilot GPT-5.3-Codex)

## Scope

Targeted behavioral/state-machine review across Server, Client, Viewer, and Common, focusing on message processing contracts, notification decision logic, auth flow trust boundaries, and configuration mutation semantics.

## Summary

- Net-new logic findings: 4
- Highest-risk logic flaw: inbound note processing can acknowledge (delete) work that was never successfully parsed.
- Major policy regression: sensor alarm SMS can bypass client intent gate.

## Findings

### LR-C1: Inbound message processing violates acknowledge-after-success contract

- **Component:** Server
- **File:** `TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino`
- **Location:** `processNotefile()` around lines 8286-8301.
- **Severity:** Critical

**Behavior**

The consume (`delete:true`) step is always executed after read, even if JSON deserialization fails.

**Consequence**

- Message durability is reduced: malformed/transiently-unparseable notes are dropped permanently.
- The system reports progress (`processed++`) without successful state transition.

**Recommendation**

- Make consume conditional on successful parse and successful handler execution.
- Track retry count and quarantine poison notes after bounded attempts.

### LR-H1: Alarm SMS decision logic no longer enforces dual-gate policy

- **Component:** Server/Client policy contract
- **File:** `TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino`
- **Location:** `handleAlarm()` around lines 8654-8679.
- **Severity:** High

**Behavior**

`smsEnabled` is initialized from client intent (`se`) but then force-set to `true` for sensor alarm categories before final send gating.

**Consequence**

- Per-monitor client opt-out can be bypassed.
- Notification behavior diverges from the documented "client intent AND server policy" contract.

**Recommendation**

- Preserve `clientWantsSms` independent of alarm type.
- Use `clientWantsSms && smsAllowedByServer` as the final decision.

### LR-H2: Post-login redirect crosses trust boundary without validation

- **Component:** Server web auth flow
- **File:** `TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino`
- **Location:** `LOGIN_HTML` around line 1611.
- **Severity:** High

**Behavior**

After successful login, browser navigation directly uses query parameter `redirect`.

**Consequence**

- External redirect/phishing flow can be chained to successful local authentication.
- User trust boundary between auth and post-auth navigation is not enforced.

**Recommendation**

- Restrict redirect target to vetted in-app relative paths.
- Reject external/protocol-form targets and default to `/`.

### LR-M1: Configuration mutation semantics differ by ingestion path

- **Component:** Server
- **File:** `TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino`
- **Location:** `handleConfigPost()` around lines 7297-7330.
- **Severity:** Moderate

**Behavior**

POST-time updates use truthy checks, so explicit `false`/`0` values are skipped. Load-time parsing uses explicit type checks.

**Consequence**

- Same schema has different meaning depending on mutation path.
- Operators may see successful save responses while runtime config remains unchanged.

**Recommendation**

- Unify mutation semantics with explicit field-presence/type checks in all paths.
- Return normalized/applied values in API response for observability.

## Suggested Validation Matrix

1. Inbound note parsing: malformed note should not be consumed on first failure.
2. Alarm SMS contract: (`se` omitted, server flag true) should not dispatch SMS.
3. Login redirect: external/protocol targets should be rejected to `/`.
4. Config POST: `false` booleans and `0` numeric fields should apply and persist.

## Outcome

- No new relay state-machine safety regressions were found in this pass.
- The highest-value follow-up is to harden server ingest and server auth/config decision boundaries.