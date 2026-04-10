# Code Review - 2026-04-09

## Scope
- Reviewed the current Server config workflow/UI, Client config apply/persistence path, and Client sensor/I2C acquisition logic in the active source tree.
- Primary files reviewed:
  - `TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino`
  - `TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino`
  - `CODE REVIEW/TODO.md`
- This was a static review only. I did not run a full Arduino build.
- The Arduino include-path squiggles currently shown by the editor look like local IntelliSense/environment issues, not validated firmware build failures.

## Findings (ordered by severity)

### 1. High - Config Generator has a live status panel in the HTML, but the page never receives or renders config dispatch state
Evidence:
- `TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:1445`
- `TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:1510`
- `TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:9431`
- `TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:9450`

Why this is a problem:
- The page defines `#configStatus`, plus retry and sync actions, but `fetchClientConfig()` only loads `c.config`.
- `GET /api/client?uid=` returns only `{"config": ...}` and omits pending-dispatch, retry, dispatch-time, and ACK metadata.
- Operators editing a client cannot tell whether a pushed config is still queued, repeatedly failing, or already acknowledged.

Recommendation:
- Extend `GET /api/client?uid=` to include `pd`, `da`, `de`, `ae`, `as`, and `cv`.
- Populate `configStatus` after load, submit, retry, and sync actions so the page reflects actual delivery state.

### 2. High - Client reports "Config applied" before flash persistence outcome is known
Evidence:
- `TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino:3570`
- `TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino:1763`
- `TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino:3986`
- `TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino:4002`

Why this is a problem:
- `pollForConfigUpdates()` sends a success ACK immediately after `applyConfigUpdate()` mutates in-memory state.
- Durable persistence happens later when the main loop reaches `persistConfigIfDirty()`.
- If the filesystem is unavailable or `saveConfigToFlash()` fails, the server can record a successful delivery even though the config is only volatile and may be lost on reboot.

Recommendation:
- Delay the success ACK until persistence succeeds, or split the protocol into `applied_volatile` and `applied_persisted` states.

### 3. Medium - Current-loop sensors still use a single raw sample while analog sensors average eight samples
Evidence:
- `TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino:4270`
- `TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino:4312`

Why this is a problem:
- The analog path explicitly averages eight samples before converting to level.
- The current-loop path takes one I2C reading and accepts it immediately.
- This makes current-loop sensors materially more sensitive to transient noise and bus jitter than the analog path.

Recommendation:
- Add a short averaging/retry loop to `readCurrentLoopSensor()` while preserving the existing "return previous value on outright read failure" behavior.

### 4. Medium - I2C recovery thresholds are still loop-count based, so recovery timing changes dramatically by power state
Evidence:
- `TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino:1521`
- `TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino:1546`
- `TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino:1563`
- `TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino:1586`

Why this is a problem:
- The code still measures prolonged failure in loop iterations, not elapsed time.
- Loop cadence changes heavily with power mode, so the same threshold means seconds in NORMAL and many minutes in CRITICAL_HIBERNATE.
- Failure handling therefore remains non-deterministic across operating states.

Recommendation:
- Convert the dual-fail and sensor-only recovery thresholds to monotonic elapsed-time checks.

### 5. Low - The master TODO is stale on VIN voltage rendering
Evidence:
- `TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:1697`
- `TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:1741`

Why this is a problem:
- The dashboard and site/client views already render VIN voltage when the server has it.
- Leaving the item open in the tracker creates review noise and can send future work toward a problem that is already resolved.

Recommendation:
- Mark `m-10` fixed in `CODE REVIEW/TODO.md` and archive the verification date.

## Suggested Improvements
- Add a shared helper for config delivery status so both API endpoints and UI pages use the same field set.
- Add a small regression check for the config-generator page that loads an existing client and verifies status text is populated.
- Keep TODO hygiene aligned with source reality after each static review so the backlog stays trustworthy.

## Suggested Tests
- Load the Config Generator for a client with a pending config and verify the page shows pending/ACK state.
- Force `saveConfigToFlash()` failure and verify the client does not send a durable success ACK.
- Inject noisy current-loop readings and compare stability before/after short averaging.
- Measure I2C recovery latency in NORMAL, ECO, LOW_POWER, and CRITICAL_HIBERNATE after converting thresholds to elapsed time.
