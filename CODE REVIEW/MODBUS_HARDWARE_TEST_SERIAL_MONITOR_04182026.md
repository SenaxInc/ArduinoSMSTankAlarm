# Modbus Hardware Test - INO and Serial Monitor Prep

Date: 2026-04-18

Purpose: Provide a repeatable bring-up path for testing SunSaver MPPT RS-485 Modbus communication using the client sketch and Serial Monitor output.

## Firmware Prep

In `TankAlarm-112025-Client-BluesOpta.ino`:

1. Enable solar monitoring in configuration (`solarCharger.enabled = true`).
2. Confirm expected Modbus settings:
   - `slaveId` (default 1)
   - `baudRate` (default 9600)
   - `timeoutMs` (default 200)
   - `pollIntervalSec` (recommend 10-30 for bench tests)
3. Uncomment the hardware-test serial flag:

```cpp
//#define SOLAR_HW_TEST_SERIAL
```

Change to:

```cpp
#define SOLAR_HW_TEST_SERIAL
```

Note: Leave this disabled for production deployments.

## Serial Monitor Setup

- Baud rate: `115200`
- Line ending: `No line ending` (or any; input is not required for this test)

## Expected Startup Output

Healthy transport initialization should include lines similar to:

- `Solar: Modbus RTU initialized at 9600 baud, slave ID 1`
- `Solar charger monitoring enabled`

If transport starts but register reads fail, expect:

- `Solar: Modbus transport initialized, but initial read failed`
- `Solar charger transport initialized, initial Modbus read failed`

## Expected Poll Output (when test flag is enabled)

One structured line is printed per due poll:

```text
SolarPoll ms=123456 comm=OK err=0 bv=12.74 av=18.31 ic=3.42 lc=0.88 cs=Bulk faults=0x0000 alarms=0x0000
```

Fields:

- `ms`: `millis()` timestamp when poll line was printed
- `comm`: `OK` or `FAIL`
- `err`: consecutive Modbus error count
- `bv`: battery voltage
- `av`: array voltage
- `ic`: charge current
- `lc`: load current
- `cs`: charge state string
- `faults`: register 45 bitfield (hex)
- `alarms`: register 47 bitfield (hex)
- `alert` (optional): appended when an active alert exists

## Wiring / Config Fault Signatures

### A/B swapped, wrong slave ID, wrong baud, or offline charger

Typical behavior:

- `comm=FAIL`
- `err` increments each poll
- after threshold, communication-failure logs appear

### Valid comms with charger fault/alarm

Typical behavior:

- `comm=OK`
- non-zero `faults` and/or `alarms`
- `alert=...` appears when alert logic is triggered

## Bench Test Sequence

1. Flash firmware with `SOLAR_HW_TEST_SERIAL` enabled.
2. Open Serial Monitor at `115200`.
3. Verify startup lines.
4. Observe 5-10 consecutive `SolarPoll` lines.
5. Confirm `comm=OK` stability and realistic `bv/av/ic` values.
6. Intentionally misconfigure one parameter (for example wrong `slaveId`) and verify `comm=FAIL` plus increasing `err`.
7. Restore correct settings and confirm recovery to `comm=OK`.

## Post-Test Cleanup

Before production deployment:

1. Re-comment `#define SOLAR_HW_TEST_SERIAL`.
2. Restore normal `pollIntervalSec` (for power-optimized operation).
3. Re-run a final compile.