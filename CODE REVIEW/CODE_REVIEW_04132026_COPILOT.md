# Code Review - April 13, 2026 (GitHub Copilot GPT-5.3-Codex)

## Scope

Targeted pass across Server, Client, Viewer, and Common with emphasis on net-new errors and behavioral pitfalls not fully captured in current TODO tracking. Focus areas were message-ingest durability, auth redirect handling, alarm notification policy, and config parsing semantics.

## Summary

- New findings: 4
- Highest-priority finding: Server inbound note consumption still deletes payloads after JSON parse failure.
- Additional high-risk findings: login redirect trust boundary and sensor alarm SMS intent regression.

## Findings

### CR-H1: Server consumes notes even when payload parse fails

- **Component:** Server
- **File:** `TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino`
- **Location:** `processNotefile()` around lines 8286-8301.
- **Severity:** High

**Problem**

`processNotefile()` peeks a note, attempts deserialization, then unconditionally sends a second `note.get` with `delete:true`:

```cpp
DeserializationError err = deserializeJson(doc, json);
...
// Consume the note now that it has been processed
JAddBoolToObject(delReq, "delete", true);
```

The consume step runs even when `deserializeJson()` fails.

**Impact**

- Transient parse failures (heap pressure, malformed/partial payload, schema drift) cause permanent message loss.
- This weakens the intended at-least-once delivery pattern introduced by two-step get/delete.

**Recommendation**

- Only consume notes after successful parse and successful handler execution.
- Add bounded retry or quarantine behavior for poison notes.
- Emit explicit parse-failure counters for operator visibility.

### CR-H2: Sensor alarm SMS path currently overrides client `se` intent

- **Component:** Server/Client contract
- **File:** `TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino`
- **Location:** `handleAlarm()` around lines 8654-8679.
- **Severity:** High

**Problem**

The handler reads `doc["se"]` first, but then force-sets `smsEnabled=true` for sensor alarm types:

```cpp
bool smsEnabled = false;  // only when client requests
if (doc["se"]) { smsEnabled = doc["se"].as<bool>(); }
...
if (strcmp(type, "high") == 0) {
  smsEnabled = true;  // Sensor alarms always eligible
  smsAllowedByServer = gConfig.smsOnHigh;
}
```

This bypasses per-monitor client preference.

**Impact**

- SMS can be sent even when client omitted `se` (opt-out), increasing notification noise/cost.
- Behavior conflicts with previously documented intent: client intent AND server policy.

**Recommendation**

- Keep separate gates: `clientWantsSms` and `smsAllowedByServer`.
- Require both: `clientWantsSms && smsAllowedByServer`.
- Add regression tests for `se` omitted/false paths.

### CR-H3: Login page trusts unsanitized `redirect` query parameter

- **Component:** Server web UI
- **File:** `TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino`
- **Location:** `LOGIN_HTML` around line 1611.
- **Severity:** High

**Problem**

After successful login, script executes:

```javascript
const params = new URLSearchParams(window.location.search);
window.location.href = params.get("redirect") || "/";
```

No validation enforces same-origin in-app paths.

**Impact**

- Open redirect risk after successful PIN entry.
- Possible scheme abuse (`javascript:`) depending on browser behavior/policy.

**Recommendation**

- Accept only relative paths starting with `/`.
- Reject `//`, absolute URLs, and non-http app-unsafe schemes.
- Fallback to `/` if invalid.

### CR-M1: `handleConfigPost()` ignores explicit `false` and `0` values

- **Component:** Server
- **File:** `TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino`
- **Location:** `handleConfigPost()` around lines 7297-7330.
- **Severity:** Moderate

**Problem**

Assignments are guarded by truthy checks such as:

```cpp
if (serverObj["dailyHour"]) { ... }
if (serverObj["smsOnHigh"]) { ... }
if (ftpObj["enabled"]) { ... }
```

Explicit `0` and `false` evaluate false and are skipped.

**Impact**

- API consumers cannot reliably disable booleans once enabled via this path.
- Midnight/top-of-hour values can be dropped.

**Recommendation**

- Use explicit presence/type checks (`containsKey`, `.is<T>()`) before assignment.
- Reuse the same normalization logic as load-time parsing.

## Improvement Opportunities

- Add a focused test harness for `processNotefile()` error branches (parse fail, handler fail, delete fail).
- Add auth redirect sanitizer utility shared by login/session pages.
- Add CI guard tests for `handleAlarm()` SMS gate contract and `handleConfigPost()` boolean/zero updates.

## Reviewed Areas

- Server: notefile ingest/deletion, auth/login redirect, alarm processing, config POST parsing.
- Client: alarm payload semantics for `se` and raw sensor fields.
- Viewer: summary consume path and request parser hardening cross-check.
- Common: previous fixes cross-validated for regressions.