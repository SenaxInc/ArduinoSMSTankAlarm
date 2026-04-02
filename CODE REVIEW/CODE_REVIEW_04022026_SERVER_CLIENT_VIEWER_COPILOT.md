# Code Review: Server, Client, and Viewer System

Date: 2026-04-02
Reviewer: GitHub Copilot (GPT-5.4)
Scope reviewed:
- TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino
- TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino
- TankAlarm-112025-Viewer-BluesOpta/TankAlarm-112025-Viewer-BluesOpta.ino
- TankAlarm-112025-Common/src/*

This review is based on static analysis of the current source tree. I did not compile the sketches or run hardware-in-the-loop tests.

## Findings

### 1. High: Unload notification settings are configured and persisted, but never serialized into unload notes

Files:
- TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino:511
- TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino:512
- TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino:4983
- TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino:5000
- TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:8848
- TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:8849
- TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:8888

Why this matters:
- The client model includes `unloadAlarmSms` and `unloadAlarmEmail`, and those fields are loaded and saved in config.
- `sendUnloadEvent()` publishes peak/empty/unit data, but never includes the `sms` or `email` flags the server reads in `handleUnload()`.
- The server therefore always sees `wantsSms == false` and `wantsEmail == false` for unload events.

Impact:
- Per-monitor unload SMS is effectively dead code.
- The server-side unload notification path cannot honor the stored client configuration.
- Operators can enable unload notifications in config and still never receive them.

### 2. High: Momentary relay timing is shared per relay output, so monitors can overwrite each other's timeout state

Files:
- TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino:716
- TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino:717
- TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino:718
- TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino:4815
- TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino:4835
- TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino:7079
- TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino:7122
- TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino:7150

Why this matters:
- Activation is tracked in `gRelayActivationTime[MAX_RELAYS]`, but active state is tracked per monitor in `gRelayActiveForMonitor[]` and `gRelayActiveMaskForMonitor[]`.
- When one monitor activates relay `R1`, it writes `gRelayActivationTime[0]`.
- A second monitor using the same relay bit overwrites the same timestamp, and a clear/reset from either monitor zeros the same shared slot.

Impact:
- Momentary durations are no longer isolated per monitor once relay masks overlap.
- One monitor can extend, shorten, or cancel another monitor's timeout.
- Timeout behavior becomes order-dependent instead of configuration-dependent.

### 3. High: Multi-relay remote actions are split into separate notes, but the receiver drains only one relay note per inbound poll

Files:
- TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino:1618
- TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino:1624
- TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino:1646
- TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino:6886
- TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino:7032
- TankAlarm-112025-Common/src/TankAlarm_Config.h:127

Why this matters:
- `triggerRemoteRelays()` emits one `relay_forward.qo` note per relay bit.
- The target client checks relay inbox only on the normal inbound polling cadence and `pollForRelayCommands()` processes a single `note.get` result per call.
- Default grid cadence is 10 minutes before power-state multipliers, and command cooldown is 5 seconds.

Impact:
- A relay mask like `R1|R2|R3|R4` is not applied atomically.
- On default timing, the remote side can take 10 to 40 minutes or longer to apply a four-relay action.
- Partial actuation windows are possible, which is especially risky for pump-stop or shutdown logic.

### 4. Medium: Server alarm handling overrides client SMS intent for ordinary sensor alarms

Files:
- TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino:4710
- TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino:4761
- TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:8412
- TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:8421
- TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:8424
- TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:8427
- TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:8430

Why this matters:
- The client only emits `se = true` when `enableAlarmSms` is enabled for that monitor.
- The server initializes `smsEnabled` from the payload, but then force-sets `smsEnabled = true` for `high`, `low`, `clear`, and digital alarm types.

Impact:
- Per-monitor SMS suppression at the client is not authoritative.
- Operators can disable SMS for a monitor and still receive server-generated SMS for its normal alarm cycle.
- Cross-component behavior does not match the configuration surface exposed to users.

## Open Questions

1. Are overlapping remote relay masks across multiple monitors a supported configuration, or should the server/client reject them outright?
2. Should remote relay actions be treated as immediate control operations, or is the current inbound polling latency considered acceptable for the product?

## Residual Risk

- I did not run firmware on hardware, so timing-related effects were inferred from the code rather than observed directly.
- The server and client sketches are both large enough that there may be additional edge cases in adjacent flows that were not exercised in this static pass.