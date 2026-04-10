# Logic Review - 2026-04-09

## Scope
- Reviewed runtime state transitions around config delivery, stale-client handling, deep-power telemetry suppression, and server-side history calculations.
- Primary files reviewed:
  - `TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino`
  - `TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino`
- This was a static logic review only. I did not run hardware-in-the-loop tests.

## Findings (ordered by severity)

### 1. High - Manual config retry breaks the ACK-driven retry state machine
Evidence:
- `TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:7208`
- `TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:7209`
- `TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:7222`
- `TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:7223`
- `TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:7959`
- `TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:8014`
- `TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:10213`

Why this is a problem:
- Fresh dispatch and auto-retry both treat ACK receipt as the event that clears `pendingDispatch`.
- Manual retry clears `pendingDispatch` immediately after `sendConfigViaNotecard()` succeeds.
- If a manually retried note is lost after being queued, the server stops retrying and presents a false synced state.

Recommendation:
- Pick one clearing rule for all paths. Either clear on Notecard queue success everywhere or clear on client ACK everywhere. The current hybrid model is logically inconsistent.

### 2. High - Stale-client recovery is silent even though stale entry is noisy
Evidence:
- `TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:10063`
- `TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:10080`

Why this is a problem:
- The server sends an SMS when a client crosses the stale threshold.
- When the client resumes reporting, the code only resets `staleAlertSent` and logs locally.
- Operators are alerted to the outage but never explicitly told that the device recovered.

Recommendation:
- Emit a single recovery SMS when `staleAlertSent` transitions from true to false, with the same rate-limiting discipline used for other operational notifications.

### 3. Medium - CRITICAL_HIBERNATE makes a live client indistinguishable from a dead client at the server
Evidence:
- `TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino:1611`
- `TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino:1620`
- `TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino:1781`

Why this is a problem:
- Sampling, inbound note polling, time sync, and daily reporting are all gated out in CRITICAL_HIBERNATE.
- Only local battery/solar polling continues, so the server receives no heartbeat at all during the deepest power-saving mode.
- Operationally, a healthy but power-starved client looks the same as a dead or disconnected client.

Recommendation:
- Send a very low-cadence heartbeat/registration note in CRITICAL_HIBERNATE, or explicitly surface this state on the server so silence is distinguishable from failure.

### 4. Medium - The 24-hour change metric is not really a rolling 24-hour comparison for frequent reporters
Evidence:
- `TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:10980`
- `TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:10984`
- `TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:10988`

Why this is a problem:
- The server subtracts the oldest retained sample that still falls inside the last 24 hours from the latest sample.
- With frequent reporting, that oldest in-window sample can remain the same for hours, so the displayed delta appears frozen.
- Users read it as "change over the last 24 hours," but the implementation is really "latest minus first sample still inside the window."

Recommendation:
- Compare against an interpolated 24-hours-ago value or keep hourly buckets so the delta advances smoothly and predictably.

### 5. Medium - Config success semantics still conflate in-memory apply with durable apply
Evidence:
- `TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino:3570`
- `TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino:1763`
- `TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino:3986`
- `TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino:4002`

Why this is a problem:
- The protocol currently treats a post-`applyConfigUpdate()` ACK as proof that the change is complete.
- In reality, persistence is deferred and can still fail later in the loop.
- That makes the server's notion of "applied" stronger than what the client has actually guaranteed.

Recommendation:
- Defer the success ACK until persistence completes, or add a second durable-apply acknowledgement stage.

## Suggested Tests
- Retry a config manually, drop the queued note after `note.add`, and verify the server still retries according to the chosen state-machine rule.
- Force a client stale alert and then resume telemetry; verify operators receive exactly one recovery message.
- Put a client into CRITICAL_HIBERNATE and verify the server still gets an explicit low-cadence liveness signal.
- Feed high-frequency history data into `/api/history` and confirm `change24h` keeps moving as the 24-hour window advances.
- Simulate filesystem save failure during config apply and verify the protocol does not report durable success prematurely.
