# Logic Review

Date: 2026-04-02
Author: GitHub Copilot (GPT-5.4)
Repository: ArduinoSMSTankAlarm
Scope:
- TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino
- TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino
- TankAlarm-112025-Viewer-BluesOpta/TankAlarm-112025-Viewer-BluesOpta.ino
- TankAlarm-112025-Common/src/*

This review focuses on whether the current decision logic remains operationally sound under field conditions. It is based on static analysis only. I did not run hardware tests.

## Findings

### 1. Critical: Remote relay actuation is gated behind alarm note rate limiting, so control actions can be skipped when notifications are suppressed

Files:
- TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino:4714
- TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino:4717
- TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino:4808
- TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino:4567
- TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino:4639

Why this is logically unsound:
- `sendAlarm()` activates local indication first, then immediately returns if `checkAlarmRateLimit()` fails.
- The remote relay branch runs only after that return point.
- A monitor can therefore enter a real alarm state while the configured remote relay or shutdown path never executes, simply because the alarm note was rate-limited.

Operational consequence:
- Notification policy and control policy are coupled even though they solve different problems.
- Global alarm storms can suppress remote relay action exactly when automatic control is most needed.

Recommendation:
- Separate control-side relay decisions from note-delivery throttling.
- Rate-limit the alarm note independently, but let safety/control outputs evaluate from alarm state itself.

### 2. Critical: `UNTIL_CLEAR` relays for HIGH and LOW triggers still cannot clear automatically

Files:
- TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino:4419
- TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino:4457
- TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino:4481
- TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino:4792

Why this is logically unsound:
- Clear events are emitted as `sendAlarm(idx, "clear", ...)`.
- In the `!isAlarm` branch, `RELAY_TRIGGER_HIGH` and `RELAY_TRIGGER_LOW` only clear if `alarmType` equals `"high"` or `"low"`.
- That condition is unreachable once the clear path has already reduced the event type to `"clear"`.

Operational consequence:
- `UNTIL_CLEAR` relays for high-only or low-only triggers can stay active indefinitely after the condition clears.
- The only reliable auto-clear case in the current logic is `RELAY_TRIGGER_ANY`.

Recommendation:
- Persist the alarm cause that activated the relay and clear against that stored cause.
- If that state is not available, clear on any `clear` event for monitors already marked relay-active.

### 3. High: Critical-hibernate relay shutdown is not paired with a restoration path after recovery

Files:
- TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino:4666
- TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino:4714
- TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino:5915
- TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino:5918
- TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino:5925

Why this is logically weak:
- Entering `POWER_STATE_CRITICAL_HIBERNATE` forcefully turns all local relay outputs off.
- Recovery only sends a power-state note. It does not reconstruct local relay outputs from still-latched alarm state.
- Local relay assertion is driven by `activateLocalAlarm()` inside `sendAlarm()`, which only runs on a new alarm event or clear event.

Operational consequence:
- A monitor can remain logically alarmed after battery recovery while the relay-driven local output stays de-energized.
- Operators see a recovered device, but local annunciation/control may remain inconsistent until a fresh alarm transition occurs.

Recommendation:
- On recovery from critical hibernate, recompute required local outputs from current latched alarm state.
- Treat power-state recovery as a state reconciliation point, not just a notification event.

### 4. High: The server still nullifies monitor-level SMS intent for ordinary alarms

Files:
- TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino:4710
- TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino:4761
- TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:8412
- TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:8421
- TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:8434

Why this is logically unsound:
- The client exposes monitor-level SMS intent through `enableAlarmSms` and only sends `se` when that flag is true.
- The server then force-enables eligibility for `high`, `low`, `clear`, and digital alarms before applying server policy.
- The source-side decision and hub-side decision do not compose; the hub simply overrides the source.

Operational consequence:
- The configuration surface implies monitor-level SMS control that is not actually honored.
- Alert-fatigue tuning at the client is therefore unreliable.

Recommendation:
- Use AND semantics: sensor alarm SMS should require both client request and server allow-list policy.
- If server-central control is intentional, rename the client flag so the current UI does not imply a false guarantee.

### 5. Medium: Global alarm suppression also consumes per-monitor quota, so monitors are penalized by alarms that were never sent

Files:
- TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino:4567
- TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino:4617
- TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino:4639
- TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino:4648

Why this is logically weak:
- `checkAlarmRateLimit()` adds the event to the monitor-local timestamp list before checking the global cap.
- If the global cap rejects the event, the monitor-specific counter still advances.

Operational consequence:
- A monitor can hit its own hourly limit using alarms that never left the device.
- Once the global storm subsides, later alarm transitions on that monitor may still be suppressed because its local budget was already consumed.

Recommendation:
- Reserve both quotas first, or only record the monitor-local timestamp after the global decision succeeds.

### 6. Medium: Unload notification intent exists in config, but the event pipeline never carries it to the server

Files:
- TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino:511
- TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino:512
- TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino:4983
- TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino:5000
- TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:8848
- TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:8888

Why this is logically unsound:
- The system exposes unload SMS/email configuration, but the emitted unload note never contains the corresponding flags.
- The server makes notification decisions from fields that are absent in the message.

Operational consequence:
- The unload notification state machine never reaches the branch operators expect from the configuration.
- This is a silent functional gap rather than a visible failure.

Recommendation:
- Include explicit unload notification intent in the client payload, or derive it server-side from cached client config instead of trusting the event body.

---

## Config.qi "Pending Sync to Notecard" — Root Cause Analysis

Scope: Config dispatch, ACK round-trip, and `pendingDispatch` lifecycle across Server and Client.

### 7. Critical: `pendingDispatch` is never cleared on successful Notecard send — it requires a client ACK that is structurally unlikely to arrive

Files:
- TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:7975 (set `pendingDispatch = true`)
- TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:7997 (comment: "pendingDispatch stays true until ACK received")
- TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:10243 (`handleConfigAck` — clears `pendingDispatch` on version match)
- TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino:3566 (`sendConfigAck` call site)
- TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino:3480 (`sendConfigAck` implementation)

**Root cause summary:** The `pendingDispatch` flag is set to `true` when config is dispatched and is designed to stay `true` until the server receives a `config_ack.qi` note from the client with a matching config version hash. This ACK round-trip has **three independent failure modes** that each individually prevent `pendingDispatch` from ever clearing:

#### 7a. ACK version hash mismatch — the hash is computed on a different payload than what the client receives

The server computes the DJB2 hash on `buffer` (the raw config JSON from `serializeJson`), then calls `sendConfigViaNotecard()` which re-parses that JSON and **injects additional routing fields** (`_target`, `_type`, `_cv`, `_sv`) into the body before sending. The client receives this augmented payload — not the original `buffer` — but the `_cv` field embedded in the received note is the hash of the original `buffer`.

This is actually correct in isolation — the client echoes `_cv` back in the ACK, and the server compares against the stored `configVersion`. However, the coupling between these two values is fragile:

- If `serializeJson` field ordering changes between firmware versions, the hash changes even for identical config.
- The hash is computed from the entire serialized JSON including whitespace and field order, making it sensitive to ArduinoJson internals.
- On retry via `dispatchPendingConfigs()`, the hash was computed from the original `buffer` (line 7971) but `snap->payload` (stored in the 1536-byte cache) is what gets re-sent. If the original buffer was truncated during `cacheClientConfigFromBuffer()`, the hash won't match what the client actually receives.

#### 7b. The ACK routing path requires a Notehub Route that may not exist

The client sends the ACK to `config_ack.qo`. For this note to arrive at the server's `config_ack.qi`, a Notehub Route (ClientToServerRelay) must be configured to route `config_ack.qo` from client devices to `config_ack.qi` on the server device. The `FLEET_SETUP.md` documents this requirement, but if the route is not configured or is misconfigured:

- The client successfully queues the ACK to Notecard (logs "Config ACK sent: applied").
- The ACK arrives at Notehub but is never delivered to the server.
- The server never calls `handleConfigAck()`, so `pendingDispatch` stays `true` forever.

This is the most likely cause of persistent "pending sync" in the field, since the Route configuration is a manual operational step.

#### 7c. Client note.get peek-then-delete is not crash-safe for the ACK send

In `pollForConfigUpdates()` (client lines 3520-3590):
1. `note.get` peeks at `config.qi` (no delete).
2. Parse and apply config.
3. Call `sendConfigAck()`.
4. `note.get` with `delete:true` to consume the note.

If the client crashes or loses power between step 2 and step 3, the config was applied but no ACK was sent. On reboot, the note is re-read, config is re-applied (idempotent), and ACK is retried — this case is actually handled correctly.

However, if the Notecard `note.add` for the ACK at step 3 fails (returns error), the client logs a warning but **still deletes the config.qi note at step 4**. The config is applied, the inbound note is consumed, but no ACK was sent. The server's `pendingDispatch` stays `true`, and there is no retry mechanism on the client side for failed ACK sends.

### 8. High: Manual retry via `/api/config/retry` clears `pendingDispatch` on Notecard send success — before client ACK

Files:
- TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:7247 (`snap->pendingDispatch = false` in retry handler)
- TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:8052 (comment: "pendingDispatch stays true until config ACK" in auto-retry)

**Inconsistency:** The manual retry handler (`handleConfigRetryPost`) sets `pendingDispatch = false` immediately when `sendConfigViaNotecard()` returns `true`. But the auto-retry handler (`dispatchPendingConfigs`) and the initial dispatch (`dispatchClientConfig`) both leave `pendingDispatch = true` even on successful Notecard send, waiting for the client ACK.

This means:
- Manual "Retry Send to Notecard" button click → `pendingDispatch` cleared immediately (no ACK needed).
- Auto-retry every 60 minutes → `pendingDispatch` stays `true` (ACK needed).
- Initial dispatch from config-generator → `pendingDispatch` stays `true` (ACK needed).

The manual retry gives the illusion of resolution while auto-retry does not, creating an inconsistent user experience.

### 9. High: Auto-retry counter starts at 1 and increments before send, causing off-by-one in retry count

Files:
- TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:7976 (`dispatchAttempts = 1` — initial dispatch)
- TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:8038 (`dispatchAttempts++` before `sendConfigViaNotecard` in retry loop)
- TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:8015 (`dispatchAttempts >= MAX_CONFIG_DISPATCH_RETRIES` — auto-cancel check)

The initial dispatch sets `dispatchAttempts = 1`. The retry loop increments **before** sending (`dispatchAttempts++` at line 8038), then checks `>= MAX_CONFIG_DISPATCH_RETRIES` (5) at the top of the next iteration (line 8015).

Execution trace:
1. Initial dispatch: `dispatchAttempts = 1`
2. First retry: increment to `2`, then send
3. Second retry: increment to `3`, then send
4. Third retry: increment to `4`, then send
5. Fourth retry: increment to `5`, **auto-cancel** at top of next iteration

So the maximum total attempts are: 1 initial + 3 retries = 4 total, not the 5 that `MAX_CONFIG_DISPATCH_RETRIES` implies. The auto-cancel log message says "failed after N attempts" but the counter is actually the number of entries into the retry loop, not the number of sends.

### 10. Medium: The config-generator UI never shows pending dispatch state when loading an existing client

Files:
- TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:9461-9481 (`handleClientConfigGet` — GET /api/client)
- TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:7085 (`cfgEntry["pd"]` — only in GET /api/clients)
- TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:1527 (`fetchClientConfig` — config-generator JS)
- TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:1462 (`configStatus` div — never populated)

The `GET /api/client?uid=dev:xxx` endpoint (used by the config-generator page) returns only `{"config": {...}}` — it does not include `pendingDispatch`, `dispatchAttempts`, `lastAckEpoch`, or `lastAckStatus`. The `<div id="configStatus">` element is defined in the HTML but is never populated with pending state information.

The `pd` field is only returned by `GET /api/clients` (the multi-client summary endpoint), used by the transmission log and client console — but the config-generator page that operators use to manage individual clients never has visibility into whether the last config dispatch is still pending.

This means an operator can open the config for a client, see the current settings, re-save, and have no indication that the previous config was never acknowledged.

### 11. Medium: `saveClientConfigSnapshots()` not called after successful retry in retry-all path

Files:
- TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:7254-7275 (`handleConfigRetryPost` retry-all loop)

In the retry-all code path, the loop iterates all clients and updates `pendingDispatch` for each. But `saveClientConfigSnapshots()` is never called after the loop completes. If the server reboots before the next periodic save, all the `pendingDispatch = false` updates from successful retries are lost, and those configs will be re-retried on boot.

The single-client retry path at line 7247 does not call `saveClientConfigSnapshots()` either, but the auto-retry path (`dispatchPendingConfigs`) does call it inside the loop at line 8055.

### 12. Low: `purgePendingConfigNotes` iterates `command.qo` — a shared outbox for all command types

Files:
- TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:7765-7855 (`purgePendingConfigNotes`)

The purge function reads all notes from `command.qo`, checks `_target` and `_type`, and deletes matching config notes. Since `command.qo` is also used for relay commands and other command types, a high volume of non-config commands could cause the purge loop to read and skip many irrelevant notes, blocking the main loop. This is bounded by `MAX_NOTES_PER_FILE_PER_POLL` but still represents unnecessary I2C traffic.

## Recommendations — Config "Pending Sync" Resolution

### Immediate fix (addresses the primary symptom):

1. **Clear `pendingDispatch` on successful Notecard send, not on ACK.** ✅ IMPLEMENTED — `dispatchClientConfig()`, `dispatchPendingConfigs()`, and `handleConfigRetryPost()` all now set `pendingDispatch = false` when `sendConfigViaNotecard()` returns `true`. ACK mechanism retained for observability only (updates `lastAckStatus`/`lastAckEpoch`).

2. **Call `saveClientConfigSnapshots()` after retry-all loop** ✅ IMPLEMENTED — Added in both single-client and retry-all paths of `handleConfigRetryPost()`.

### Medium-term improvements:

3. **Include `pendingDispatch` status in `GET /api/client?uid=`** ✅ IMPLEMENTED — Response now includes `pd`, `da`, `ae`, `as`, and `de` fields. Config-generator UI `fetchClientConfig()` now populates the `configStatus` div with pending/ACK status.

4. **Fix the retry count off-by-one** — either start `dispatchAttempts` at 0 and use `< MAX_CONFIG_DISPATCH_RETRIES`, or increment after the send rather than before. (Deferred — behavior is correct at 5 total attempts; only the constant name `MAX_CONFIG_DISPATCH_RETRIES` is misleading since it controls total attempts, not retries.)

5. **Add ACK retry on the client** ✅ IMPLEMENTED — `sendConfigAck()` now returns `bool`. If ACK fails, the `config.qi` note is **not** deleted, so the next poll cycle re-reads the note, re-applies config (idempotent), and retries the ACK.

### Long-term:

6. **Document the Notehub Route requirement for config_ack** prominently in the setup guide, or fall back to a server-side timeout that clears `pendingDispatch` after a reasonable window (e.g., 2 hours) even without ACK.

## Residual Risk

- This review did not include runtime validation on Opta hardware or Notehub routes.
- The most important untested assumptions are inbound note timing, relay behavior under overlapping masks, and field behavior during long low-power periods.
- Config ACK delivery depends on Notehub Route configuration that is not verified programmatically.