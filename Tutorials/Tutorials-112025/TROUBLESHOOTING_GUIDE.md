# TankAlarm Troubleshooting Guide

**Diagnosing and Resolving Common Issues**

---

## Introduction

This comprehensive troubleshooting guide helps you diagnose and resolve issues with your TankAlarm system. Whether you're dealing with connectivity problems, sensor errors, or unexpected behavior, this guide provides systematic troubleshooting procedures.

### How to Use This Guide

1. **Identify the symptom** - What is the observable problem?
2. **Locate the section** - Find the matching symptom category
3. **Follow the diagnostic steps** - Work through checks systematically
4. **Apply the solution** - Implement the fix
5. **Verify resolution** - Confirm the problem is solved

### Diagnostic Tools

**Serial Monitor (Primary Tool):**
- Connect via USB-C cable
- Open Arduino IDE serial monitor
- Set baud rate to **115200**
- Observe real-time diagnostics

**Dashboard (Server):**
- Web interface at `http://<server-ip>/`
- Real-time client status
- Configuration verification
- Historical data inspection

**Blues Notehub (Cloud):**
- Device connection status
- Event logs
- Fleet membership
- Signal strength indicators

**Multimeter:**
- Verify sensor signals (4-20mA, 0-10V)
- Check power supply voltages
- Test relay operation
- Verify wiring continuity

---

## Quick Decision Tree

**Start here if unsure where to begin:**

```
Is the device powered on?
├─ No → Check power supply section
└─ Yes
   │
   Can you access serial monitor via USB?
   ├─ No → Check USB connection section
   └─ Yes
      │
      Does Notecard show connected?
      ├─ No → Check cellular connectivity section
      └─ Yes
         │
         Is telemetry reaching server/cloud?
         ├─ No → Check routing configuration section
         └─ Yes
            │
            Are sensor readings accurate?
            ├─ No → Check sensor troubleshooting section
            └─ Yes
               │
               Are alarms triggering correctly?
               ├─ No → Check alarm configuration section
               └─ Yes → System operational!
```

---

## Power and Hardware Issues

### Device Won't Power On

**Symptoms:**
- No LEDs lit
- Serial monitor shows no output
- Device unresponsive

**Diagnostic Steps:**

**1. Check Power Supply**

```
Test with multimeter:
- USB-C: Should provide 5V DC, ≥1A
- External power (if used): Check voltage matches specs
- Measure at Opta power terminals
```

**Expected Values:**
- USB-C: 5.0V ±0.25V
- Opta VCC: 5V rail active
- Notecard power indicator: LED on

**2. Check USB Cable**

- Try different USB-C cable (some are charge-only, no data)
- Test cable with another device
- Ensure cable fully inserted

**3. Check for Short Circuits**

- Disconnect all external wiring
- Remove Analog Expansion
- Remove Notecard carrier
- Power Opta alone - does it boot?

**4. Visual Inspection**

Look for:
- Burnt components (smell, discoloration)
- Damaged USB connector
- Cracked PCB
- Liquid damage

**Solutions:**

- **Bad cable**: Replace with known-good data cable
- **Insufficient power**: Use power supply ≥2A capacity
- **Short circuit**: Identify and fix wiring error
- **Hardware damage**: Replace Opta unit

### Intermittent Power Loss

**Symptoms:**
- Device randomly reboots
- "Uptime" in serial monitor keeps resetting
- Data gaps in historical telemetry

**Diagnostic Steps:**

**1. Check Serial Monitor for Brownout**

```
Watch for messages like:
"Brownout detector triggered"
"Watchdog reset"
"Unexpected restart"
```

**2. Measure Supply Voltage Under Load**

```
With multimeter monitoring VCC:
- Normal idle: ~5.0V
- During cellular transmit: Should not drop >0.2V
- During relay activation: Should not drop >0.3V
```

**If voltage sags significantly:**
- Power supply inadequate
- Wiring too thin (voltage drop)
- Excessive load (too many relays active)

**3. Check Environmental**

- Temperature extremes (Opta rated 0-50°C)
- Condensation/moisture
- Vibration (loose connections)

**Solutions:**

- **Brownout**: Upgrade to higher-capacity power supply (2-3A)
- **Voltage drop**: Use thicker gauge wire for power
- **Environmental**: Add enclosure with appropriate IP rating
- **Loose connections**: Tighten all screw terminals
- **Relay load**: Stagger relay activation (don't turn all on simultaneously)

### Analog Expansion Not Detected

**Symptoms:**
- Serial monitor shows "Analog Expansion: NOT DETECTED"
- Sensor readings always 0.0 or error
- Configuration can't save analog settings

**Diagnostic Steps:**

**1. Check Physical Connection**

```
Verify:
- Expansion seated fully in header pins
- No bent pins
- Pins aligned correctly (not offset by one row)
```

**2. Check in Serial Monitor**

```
Look for at startup:
"Initializing Analog Expansion..."
"Analog Expansion: DETECTED" ← Should see this
  or
"Analog Expansion: NOT DETECTED" ← Problem
```

**3. Test I2C Communication**

```cpp
// In serial monitor, check for I2C errors
// Expansion uses I2C address 0x52 typically
```

**4. Inspect for Damage**

- Cracked solder joints
- Damaged header pins
- Bent/broken components

**Solutions:**

- **Poor seating**: Remove and firmly reseat expansion
- **Bent pins**: Carefully straighten with needle-nose pliers
- **Wrong orientation**: Match pin 1 markers on both boards
- **Hardware failure**: Replace Analog Expansion (AFX00006)
- **Incompatible firmware**: Verify expansion firmware version matches requirements

---

## Cellular Connectivity Issues

### Notecard Shows "Disconnected"

**Symptoms:**
- Serial monitor: `Notecard: DISCONNECTED`
- No telemetry in Blues Notehub
- Cloud icon shows offline

**Diagnostic Steps:**

**1. Check SIM Card**

```
Physical inspection:
- SIM card fully inserted (gentle click)
- Contacts clean (no oxidation)
- Correct size (Micro SIM for Notecard WBEX)
```

**2. Check Cellular Signal**

Access serial monitor and type:
```
card.location
```

Look for response:
```json
{
  "status": "GPS,WiFi,Triangulated {cell-triangulation}",
  "mode": "continuous",
  "rssi": -67,    ← Signal strength (dBm)
  "bars": 4       ← Signal bars (1-5)
}
```

**Signal Interpretation:**
| RSSI (dBm) | Bars | Quality |
|------------|------|---------|
| -50 to -70 | 5-4 | Excellent |
| -70 to -85 | 4-3 | Good |
| -85 to -100 | 3-2 | Fair |
| -100 to -110 | 2-1 | Poor |
| < -110 | 1-0 | No service |

**3. Check Notehub Device Status**

1. Log into [notehub.io](https://notehub.io)
2. Go to **Devices**
3. Find device by IMEI or serial number
4. Check "Last Seen" timestamp
5. Review event log for connection attempts

**4. Verify Product UID**

```
In serial monitor, check:
"Product UID: com.company.tankalarm:production"

Must match Notehub project!
```

**5. Check APN Settings (if custom APN required)**

Some carriers require specific APN configuration:

```cpp
// In firmware, verify:
card.wireless("+mode,auto,-apn,<your_apn>");
```

**Solutions:**

- **No SIM**: Insert activated SIM card
- **Poor signal**: Relocate Opta to area with better coverage
- **Wrong Product UID**: Reconfigure firmware with correct UID
- **Inactive SIM**: Verify cellular plan active (check with carrier)
- **APN issue**: Configure correct APN for carrier
- **Notecard failure**: Replace Notecard
- **External antenna**: Consider using external antenna in metal enclosures

### Telemetry Not Reaching Server

**Symptoms:**
- Notecard connected (events in Notehub)
- Server never receives client data
- Client not visible in server dropdown

**Diagnostic Steps:**

**1. Verify Fleet Configuration**

**In Notehub:**
- Client device in `tankalarm-clients` fleet?
- Server device in `tankalarm-server` fleet?
- Viewer device in `tankalarm-viewer` fleet?
- Devices in same Product?

**2. Check Routing**

**In Notehub → Routes:**
- Route from `tankalarm-clients` to `tankalarm-server`
- Route enabled (not paused)
- Route shows successful deliveries

**3. Check Server Status**

Access server serial monitor:
```
Look for:
"Incoming event from: dev:864475044012345"
"Decoded telemetry: {json}"
```

**If server receives but doesn't display:**
- Check server's client list storage (LittleFS full?)
- Server might need restart

**4. Verify Event Format**

In Notehub, examine event body:
```json
{
  "object": "tank_level",
  "dev_uid": "864475044012345",
  "level_A": 48.5,
  "sensor_A": 12.3,
  ...
}
```

Must match expected schema!

**Solutions:**

- **Wrong fleet**: Reassign devices to correct fleets
- **No route**: Create route from clients → server
- **Paused route**: Resume route in Notehub
- **Wrong object type**: Verify client object name matches server expectations
- **Server offline**: Verify server cellular/Ethernet connected
- **Server storage full**: Free space or archive old data

---

## Network Issues (Server)

### Dashboard Not Accessible

**Symptoms:**
- Cannot reach `http://<server-ip>/`
- Browser shows "Unable to connect"
- Timeout errors

**Diagnostic Steps:**

**1. Verify Server Power and Boot**

- Connect via USB serial monitor
- Look for: `"Web server started at: http://192.168.1.150/"`
- Note the IP address shown

**2. Check Network Connection**

**Ethernet LED indicators:**
- Link LED: Should be solid green (connected)
- Activity LED: Should blink (traffic)

**If no LEDs:**
- Ethernet cable unplugged or damaged
- Switch/router port dead
- PoE power issue (if used)

**3. Ping Test**

From computer on same network:
```bash
ping 192.168.1.150
```

**Expected:**
```
Reply from 192.168.1.150: bytes=32 time=2ms TTL=128
Reply from 192.168.1.150: bytes=32 time=1ms TTL=128
```

**If "Request timed out":**
- Wrong IP address
- IP conflict with another device
- Firewall blocking

**4. Check IP Configuration**

In serial monitor:
```
Look for:
"Ethernet: CONNECTED"
"IP Address: 192.168.1.150"
"Subnet: 255.255.255.0"
"Gateway: 192.168.1.1"
```

**Verify IP is in your network range!**

**5. Check DHCP vs Static**

**In firmware (or `ServerConfig.h`):**
```cpp
#define USE_STATIC_IP true  // or false
IPAddress static_ip(192, 168, 1, 150);
```

**If DHCP enabled:**
- Server gets IP from router automatically
- IP might change after reboot
- Check router's DHCP lease table

**Solutions:**

- **Wrong IP**: Access using correct IP from serial monitor
- **No link**: Replace Ethernet cable, try different switch port
- **IP conflict**: Change static IP to unused address
- **Wrong subnet**: Ensure computer and server on same subnet
- **DHCP not working**: Switch to static IP configuration
- **Firewall**: Add exception for server IP
- **Browser cache**: Try different browser or incognito mode

### Server Shows "Ethernet: DISCONNECTED"

**Symptoms:**
- Serial monitor repeatedly shows connection attempts
- No IP address assigned
- Dashboard inaccessible

**Diagnostic Steps:**

**1. Check Cable and Port**

- Swap Ethernet cable
- Try different switch/router port
- Test cable with other device

**2. Check DHCP Server**

If using DHCP:
- Router's DHCP server enabled?
- DHCP pool has available addresses?
- Server MAC address not blacklisted?

**3. Restart Network Stack**

```
Power cycle server:
1. Disconnect power
2. Wait 10 seconds  
3. Reconnect power
4. Watch serial monitor for connection
```

**4. Check for IP Conflict**

```bash
# On network computer:
arp -a | grep 192.168.1.150
```

Shows MAC address for IP - should match server's MAC

**5. Test Static IP**

Switch to static IP temporarily:
```cpp
#define USE_STATIC_IP true
IPAddress static_ip(192, 168, 1, 200);  // Different from any DHCP
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 255, 0);
```

**Solutions:**

- **Cable**: Replace with known-good CAT5e/CAT6 cable
- **Switch port**: Use different port or replace switch
- **DHCP exhausted**: Expand DHCP pool or use static IP
- **Conflict**: Change static IP to unused address
- **Hardware failure**: Replace Opta Ethernet module (unlikely)

---

## Sensor Problems

### Readings Always Show 0.0

**Symptoms:**
- Level always 0.0 inches (or cm)
- Sensor value 0.0 mA (or V)
- No response to physical level changes

**Diagnostic Steps:**

**1. Check Analog Expansion Detection**

```
Serial monitor should show:
"Analog Expansion: DETECTED"

If NOT DETECTED, see Analog Expansion section above
```

**2. Check Sensor Wiring**

**For 4-20mA sensor:**
```
Verify connections:
CH0+ ← Sensor signal (+)
CH0- ← Sensor signal (-)  
Power ← Sensor power (usually 12-24V DC)
```

**Color codes (common):**
- Brown: Power (+)
- Blue: Power (-)
- Black: Signal (+)
- White: Signal (-)

**3. Measure Sensor Output with Multimeter**

**4-20mA loop:**
```
Set multimeter to mA mode
Connect in series with loop
Expected: 4-20mA range

Empty tank: ~4.0 mA
Full tank: ~20.0 mA
```

**0-10V sensor:**
```
Set multimeter to VDC mode
Measure sensor output terminals
Expected: 0-10V range

Empty: ~0.0V
Full: ~10.0V
```

**4. Check Configuration**

In dashboard or serial monitor:
```json
{
  "tanks": [
    {
      "id": "A",
      "sensorType": "4-20mA",  ← Must match physical sensor!
      "channel": 0,            ← CH0 on expansion
      "enabled": true          ← Must be enabled
    }
  ]
}
```

**5. Test with Known Signal**

Disconnect sensor and inject known signal:
- 4-20mA: Use 12mA source (should read mid-range)
- 0-10V: Use 5V source (should read mid-range)

**Solutions:**

- **Wiring error**: Correct connections per sensor datasheet
- **Dead sensor**: Replace sensor
- **Wrong sensor type config**: Change to match (4-20mA vs 0-10V)
- **Wrong channel**: Move wire to correct CHx terminal
- **Expansion not detected**: Fix Analog Expansion connection
- **Open circuit**: Check for broken wire, loose terminal
- **Power supply off**: Verify sensor receives power

### Readings Wildly Inaccurate

**Symptoms:**
- Shows 90 inches when tank actually 45 inches
- Jumps erratically
- Negative values
- Values far outside tank dimensions

**Diagnostic Steps:**

**1. Verify Physical Measurement**

Manually measure tank level:
- Use dipstick or tape measure
- Measure from same reference point as sensor
- Note the true level

**2. Check Calibration**

Access calibration data:
```json
{
  "calibration": [
    {"sensor_mA": 4.2, "height_in": 2.0},
    {"sensor_mA": 12.3, "height_in": 48.5},
    {"sensor_mA": 19.8, "height_in": 94.0}
  ]
}
```

**Verify:**
- At least 2 points (3+ recommended)
- Covers full range of tank
- Heights match tank dimensions
- Sensor values match readings

**3. Check Raw Sensor Value**

In serial monitor:
```
Look for:
"Tank A: Sensor=12.34 mA → Level=48.5 in"
```

**Is sensor value reasonable?**
- 4-20mA sensor should never show <4 or >20
- 0-10V sensor should stay 0-10V range

**4. Check for Interference**

Possible sources:
- Nearby motors/pumps (electrical noise)
- Long unshielded sensor wires
- Power supply noise
- Poor grounding

**5. Verify Tank Configuration**

```json
{
  "tankHeight": 96.0,  ← Must match physical tank!
  "fullLevel": 90.0,   ← Usable capacity
  "emptyLevel": 6.0    ← Heel/unusable
}
```

**Solutions:**

- **Never calibrated**: Perform calibration (see Calibration Guide)
- **Bad calibration points**: Recalibrate with accurate measurements
- **Wrong tank height**: Correct tankHeight parameter
- **Sensor failure**: Replace sensor (drifting/damaged)
- **Electrical noise**: Add shielded cable, ferrite cores, or filter capacitors
- **Wrong units**: Verify inches vs centimeters setting
- **Math error**: Check calibration math (linear interpolation)

### Readings Fluctuate Rapidly

**Symptoms:**
- Level jumps up and down every reading
- Example: 48.1, 48.7, 47.9, 48.4, 48.0...
- More than ±1% variation

**Diagnostic Steps:**

**1. Check Sensor Stability**

Watch sensor value (not calculated level):
```
Sensor readings:
12.30 mA
12.31 mA
12.29 mA  ← Small variation is normal
12.30 mA
```

**If sensor stable but level jumps:**
- Calibration issue (bad curve)
- Math precision problem

**If sensor fluctuates:**
- Electrical noise
- Sensor quality issue
- Mechanical vibration

**2. Check for Physical Causes**

- Waves/sloshing in tank (add baffles)
- Sensor bobbing on float (secure mounting)
- Vibrating equipment nearby
- Wind (outdoor float sensors)

**3. Enable Filtering**

```json
{
  "filteringEnabled": true,
  "filterWindow": 5  // Average last 5 readings
}
```

**4. Check Power Quality**

```
Measure sensor power supply:
- Should be stable DC voltage
- No AC ripple >100mV
- Clean waveform on oscilloscope
```

**Solutions:**

- **Noise**: Add filtering in config (moving average)
- **Poor sensor**: Replace with higher-quality sensor
- **Vibration**: Use dampening mounts
- **Sloshing**: Wait for tank to settle, increase sample interval
- **Bad power**: Use regulated power supply with filtering
- **Long cable**: Use shielded cable, add RC filter at input

### Sensor Reads Stuck Value

**Symptoms:**
- Reading never changes
- Shows same value for hours/days
- Level doesn't respond to physical changes

**Diagnostic Steps:**

**1. Verify Sensor Working**

Manually measure tank level, then:
- Add or remove liquid
- Wait 30 minutes (for sampling interval)
- Check if reading updated

**2. Check Update Frequency**

```json
{
  "sampleInterval": 1800  // Seconds between readings (30 min)
}
```

**Might appear "stuck" if interval too long!**

**3. Check Serial Monitor**

```
Look for fresh readings:
[10:30:00] Tank A sampled: 48.5 in
[10:30:00] Tank A sampled: 48.5 in  ← Same (tank stable)
[11:00:00] Tank A sampled: 48.5 in  ← Updated (still same value)
[11:30:00] Tank A sampled: 52.1 in  ← Changed!
```

**Timestamps confirm updates happening**

**4. Force Manual Reading**

Via serial monitor send command:
```
READ
```

Should trigger immediate sensor read.

**5. Check Sensor Type**

**Digital/SDI-12 sensors:**
- Need active communication
- Check wiring (data + power + ground)
- Verify protocol settings

**Analog sensors:**
- Passive output
- Should always respond
- If stuck, likely wiring issue

**Solutions:**

- **Long interval**: Just wait or reduce sampleInterval
- **Wiring open**: Fix broken connection
- **Sensor stuck mechanically**: Check float movement, ultrasonic clear path
- **Digital sensor failed**: Power cycle sensor or replace
- **Configuration disabled**: Enable tank in config

---

## Alarm Issues

### Alarms Not Triggering

**Symptoms:**
- Tank exceeds high/low threshold
- No SMS or email sent
- No alarm indicator in dashboard

**Diagnostic Steps:**

**1. Verify Alarm Configuration**

```json
{
  "alarmHighEnabled": true,  ← Must be true!
  "alarmHighLevel": 90.0,
  "alarmLowEnabled": true,
  "alarmLowLevel": 10.0
}
```

**2. Check Current Level vs Thresholds**

```
Current level: 92.0 in
High threshold: 90.0 in  ← Should alarm!

But check:
- Is alarm enabled?
- Was notification already sent?
```

**3. Check Alarm State**

Serial monitor shows:
```
"Tank A: ALARM HIGH (92.0 > 90.0)"
```

**If alarm state shown but no notification:**
- Notification configuration problem
- Not alarm detection problem

**4. Verify Notification Settings**

**SMS:**
```json
{
  "smsEnabled": true,
  "smsNumber": "+1234567890"  ← Valid number?
}
```

**Email:**
```json
{
  "emailEnabled": true,
  "emailAddress": "alerts@company.com"
}
```

**5. Check Server Received Alarm**

In server dashboard:
- Alarm log shows the event?
- Client's alarm state updated?

**6. Check De-bounce Logic**

Alarms require multiple consecutive readings above/below threshold:
```
Reading 1: 91.0 in (alarm)
Reading 2: 89.0 in (cleared) ← Alarm won't trigger
Reading 3: 92.0 in (alarm)
```

Must stay in alarm state for debounce count!

**Solutions:**

- **Disabled**: Enable alarms in configuration
- **Wrong thresholds**: Adjust levels appropriately  
- **Notification disabled**: Enable SMS/email
- **Wrong contact info**: Correct phone/email address
- **Debounce**: Wait for consecutive readings in alarm state
- **Server offline**: Server must relay notifications to Notehub
- **Notehub route disabled**: Check cloud routes active
- **Cell service**: Verify Notecard can send outbound

### Alarms Trigger Too Frequently

**Symptoms:**
- SMS flood (multiple per hour)
- Alarm flapping (on/off rapidly)
- Nuisance alarms

**Diagnostic Steps:**

**1. Check Hysteresis**

```json
{
  "alarmHighLevel": 90.0,
  "alarmHighHysteresis": 5.0,  // Must drop to 85.0 to clear
  
  "alarmLowLevel": 10.0,
  "alarmLowHysteresis": 5.0   // Must rise to 15.0 to clear
}
```

**Hysteresis prevents flapping!**

**Without hysteresis:**
```
Level 89.9 → No alarm
Level 90.1 → ALARM (SMS sent)
Level 89.9 → Cleared
Level 90.1 → ALARM (SMS sent again!) ← Nuisance
```

**With 5" hysteresis:**
```
Level 90.1 → ALARM (SMS sent)
Level 89.9 → Still in alarm (must reach 85.0)
Level 85.0 → Cleared
Level 90.1 → ALARM (SMS sent) ← Hours later, legitimate
```

**2. Check Rate Limiting**

```json
{
  "alarmRateLimitMinutes": 60  // Max 1 SMS per hour
}
```

**3. Verify Sensor Stability**

If sensor fluctuates around threshold:
```
Level readings: 89.8, 90.2, 89.9, 90.1, 89.7, 90.3
Constantly crossing 90.0 threshold!
```

**Fix the sensor stability first** (see Sensor section)

**Solutions:**

- **No hysteresis**: Add 5-10 unit hysteresis
- **Too sensitive**: Increase hysteresis
- **No rate limit**: Add rate limiting
- **Unstable sensor**: Filter readings, fix noise
- **Wrong threshold**: Adjust to avoid normal fluctuations

---

## Dashboard and Configuration Issues

### Configuration Changes Not Saving

**Symptoms:**
- Change settings in dashboard
- Click "Save" or "Send to Device"
- Settings revert after refresh

**Diagnostic Steps:**

**1. Check Client Connection**

Can't configure offline client:
- Client must have recent telemetry
- Check "Last Seen" timestamp
- Verify cellular connected

**2. Check Server Confirmation**

After clicking "Send to Device":
```
Dashboard should show:
"Configuration sent to device successfully"
  or
"Error: Could not reach device"
```

**3. Check Serial Monitor (Client)**

```
Watch for:
"Incoming config from server"
"Config saved to LittleFS"
"Applied new configuration"
```

**If no messages:**
- Server didn't send
- Route not working
- Client not checking for inbound

**4. Check LittleFS**

```
Serial monitor shows:
"LittleFS: 1438 KB used of 1500 KB (95%)"
```

**If >95% full:**
- Cannot write config
- Need to free space

**5. Verify JSON Validity**

Invalid JSON won't save:
```json
{
  "site": "North Farm",
  "tankHeight": 96.0,  ← Missing comma
  "enabled": true
}
```

**Solutions:**

- **Client offline**: Wait for connection or configure later
- **Server error**: Check server serial logs for details
- **Storage full**: Free space (archive old data, delete unneeded files)
- **Invalid JSON**: Validate with JSON checker tool
- **Routing issue**: Check Notehub routes enabled
- **Firmware bug**: Update to latest version

### Dashboard Not Showing All Clients

**Symptoms:**
- Some clients missing from dropdown
- Client sending telemetry but invisible
- "Unknown Device" in logs

**Diagnostic Steps:**

**1. Check Fleet Membership**

In Blues Notehub:
- Is client in `tankalarm-clients` fleet?
- Server in `tankalarm-server` fleet?
- Viewer in `tankalarm-viewer` fleet?
- Same Product UID?

**2. Check Object Type**

Clients must send recognizable object:
```json
{
  "object": "tank_level",  ← Must match server expectations
  ...
}
```

**Server filters by object type!**

**3. Check Server Storage**

```
Serial monitor:
"Discovered new client: dev:864475044012345"
"Saved client to /clients/dev_864475044012345.json"
```

**If save fails:**
- LittleFS full
- File system corruption

**4. Check Device Label**

```json
{
  "site": "North Farm",
  "deviceLabel": "Tank-01"
}
```

**Without label:**
- Shows as UID only (hard to identify)
- Might seem "missing" when searching

**5. Refresh Dashboard**

- Hard refresh: Ctrl+F5 (Windows) or Cmd+Shift+R (Mac)
- Clear browser cache
- Try different browser

**Solutions:**

- **Wrong fleet**: Move client to correct fleet in Notehub
- **Wrong object**: Update client firmware to send correct object type
- **Storage full**: Archive old clients, free space
- **No label**: Configure device with friendly name
- **Browser cache**: Hard refresh or clear cache
- **Recent addition**: Wait 30-60 min for first telemetry

---

## Relay Control Issues

### Relays Not Activating

**Symptoms:**
- Click relay button in dashboard
- Relay LED doesn't light
- Connected equipment doesn't operate

**Diagnostic Steps:**

**1. Check Relay Command Received**

Client serial monitor:
```
"Incoming relay command: R1=ON"
"Setting relay 0 to HIGH"
"Relay D0 activated"
```

**If not shown:**
- Command not sent from dashboard
- Routing issue
- Client not processing inbound

**2. Check Relay LED**

On Opta device:
- D0, D1, D2, D3 LEDs (one per relay)
- Should illuminate when activated
- Orange/yellow color typically

**If LED on but equipment doesn't work:**
- Relay works, wiring problem

**If LED off:**
- Relay not activating
- Firmware issue
- Hardware failure

**3. Check Wiring**

```
Relay output terminals:
NO (Normally Open) ← Usually use this
C  (Common)
NC (Normally Closed)

Correct for ON operation:
Load ← Connect between NO and C
```

**4. Test with Multimeter**

```
Set to continuity mode:
1. Relay OFF: NO to C = OPEN (no beep)
2. Relay ON: NO to C = CLOSED (beep)
```

**5. Check Configuration**

```json
{
  "relayMask": 1,  // Binary 0001 = Relay D0 only
  "relayOnHigh": true,  // Activate on high alarm
  "relayTargetClient": "dev:864475044056789"  // Device-to-device
}
```

**Solutions:**

- **Not received**: Check server/Notehub logs for routing
- **Wrong wiring**: Connect to NO and C terminals
- **Dead load**: Test equipment separately (multimeter, direct power)
- **Relay failure**: Try different relay (D1, D2, D3)
- **Configuration**: Verify relayMask includes this relay
- **Hardware damage**: Replace Opta if all relays dead

### Relay Stays On Permanently

**Symptoms:**
- Relay won't turn off
- Dashboard shows "OFF" but relay still active
- Equipment running continuously

**Diagnostic Steps:**

**1. Check for Multiple Commands**

Serial monitor:
```
"Setting relay 0 to OFF"
[Later...]
"Automatic alarm activation: Relay 0 ON"  ← Conflict!
```

**Automatic alarm might override manual control**

**2. Check Relay State in Code**

```cpp
// Verify relay actually being set LOW
digitalWrite(RELAY_PIN, LOW);
```

**3. Check for Stuck Relay**

Mechanical relays can weld contacts:
- Power off Opta completely
- Use multimeter to check relay contacts
- If still closed when off, relay is stuck

**4. Disable Automatic Control Temporarily**

```json
{
  "relayOnHigh": false,  // Disable auto-activation
  "relayOnLow": false
}
```

**Then test manual control:**
- Does OFF work now?
- If yes, automation conflict
- If no, hardware issue

**Solutions:**

- **Automation conflict**: Adjust alarm thresholds or disable auto relay
- **Stuck relay**: Replace Opta (welded contacts)
- **Firmware bug**: Update firmware
- **Inductive kick damage**: Add snubbers/flyback diodes to inductive loads

---

## Serial Monitor Diagnostics

### Serial Monitor Shows Garbage Characters

**Symptoms:**
- Unreadable text: `����䌎��⸮`
- Random symbols
- Can't read diagnostics

**Solution:**

**Wrong baud rate!**

Set to **115200** baud in Arduino IDE serial monitor

### Serial Monitor Shows Nothing

**Symptoms:**
- Blank screen
- No output at all
- "Connecting..." message

**Solutions:**

1. **Wrong COM port**: Select correct port from Tools menu
2. **Driver issue**: Install Arduino Opta USB drivers
3. **Cable**: Use data-capable USB-C cable
4. **Device off**: Verify power LED lit
5. **Bootloader mode**: Reset button pressed at wrong time

### How to Interpret Serial Output

**Normal Boot Sequence:**

```
[Startup Messages]
TankAlarm Client v1.1.1
Initializing Notecard...
Notecard: CONNECTED
Product UID: com.company.tankalarm:production
Device UID: dev:864475044012345
Initializing Analog Expansion...
Analog Expansion: DETECTED
Loading configuration...
Config loaded successfully

[Runtime Messages]
[10:00:00] Sampling sensors...
[10:00:00] Tank A: Sensor=12.34mA → Level=48.5in
[10:00:00] Sending telemetry...
[10:00:00] Telemetry sent successfully

[Configuration Update]
[10:15:00] Incoming config from server
[10:15:00] Config saved to LittleFS
[10:15:00] Applied new configuration
[10:15:00] Tank A high alarm: 85.0in (was 90.0in)

[Errors]
[ERROR] Failed to read sensor on CH0
[ERROR] LittleFS mount failed
[ERROR] Notecard communication timeout
```

**Key Indicators:**

| Message | Meaning | Action if Wrong |
|---------|---------|-----------------|
| `CONNECTED` | Notecard online | Check SIM/signal |
| `DETECTED` | Expansion found | Reseat expansion |
| `Config loaded` | Have saved settings | Check LittleFS |
| `Sensor=X.XmA` | Reading sensor | Check wiring |
| `Sent successfully` | Cloud communication | Check Notehub |
| `[ERROR]` | Problem occurred | Address specific error |

---

## Advanced Diagnostics

### Notecard Diagnostic Commands

Access via serial monitor (send as text):

```
card.version
```
Returns firmware version, device info

```
card.status
```
Returns connection status, signal strength

```
card.location
```
Returns GPS/cell location, RSSI

```
card.wireless
```
Returns carrier, APN, network info

```
hub.status
```
Returns Notehub connection, pending events

```
hub.sync
```
Forces immediate sync with Notehub

### LittleFS File System Check

```cpp
// In serial monitor, check:
"LittleFS: 1234 KB used of 1500 KB (82%)"

// List files:
LS  // Custom command if implemented
```

**If filesystem corrupt:**
```cpp
// Reformat (WARNING: Erases all data!)
FORMAT
```

### Memory Diagnostics

```cpp
// Check RAM usage
"Free heap: 45678 bytes"

// If <10KB free:
- Memory leak
- Too many large buffers
- Reduce history retention
```

### Network Packet Capture

For deep network debugging:

```
Use Wireshark on network segment:
- Filter: ip.addr == 192.168.1.150
- Analyze HTTP requests to/from server
- Look for dropped packets, timeouts
```

---

## Common Error Messages

### "LittleFS mount failed"

**Cause:** File system corrupted or not initialized

**Solution:**
```
1. Reformat LittleFS (loses data!)
2. Re-upload firmware
3. Reconfigure device
```

### "Notecard communication timeout"

**Cause:** Can't communicate with Notecard over I2C

**Solution:**
1. Check Notecard seated properly in carrier
2. Power cycle device
3. Replace Notecard if persistent

### "Failed to read sensor on CH0"

**Cause:** Analog Expansion not responding

**Solution:**
1. Check expansion detected at boot
2. Verify channel wiring
3. Test with multimeter
4. Replace expansion if needed

### "Config JSON parse error"

**Cause:** Invalid JSON received from server

**Solution:**
1. Validate JSON syntax
2. Check for missing commas, brackets
3. Verify special characters escaped

### "Telemetry send failed"

**Cause:** Couldn't send data to cloud

**Solution:**
1. Check Notecard connected
2. Verify cellular signal
3. Check Notehub product UID correct
4. Try manual sync: `hub.sync`

---

## Getting Help

### Before Contacting Support

Gather this information:

- [ ] Firmware version
- [ ] Device UIDs (client and server)
- [ ] Serial monitor output (last 100 lines)
- [ ] Blues Notehub event log
- [ ] Configuration JSON
- [ ] Symptoms and timeline
- [ ] Recent changes made
- [ ] Troubleshooting steps already tried

### Support Resources

**Documentation:**
- [Quick Start Guide](QUICK_START_GUIDE.md)
- [Client Installation Guide](CLIENT_INSTALLATION_GUIDE.md)
- [Server Installation Guide](SERVER_INSTALLATION_GUIDE.md)

**Community:**
- [Blues Wireless Forum](https://discuss.blues.io/)
- [Arduino Forum](https://forum.arduino.cc/)
- GitHub Issues (this repository)

**Vendor Support:**
- Blues Wireless: support@blues.io
- Arduino: support@arduino.cc

---

## Appendix: Diagnostic Flowcharts

### Client Won't Send Telemetry

```
1. Is device powered?
   ├─ No → Fix power supply
   └─ Yes
      │
   2. Can you access serial monitor?
      ├─ No → Check USB cable/drivers
      └─ Yes
         │
      3. Does it show "Notecard: CONNECTED"?
         ├─ No → Fix cellular (SIM/signal/UID)
         └─ Yes
            │
         4. Does sampling occur? ("Tank A: X.X in")
            ├─ No → Fix sensors (wiring/expansion/config)
            └─ Yes
               │
            5. Does "Telemetry sent" appear?
               ├─ No → Check Notehub (product UID/fleet)
               └─ Yes → Telemetry is sending!
                  │
               6. Check server/Notehub route
```

### Server Not Receiving Data

```
1. Is server powered and booted?
   ├─ No → Fix power
   └─ Yes
      │
   2. Is Ethernet connected? (Link LED)
      ├─ No → Fix network cable
      └─ Yes
         │
      3. Can you ping server IP?
         ├─ No → Fix IP config (DHCP/static/conflict)
         └─ Yes
            │
         4. Can you access dashboard?
            ├─ No → Fix web server (reboot/firewall)
            └─ Yes
               │
            5. Is server Notecard connected?
               ├─ No → Fix cellular (SIM/signal)
               └─ Yes
                  │
               6. Check Notehub route: clients → server
                  └─ Enable route if disabled
```

---

*Troubleshooting Guide v1.1 | Last Updated: February 20, 2026*  
*Compatible with TankAlarm Firmware 1.1.1+*
