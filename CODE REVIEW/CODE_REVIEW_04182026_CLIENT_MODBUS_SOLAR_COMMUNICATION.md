# Code Review: Client Modbus Solar Communication

Date: 2026-04-18

Scope: Review of the SunSaver MPPT Modbus RTU communication path used by the client sketch, including client integration and the shared solar manager used by the client.

Files reviewed:
- `TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino`
- `TankAlarm-112025-Common/src/TankAlarm_Solar.h`
- `TankAlarm-112025-Common/src/TankAlarm_Solar.cpp`

## Findings

### 1. Communication-failure alerts cannot fire because alert evaluation only runs after a successful poll
Severity: High

Evidence:
- `gSolarManager.checkAlerts()` is only called inside the `if (gSolarManager.poll(now))` success branch in `TankAlarm-112025-Client-BluesOpta.ino:1710-1712`.
- `SolarManager::checkAlerts()` contains explicit communication-failure logic at `TankAlarm-112025-Common/src/TankAlarm_Solar.cpp:265`.
- `SolarManager::poll()` returns `readRegisters()`, so failed Modbus reads return `false` and skip alert evaluation entirely (`TankAlarm-112025-Common/src/TankAlarm_Solar.cpp:98`).

Impact:
- When RS-485 wiring, slave ID, baud rate, or charger availability causes repeated read failures, `_data.communicationOk` eventually flips false, but the client never evaluates `SOLAR_ALERT_COMM_FAILURE` because it only checks alerts on successful polls.
- The `alertOnCommFail` configuration flag is therefore effectively dead in normal failure scenarios.

Recommendation:
- Separate "poll due" from "poll succeeded" in the client loop, or make `poll()` expose whether a poll attempt occurred.
- After any due poll attempt, evaluate `checkAlerts()` regardless of Modbus success so communication-failure alerts can be emitted when the error threshold is crossed.

### 2. Solar fault SMS escalation is gated by the low-battery flag instead of the fault flag
Severity: Medium

Evidence:
- In `sendSolarAlarm()`, `critical` is set for both `SOLAR_ALERT_BATTERY_CRITICAL` and `SOLAR_ALERT_FAULT`, but SMS escalation is only enabled when `gConfig.solarCharger.alertOnLowBattery` is true at `TankAlarm-112025-Client-BluesOpta.ino:5316-5319`.
- The configuration model has a separate `alertOnFault` control in `TankAlarm-112025-Common/src/TankAlarm_Solar.h` and it is loaded/saved independently in the client config logic.

Impact:
- Disabling low-battery alerts also disables SMS escalation for charger faults, even when `alertOnFault` remains enabled.
- This couples two independent policy knobs and can suppress the most serious hardware-fault notifications.

Recommendation:
- Gate battery-critical SMS behavior with `alertOnLowBattery`, and gate fault/alarm SMS behavior with `alertOnFault`.
- If the intended behavior is a single "critical solar SMS" knob, that needs to be made explicit in schema and naming; the current implementation does not match the exposed config surface.

### 3. Daily reports can include stale solar telemetry after communication has already failed
Severity: Medium

Evidence:
- `appendSolarDataToDaily()` only suppresses solar output when communication is bad *and* there has never been a successful read: `if (!data.communicationOk && data.lastReadMillis == 0) return false;` at `TankAlarm-112025-Client-BluesOpta.ino:5336`.
- On a later communication failure, `readRegisters()` leaves the previously read values in place and only flips `communicationOk` after the error threshold is reached (`TankAlarm-112025-Common/src/TankAlarm_Solar.cpp:191-199`).

Impact:
- After the charger stops responding, daily reports can still publish old battery voltage, array voltage, current, and fault/alarm strings as if they were current readings.
- The comment in `appendSolarDataToDaily()` says communication is implicit if solar data exists, but that assumption is false once stale data has been cached from an earlier successful poll.

Recommendation:
- Only include solar data in the daily report when `data.communicationOk` is currently true.
- If retaining stale data is desirable for diagnostics, include an explicit `commOk=false` or `ageSec` field so downstream consumers can distinguish cached telemetry from fresh telemetry.

### 4. Initialization reports success even when the initial Modbus read fails
Severity: Medium

Evidence:
- `SolarManager::begin()` sets up Modbus, performs `readRegisters()`, ignores the return value, and then returns `true` unconditionally unless `ModbusRTUClient.begin()` itself failed (`TankAlarm-112025-Common/src/TankAlarm_Solar.cpp:51-70`).
- The client setup path logs `Solar charger monitoring enabled` when `gSolarManager.begin(...)` returns true at `TankAlarm-112025-Client-BluesOpta.ino:1375-1380`.

Impact:
- Miswired RS-485, wrong slave ID, wrong baud rate, or an offline charger still produce a successful initialization message.
- Field diagnosis becomes harder because startup logs imply the solar subsystem is healthy when only the serial port opened successfully.

Recommendation:
- Treat the initial read result separately from transport initialization.
- Either return failure from `begin()` when the first read fails, or log a degraded state such as "transport initialized, initial Modbus read failed" so operators can distinguish configuration/connectivity problems from success.

### 5. Each solar poll can block the main loop for roughly 2.2 seconds on a full timeout sweep
Severity: Medium

Evidence:
- The default timeout is 200 ms (`TankAlarm-112025-Common/src/TankAlarm_Solar.h:195`).
- `readRegisters()` issues 11 sequential `ModbusRTUClient.requestFrom(...)` calls at `TankAlarm-112025-Common/src/TankAlarm_Solar.cpp:105`, `:113`, `:121`, `:129`, `:137`, `:145`, `:153`, `:160`, `:167`, `:175`, and `:183`.
- The client loop kicks the watchdog once near the top of each iteration (`TankAlarm-112025-Client-BluesOpta.ino:1474-1484`), and the system watchdog timeout is 30 seconds (`TankAlarm-112025-Common/src/TankAlarm_Common.h:92`).

Impact:
- A single due poll can stall the main loop for about $11 \times 200\text{ ms} = 2.2\text{ s}$ before considering serial turnaround overhead.
- This probably does not trip the 30 s watchdog by itself, but it does increase loop jitter and can delay unrelated work such as Notecard servicing, request polling, and relay handling.
- The code review history already reflects sensitivity to watchdog starvation and long blocking calls; this path works against that design direction.

Recommendation:
- Prefer batched contiguous register reads where the device map allows it, or split the polling across loop iterations with a small state machine.
- At minimum, document the worst-case blocking budget explicitly and consider kicking the watchdog around longer RS-485 transactions if the implementation remains synchronous.

## Notes

- The register addresses and basic scaling formulas appear internally consistent with the stated "0-based address" convention for ArduinoModbus.
- The most important functional defect is the dead communication-failure alert path, because it defeats the explicit `alertOnCommFail` feature rather than just degrading diagnostics.
- Follow-on architecture and power-priority rationale is documented in `CODE REVIEW/MODBUS_DESIGN_NOTES_04182026.md`.

## Suggested Next Fix Order

1. Make communication-failure alerting reachable from the client loop.
2. Decouple fault SMS escalation from the low-battery flag.
3. Stop emitting stale solar data as fresh daily-report telemetry.
4. Improve startup status so initial Modbus read failure is visible immediately.
5. Reduce per-poll blocking time if this path is expected to coexist with tight loop-latency requirements.