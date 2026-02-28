# Code Review — 2026-02-28 (Copilot)

## Scope Reviewed
- `TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino`
- `TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino` (targeted architecture/pass only)
- `TankAlarm-112025-Viewer-BluesOpta/TankAlarm-112025-Viewer-BluesOpta.ino`
- `TankAlarm-112025-Common/src/TankAlarm_Common.h`
- `TankAlarm-112025-Common/src/TankAlarm_Utils.h`

---

## Executive Summary
The codebase shows strong operational hardening in several areas (watchdog integration, I2C recovery/backoff, payload caps, and many PIN checks on mutating endpoints). 

The highest-risk findings are:
1. **Unauthenticated GET routes exposing operational and PII data** on the server.
2. **PIN handling model uses browser `localStorage` as bearer auth**, increasing credential exposure risk.
3. **Potential buffer overflow in FTP response parsing** (`strcat` bound check off-by-one + `maxLen==0` unsafe path).
4. **HTTP body read loop can block indefinitely** if client stalls after headers (server + viewer).

---

## Findings

### 1) Missing server-side auth guard on many GET routes (High)
**Where**
- `handleWebRequests()` route table around lines `5666+` in server sketch.
- Examples include unauthenticated:
  - `/api/clients`, `/api/tanks`, `/api/history`, `/api/contacts`, `/api/serial-logs`, `/api/transmission-log`, `/api/location?...`
  - HTML pages (`/`, `/server-settings`, `/contacts`, etc.) are also served without server auth checks.

**Why this matters**
- Frontend checks `localStorage` before rendering, but this is **not enforcement**.
- Any host on LAN can call these HTTP endpoints directly and obtain fleet telemetry, contact info, and serial output.

**Recommendation**
- Add a centralized auth gate in `handleWebRequests()` for all sensitive GET routes.
- Keep a narrow allowlist unauthenticated (e.g., `/login`, `/style.css`, maybe health ping).
- Return `401`/`403` from server for unauthorized requests.

---

### 2) PIN is persisted and reused as a bearer token in browser localStorage (High)
**Where**
- Login flow stores PIN directly in localStorage (embedded login page script around lines `1456+`).
- Multiple UIs send `{ pin: token }` in API request bodies (several embedded page scripts).

**Why this matters**
- Any XSS or compromised browser context exposes long-lived admin PIN.
- Shared kiosk/browser scenarios can leak admin PIN between users.
- PIN becomes de facto API credential reused broadly.

**Recommendation**
- Replace PIN-in-localStorage with short-lived server session token (HttpOnly cookie preferred).
- Keep PIN usage only at login/challenge boundaries.
- If cookies are not feasible, use short-expiry nonce/session IDs and rotate frequently.

---

### 3) FTP control-channel response accumulation uses unsafe `strcat` pattern (High)
**Where**
- `ftpReadResponse(...)` around lines `3260–3305`.

**Issue details**
- Bound check uses `if (needed <= maxLen)` then appends with `strcat`. 
- `needed` includes terminating NUL, so `needed == maxLen` is already full and append can overrun by one.
- Function initializes `message[0]` only when `maxLen > 0`, but still calls `strlen(message)` unconditionally, which is UB when `maxLen == 0`.

**Recommendation**
- Replace `strcat` flow with explicit bounded append (`strlcat`-style helper or manual indexed append).
- Require `maxLen > 0` early and return false otherwise.
- Use `< maxLen` safety logic for writable capacity, not `<=`.

---

### 4) HTTP body-read loop can hang indefinitely after headers (Medium-High)
**Where**
- Server: `readHttpRequest(...)` around lines `6035+`.
- Viewer: `readHttpRequest(...)` around lines `530+`.

**Issue details**
- Header parse has a timeout.
- Body read loop (`while (readBytes < contentLength && client.connected())`) has **no timeout/backoff exit** if peer keeps TCP open but stops sending body bytes.

**Impact**
- A slow/stalled client can tie up loop execution and degrade responsiveness (DoS vector on LAN).

**Recommendation**
- Add body-phase timeout (e.g., last-byte deadline) with `safeSleep(1)` in no-data path.
- Abort with `408` or `400` on body timeout.

---

### 5) First-login behavior can leave system effectively open until PIN is set (Medium)
**Where**
- `handleLoginPost(...)` near lines `5620–5660`.

**Issue details**
- If no configured PIN, logic currently accepts blank/any PIN and can return success without immediately setting an admin PIN.

**Impact**
- On fresh install, unauthorized user on LAN can access interface before owner sets PIN.

**Recommendation**
- Force atomic first-run enrollment: require valid 4-digit PIN in first login call and persist immediately.
- Return explicit onboarding error if PIN absent/invalid.

---

## Positive Notes
- Good use of watchdog-safe sleep chunking and regular kick strategy.
- Good Notecard health backoff/recovery design.
- Good payload-size checks in route dispatch.
- Frequent use of `strlcpy` and fixed-size buffers in many paths.

---

## Suggested Fix Order
1. Add server-side auth middleware/guard for GET and sensitive pages.
2. Move away from PIN-as-token (`localStorage`) to session credentials.
3. Patch `ftpReadResponse` append logic.
4. Add body-read timeout in both server/viewer HTTP parsers.
5. Tighten first-login PIN enrollment flow.

---

## Optional Follow-up
If helpful, I can provide a patch set for items 3 and 4 first (lowest blast radius), then a second patch for auth/session hardening.