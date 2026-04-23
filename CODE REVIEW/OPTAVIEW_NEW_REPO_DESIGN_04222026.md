# OptaView: Morningstar MSView-on-Opta — New Repository Design

Date: 2026-04-22
Author: TankAlarm engineering
Status: Proposal

## 1. Purpose

Stand up a new, separate repository (`OptaView`) that turns an Arduino Opta into a
field-portable replacement for Morningstar's Windows-only `MSView` configuration
and diagnostic utility.

The motivating problem: bringing up a SunSaver MPPT in the field currently
requires (a) a Windows laptop, (b) a USB↔RS-232 or USB↔MeterBus dongle, (c) the
proprietary MSView installer, and (d) a vendor `.cfg` file. None of those are
practical in a remote tank/well/cellular gateway install.

OptaView gives us a self-contained, ruggedized, screenless device that can:

1. Probe and identify a SunSaver MPPT (or any Morningstar charge controller
   with a Modbus-over-RS-485 interface, including the MRC-1 bridge).
2. Read every documented register on demand and display live telemetry.
3. Write configurable EEPROM settings (battery type, voltage thresholds,
   load-disconnect setpoints, equalization schedule, etc.).
4. Capture and replay register snapshots for off-line analysis.
5. Run a calibration / commissioning checklist.

Crucially, OptaView is the natural "ground truth" tool for the
TankAlarm-112025-Client when its solar monitoring shows anomalies in the field.

## 2. Why a new repository, not a sketch in this one

| Concern | Reason for separation |
|---------|----------------------|
| Hardware target | OptaView dedicates the Opta entirely to a Modbus master role; TankAlarm dedicates it to tank monitoring. They cannot share the same firmware image. |
| Release cadence | OptaView is a service tool that should be revised whenever Morningstar publishes new register maps; TankAlarm is a long-lived field firmware and should change rarely. |
| Dependency surface | OptaView wants a richer Modbus library (function codes 0x05/0x06/0x10 for writes, 0x2B/0x2B/0x10 for device identification) that TankAlarm doesn't need and shouldn't carry. |
| Audience | OptaView is for installers and field techs; TankAlarm is for remote unattended operation. Different quality bars, different docs. |

## 3. Hardware

| Item | Notes |
|------|-------|
| Arduino Opta WiFi (AFX00002) | Provides the on-board RS-485 transceiver and a captive-portal capable WiFi radio for the web UI. The cheaper Opta Lite is acceptable if the WiFi UI is dropped in favor of USB-only. |
| Morningstar MRC-1 (MeterBus → RS-485) | Powered by the SunSaver via RJ-11. Accept either wiring convention and surface a clear "if no reply, swap A/B" prompt. |
| 12 V supply | The Opta accepts 12-24 V on its power input; OptaView is normally bench-powered from the same 12 V battery as the SunSaver. |
| (Optional) USB-C cable | The host-PC fallback path: a Web Serial UI reachable from any modern browser, no driver install. |

The OptaView repo MUST NOT assume the A0602 expansion board, the Notecard, the
Blues SIM, or any of the TankAlarm sensor wiring. Bare-Opta-only.

## 4. Software architecture

```
┌──────────────────────────────────────────────────────────┐
│ Web UI (served from Opta WiFi AP or station)             │
│   - Live register table  (HTMX over /api/registers)      │
│   - Register editor      (POSTs to /api/write)           │
│   - Snapshot / restore   (JSON download/upload)          │
│   - Modbus probe wizard  (/api/probe)                    │
└─────────────────────────┬────────────────────────────────┘
                          │ HTTP / WebSerial
┌─────────────────────────┴────────────────────────────────┐
│ HTTP server  (mbed httplib, no TLS in v0.1)              │
└─────────────────────────┬────────────────────────────────┘
                          │
┌─────────────────────────┴────────────────────────────────┐
│ ModbusMasterCore                                         │
│   - probe(slaveId, baud, parity)                         │
│   - readHolding(slave, addr, count) -> std::vector<u16>  │
│   - readInput  (slave, addr, count) -> std::vector<u16>  │
│   - writeSingle(slave, addr, value)                      │
│   - writeMultiple(slave, addr, std::vector<u16>)         │
│   - reportSlaveId(slave)                                 │
│   - diagnostics(slave, sub)                              │
│   ALL wrap RS485 with the proven Opta TX bracket:        │
│     noReceive + beginTransmission + write + flush        │
│     + delay(1) + endTransmission + receive               │
│   AND set RS485.setDelays(0, ceil(11/baud * 1e6)) at     │
│   begin() to avoid the last-byte-corruption bug          │
│   (Arduino forum thread #1421875 post #18).              │
└─────────────────────────┬────────────────────────────────┘
                          │
┌─────────────────────────┴────────────────────────────────┐
│ DeviceProfiles  (Morningstar register maps as data)      │
│   - sunsaver-mppt.json   (verified by bench, 2026-04)    │
│   - sunsaver-duo.json                                    │
│   - tristar-mppt.json                                    │
│   - prostar-mppt.json                                    │
│   - prostar-gen3.json                                    │
│   Each profile lists: { name, address, type, scaleNum,   │
│   scaleDen, units, group, writable, enumValues }.        │
│   Profiles are loaded from LittleFS, not compiled-in,    │
│   so a profile fix does not require a new firmware       │
│   build.                                                 │
└──────────────────────────────────────────────────────────┘
```

## 5. Why the register maps live in JSON, not C headers

This is the lesson from the TankAlarm SunSaver bring-up: the legacy
`SS_REG_BATTERY_VOLTAGE = 0x0012` was wrong on the actual hardware. With
register maps as data, fixing a profile in the field is a file upload over
the web UI, not a firmware re-flash. Each profile carries its own version and
"verified-against" metadata so we know which units have been bench-confirmed.

Profile schema (proposal):

```json
{
  "profile": "sunsaver-mppt",
  "version": "2026-04-22",
  "verifiedBy": "TankAlarm bench, MPPT-15L unit, MRC-1 bridge",
  "modbus": {
    "defaultSlaveId": 1,
    "defaultBaud": 9600,
    "defaultParity": "8N2",
    "minPostDelayUs": 1200
  },
  "scales": {
    "voltage12V": { "num": 96.667, "den": 32768 },
    "current12V": { "num": 79.16,  "den": 32768 }
  },
  "registers": [
    { "addr": "0x0008", "name": "adc_vb_f",   "type": "u16",  "scale": "voltage12V", "units": "V",  "group": "live",   "writable": false },
    { "addr": "0x0009", "name": "adc_va_f",   "type": "u16",  "scale": "voltage12V", "units": "V",  "group": "live",   "writable": false },
    { "addr": "0x000A", "name": "adc_vl_f",   "type": "u16",  "scale": "voltage12V", "units": "V",  "group": "live",   "writable": false },
    { "addr": "0x000B", "name": "adc_ic_f",   "type": "u16",  "scale": "current12V", "units": "A",  "group": "live",   "writable": false },
    { "addr": "0x000C", "name": "adc_il_f",   "type": "u16",  "scale": "current12V", "units": "A",  "group": "live",   "writable": false },
    { "addr": "0x001B", "name": "T_hs",       "type": "i16",  "units": "C",                          "group": "live",   "writable": false },
    { "addr": "0x002B", "name": "charge_state","type":"enum8","enumValues": ["START","NIGHT_CHECK","DISCONNECT","NIGHT","FAULT","BULK","ABSORPTION","FLOAT","EQUALIZE"], "group": "status", "writable": false }
  ]
}
```

Note: addresses with `?` or `unverified: true` are surfaced in red in the UI
until a bench capture confirms them.

## 6. UX flows

### 6.1 First-time probe wizard

1. User opens OptaView WiFi AP from a phone, browses to `192.168.4.1`.
2. Page asks: "What are you connecting to?" with three buttons:
   - "Morningstar SunSaver / Tristar via MRC-1 (MeterBus->RS-485)"
   - "Generic Modbus RTU device"
   - "I don't know — sniff"
3. Probe runs the proven baud + parity + slave sweep; for SunSaver-class targets,
   it specifically probes 9600 8N2 and 9600 8N1 first, slave IDs 1 then 247.
4. If probe fails, the page shows the **MRC-1 LED diagnostic ladder** documented
   in `CODE REVIEW/OPTA_RS485_DIAGNOSTIC_GUIDE.md` (to be created in the new
   repo) — including the "swap A and B at the Opta end" hint and the
   forum-fix `setDelays(0, 1042)` reminder.

### 6.2 Live monitor

A read-only dashboard that polls the `live` register group every 1 s
(configurable). Same data the original MSView "Meters" tab shows: battery V,
array V, charge I, load I, charge state, faults, alarms, heatsink T.

### 6.3 Configuration editor

Loads the `eeprom` register group, presents one form per logical setting
(battery type, V_LVD, V_LVR, V_HVD, V_HVR, etc.), and writes via FC 0x10
(write multiple) so the SunSaver does the change atomically.

A change set is **always confirmed** in a modal that shows old vs new values
before writing.

### 6.4 Snapshot / restore

- **Snapshot** = read every documented register, save as a JSON blob to the
  Opta's LittleFS, optionally download to the phone.
- **Restore** = upload a JSON blob, diff it against the current EEPROM, present
  the diff for approval, then write.

This is the OptaView equivalent of the MSView .cfg file flow. JSON is a
human-readable superset.

## 7. Reusing what TankAlarm has already proven

OptaView starts NOT from scratch; it starts from these TankAlarm artifacts:

| TankAlarm asset | OptaView reuse |
|-----------------|---------------|
| [TankAlarm-112025-Common/src/TankAlarm_Solar.cpp](../TankAlarm-112025-Common/src/TankAlarm_Solar.cpp) | Lift `RS485.setDelays(0, 1200)` and the TX bracket pattern verbatim. |
| [firmware/sunsaver-rs485-raw/](../firmware/sunsaver-rs485-raw/) | Lift the manual Modbus framing / CRC routines as the lowest-level fallback. |
| [firmware/sunsaver-rs485-passive-sniff/](../firmware/sunsaver-rs485-passive-sniff/) | Becomes the OptaView "sniff bus" diagnostic mode. |
| [firmware/sunsaver-rs485-full-id-scan/](../firmware/sunsaver-rs485-full-id-scan/) | Becomes the OptaView "find my slave" probe. |
| [memory note `sunsaver-modbus-bringup-2026-04-21.md`](../) | Becomes the seed for the OptaView troubleshooting documentation. |

## 8. Repository layout (proposed)

```
OptaView/
├── README.md
├── LICENSE
├── docs/
│   ├── opta-rs485-diagnostic-guide.md      # The MRC-1 LED ladder + setDelays story
│   ├── morningstar-protocol-notes.md       # 8N2, MARK polarity, MRC-1 bridge
│   └── adding-a-device-profile.md
├── firmware/
│   └── OptaView/
│       ├── OptaView.ino                    # Top-level (small)
│       ├── ModbusMasterCore.h/.cpp         # The hardened TX bracket layer
│       ├── DeviceProfile.h/.cpp            # JSON profile loader
│       ├── HttpUi.h/.cpp                   # Mongoose / mbed httplib server
│       └── ProbeWizard.h/.cpp              # First-time bring-up flow
├── profiles/                               # Loaded onto LittleFS at flash time
│   ├── sunsaver-mppt.json                  # Verified 2026-04-22
│   ├── sunsaver-duo.json                   # Unverified
│   ├── tristar-mppt.json                   # Unverified
│   └── prostar-mppt.json                   # Unverified
├── tools/
│   ├── pack-profiles.py                    # Bundles JSON profiles into LittleFS image
│   └── verify-profile.py                   # Lints profile JSON against a schema
├── tests/
│   ├── modbus-tx-bracket-bench/            # Standalone Opta sketch that asserts
│   │   └── modbus-tx-bracket-bench.ino     # the post-delay fix is in effect
│   └── profile-schema/                     # JSON-schema validation
└── .github/
    └── workflows/
        ├── arduino-ci.yml                  # Compile firmware against arduino-cli
        └── profile-lint.yml                # Validate every JSON profile on push
```

## 9. Out of scope for v0.1

These are explicitly NOT in the first release, to keep scope honest:

- TLS / HTTPS — local-only WiFi AP, no internet exposure.
- Authentication — assume physical access to the Opta is the security boundary.
- Firmware OTA — the user re-flashes via USB.
- Logging beyond a rolling in-memory event ring.
- Cloud sync of snapshots.
- Non-Morningstar profiles (Outback, Victron, etc.) — proven generic Modbus
  is the foundation; Morningstar is the only profile shipped in v0.1.

## 10. Success criteria for v0.1

OptaView v0.1 is "done" when, **on a SunSaver MPPT-15L via MRC-1**, all of
the following are achievable from a phone browser without a Windows machine:

1. Probe identifies the unit within 5 seconds.
2. Live battery V matches a multimeter measurement to within 0.1 V.
3. A write to V_LVD (low-voltage disconnect) is accepted, persisted in
   EEPROM, and round-trips correctly on the next read.
4. A snapshot saved on the Opta can be downloaded as JSON, edited, uploaded
   back, and applied with a confirmation diff.
5. The "what to do if it doesn't work" wizard surfaces the LED ladder,
   the Morningstar A/B polarity note, and the post-delay reminder.

## 11. Owner / next step

- Repository creation owner: TankAlarm maintainer.
- First commit: import this design doc + the four lifted TankAlarm files
  (Modbus core, raw probe, passive sniff, full ID scan), all unmodified
  except for namespace adjustments.
- First PR: replace `RS485.setDelays(50, 50)` with `RS485.setDelays(0, 1200)`
  in every lifted source file as the very first change, with a comment
  pointing back to the forum thread and to this design doc.

## 12. Cross-references

- TankAlarm production fix: [TankAlarm-112025-Common/src/TankAlarm_Solar.cpp](../TankAlarm-112025-Common/src/TankAlarm_Solar.cpp)
- TankAlarm bench history (memory): see repo memory file `sunsaver-modbus-bringup-2026-04-21.md`
- Forum root cause: <https://forum.arduino.cc/t/opta-rs485-bug-last-byte-of-any-frame-is-modified-crc-always-wrong/1421875/18>
- Morningstar tech-support email (paraphrased): Terminal A on Morningstar
  controllers is inverting / MARK=low (DATA-); Terminal B is non-inverting /
  MARK=high (DATA+). Most modern adapters use the opposite convention, so wires
  must be crossed (Opta A → MRC-1 B, Opta B → MRC-1 A, GND → G).
