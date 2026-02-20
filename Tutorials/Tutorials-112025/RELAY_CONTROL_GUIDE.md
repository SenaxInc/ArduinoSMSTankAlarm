# TankAlarm Relay Control and Automation Guide

**Automating Equipment Control with Arduino Opta Built-in Relays**

---

## Introduction

The Arduino Opta includes 4 built-in industrial-grade relays that can be controlled remotely via cellular connection, enabling automated responses to tank conditions without site visits. This guide covers setup, configuration, and practical applications for relay-based automation.

### What You'll Learn

- Understanding Arduino Opta relay capabilities
- Wiring external equipment safely
- Manual relay control via dashboard
- Automated alarm-triggered responses
- Device-to-device relay coordination
- Safety interlocks and best practices
- Troubleshooting relay issues

### What You Can Automate

- **Pumps**: Turn on/off based on tank levels
- **Valves**: Open/close for fill/drain operations
- **Alarms**: Activate sirens, strobe lights, bells
- **Indicators**: Warning lights, status beacons
- **Equipment**: Disable machinery during alarms
- **Notifications**: Physical alarm panels

### Prerequisites

- [ ] Client device installed (see [Client Installation Guide](CLIENT_INSTALLATION_GUIDE.md))
- [ ] Understanding of electrical safety
- [ ] Equipment to control (pumps, lights, etc.)
- [ ] Basic wiring skills
- [ ] Access to server dashboard

---

## Hardware Overview

### Arduino Opta Relay Specifications

**Built-in Relays:**
- **Quantity**: 4 mechanical relays
- **Designation**: D0, D1, D2, D3
- **Type**: SPST (Single Pole Single Throw) normally open
- **Contact Rating**: 10A @ 250VAC or 30VDC
- **Switching**: Mechanical (audible click)
- **LED Indicators**: Status LEDs for each relay

**Relay Contact Specifications:**

| Parameter | Rating |
|-----------|--------|
| **Max Voltage** | 250VAC / 30VDC |
| **Max Current** | 10A resistive |
| **Max Power** | 2500VA / 300W |
| **Contact Life** | 100,000 cycles (rated load) |
| **Operate Time** | <10ms |
| **Release Time** | <5ms |

**Important Safety Limits:**
- ⚠️ **Never exceed 10A continuous**
- ⚠️ **AC loads**: Max 250VAC
- ⚠️ **DC loads**: Max 30VDC
- ⚠️ **Inductive loads**: Use suppression (see below)

### Relay Terminal Layout

```
Arduino Opta Top View:
┌────────────────────────────────┐
│  D0   D1   D2   D3            │
│  ┌─┐  ┌─┐  ┌─┐  ┌─┐           │
│  │N│  │N│  │N│  │N│    Relay  │
│  │O│  │O│  │O│  │O│  Terminals│
│  │C│  │C│  │C│  │C│           │
│  └─┘  └─┘  └─┘  └─┘           │
│                                │
│  COM COM COM COM      Common   │
│   │   │   │   │     Terminals │
└────────────────────────────────┘

NO = Normally Open contact (closes when energized)
COM = Common terminal (connects to NO when energized)
```

**Wiring:**
- **NO (Normally Open)**: Connects to COM when relay activates
- **COM (Common)**: Always connects to load
- **NC (Normally Closed)**: Not available on Opta

---

## Safety and Electrical Considerations

### Electrical Safety

⚠️ **DANGER - HIGH VOLTAGE**
- Relays can switch dangerous voltages (up to 250VAC)
- Only qualified electricians should wire AC mains
- Follow local electrical codes
- Use proper insulation and strain relief

**Safety Requirements:**

1. **Power Off**: De-energize all circuits before wiring
2. **Lockout/Tagout**: Prevent accidental energization
3. **Proper Rating**: Ensure wire gauge matches load
4. **Fusing**: Install appropriate fuses/breakers
5. **Enclosure**: Use rated electrical enclosure for AC mains

### Inductive Load Protection

**Inductive loads** (motors, solenoids, contactors) generate voltage spikes when de-energized.

**Protection Required:**

**AC Loads:**
```
Relay Contact
    |
    ├──[Load (Motor)]──┐
    |                   |
  [MOV or            [AC Line]
   RC Snubber]         |
    |                   |
    └───────────────────┘
```

**MOV (Metal Oxide Varistor):**
- Clamps voltage spikes
- Install across relay contacts
- Example: 275VAC MOV for 120VAC circuits

**RC Snubber:**
- 0.1µF capacitor + 100Ω resistor in series
- Absorbs switching energy
- Extends relay contact life

**DC Loads:**
```
    +V
     |
  [Diode] ← Flyback diode (1N4007 or equivalent)
  Cathode to +V
     |
  [Load (Solenoid)]
     |
  Relay Contact
     |
    GND
```

**Flyback Diode:**
- Must be rated for load current
- Anode to load, cathode to positive
- Prevents reverse voltage spike

### Wire Sizing

**Current Carrying Capacity:**

| Wire Gauge (AWG) | Max Current | Typical Use |
|------------------|-------------|-------------|
| 18 AWG | 7A | Indicators, small loads |
| 16 AWG | 10A | Relays, medium pumps |
| 14 AWG | 15A | Large pumps, heaters |
| 12 AWG | 20A | High-power equipment |

**Always:**
- Use wire rated for environment (wet/dry)
- Secure connections with proper terminals
- Strain relief at entry points
- Label all wiring clearly

---

## Wiring Examples

### Example 1: 120VAC Pump Control

**Application**: Control fill pump based on low tank alarm

**Wiring:**
```
        120VAC Hot ──┬───[Breaker 15A]───┐
                      │                    │
                      │                [Pump Motor]
                      │                    │
                      │                    │
                    [Fuse]             Relay NO (D0)
                      │                    │
                      └────────────────Relay COM
                      
        120VAC Neutral ─────────────[Pump Neutral]
        
        Ground ────────────────────[Pump Ground]
                              (via proper grounding)
```

**Components:**
- 15A circuit breaker
- 10A fuse near relay
- 16 AWG wire minimum
- MOV across pump for spike protection
- Proper electrical box for junction

**Safety Notes:**
- Licensed electrician should wire AC mains
- Ground pump motor frame
- Use GFCI protection if required by code
- Test with pump disconnected first

### Example 2: 12VDC Solenoid Valve

**Application**: Open valve when tank high alarm triggers

**Wiring:**
```
    +12VDC ─┬───[Fuse 5A]───┬───[Solenoid Valve]───┐
            │                │                       │
            │              [Flyback Diode]       Relay NO (D1)
            │              (Cathode to +12V)         │
            │                                    Relay COM
            │                                         │
    Ground ─┴─────────────────────────────────────────┘
```

**Components:**
- 12VDC power supply (2A minimum)
- 5A fuse
- 1N4007 flyback diode (or equivalent)
- 18 AWG wire minimum
- Solenoid valve rated for 12VDC

**Safety Notes:**
- Verify solenoid coil voltage matches (12VDC)
- Check current draw (usually 0.5-2A)
- Flyback diode prevents relay damage
- Secure power supply properly

### Example 3: Strobe Light / Beacon

**Application**: Activate warning light during any alarm

**Wiring (24VDC Strobe):**
```
    +24VDC ───[Fuse 3A]───[Strobe Light +]
                                  |
                              Relay NO (D2)
                                  |
                              Relay COM
                                  |
    Ground ───────────────[Strobe Light -]
```

**Components:**
- 24VDC strobe beacon (LED type recommended)
- 3A fuse
- 18 AWG wire
- Mounting hardware

**Alternative (120VAC Strobe):**
- Same as pump example but with strobe
- Use UL-listed strobe for AC operation
- Install in NEMA rated enclosure if outdoor

### Example 4: Multi-Relay Coordination

**Application**: High alarm triggers pump shutoff AND warning light

**Wiring:**
```
Relay D0 (Pump):
    120VAC ───[Breaker]───[Pump]───NO/COM

Relay D1 (Warning Light):
    24VDC ───[Fuse]───[Strobe]───NO/COM

Configuration:
  relayMask = 3  (binary: 0011 = relays D0 + D1)
```

**Result:**
- Both relays activate simultaneously
- Pump turns on, warning light illuminates
- Single alarm condition triggers both actions

---

## Configuration

### Understanding Relay Masks

Relays are addressed using **bitmask** values:

```
Relay:     D3    D2    D1    D0
Bit:       3     2     1     0
Weight:    8     4     2     1
           ↓     ↓     ↓     ↓
Binary:   [0]   [0]   [0]   [1]  = Decimal 1 (D0 only)
Binary:   [0]   [1]   [0]   [1]  = Decimal 5 (D0 + D2)
Binary:   [1]   [1]   [1]   [1]  = Decimal 15 (All relays)
```

**Common Relay Masks:**

| Decimal | Binary | Active Relays | Use Case |
|---------|--------|---------------|----------|
| 0 | 0000 | None | Disabled |
| 1 | 0001 | D0 | Single relay |
| 2 | 0010 | D1 | Single relay |
| 3 | 0011 | D0, D1 | Dual action |
| 4 | 0100 | D2 | Single relay |
| 5 | 0101 | D0, D2 | Alternating relays |
| 7 | 0111 | D0, D1, D2 | Three relays |
| 8 | 1000 | D3 | Single relay |
| 15 | 1111 | All | Emergency shutdown |

### Client Configuration via Dashboard

**Step 1: Access Configuration**

1. Open server dashboard: `http://<server-ip>/`
2. Select client device from dropdown
3. Click **"Configure"** or **"Edit Configuration"**

**Step 2: Configure Tank Relay Triggers**

For each tank, set:

```json
{
  "tanks": [
    {
      "id": "A",
      "name": "Primary Storage Tank",
      "highAlarm": 90.0,
      "lowAlarm": 10.0,
      
      "relayTargetClient": "dev:864475044012345",
      "relayMask": 1,
      "relayOnHigh": true,
      "relayOnLow": false
    }
  ]
}
```

**Configuration Fields:**

- **`relayTargetClient`**: Device UID to control
  - Get from Notehub Devices tab
  - Format: `dev:IMEI` (e.g., "dev:864475044012345")
  - Leave empty `""` to disable relay control
  - Can target self (same client) or another client

- **`relayMask`**: Which relays to activate (see table above)
  - Value 0-15
  - Bitmask of relays D0-D3

- **`relayOnHigh`**: Activate relays when high alarm triggers
  - `true`: Relays turn ON during high alarm
  - `false`: No action on high alarm

- **`relayOnLow`**: Activate relays when low alarm triggers
  - `true`: Relays turn ON during low alarm
  - `false`: No action on low alarm

**Step 3: Send Configuration**

1. Click **"Send Configuration to Device"**
2. Server transmits via Notehub
3. Client receives and applies within 5-10 minutes
4. Serial monitor confirms: `"Relay config updated"`

### Finding Device UIDs

**Method 1: Notehub**
1. Log into [notehub.io](https://notehub.io)
2. Navigate to your product
3. Click **Devices** tab
4. Find device in list
5. UID shown in **"Device UID"** column (e.g., `dev:864475044012345`)

**Method 2: Server Dashboard**
1. Open dashboard
2. Client dropdown shows UIDs in parentheses
3. Example: `"Site A Tank Monitor (dev:864475044012345)"`

**Method 3: Serial Monitor**
1. Connect to client via USB-C
2. Open serial monitor (115200 baud)
3. Watch for startup: `"Device UID: dev:864475044012345"`

---

## Operation Modes

### Mode 1: Manual Control via Dashboard

**Purpose**: Operator-initiated relay control

**Steps:**
1. Open server dashboard
2. Locate client in telemetry table
3. Find relay control buttons: **R1 R2 R3 R4**
4. Click button to toggle relay
5. Button highlights when relay is ON
6. Command sent immediately via cellular

**Use Cases:**
- Testing relay wiring
- Manual pump operation
- Emergency equipment control
- Maintenance mode activation

**Response Time:**
- **Continuous mode**: 30-60 seconds
- **Periodic mode**: Up to 10 minutes (next sync)
- **Instant**: Use serial monitor for immediate control

### Mode 2: Automatic Alarm-Triggered Control

**Purpose**: Automatic response to tank conditions

**How It Works:**

1. **Tank reaches alarm threshold** (high or low)
2. **Client evaluates relay configuration**:
   - Is `relayTargetClient` set?
   - Is `relayMask` non-zero?
   - Does alarm type match `relayOnHigh` or `relayOnLow`?
3. **If conditions met**:
   - Client sends `relay_forward.qo` to server
   - Server receives via Route #1 and re-dispatches via `command.qo`
   - Target device receives `relay.qi` and activates relays
4. **Alarm clears**:
   - Relays can auto-deactivate (if configured)
   - Or remain active until manual intervention

**Example Scenario:**

```
Configuration:
  Tank A low alarm: 10 inches
  relayTargetClient: dev:123456 (pump control device)
  relayMask: 1 (relay D0)
  relayOnLow: true

Sequence:
  1. Tank A drops to 9 inches
  2. Low alarm triggers
  3. Client automatically sends: "Activate relay D0 on dev:123456"
  4. Pump control device receives command
  5. Pump relay D0 closes → Pump starts
  6. Tank begins filling
  7. Tank reaches 12 inches (alarm clears)
  8. (Optional) Relay D0 opens → Pump stops
```

### Mode 3: Server-Mediated Cross-Client Relay Forwarding

**Purpose**: Complex multi-site automation

**Architecture:**

```
Site A (Tank Monitor)         Site B (Pump Controller)
├── Tank level sensor         ├── Relay D0: Fill pump
├── Low alarm at 10"          ├── Relay D1: Warning light
└── Triggers relay mask=3     └── Receives commands

When Site A tank goes low:
  → Site B relay D0 activates (pump)
  → Site B relay D1 activates (light)
  → Both sites coordinate via cloud
```

**Configuration (Site A):**
```json
{
  "relayTargetClient": "dev:SITE_B_UID",
  "relayMask": 3,
  "relayOnLow": true
}
```

**Result:**
- No direct wiring between sites needed
- Commands routed through Blues Notehub
- Works across any distance with cellular coverage
- Latency: typically 1-5 minutes

---

## Advanced Automation Scenarios

### Scenario 1: Pump Protection

**Problem**: Prevent pump from running dry (damaging pump)

**Solution**: Cut power to pump when tank low alarm triggers

**Wiring:**
```
Pump Power ───[Main Breaker]───[Relay D0 NO/COM]───[Pump Motor]
                               (Normally Open)
```

**Configuration:**
```json
{
  "tanks": [{
    "id": "SOURCE",
    "lowAlarm": 10.0,
    "relayTargetClient": "dev:SELF",
    "relayMask": 1,
    "relayOnLow": false
  }]
}
```

**Logic:**
- Relay D0 normally OFF (open circuit, pump can't run)
- Operator manually activates relay to run pump
- If tank goes low, alarm forces relay OFF
- Pump protection even if operator forgets

### Scenario 2: Cascade Fill System

**Problem**: Fill multiple tanks sequentially

**Solution**: Coordinate relays across multiple clients

**System:**
```
Tank 1 (Primary)    → When full, trigger Tank 2 fill valve
Tank 2 (Secondary)  → When full, trigger Tank 3 fill valve
Tank 3 (Tertiary)   → When full, close all valves
```

**Configuration:**

**Tank 1 Client:**
```json
{
  "tanks": [{
    "highAlarm": 90.0,
    "relayTargetClient": "dev:TANK2_CLIENT",
    "relayMask": 1,
    "relayOnHigh": true
  }]
}
```

**Tank 2 Client:**
```json
{
  "tanks": [{
    "highAlarm": 90.0,
    "relayTargetClient": "dev:TANK3_CLIENT",
    "relayMask": 1,
    "relayOnHigh": true
  }]
}
```

**Result:**
- Automatic sequential filling
- No manual intervention required
- System coordinates via cloud

### Scenario 3: Emergency Shutdown

**Problem**: Shut down all equipment on critical alarm

**Solution**: Broadcast relay command to all clients

**Configuration (Critical Tank):**
```json
{
  "tanks": [{
    "id": "CRITICAL",
    "highAlarm": 95.0,
    "relayTargetClient": "dev:ALL_CLIENTS",
    "relayMask": 15,
    "relayOnHigh": true
  }]
}
```

**Note**: `dev:ALL_CLIENTS` requires custom firmware modification. Current firmware only supports device-specific targeting.

**Alternative Implementation:**
- Configure multiple `relayTargetClient` entries (one per client)
- Or use server-side logic to broadcast

---

## Relay State Management

### Persistent vs Non-Persistent State

**Current Behavior (Non-Persistent):**
- Relay states reset on power cycle
- After power loss, all relays default to OFF
- No EEPROM/flash storage of relay states

**Implications:**
- Safe default: equipment won't restart unexpectedly
- May require manual intervention after power outages
- Critical equipment needs external controls

**Future Enhancement:**
- Persistent relay state in LittleFS
- Configurable startup behavior per relay
- Remember last state vs safe default

### Auto-Off Timers (Future Feature)

**Planned Capability:**

```json
{
  "relay": 1,
  "state": true,
  "duration": 300
}
```

- **`duration`**: Seconds until auto-off (0 = manual)
- **Use Case**: Pulse control, timed operations
- **Example**: Run pump for 5 minutes then stop

**Current Workaround:**
- Use server-side scheduling
- Send ON command, wait, send OFF command
- Or implement in client firmware custom code

---

## Troubleshooting

### Relay Not Activating

**Check LED Indicator:**
- Does relay LED illuminate on Opta?
- Yes → Relay coil energizing, contact should close
- No → Command not received or firmware issue

**Serial Monitor Diagnostics:**

Connect to client USB-C, watch for:
```
✓ "Relay command received: D0 = ON"
✓ "Relay D0 activated"
✗ "Relay command ignored: invalid target"
✗ "Relay D0 failed: pin error"
```

**Common Causes:**

1. **No Cellular Connectivity:**
   - Check Notecard status LED
   - Verify signal strength in Notehub
   - Force sync: `{"req":"hub.sync"}`

2. **Wrong Target UID:**
   - Verify `relayTargetClient` matches actual device
   - Check for typos in UID
   - Confirm UID format: `dev:IMEI`

3. **Relay Mask Zero:**
   - `relayMask: 0` means no relays active
   - Must be 1-15 for any relay control

4. **Alarm Conditions Not Met:**
   - `relayOnHigh: false` but high alarm triggered
   - Check alarm thresholds configured correctly

### Physical Load Not Responding

**Relay activates but equipment doesn't run:**

**Check Wiring:**
1. **Continuity Test** (power OFF):
   - Measure continuity from relay NO to load
   - Should be OPEN when relay OFF
   - Should be CLOSED when relay ON

2. **Voltage Test** (power ON):
   - Measure voltage at load terminals
   - Should match source when relay ON
   - Should be zero when relay OFF

**Check Load:**
1. **Disconnect from relay**
2. **Apply power directly** to load
3. **If load doesn't run**: Problem is with load, not relay
4. **If load runs**: Problem is wiring or contacts

**Common Issues:**

- **Loose terminals**: Tighten all connections
- **Wrong wire gauge**: Replace with proper size
- **Failed relay contact**: Rare but possible (replace Opta)
- **Insufficient power**: Load draws more than relay can switch
- **Blown fuse**: Check inline fuses
- **Tripped breaker**: Reset circuit breaker

### Dashboard Buttons Not Working

**Symptoms:**
- Click relay button, nothing happens
- Button doesn't change color
- No error message

**Diagnostics:**

1. **Browser Console** (F12):
   ```
   Look for:
   - JavaScript errors
   - Failed API calls to /api/relay
   - Network timeouts
   ```

2. **Check Server Logs:**
   - Serial monitor on server Opta
   - Should show: `"Relay command queued for dev:123456"`
   - Error: `"Failed to send relay command"`

3. **Verify Dashboard State:**
   - Refresh page
   - Confirm client dropdown has device selected
   - Check that client UID is displayed

**Solutions:**

- **Clear browser cache**: Shift+Ctrl+R
- **Try different browser**: Chrome, Firefox, Edge
- **Check server Notecard**: Must be connected
- **Verify server firmware**: Latest version supports relay API

### Intermittent Relay Operation

**Symptoms:**
- Relay works sometimes, fails other times
- Unpredictable behavior
- Chattering (rapid on/off)

**Causes:**

1. **Weak Cellular Signal:**
   - Commands delayed or lost
   - Check signal strength in Notehub
   - Relocate Notecard antenna if needed

2. **Power Supply Issues:**
   - Brownouts during relay switching
   - Undersized power supply
   - Measure voltage during relay operation

3. **Inductive Kick:**
   - Voltage spikes from inductive load
   - Feedback into Opta power supply
   - Add snubber/flyback protection

4. **Multiple Command Sources:**
   - Dashboard command conflicts with automation
   - Race conditions in firmware
   - Review configuration for conflicts

**Solutions:**

- **Stable Power**: Use quality power supply, add capacitors
- **Spike Protection**: Install MOV/RC snubbers on loads
- **Single Control Source**: Disable one control method
- **Increase Command Timeout**: Allow more time between commands

---

## Best Practices

### Planning Relay Assignments

**Before Implementation:**

1. **Document Relay Map:**
   ```
   Client: Site A Main Controller
   
   D0: Fill pump (120VAC, 8A)
   D1: Drain valve (12VDC, 1.5A)
   D2: High level strobe (24VDC, 0.5A)
   D3: Low level alarm horn (24VDC, 0.3A)
   ```

2. **Group Related Functions:**
   - Keep emergency shutdown relays together
   - Coordinate relay masks logically
   - Example: Mask 3 (D0+D1) = pump AND valve

3. **Plan for Expansion:**
   - Leave spare relays for future needs
   - Don't assign all 4 immediately
   - Consider adding Opta devices for more relays

### Safety Interlocks

**Critical Equipment:**

**Never rely solely on TankAlarm for safety-critical shutdowns**
- Cellular delays can be several minutes
- Network outages can prevent commands
- Always use local safety controls as primary protection

**Recommended Architecture:**
```
Primary Safety: Local PLC or safety relay
  ↓ (hard-wired, <1 second response)
Equipment Shutdown

Secondary Notification: TankAlarm relay
  ↓ (cellular, 1-5 minute response)  
Operator Alert / Remote Actions
```

**Best Practice:**
- **TankAlarm relays**: Supervision, alerts, non-critical control
- **Local safety systems**: Emergency stops, critical interlocks
- **Both**: Defense in depth

### Testing Procedures

**Before Going Live:**

1. **Bench Test:**
   - Test relays with dashboard (no load connected)
   - Verify all 4 relays click and LEDs illuminate
   - Check serial monitor for correct commands

2. **Low-Voltage Load Test:**
   - Connect 12V light bulb to relay
   - Test manual control from dashboard
   - Test automatic triggering with simulated alarms

3. **Full Load Test (Supervised):**
   - Connect actual equipment
   - Test with qualified electrician present
   - Verify load operates correctly
   - Check for overheating, arcing, noise

4. **Failure Mode Testing:**
   - Disconnect cellular (simulate outage)
   - Verify safe behavior
   - Power cycle client and server
   - Confirm recovery after restore

### Maintenance

**Monthly:**
- Exercise all relays (click on/off)
- Prevents contact oxidation
- Verifies operation before emergency

**Quarterly:**
- Visual inspection of terminals
- Tighten connections
- Check for signs of arcing (blackening)

**Annually:**
- Full load testing
- Verify calibration of controlled equipment
- Review relay configuration for changes

---

## API Reference

### HTTP Endpoint: /api/relay

**Send relay command to client:**

```bash
POST /api/relay
Content-Type: application/json

{
  "clientUid": "dev:864475044012345",
  "relay": 2,
  "state": true
}
```

**Request Fields:**
- **`clientUid`** (string, required): Target client device UID
- **`relay`** (integer, required): Relay number 1-4
- **`state`** (boolean, required): true = ON, false = OFF

**Response:**

**Success (200 OK):**
```json
{
  "success": true,
  "message": "Relay command queued for dev:864475044012345"
}
```

**Error (400 Bad Request):**
```json
{
  "success": false,
  "error": "Invalid relay number (must be 1-4)"
}
```

**Error (500 Internal Server Error):**
```json
{
  "success": false,
  "error": "Failed to send Notecard command"
}
```

### Notecard Command Format

**Client receives in `relay.qi` notefile:**

```json
{
  "relay": 1,
  "state": true,
  "source": "server",
  "timestamp": 1704672000
}
```

**Fields:**
- **`relay`**: 1-4 (relay number)
- **`state`**: true/false (on/off)
- **`source`**: "server", "client-alarm", "manual"
- **`timestamp`**: Unix epoch (optional)

**Client Response:**
- Deletes message after processing
- Serial output confirms action
- No acknowledgment sent (future enhancement)

---

## Resources and Further Reading

### External Documentation

**Arduino Opta:**
- [Opta Datasheet](https://docs.arduino.cc/resources/datasheets/ABX00049-datasheet.pdf)
- [Opta Relay Specifications](https://docs.arduino.cc/hardware/opta#relays)
- [Opta User Manual](https://docs.arduino.cc/hardware/opta)

**Relay Theory:**
- [Relay Application Guide](https://www.te.com/commerce/DocumentDelivery/DDEController?Action=showdoc&DocId=Specification+Or+Standard%7F13C3191_D%7FY%7Fpdf%7FEnglish%7FENG_SS_13C3191_D.pdf%7F)
- [Understanding Relay Ratings](https://www.electronicdesign.com/technologies/power/article/21808254/understanding-relay-specifications)

**Electrical Safety:**
- [NFPA 70E - Electrical Safety](https://www.nfpa.org/codes-and-standards/all-codes-and-standards/list-of-codes-and-standards/detail?code=70E)
- [OSHA Electrical Standards](https://www.osha.gov/laws-regs/regulations/standardnumber/1910/1910.302)

### Related TankAlarm Guides

- [Client Installation Guide](CLIENT_INSTALLATION_GUIDE.md) - Initial setup
- [Advanced Configuration Guide](ADVANCED_CONFIGURATION_GUIDE.md) - Complex automations
- [Troubleshooting Guide](TROUBLESHOOTING_GUIDE.md) - Diagnostic procedures
- [Dashboard Guide](DASHBOARD_GUIDE.md) - Web interface usage

---

*Relay Control and Automation Guide v1.0 | Last Updated: January 7, 2026*  
*Compatible with TankAlarm Firmware 1.0.0+*
