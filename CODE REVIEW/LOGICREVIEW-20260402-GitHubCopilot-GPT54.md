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

## Residual Risk

- This review did not include runtime validation on Opta hardware or Notehub routes.
- The most important untested assumptions are inbound note timing, relay behavior under overlapping masks, and field behavior during long low-power periods.