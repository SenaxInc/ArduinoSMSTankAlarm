# Logic Review

Date: 2026-03-24
Author: GitHub Copilot GPT-5.4
Repository: ArduinoSMSTankAlarm
Scope: Current active logic in TankAlarm-112025-Client-BluesOpta, TankAlarm-112025-Server-BluesOpta, TankAlarm-112025-Viewer-BluesOpta, and TankAlarm-112025-Common

## Objective

This review re-evaluates the current codebase after the latest pull, focusing on where the firmware and server make consequential decisions: alarm transitions, relay control, message handling, scheduling, DFU control, solar-health interpretation, and server-side policy enforcement. The criterion is whether each branch is supported by a sound operational assumption under field conditions, not merely whether the branch is syntactically correct.

## What Changed Since The Prior Pass

Two earlier concerns are no longer valid in the current tree:

1. The client config ACK schema is now aligned with the server’s expectation. The client sends `status` and `cv`, and the server consumes those fields.
2. Config retry dispatch no longer clears `pendingDispatch` immediately on send success; it remains ACK-driven.

Those fixes improve the config-delivery state machine materially. The findings below are therefore limited to issues that remain present in the current code.

## Findings

### 1. Critical: Viewer DFU enable path calls the wrong Notecard request, so the update decision path is internally self-contradictory

Files:
- `TankAlarm-112025-Viewer-BluesOpta/TankAlarm-112025-Viewer-BluesOpta.ino`

Evidence:
- `checkForFirmwareUpdate()` correctly queries `dfu.status`.
- `enableDfuMode()` is documented as sending `dfu.mode`, but actually constructs a new request with `"dfu.status"` and then adds `on = true`.

Why this is logically unsound:
- The comments, intent, and API contract disagree with the actual command emitted.
- A status query is not the same thing as a mode-changing command.

Operational consequence:
- The viewer may correctly detect that an update is available but fail to transition into download mode.
- This breaks the final actuation step of the DFU state machine while making the rest of the flow appear healthy.

Recommendation:
- Change the enable path to issue the correct Notecard control request and treat any schema mismatch as a hard failure.

### 2. Critical: `UNTIL_CLEAR` relay deactivation for `HIGH` and `LOW` triggers is still structurally unreachable

Files:
- `TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino`

Evidence:
- `sendAlarm()` handles relay clear logic in the `!isAlarm` branch.
- In that branch, the code checks whether `alarmType` equals `"high"` or `"low"` to decide whether to clear a relay configured for `RELAY_TRIGGER_HIGH` or `RELAY_TRIGGER_LOW`.
- But the clear path is entered only when `alarmType == "clear"`.

Why this is logically unsound:
- The code is trying to infer the original activation cause from the clear event, but that cause is no longer available in the current state.
- For non-`ANY` triggers, the test can never succeed.

Operational consequence:
- Relays configured as `UNTIL_CLEAR` can remain active indefinitely after the monitored condition has cleared.
- This can leave horns, lamps, or remote devices energized with no valid current alarm.

Recommendation:
- Persist the activation cause per monitor, then clear against that stored cause.
- The simpler fallback is to deactivate on any `clear` when the monitor is currently latched in `UNTIL_CLEAR` mode.

### 3. High: Solar communication alarms are suppressed by a cross-module return-value mismatch between `SolarManager::poll()` and the client loop

Files:
- `TankAlarm-112025-Common/src/TankAlarm_Solar.cpp`
- `TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino`

Evidence:
- `SolarManager::poll()` returns `false` both when it is not time to poll and when `readRegisters()` fails.
- The client loop only evaluates `checkAlerts()` inside `if (gSolarManager.poll(now))`.
- `readRegisters()` increments `consecutiveErrors` and can mark `communicationOk = false`, but on a failed poll the caller skips alert evaluation entirely.

Why this is logically unsound:
- The caller treats `false` as “no new state to inspect,” while the callee also uses `false` to mean “a failure just occurred.”
- A failure state is therefore recorded internally but not surfaced externally.

Operational consequence:
- Solar communication failures can accumulate without ever generating a solar alarm at the sketch layer.
- The system can silently lose charger visibility while appearing only “quiet.”

Recommendation:
- Separate `poll()` outcomes into distinct states such as `not_due`, `success`, and `failed`.
- Alternatively, evaluate `checkAlerts()` after every attempted poll, not only after successful register reads.

### 4. High: Digital sensors still default into stuck-detection logic that is conceptually invalid for steady-state binary signals

Files:
- `TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino`

Evidence:
- `validateSensorReading()` applies stuck detection whenever `cfg.stuckDetectionEnabled` is true.
- The default monitor initialization sets `stuckDetectionEnabled = true`.
- There is no automatic disabling of stuck detection for digital sensors.

Why this is logically unsound:
- For a digital float switch, repeated identical values are usually normal, not evidence of failure.
- A general “unchanged value” rule does not transfer from analog sensors to binary state sensors.

Operational consequence:
- A correctly functioning digital sensor can still self-classify as stuck after enough unchanged samples unless configuration explicitly disables that feature.

Recommendation:
- Default `stuckDetectionEnabled` to false for digital sensors, or bypass stuck detection whenever `sensorInterface == SENSOR_DIGITAL`.

### 5. High: Current-loop read failure is still transformed into a valid-looking measurement, so downstream decisions operate on stale state without knowing it

Files:
- `TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino`

Evidence:
- `readCurrentLoopSensor()` returns `gMonitorState[idx].currentInches` when `readCurrentLoopMilliamps()` fails.
- Validation and alarm logic then consume that reused value as though it were a fresh measurement.

Why this is logically unsound:
- The acquisition layer erases the distinction between “I measured this value” and “I could not measure anything.”
- The decision layer therefore has no chance to classify transport failure correctly.

Operational consequence:
- A broken current-loop path can look like a perfectly stable sensor.
- Alarm, unload, telemetry, and fault decisions can all be made from stale data without any explicit evidence trail.

Recommendation:
- Represent acquisition failure explicitly with a success flag or invalid sentinel and let validation handle it as a true read failure.

### 6. High: The server still overrides per-monitor SMS intent for ordinary alarms

Files:
- `TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino`
- `TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino`

Evidence:
- The client only includes `se = true` when `enableAlarmSms` is enabled for that monitor.
- The server initializes `smsEnabled` from the payload but then force-sets `smsEnabled = true` for `high`, `low`, `clear`, and digital alarm types.

Why this is logically unsound:
- The code expresses two levels of decision authority but does not compose them.
- The server effectively nullifies the more specific monitor-level decision made at the source.

Operational consequence:
- Users can disable SMS for a monitor and still receive SMS for that monitor’s ordinary alarms.
- This increases alert fatigue and undermines the meaning of the configuration.

Recommendation:
- Combine `se` and server policy with AND semantics for ordinary alarms.
- If server policy must be absolute, rename the fields so the current configuration does not imply a false control surface.

### 7. High: Alarm payloads still omit sensor-interface metadata, so first-seen alarms can be decoded using stale or absent server context

Files:
- `TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino`
- `TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino`

Evidence:
- `sendAlarm()` includes object type and measurement unit, but not sensor interface metadata.
- `handleAlarm()` decides how to interpret raw fields from `rec->sensorType`.
- Unlike telemetry handling, alarm handling does not refresh `rec->sensorType` from the incoming alarm document.

Why this is logically unsound:
- The server assumes telemetry or config-derived metadata has already been applied before alarm arrival.
- Distributed note delivery does not guarantee that ordering.

Operational consequence:
- A first-seen alarm can be decoded as level `0.0` or otherwise misinterpreted if the server lacks current sensor-type context.

Recommendation:
- Include sensor-interface metadata in alarm payloads, or derive it deterministically from the cached client configuration before decoding alarm bodies.

### 8. Medium: Viewer scheduling trusts unbounded upstream cadence fields and passes them into a scheduler that performs no range validation

Files:
- `TankAlarm-112025-Viewer-BluesOpta/TankAlarm-112025-Viewer-BluesOpta.ino`
- `TankAlarm-112025-Common/src/TankAlarm_Utils.h`

Evidence:
- `handleViewerSummary()` copies `rs` and `bh` from the summary into `gSourceRefreshSeconds` and `gSourceBaseHour`.
- Only `rs == 0` is corrected; there is no clamp for extreme values.
- `tankalarm_computeNextAlignedEpoch()` validates only `epoch` and `intervalSeconds != 0`, not the semantic range of `baseHour`.

Why this is logically weak:
- The viewer treats upstream scheduling hints as trusted control inputs rather than advisory metadata.
- A malformed or hostile summary can push the local scheduler into pathological behavior.

Operational consequence:
- Fetch scheduling can drift far outside intended cadence or become effectively unusable.
- This is especially undesirable in a read-only mirror, where predictability matters more than flexibility.

Recommendation:
- Clamp `refreshSeconds` and `baseHour` to safe ranges before scheduling.
- Treat out-of-range values as invalid and fall back to local defaults.

### 9. Medium: The codebase still has a recurring “delete-before-accept” message-consumption pattern

Files:
- `TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino`
- `TankAlarm-112025-Viewer-BluesOpta/TankAlarm-112025-Viewer-BluesOpta.ino`
- `TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino`

Evidence:
- Client config polling fetches with `delete = true` before successful parse/application is guaranteed.
- Viewer summary fetch also uses `delete = true` before parse and storage success are guaranteed.
- Server `processNotefile()` follows the same pattern for inbound note processing.

Why this is logically weak:
- “Delivered to firmware” is treated as equivalent to “accepted by firmware.”
- Parse failure, OOM, or mid-handler faults can consume data irreversibly.

Operational consequence:
- A transient memory or parsing problem can permanently drop commands or summaries.

Recommendation:
- For high-value control messages, separate retrieval from acknowledgement, or locally persist the payload before consuming it.

### 10. Medium: Solar alert prioritization can report stale battery severity ahead of communication failure

Files:
- `TankAlarm-112025-Common/src/TankAlarm_Solar.cpp`

Evidence:
- `checkAlerts()` evaluates battery-critical and fault conditions before communication failure.
- `batteryVoltage` remains populated from older successful polls even after `communicationOk` becomes false.

Why this is logically weak:
- Once communications have failed, prior analog measurements become stale evidence.
- Alert priority should reflect data freshness, not just raw severity ranking.

Operational consequence:
- The system can continue reporting a severe battery state based on old data while obscuring the more immediate fact that charger telemetry is no longer trustworthy.

Recommendation:
- Gate voltage-derived alerts on freshness, or make communication failure dominate once the data source is stale.

## Systemic Themes

### Return values are overloaded in ways that erase decision meaning

The solar path is the clearest example: one boolean is used for “not due yet” and “poll failed.” Once those meanings are collapsed, the caller can no longer make the correct downstream decision.

### Some state machines still fail because causal state is not preserved

The relay clear bug persists because the code tries to reverse a previous actuation without preserving why that actuation happened.

### The code often trusts upstream metadata too much and transport success too little

Viewer scheduling treats cadence hints as authoritative. Current-loop sensing treats read failure as if nothing happened. Both problems stem from weak boundaries between evidence and decision.

## Positive Observations

1. The config ACK path is materially better than in the earlier revision.
2. Session-token middleware now protects `/api/` routes on the server, which closes a class of earlier API-exposure concerns.
3. Config dispatch retry state is now more coherent because ACK, not send success, remains the completion criterion.

## Priority Recommendations

1. Fix viewer DFU enable to use the correct Notecard control request.
2. Fix `UNTIL_CLEAR` relay deactivation by preserving or simplifying trigger-cause semantics.
3. Separate solar poll outcomes into distinguishable states and surface alert checks after failed polls.
4. Disable stuck detection by default for digital sensors.
5. Stop converting current-loop read failure into a valid-looking sample.
6. Respect client monitor SMS intent on the server, or rename the configuration to match current authority.
7. Add missing sensor-interface metadata to alarm payloads.
8. Clamp viewer cadence inputs and treat invalid upstream schedule hints as non-authoritative.

## Closing Assessment

The current codebase is stronger than the prior revision in config acknowledgement and API session handling. The remaining logic risks are concentrated in cross-module boundaries: shared library return semantics, viewer control paths, relay deactivation semantics, and places where stale state is silently promoted into current truth.

The central engineering lesson is consistent across the findings: when the system makes a decision, it must preserve whether the evidence is fresh, complete, and attributable. Where that contract is weak, the code still makes decisions confidently but not always correctly.