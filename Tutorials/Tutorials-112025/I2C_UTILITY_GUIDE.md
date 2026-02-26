# TankAlarm I2C Utility Guide

**Diagnose and Recover Notecard I2C Communication on Client, Server, and Viewer Devices**

---

## Introduction

The TankAlarm I2C Utility is a dedicated diagnostic sketch for verifying and repairing communication between Arduino Opta devices and the Blues Notecard. Instead of editing production firmware to test I2C behavior, you can upload this utility, run guided checks, apply recovery actions, and then return to normal firmware.

This guide follows a step-by-step workflow so field technicians can isolate failures quickly.

### What You'll Learn

- How to run a full I2C scan from an Opta
- How to detect Notecard address mismatches
- How to validate Notecard health requests (`hub.get`, `card.version`, `card.wireless`)
- How to reset Notecard I2C address back to default (`0x17`)
- How to run safe and destructive recovery actions intentionally

### Target Devices

Use this guide with any TankAlarm 112025 role:

- **Client** devices
- **Server** devices
- **Viewer** devices

### Utility Sketch Location

The utility sketch is located at:

- `TankAlarm-112025-I2C_Utility/TankAlarm-112025-I2C_Utility.ino`

---

## Required Materials

### Hardware

- Arduino Opta device (Lite/Advanced) configured as Client, Server, or Viewer
- Blues Wireless for Opta with Notecard installed
- USB-C data cable
- Stable power source for the Opta

### Software

- Arduino IDE 2.0+
- Required libraries already used in this repo (including Blues Notecard)
- This repository cloned locally

### Suggested Reading

- [I2C Communication Basics](https://learn.sparkfun.com/tutorials/i2c)
- [TankAlarm Troubleshooting Guide](TROUBLESHOOTING_GUIDE.md)

---

## Before You Start

### Safety and Operational Notes

- Run this utility during a maintenance window when possible.
- While utility firmware is loaded, normal tank telemetry and control logic are not running.
- The `card.restore` command is **destructive** and should only be used when standard recovery fails.

### Expected Serial Settings

- **Baud Rate:** `115200`
- **Line Ending:** `Newline` (recommended)

---

## Step 1: Upload the Utility Sketch

1. Open Arduino IDE.
2. Open `TankAlarm-112025-I2C_Utility/TankAlarm-112025-I2C_Utility.ino`.
3. Select the correct board and COM port.
4. Upload the sketch.
5. Open Serial Monitor at `115200` baud.

After boot, you should see:

- Utility banner
- Automatic I2C scan
- Command menu

**Checkpoint:** The serial monitor shows command options including `s`, `a`, `d`, and `r`.

---

## Step 2: Run an I2C Bus Scan

From Serial Monitor, send:

- `s`

The utility will print every detected I2C address.

### How to Interpret Results

- If you see `0x17`, the Notecard default address is present.
- If you do not see `0x17`, the Notecard may:
  - Be unpowered or not seated
  - Have an alternate address configured
  - Have a hardware or bus issue

**Checkpoint:** At least one expected device appears on the I2C bus.

---

## Step 3: Attach to the Notecard

Use one of these options:

- `a` = auto-detect Notecard
- `n` = manually enter a hex address

### Recommended Sequence

1. Try `a` first.
2. If auto-detect fails, run `n` and test likely addresses (`17`, `18`, etc.).

If attach succeeds, the utility validates `card.version` and confirms the active address.

**Checkpoint:** Serial output confirms the Notecard is attached and responsive.

---

## Step 4: Run Diagnostics

Send:

- `d`

This runs:

- `hub.get`
- `card.version`
- `card.wireless`

### Pass Criteria

- Requests return JSON responses
- No persistent `{io}` transport errors
- No null response failures

If diagnostics fail with transport-level errors, focus on physical bus integrity before cloud configuration.

**Checkpoint:** `Diagnostics complete: PASS`.

---

## Step 5: Apply Recovery Actions (If Needed)

Use recovery commands in this order (least disruptive to most disruptive).

### 5.1 Force Notehub Sync

- Command: `y`
- Action: Sends `hub.sync`
- Use when comms are up but cloud delivery appears delayed

### 5.2 Re-apply Hub Configuration

- Command: `u`
- Action: Sends `hub.set` using values in sketch constants
- Use when Product UID or fleet configuration is suspected incorrect

> Edit `PRODUCT_UID`, `DEVICE_FLEET`, and `HUB_MODE` in the sketch before upload if you plan to use `u`.

### 5.3 Reset I2C Address to Default

- Command: `r`
- Action: Sends `card.io` with `{"i2c": -1}` and re-attaches at `0x17`
- Use when the Notecard address was changed in past testing

### 5.4 Factory Restore (Destructive)

- Command: `x`
- Action: Sends `card.restore` with `{"delete": true}`
- Requires typed confirmation: `RESTORE`

Use only when all non-destructive actions fail.

**Checkpoint:** After any recovery action, re-run `a`, then `d` to verify success.

---

## Common Scenarios

### Scenario A: `0x17` Not Found in Scan

Likely causes:

- Notecard not seated correctly
- Power issue on Notecard/carrier
- SDA/SCL bus problem
- Notecard address changed

Actions:

1. Power down and reseat hardware
2. Re-scan (`s`)
3. Auto-attach (`a`)
4. Manual attach (`n`) for alternate addresses

### Scenario B: Attach Works, Diagnostics Fail with `{io}`

Likely causes:

- Unstable I2C bus
- Intermittent power
- Hardware fault on carrier or Notecard

Actions:

1. Verify stable supply voltage
2. Try a different USB cable and power source
3. Re-run scan and diagnostics
4. Swap Notecard into known-good hardware to isolate board vs module

### Scenario C: Diagnostics Pass but System Still Offline in Notehub

Likely causes:

- Product UID mismatch
- Fleet or route configuration mismatch
- Cloud-side routing issues

Actions:

1. Use `u` (hub.set) with verified Product UID
2. Use `y` (hub.sync)
3. Validate routes in Notehub
4. Review [NOTEHUB_ROUTES_SETUP.md](NOTEHUB_ROUTES_SETUP.md)

---

## Returning to Production Firmware

After diagnostics/recovery:

1. Re-open the target production sketch (Client/Server/Viewer).
2. Upload normal firmware back to device.
3. Verify serial startup logs.
4. Confirm telemetry/commands flow in dashboard and Notehub.

**Final Checkpoint:** Production firmware reports normal Notecard operation without `{io}` errors.

---

## Quick Command Reference

| Command | Action |
|---------|--------|
| `h` / `?` | Show menu |
| `s` | Scan I2C bus |
| `p` | Print attach/status |
| `a` | Auto-detect and attach Notecard |
| `n` | Manual Notecard address attach |
| `d` | Run diagnostics (`hub.get`, `card.version`, `card.wireless`) |
| `u` | Send `hub.set` |
| `y` | Send `hub.sync` |
| `r` | Reset I2C address to default (`0x17`) |
| `x` | Factory restore (`card.restore`) |

---

## Troubleshooting Checklist

Before escalating support, confirm all of the following:

- [ ] I2C scan shows expected devices
- [ ] Utility can attach to Notecard
- [ ] `d` diagnostics pass
- [ ] Address reset (`r`) attempted if needed
- [ ] Hub config (`u`) validated with correct Product UID
- [ ] Cloud route setup verified
- [ ] Production firmware re-uploaded and verified

---

## Related Guides

- [Troubleshooting Guide](TROUBLESHOOTING_GUIDE.md)
- [Client Installation Guide](CLIENT_INSTALLATION_GUIDE.md)
- [Server Installation Guide](SERVER_INSTALLATION_GUIDE.md)
- [Firmware Communication Guide](FIRMWARE_COMMUNICATION_GUIDE.md)
- [Notehub Routes Setup](NOTEHUB_ROUTES_SETUP.md)
