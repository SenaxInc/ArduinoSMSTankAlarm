# Code Review Report — 2026-01-07

Repository: `ArduinoSMSTankAlarm` (branch: `master`)

## Scope
Reviewed the primary 112025 generation sketches and shared headers:

- `TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino`
- `TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino`
- `TankAlarm-112025-Viewer-BluesOpta/TankAlarm-112025-Viewer-BluesOpta.ino`
- `TankAlarm-112025-Common/src/TankAlarm_Common.h`
- `TankAlarm-112025-Common/src/TankAlarm_Notecard.h`
- `TankAlarm-112025-Common/src/TankAlarm_Platform.h`
- `TankAlarm-112025-Common/src/TankAlarm_Utils.h`

This review is focused on correctness, resilience (long-lived uptime), security of the LAN-facing server UI/API, and embedded constraints (RAM/flash, heap fragmentation, blocking calls).

## Executive Summary
The project is thoughtfully structured for a constrained embedded environment: strong use of fixed-size buffers, a shared “common” header set, configurable notefile names, and attention to watchdog/filesystem portability.

The highest-risk items are:

1. **Potential RAM fragmentation / exhaustion** from heavy `String` usage and building large HTML/CSV/JSON payloads in-memory on the server.
2. **Filesystem initialization failure modes** that permanently halt the client (and likely server) in `while(true)` loops.
3. **Authentication hardening**: the admin PIN control is a good start, but the 4‑digit PIN plus lack of rate limiting and LAN plaintext transport are weak against a local attacker.

None of these are “one-line fixes,” but addressing them will meaningfully improve long-term stability and field robustness.

## What’s Working Well
- **Shared platform abstractions**: `TankAlarm_Platform.h` and `TankAlarm_Notecard.h` provide consistent helpers across Opta/Mbed and STM32, with wrap-safe `millis()` deltas.
- **Clear config defaults and backward compatibility**: client config loader supports `tanks` vs `monitors`, and old vs new field names.
- **Good operational features**: server dashboards, client console, serial log collection, calibration system, and viewer mode demonstrate practical maintenance awareness.
- **Reasonable guardrails**: `MAX_HTTP_BODY_BYTES` exists (server + viewer), and the viewer explicitly caps request bodies.

## Findings (Prioritized)

### Critical (Stability / Data Loss)

#### C1) Server: building large responses with `String` risks heap fragmentation and crashes over time
**Where:** `TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino`

- Server handlers allocate and concatenate large `String` instances (HTML pages are massive constants; JSON responses and CSV downloads are built as `String`).
- Embedded heaps (even on STM32H7) can fragment under repeated `String` growth/copy. Over weeks/months, this commonly becomes “random resets” or allocation failures.

**Recommendations:**
- Prefer **streaming responses** rather than building them fully in RAM.
  - For PROGMEM HTML: send in chunks (or use `client.write_P()` style patterns where available).
  - For CSV downloads: write rows directly to the socket.
  - For JSON: use ArduinoJson’s `serializeJson(doc, client)` to stream.
- Avoid repeated `String += ...` in loops; if you must keep `String`, `reserve()` upfront based on an upper bound.

#### C2) Client: filesystem initialization failures hard-halt the device
**Where:** `TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino`

`initializeStorage()` enters `while (true) { delay(1000); }` on format/init failure.

In the field, flash corruption or an unexpected block device state could permanently brick the unit until physical intervention.

**Recommendations:**
- Prefer a **degraded mode**: run with default config in RAM + periodic retry of filesystem mount.
- If you do want a hard fail, consider a watchdog-triggered reboot loop with backoff, and a clear serial/telemetry “storage failed” status.

#### C3) ArduinoJson document sizes are fixed and may be insufficient for real-world configs
**Where:**
- Client `loadConfigFromFlash()` uses `DynamicJsonDocument(4096)` even when file size can be up to `8192` bytes.
- Client `saveConfigToFlash()` uses `DynamicJsonDocument(4096)`.

If customers add fields/tanks/monitors, the JSON doc may exceed 4096, causing deserialization failures or truncated output.

**Recommendations:**
- Compute capacity based on expected schema (ArduinoJson Assistant) or file size:
  - Allocate `DynamicJsonDocument(fileSize + overhead)` as a pragmatic approach.
- If RAM is a concern, switch to **streaming parsing** when possible (deserialize directly from `File`/`Stream`), but you still need sufficient capacity for the DOM.

### High (Security / Robustness)

#### H1) Admin PIN model is weak without rate limiting and transport protections
**Where:** server PIN functions and web UI.

Current posture:
- PIN is **4 digits**.
- No visible rate limiting / lockout on `/api/pin` and other pin-protected endpoints.
- Typical deployment appears to be LAN HTTP (no TLS). Anyone on the LAN can sniff the PIN during use, and brute force is feasible.

**Recommendations (incremental, compatible):**
- Implement **server-side rate limiting** for PIN attempts (per-IP + global), with exponential backoff.
- Add a lockout window after N failed attempts.
- Consider longer secrets (6–8 digits) or a stronger auth token.
- If TLS isn’t feasible on-device, document the requirement: isolate on trusted LAN/VLAN.

#### H2) `pinMatches` compares up to `sizeof(configPin)` not exact length
**Where:** `pinMatches()` uses `strncmp(pin, gConfig.configPin, sizeof(gConfig.configPin))`.

It works if `configPin` is always cleanly NUL-terminated, but it’s a slightly surprising compare for a “4-digit PIN.”

**Recommendation:**
- Validate stored PIN is exactly 4 digits at save-time and compare with `strcmp()` (or compare first 4 bytes + ensure NUL).

#### H3) Hard-coded default MAC addresses can collide
**Where:** Server and Viewer define static default MACs.

If multiple devices are on the same LAN with the same MAC, it will cause network instability.

**Recommendations:**
- Encourage users to set per-device MACs via config.
- Or derive a pseudo-unique MAC from a device unique ID (chip ID / notecard UUID hash) while preserving the locally-administered bit.

### Medium (Reliability / Maintainability)

#### M1) Watchdog usage is present but failure paths may prevent useful recovery
Client/server both have watchdog helpers, but some hard loops (`while(true)` with `delay`) prevent the device from recovering gracefully.

**Recommendation:**
- In fatal error loops, ensure watchdog is enabled and not kicked, so the device reboots.

#### M2) Mixed filesystem paths and duplicated constants
There are several filesystem path constants in multiple sketches (`CLIENT_CONFIG_PATH`, notefile names, `POSIX_FS_PREFIX`).

**Recommendation:**
- Consolidate more of these into `TankAlarm_Common.h` to avoid drift.

#### M3) Large HTML templates embedded in `.ino` reduce maintainability
The server sketch contains many large pages inline. This is functional, but hard to review and error-prone.

**Recommendation:**
- Consider moving HTML to separate header files (still PROGMEM), one per page.
- Add a small build step (optional) if you ever want minification/versioning, but keep it simple if you prefer Arduino-only builds.

### Low (Polish / Small improvements)

#### L1) Prefer consistent naming: “tanks” vs “monitors”
The client supports both `tanks` and `monitors`, which is great for compatibility, but increases cognitive load.

**Recommendation:**
- Pick one canonical term for new docs and server-generated configs (and keep backward compatibility in parsing).

#### L2) Error reporting could be surfaced via Notecard
Some errors are printed to Serial only.

**Recommendation:**
- For field support, consider emitting a “health/status” note on severe failures (storage init fail, config parse fail, etc.).

## Suggested Next Steps (Practical Order)
1. **Server streaming responses**: remove big `String` concatenation hot paths (JSON/CSV first).
2. **Client FS failure behavior**: switch from permanent halt → degraded mode + retry.
3. **Right-size JSON documents**: derive doc capacity from schema/file size.
4. **PIN hardening**: rate limit + longer PIN option.
5. **MAC uniqueness**: require or auto-generate unique MAC by default.

## Appendix: Notes on Viewer
The viewer is appropriately scoped to read-only behavior and caps request bodies. This is a good design choice for deployments where you want visibility without exposing administrative endpoints.
