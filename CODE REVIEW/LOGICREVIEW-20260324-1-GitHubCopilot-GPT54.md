# Logic Review

Date: 2026-03-24
Author: GitHub Copilot GPT-5.4
Repository: ArduinoSMSTankAlarm
Scope: Active logic paths in TankAlarm-112025-Client-BluesOpta, TankAlarm-112025-Server-BluesOpta, and TankAlarm-112025-Common

## Objective

This review examines the decision-making functions that determine alarm state, relay behavior, unload detection, SMS escalation, power-state transitions, config delivery, and server-side event handling. The standard applied here is not merely whether the code compiles or behaves plausibly in the happy path, but whether each decision is tied to a defensible operational assumption under real field conditions.

## Review Method

The review focused on the following decision-bearing functions and pathways:

- Client: `validateSensorReading()`, `readTankSensor()`, `sampleTanks()`, `evaluateAlarms()`, `checkAlarmRateLimit()`, `sendAlarm()`, `evaluateUnload()`, `checkNotecardHealth()`, `pollForConfigUpdates()`, `processRelayCommand()`, `checkRelayMomentaryTimeout()`, `updatePowerState()`
- Server: `processNotefile()`, `handleTelemetry()`, `handleAlarm()`, `checkSmsRateLimit()`, `dispatchClientConfig()`, `dispatchPendingConfigs()`, `handleConfigAck()`, `checkStaleClients()`, `convertMaToLevelWithTemp()`

The findings below are ordered by severity and based on direct source inspection.

## Findings

### 1. Critical: Digital sensors are treated as "stuck" merely for remaining in one state

Files:
- `TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino` around `validateSensorReading()`

Evidence:
- The function applies stuck-sensor detection to all sensor interfaces after range validation.
- For digital inputs, valid readings are naturally only `0.0` or `1.0`.
- The code increments `stuckReadingCount` whenever two consecutive readings differ by less than `0.05f`.
- After `SENSOR_STUCK_THRESHOLD` repeated identical readings, the sensor is marked failed and a `sensor-stuck` alarm is generated.

Why this is logically unsound:
- A float switch, limit switch, or binary interlock is expected to remain unchanged for long periods.
- The logic assumes “unchanged reading” implies malfunction, but for digital sensors “unchanged” is the normal steady-state condition.

Operational consequence:
- Any correctly functioning digital sensor can self-declare failure after enough unchanged samples.
- With 30-minute sampling, a stable switch can be marked failed after roughly 5 hours.

Recommendation:
- Exclude digital sensors from stuck-reading detection, or implement a sensor-type-specific stuck heuristic.
- If a stuck heuristic is desired for digital sensors, base it on external contradiction signals, not value stability alone.

### 2. Critical: Relays configured as `UNTIL_CLEAR` do not clear for `HIGH` or `LOW` trigger modes

Files:
- `TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino` around `sendAlarm()`

Evidence:
- On alarm clear, the deactivation logic checks `strcmp(alarmType, "high") == 0` or `strcmp(alarmType, "low") == 0`.
- But the clearing invocation passes `alarmType == "clear"`.
- Therefore, for `RELAY_TRIGGER_HIGH` and `RELAY_TRIGGER_LOW`, `shouldClear` never becomes true.

Why this is logically unsound:
- The code attempts to decide whether the clear event corresponds to the original triggering condition, but the information needed to make that decision is no longer present in the `clear` event payload.
- The branch is structurally unreachable for non-`ANY` triggers.

Operational consequence:
- A relay activated by a high-only or low-only alarm can remain energized indefinitely even after the alarm has cleared.
- This creates a real field risk: persistent horn/strobe output, unintended remote actuation, wasted power, and operator confusion.

Recommendation:
- Persist the cause of the active relay state per tank, then clear against that stored cause.
- Alternatively, on any `clear`, deactivate if the relay is currently active and the tank is configured for `UNTIL_CLEAR`.

### 3. Critical: Config ACK protocol is inconsistent between client and server, so ACK-based decision making cannot work reliably

Files:
- `TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino` around `sendConfigAck()` and `pollForConfigUpdates()`
- `TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino` around `dispatchClientConfig()` and `handleConfigAck()`

Evidence:
- The client ACK sends `client`, `success`, `message`, and `epoch`.
- The server expects `client`, `configVersion` or `cv`, and `status` or `st`.
- The server only clears `pendingDispatch` when the received version matches `snap->configVersion`.
- The client never sends that version.

Why this is logically unsound:
- The server’s dispatch state machine is built around version-matched acknowledgement.
- The client emits a different schema, so the server cannot know whether a specific config revision was applied.

Operational consequence:
- ACKs are received but are semantically unusable.
- `pendingDispatch` may remain set even when the client applied the config.
- Server state, UI state, and actual client state can diverge silently.

Recommendation:
- Define one ACK contract and enforce it on both ends.
- The client should echo the exact config version/hash it applied, plus an explicit status field.
- The server should treat schema mismatch as an error, not a weak success.

### 4. High: The retry path clears `pendingDispatch` before ACK, contradicting the initial dispatch logic

Files:
- `TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino` around `dispatchClientConfig()` and `dispatchPendingConfigs()`

Evidence:
- Initial dispatch leaves `pendingDispatch = true` until ACK is received.
- Retry dispatch sets `pendingDispatch = false` immediately after `sendConfigViaNotecard()` succeeds.

Why this is logically unsound:
- A successful queue-to-Notecard event is not equivalent to confirmed client application.
- The retry path weakens the acknowledgement model and changes semantics depending on which path happened to deliver the config.

Operational consequence:
- The server can believe a retry was fully completed when the client never received or never applied the config.
- This undermines trust in the config management UI and in field diagnostics.

Recommendation:
- Keep `pendingDispatch` true until a version-matched ACK arrives, regardless of whether delivery happened on the first attempt or a retry.

### 5. High: Server-side SMS policy overrides the client’s per-monitor `enableAlarmSms` intent

Files:
- `TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino` around `sendAlarm()`
- `TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino` around `handleAlarm()`

Evidence:
- The client only emits `se = true` when `enableAlarmSms` is enabled for that monitor.
- The server reads `se`, but then force-sets `smsEnabled = true` for `high`, `low`, `clear`, and digital alarm types.

Why this is logically unsound:
- The system has two decision layers for SMS eligibility, but the server nullifies the more specific per-monitor decision made at the source.
- This means the downstream system is not honoring upstream intent.

Operational consequence:
- Operators may disable SMS on a monitor and still receive SMS alerts for it.
- This erodes confidence in configuration correctness and increases alert fatigue.

Recommendation:
- Treat client `se` as the monitor-level eligibility signal and combine it with server policy as an AND condition.
- If the server must remain authoritative, rename the fields and document that client `alarmSms` is advisory only. At present the behavior is misleading.

### 6. High: Current-loop read failures are masked as valid steady readings

Files:
- `TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino` around `readTankSensor()` and `validateSensorReading()`

Evidence:
- When `readCurrentLoopMilliamps()` fails, `readTankSensor()` returns `gMonitorState[idx].currentInches` instead of a failure sentinel.
- `validateSensorReading()` then validates that prior value, which usually passes.

Why this is logically unsound:
- The acquisition layer converts a transport or hardware failure into an apparently valid measurement.
- The decision layer therefore reasons over stale data while believing it is current data.

Operational consequence:
- A dead or disconnected current-loop interface can be hidden for an extended period.
- The system may drift into a false “sensor-stuck” interpretation instead of recognizing read failure.
- Alarm, unload, and telemetry decisions are then based on stale state.

Recommendation:
- Distinguish acquisition failure from measurement value.
- Return an explicit invalid sentinel or a `(success, value)` pair so validation can classify the failure correctly.

### 7. High: Alarm interpretation on the server depends on pre-existing sensor metadata that alarm messages do not carry

Files:
- `TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino` around `handleAlarm()` and `handleTelemetry()`

Evidence:
- `handleAlarm()` infers decoding rules from `rec->sensorType`.
- Unlike `handleTelemetry()`, `handleAlarm()` does not refresh `rec->sensorType` from the incoming document.
- The client’s `sendAlarm()` payload also omits sensor interface metadata.

Why this is logically unsound:
- The server assumes that telemetry or daily state has already populated the record before the first alarm arrives.
- That assumption is not guaranteed in distributed systems with independent notefiles and asynchronous delivery.

Operational consequence:
- A first-seen alarm can be decoded as level `0.0`, logged incorrectly, and formatted into a misleading SMS.
- This is especially problematic during startup, after registry loss, or when alarm traffic outruns telemetry traffic.

Recommendation:
- Include sensor interface metadata in alarm payloads, or ensure the server can derive it from cached client config snapshots before interpreting raw fields.

### 8. Medium: Config retrieval deletes the update before validation and application are proven successful

Files:
- `TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino` around `pollForConfigUpdates()`

Evidence:
- `note.get` is called with `delete = true`.
- The config note is therefore removed before JSON validity, memory availability, and application success are confirmed.
- On OOM, the code logs an error but does not preserve the config for retry.

Why this is logically unsound:
- A command that has not been safely parsed or applied should not be considered consumed.
- The present logic treats “delivered to firmware” as equivalent to “accepted and committed.”

Operational consequence:
- Malformed payloads, transient memory pressure, or partial application failures can permanently discard a config update.
- This is particularly problematic for remote-only deployments.

Recommendation:
- Either fetch without delete and delete only after successful application, or requeue the payload locally until commit is confirmed.

### 9. Medium: Unload detection silently chooses the stricter of percentage-drop and absolute-drop thresholds when both are set

Files:
- `TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino` around `evaluateUnload()`

Evidence:
- The code computes a percentage-based trigger and then replaces it with the absolute trigger only when the absolute trigger level is lower.
- That means the logic chooses the larger required drop whenever both are configured.

Why this is logically weak:
- The comment says “use percentage or absolute, whichever is configured,” but the behavior for “both configured” is a hidden policy choice.
- The current choice is not clearly justified in code comments or configuration semantics.

Operational consequence:
- Operators may believe they configured an additional safeguard, while the firmware actually applies one implicit precedence rule.
- This can delay or suppress expected unload events.

Recommendation:
- Make the rule explicit in schema and UI: `percent`, `absolute`, `either`, or `both`.
- Do not bury configuration semantics inside threshold arithmetic.

### 10. Medium: The server allows SMS before time sync, which bypasses temporal rate controls at the moment uncertainty is highest

Files:
- `TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino` around `checkSmsRateLimit()`

Evidence:
- If `currentEpoch() <= 0.0`, the function returns `true` and allows SMS.

Why this is logically weak:
- When time is unknown, the server cannot enforce minimum interval or hourly quotas.
- The current policy resolves uncertainty by fully disabling protections.

Operational consequence:
- On cold start or time-sync failure, multiple alarms can escape temporal controls and create a burst.

Recommendation:
- Use a conservative fallback window based on uptime, or suppress non-critical SMS until time is known.

## Systemic Themes

### Decision authority is split but not consistently composed

Several paths show a recurring pattern: the client makes a localized decision, then the server makes a second decision using a different information set and different assumptions. This is visible in SMS escalation, alarm decoding, config acknowledgement, and relay behavior. When the second decision does not explicitly compose with the first, system behavior becomes surprising even if each local branch looks reasonable.

### State machines are present, but some of them lose the causal state they need

The relay clear logic and config ACK logic both fail for the same structural reason: they attempt to validate or reverse a previous decision without preserving the exact state that caused the original decision.

### Some fault paths are converted into stale-data paths

Current-loop acquisition failure is not represented as failure. Instead, it is converted into reuse of the last good value. That makes downstream decisions appear stable while actually being under-informed.

## Priority Recommendations

1. Fix the digital stuck-sensor logic immediately.
2. Fix `UNTIL_CLEAR` relay deactivation semantics immediately.
3. Unify the config ACK schema and keep `pendingDispatch` ACK-driven on all paths.
4. Stop overriding monitor-level SMS eligibility on the server.
5. Represent sensor acquisition failure explicitly instead of substituting stale values.
6. Add missing metadata to alarm payloads or derive it from config snapshots before interpretation.

## Closing Assessment

The codebase contains several well-intended state machines and guardrails, especially around power conservation, debouncing, and rate limiting. The strongest logic is where the decision and the evidence for that decision remain in the same function. The weaker areas are cross-component decisions, where the code assumes prior state, prior delivery order, or a shared interpretation that is not actually enforced by the protocol.

In short: the principal risks are not raw algorithmic complexity, but hidden assumptions between modules. Those assumptions should be made explicit in message schemas, persisted state, and decision contracts.