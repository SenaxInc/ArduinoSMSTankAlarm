# Code Review - 2026-04-02

## Scope
- Reviewed server-side request handling, authentication/session flow, and JSON API serialization.
- Primary file reviewed: `TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino`.

## Findings (ordered by severity)

### 1. High - First-login PIN path accepts any 4-character string, not only digits
Evidence:
- `TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:5950`
- `TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:5953`

Why this is a problem:
- The comment says "valid 4-digit PIN", but the implementation only checks `strlen(pin) == 4` on first-time setup.
- Values like `abcd` are accepted and persisted, which weakens the intended authentication model and creates inconsistent behavior with later digit-only validation (`isValidPin`).

Recommendation:
- Replace `strlen(pin) == 4` with `isValidPin(pin)` in the first-login branch.
- Add a regression test for first-login PIN setup rejecting non-digit values.

### 2. Medium - Multiple JSON responses are manually string-built without escaping
Evidence:
- `TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:13294`
- `TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:13319`
- `TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:13402`
- `TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:13586`

Why this is a problem:
- Fields like site names, labels, error strings, and client UIDs can contain quotes, backslashes, or control characters.
- Manual concatenation can produce malformed JSON and break clients.

Recommendation:
- Build these responses using `JsonDocument` + `serializeJson()` instead of concatenating `String` fragments.

### 3. Medium - Authentication lockout counters are global process state
Evidence:
- `TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:718`
- `TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:1159`
- `TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:1193`
- `TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:5935`

Why this is a problem:
- Failed attempts from one source affect all users/sessions because lockout state is shared globally.
- This enables easy login denial-of-service on the LAN by repeatedly submitting bad PINs.

Recommendation:
- Track lockout per source (for example by remote IP or session fingerprint), or at minimum isolate login lockout from already-authenticated API operations.

### 4. Low - Debug mutation endpoint is active in normal routing
Evidence:
- `TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:6242`
- `TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:13368`

Why this is a problem:
- `/api/debug/sensors` includes a mutating dedup action.
- Even though session-protected, debug-only mutation APIs increase operational risk in production builds.

Recommendation:
- Guard route registration and handler body with a compile-time debug flag, or require explicit runtime enablement in server settings.

## Suggested Tests
- First-login rejects non-digit PINs (`abcd`, `12a4`, empty).
- JSON endpoints remain valid when labels/site names contain `"`, `\\`, and newline characters.
- Failed logins from one source do not lock out active sessions from another source.
