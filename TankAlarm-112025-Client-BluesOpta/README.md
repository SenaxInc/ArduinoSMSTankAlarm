# TankAlarm 112025 Client - Blues Opta

Multi-purpose monitoring client using Arduino Opta with Blues Wireless Notecard cellular connectivity.

## Overview

The TankAlarm 112025 Client monitors tanks, engines, pumps, and other equipment using various sensor types and reports data to a central server via Blues Wireless Notecard. Key features include:

- **Cellular connectivity** via Blues Wireless Notecard
- **Multi-monitor support** - Monitor up to 8 sensors per device
- **Flexible object types** - Tanks, engines, pumps, gas systems, flow meters
- **Multiple sensor interfaces** - Digital, analog voltage, 4-20mA, pulse/RPM
- **Configurable alarms** - High and low thresholds per monitor
- **Remote configuration** - Update settings from server web interface
- **Persistent storage** - LittleFS internal flash (no SD card needed)
- **Fleet-based communication** - Simplified device-to-device routing
- **Watchdog protection** - Auto-recovery from hangs

## Architecture

### Object Types (What You're Monitoring)
The system separates **what** is being monitored from **how** it's measured:

| Object Type | Description | Typical Sensors |
|-------------|-------------|-----------------|
| `tank` | Liquid storage tank | 4-20mA level sensor, float switch |
| `engine` | Engine or motor | Hall effect RPM sensor |
| `pump` | Pump operation | Digital status, flow meter |
| `gas` | Gas pressure system | 4-20mA pressure transducer |
| `flow` | Flow measurement | Pulse-based flow meter |
| `custom` | User-defined | Any sensor type |

### Sensor Interfaces (How You're Measuring)
| Interface | Description | Output Type |
|-----------|-------------|-------------|
| `digital` | Binary on/off | Float state (0/1) |
| `analog` | Voltage output | Raw voltage (V) |
| `currentLoop` | 4-20mA | Raw milliamps (mA) |
| `pulse` | Pulse counting | RPM or flow rate |

## Hardware

- **Arduino Opta Lite** - Industrial controller with built-in connectivity
- **Blues Wireless for Opta** - Cellular Notecard carrier
- **Arduino Opta Ext A0602** - Analog expansion module for sensors
- Analog sensors compatible with 0-10V or 4-20mA inputs

## Quick Start

### 1. Hardware Setup
1. Install Blues Wireless for Opta carrier on Arduino Opta
2. Activate Notecard with Blues Wireless (see [blues.io](https://blues.io))
3. Connect Arduino Opta Ext A0602 for analog inputs
4. Connect sensors to appropriate inputs

### 2. Software Setup
1. Install Arduino IDE 2.x or later
2. Install Arduino Mbed OS Opta Boards core via Boards Manager
3. Install required libraries:
   - **ArduinoJson** (version 7.x or later)
   - **Blues Wireless Notecard** (latest)
   - LittleFS (built into Mbed core)
4. Open `TankAlarm-092025-Client-BluesOpta.ino` in Arduino IDE
5. Update `PRODUCT_UID` to match your Blues Notehub project
6. Compile and upload to Arduino Opta

**For detailed step-by-step instructions, see [INSTALLATION.md](INSTALLATION.md)**

### 3. Blues Notehub Setup
1. Create account at [notehub.io](https://notehub.io)
2. Create a product for your tank alarm system
3. Create fleet named `tankalarm-clients`
4. Assign client Notecards to the client fleet
5. Claim your Notecard into the product

### 4. Configuration
The client creates a default configuration on first boot. You can update configuration:

**Via Server Web Interface (Recommended):**
1. Deploy the TankAlarm 112025 Server
2. Access server dashboard at `http://<server-ip>/`
3. Select client from dropdown
4. Update settings (site name, monitors, thresholds, etc.)
5. Click "Send Config to Client"

**Default Configuration:**
- Sample interval: 1800 seconds (30 minutes)
- Single monitor: "Tank A" (tank, analog)
- High alarm: 110 inches
- Low alarm: 18 inches
- Server fleet: "tankalarm-server"

## Configuration Fields

### Basic Settings
- **Site Name**: Descriptive location (e.g., "North Tank Farm")
- **Device Label**: Short identifier (e.g., "Tank-01")
- **Server Fleet**: Fleet name for routing data (default: "tankalarm-server")

### Sampling Settings
- **Sample Interval**: Seconds between sensor readings (default: 1800)
- **Level Change Threshold**: Change amount to trigger telemetry (0 to disable)

### Monitor Configuration (per monitor)
- **Monitor ID**: Single letter identifier (A-H)
- **Monitor Name**: Descriptive name
- **Object Type**: What is being monitored (tank, engine, pump, gas, flow, custom)
- **Sensor Interface**: How measurement is taken (digital, analog, currentLoop, pulse)
- **High Alarm**: Threshold for high alarm
- **Low Alarm**: Threshold for low alarm
- **Measurement Unit**: Display unit ("inches", "rpm", "psi", "gpm", etc.)
- **Analog Pin**: Arduino Opta analog input (A0-A7, I1-I8)

### 4-20mA Current Loop Sensor Configuration

For 4-20mA current loop sensors, two mounting options are supported. The implementation uses the sensor's **native measurement range** (sensorRangeMin/Max/Unit) for accurate pressure-to-height conversions.

#### Pressure Sensor (Bottom-Mounted)
Used for sensors like the Dwyer 626-06-CB-P1-E5-S1 (0-5 PSI) mounted near the bottom of the tank.

- **Current Loop Type**: "pressure"
- **How it works**: Measures the pressure of the liquid column above the sensor
  - 4mA = `sensorRangeMin` (e.g., 0 PSI = no liquid above sensor)
  - 20mA = `sensorRangeMax` (e.g., 5 PSI = max liquid height)
- **Sensor Range**: The native measurement range from the sensor datasheet
  - `sensorRangeMin`: Minimum pressure at 4mA (typically 0)
  - `sensorRangeMax`: Maximum pressure at 20mA (e.g., 5 for 0-5 PSI)
  - `sensorRangeUnit`: Pressure unit - "PSI", "bar", "kPa", "mbar", or "inH2O"
- **Sensor Mount Height**: Height of sensor above tank bottom (usually 0-2 inches)

**Known Limitation - Blind Spot:** Pressure sensors cannot detect liquid levels below their mount height. When the tank is empty (0 PSI), the reported level will be the sensor mount height (e.g., 2"), not 0". Mount the sensor as close to the tank bottom as possible to minimize this blind spot.

**Pressure-to-Height Conversion:**
The system automatically converts pressure to inches using these factors:
- 1 PSI = 27.68 inches of water
- 1 bar = 401.5 inches of water
- 1 kPa = 4.015 inches of water
- 1 mbar = 0.4015 inches of water
- 1 inH2O = 1 inch of water

**Example Configuration** (0-5 PSI sensor on 120" tank):
- Sensor mounted 2 inches above tank bottom
- Max sensor range = 5 PSI = ~138 inches of water column
- Configuration:
  - `currentLoopType`: "pressure"
  - `sensorRangeMin`: 0
  - `sensorRangeMax`: 5
  - `sensorRangeUnit`: "PSI"
  - `sensorMountHeight`: 2.0

**How It Works:**
1. 4mA → 0 PSI → 0 inches of liquid above sensor
2. Total height = 0 + 2" mount height = 2" (minimum reported value due to blind spot)
3. When tank fills: 12mA → 2.5 PSI → 69.2" + 2" = 71.2" total

#### Ultrasonic Sensor (Top-Mounted)
Used for sensors like the Siemens Sitrans LU240 mounted on top of the tank looking down.

- **Current Loop Type**: "ultrasonic"
- **How it works**: Measures the distance from the sensor to the liquid surface
  - 4mA = `sensorRangeMin` (minimum distance, typically a blind spot)
  - 20mA = `sensorRangeMax` (maximum measurable distance)
- **Sensor Range**: The native measurement range from the sensor datasheet
  - `sensorRangeMin`: Minimum distance at 4mA (e.g., 0.5m for blind spot)
  - `sensorRangeMax`: Maximum distance at 20mA (e.g., 10m)
  - `sensorRangeUnit`: Distance unit - "m", "cm", "ft", or "in"
- **Sensor Mount Height**: Distance from sensor to tank bottom when tank is empty (in inches)

**Distance Unit Conversion:**
The system automatically converts distance to inches using:
- 1 m = 39.3701 inches
- 1 cm = 0.393701 inches
- 1 ft = 12 inches

**Example Configuration** (ultrasonic sensor with 0.5-10m range on 10-foot tank):
- Sensor mounted 124 inches above tank bottom (tank is 120" + 4" clearance)
- Configuration:
  - `currentLoopType`: "ultrasonic"
  - `sensorRangeMin`: 0.5 (blind spot in meters)
  - `sensorRangeMax`: 10.0 (max range in meters)
  - `sensorRangeUnit`: "m"
  - `sensorMountHeight`: 124.0

**How It Works:**
1. 4mA → 0.5m (19.7") → liquid level = 124" - 19.7" = 104.3" (nearly full)
2. 20mA → 10m (393.7") → liquid level = 124" - 393.7" = clamped to 0" (empty/beyond range)

**Calibration Tips for 4-20mA Sensors:**
1. Record the actual mA output at known liquid levels (empty, half-full, full)
2. Verify sensor mount height is accurate using a tape measure
3. Enter the correct sensor native range (as specified in sensor datasheet)
4. For pressure sensors: account for specific gravity if not measuring water (multiply PSI by 1.0/SG)
5. Check for temperature effects on readings (cold liquids are denser)

### Float Switch Configuration (Digital Sensors)
Float switches can be configured as either normally-open (NO) or normally-closed (NC):

- **Digital Switch Mode**: "NO" (normally-open) or "NC" (normally-closed)
  - **NO (Normally-Open)**: Switch is open by default, closes when fluid reaches the switch position
  - **NC (Normally-Closed)**: Switch is closed by default, opens when fluid reaches the switch position
- **Digital Trigger**: When to trigger the alarm
  - "activated": Alarm when switch is activated (fluid present)
  - "not_activated": Alarm when switch is not activated (fluid absent)

**Wiring Note**: For both NO and NC float switches, connect the switch between the digital input pin and GND. The Arduino uses an internal pull-up resistor, and the software interprets the signal based on your configured switch mode. The wiring is the same for both modes - only the software interpretation changes.

### Analog Voltage Sensor Configuration

For analog voltage sensors (like the Dwyer 626 series with voltage output), the system supports the same native range configuration as 4-20mA sensors. This allows you to specify both the voltage range and pressure range for accurate pressure-to-height conversion.

**Supported Voltage Output Configurations:**
- 0-10V (default)
- 0-5V
- 1-5V  
- 0.5-4.5V
- 2-10V
- Any configurable range

**Configuration Parameters:**
- `analogVoltageMin`: Minimum voltage output (e.g., 0.0 for 0-10V, 1.0 for 1-5V)
- `analogVoltageMax`: Maximum voltage output (e.g., 10.0 for 0-10V, 5.0 for 1-5V)
- `sensorRangeMin` / `sensorRangeMax`: Pressure range in native units
- `sensorRangeUnit`: Pressure unit - "PSI", "bar", "kPa", "mbar", or "inH2O"
- `sensorMountHeight`: Height of sensor above tank bottom (inches)

**Example Configuration** (Dwyer 626 with 1-5V output, 0-5 PSI range):
- Configuration:
  - `sensorType`: "analog"
  - `analogVoltageMin`: 1.0
  - `analogVoltageMax`: 5.0
  - `sensorRangeMin`: 0
  - `sensorRangeMax`: 5
  - `sensorRangeUnit`: "PSI"
  - `sensorMountHeight`: 2.0

### Hall Effect RPM Sensor Configuration

Hall effect sensors can be used to measure RPM (rotations per minute) for applications such as pump monitoring, motor speed tracking, or flow measurement. The system supports multiple types of hall effect sensors and detection methods.

**Sensor Types:**

1. **Unipolar** (default)
   - Triggered by a single magnetic pole (usually South pole)
   - Resets when magnetic field is removed
   - Output: Active LOW when magnet present, HIGH when absent
   - Use case: Simple magnet detection, one pulse per rotation

2. **Bipolar (Latching)**
   - Requires South pole to turn ON and North pole to turn OFF
   - Maintains state until opposite pole is detected
   - Output: Latches between HIGH and LOW states
   - Use case: Motor applications, precise position sensing

3. **Omnipolar**
   - Responds to either North or South pole
   - Simplifies magnet placement and orientation
   - Output: Toggles on any magnetic field
   - Use case: Flexible installations, bidirectional sensing

4. **Analog (Linear)**
   - Outputs voltage proportional to magnetic field strength
   - Can be used in digital threshold mode
   - Output: Voltage varies with field strength
   - Use case: Distance or angle measurement, fuel gauges

**Detection Methods:**

1. **Pulse Counting** (default)
   - Counts all pulses over a configurable sampling period (default 60 seconds)
   - More accurate for steady speeds
   - Averages multiple revolutions for better precision
   - Formula: RPM = (pulses × 60000) / (sample_duration_ms × pulses_per_rev)
   - Minimum detectable RPM = 60000 / (sample_duration_ms × pulses_per_rev)
     - With 60s sampling and 1 pulse/rev: minimum 1 RPM

2. **Time-Based**
   - Measures the period between two consecutive pulses
   - More responsive to speed changes
   - Works with fewer pulses (minimum 2)
   - More flexible for different magnet types and orientations
   - Formula: RPM = 60000 / (period_ms × pulses_per_rev)

3. **Accumulated Mode** (for very low RPM < 1)
   - Counts pulses between telemetry reports (e.g., 30 minutes)
   - Enable with `rpmAccumulatedMode: true`
   - Ideal for slow-moving pumps, flow meters, or agitators
   - Formula: RPM = (accumulated_pulses × 60000) / (elapsed_ms × pulses_per_rev)
   - With sampleSeconds=1800 (30 min) and 1 pulse/rev: detects down to 0.033 RPM

**Configuration Parameters:**
- `sensorType`: "rpm"
- `rpmPin`: Digital input pin for hall effect sensor (uses internal pull-up)
- `pulsesPerRevolution`: Number of pulses generated per complete rotation (default: 1)
  - Single magnet: 1 pulse per revolution
  - Multiple magnets: Set to number of magnets
  - **Note:** For **bipolar** or **omnipolar** sensors, a single magnet generates 2 pulses per revolution (one for each pole). Set `pulsesPerRevolution` to `2 × number of magnets` in these cases.
- `hallEffectType`: Sensor type - "unipolar", "bipolar", "omnipolar", or "analog"
- `hallEffectDetection`: Detection method - "pulse" or "time"
- `rpmSampleDurationMs`: Sample duration in milliseconds (default: 60000 = 60 seconds)
  - Longer durations detect lower RPM but increase measurement time
  - For 0.1 RPM detection without accumulated mode: use 600000 (10 minutes)
- `rpmAccumulatedMode`: Set to `true` for very low RPM measurement (< 1 RPM)
  - Counts pulses between telemetry reports instead of during a fixed sample window
  - Best for applications where RPM is very slow (e.g., 0.1 RPM = 1 rotation per 10 minutes)
- `highAlarm`: Maximum expected RPM for alarm (e.g., 3000)
- `lowAlarm`: Minimum expected RPM for alarm (e.g., 100)

**Example Configuration** (Motor monitoring with 4 magnets):
```json
{
  "sensor": "rpm",
  "rpmPin": 2,
  "pulsesPerRev": 8,
  "hallEffectType": "omnipolar",
  "hallEffectDetection": "time",
  "highAlarm": 3000,
  "lowAlarm": 500
}
```
Note: With omnipolar sensor and 4 magnets, use pulsesPerRev = 8 (2 pulses per magnet × 4 magnets)

**Example Configuration** (Slow pump flow meter - 0.1 RPM detection):
```json
{
  "sensor": "rpm",
  "rpmPin": 3,
  "pulsesPerRev": 1,
  "hallEffectType": "unipolar",
  "hallEffectDetection": "pulse",
  "rpmAccumulatedMode": true,
  "highAlarm": 10,
  "lowAlarm": 0.05
}
```
Note: With accumulated mode enabled and 30-minute sample intervals, this can detect down to 0.033 RPM

**Wiring:**
- Connect hall effect sensor VCC to 5V or 3.3V (check sensor datasheet)
- Connect sensor GND to Arduino GND
- Connect sensor output to configured digital pin
- No external pull-up needed (Arduino uses internal pull-up)

**Tips:**
- Use "time" detection for faster response to speed changes
- Use "pulse" detection for better accuracy at steady speeds
- Use `rpmAccumulatedMode: true` for very slow rotation (< 1 RPM)
- For multiple magnets, set `pulsesPerRev` to the number of magnets
- For omnipolar sensors, each magnet passing creates 2 pulses (both N and S poles trigger)
- Monitor both high and low thresholds to detect over-speed and stall conditions

### Solar Charger Monitoring (SunSaver MPPT)

The system supports optional monitoring of Morningstar SunSaver MPPT solar battery chargers via Modbus RTU over RS-485. This enables:

- **Battery voltage monitoring** - Track charge state and detect low battery conditions
- **Solar panel voltage monitoring** - Verify solar input is functioning
- **Charge current monitoring** - See how much power is being harvested
- **Fault detection** - Immediate alerts for overcurrent, overvoltage, or hardware faults
- **Daily statistics** - Min/max voltage, amp-hours charged, included in daily reports
- **Battery health alerts** - SMS/email warnings when battery voltage drops below thresholds

**Required Hardware:**

1. **Arduino Opta with RS485** - Use the Opta WiFi/RS485 variant (AFX00005) which has built-in RS485, or add an RS485 expansion to the Opta Lite

2. **Morningstar MRC-1 Adapter** (MeterBus to EIA-485)
   - Converts SunSaver's proprietary RJ-11 MeterBus to industry-standard RS-485
   - Powered by the SunSaver via RJ-11 cable - no external power needed
   - Simply connect to SunSaver RJ-11 port and wire RS-485 terminals to Opta
   - Purchase from Morningstar or authorized distributors

3. **SunSaver MPPT Solar Charge Controller** - Any model in the SunSaver MPPT family

**RS-485 Wiring (Using MRC-1):**

| Arduino Opta RS485 | MRC-1 Terminal | Function |
|--------------------|----------------|----------|
| **A (-)** | **Terminal B (-)** | Data Negative |
| **B (+)** | **Terminal A (+)** | Data Positive |
| **GND** | **Terminal G** | Signal Ground |

> **Note:** A/B labeling varies between manufacturers. If communication fails, try swapping the A and B wires at the Opta end.

**Configuration Parameters:**

```json
{
  "solarCharger": {
    "enabled": true,
    "slaveId": 1,
    "baudRate": 9600,
    "timeoutMs": 200,
    "pollIntervalSec": 60,
    "batteryLowV": 11.8,
    "batteryCriticalV": 11.5,
    "batteryHighV": 14.8,
    "alertOnLow": true,
    "alertOnFault": true,
    "alertOnCommFail": false,
    "includeInDaily": true
  }
}
```

**Configuration Fields:**

| Field | Default | Description |
|-------|---------|-------------|
| `enabled` | `false` | Enable solar charger monitoring |
| `slaveId` | `1` | Modbus slave ID of SunSaver (usually 1) |
| `baudRate` | `9600` | Serial baud rate (typically 9600) |
| `timeoutMs` | `200` | Modbus read timeout in milliseconds |
| `pollIntervalSec` | `60` | How often to poll charger (seconds) |
| `batteryLowV` | `11.8` | Low voltage warning threshold (12V AGM) |
| `batteryCriticalV` | `11.5` | Critical voltage alarm threshold |
| `batteryHighV` | `14.8` | High voltage (overcharge) threshold |
| `alertOnLow` | `true` | Send alerts for low battery |
| `alertOnFault` | `true` | Send alerts for charger faults |
| `alertOnCommFail` | `false` | Alert on RS-485 communication failure |
| `includeInDaily` | `true` | Include solar data in daily report |

**Battery Voltage Thresholds (12V AGM):**

| Voltage | State | Description |
|---------|-------|-------------|
| < 11.5V | Critical | Battery damage imminent, immediate alarm |
| 11.5-11.8V | Low | Battery should be charged soon, warning |
| 12.0-13.4V | Normal | Healthy operating range |
| 13.4-14.4V | Charging | Bulk or absorption charging |
| 14.4-14.8V | Float | Fully charged, maintenance mode |
| > 14.8V | High | Overcharge condition, investigate |

**Charge States:**

| State | Description |
|-------|-------------|
| Night | Solar offline, battery supplying loads |
| Bulk | Rapid charging (battery < 80%) |
| Absorption | Finishing charge (battery 80-100%) |
| Float | Fully charged, maintenance mode |
| Equalize | Optional equalization charge |
| Fault | Hardware fault detected |

**Data Included in Daily Report:**

When `includeInDaily` is enabled, the daily report includes:
- Current battery voltage
- Current array (solar) voltage  
- Current charge current
- Charge state
- Daily min/max battery voltage
- Amp-hours charged today
- Health status (battery OK, communication OK)
- Any active faults or alarms

**Example Alert Message:**

```
Solar: Battery voltage CRITICAL (11.3V)
Site: North Tank Farm
Charge State: Night
Faults: None
Action: Check solar panel connections and battery
```

**Troubleshooting:**

| Issue | Solution |
|-------|----------|
| No communication | Check RS-485 wiring, swap A/B if needed |
| Slave not found | Verify Modbus slave ID (check SunSaver DIP switches) |
| Intermittent reads | Increase `timeoutMs` to 500, check cable length |
| No data in daily | Verify `includeInDaily` is true |
| No alerts | Check `alertOnLow` and `alertOnFault` settings |

### Battery Voltage Monitoring (Notecard Direct)

The system supports battery voltage monitoring using the Notecard's built-in voltage measurement when wired directly to a 12V battery. This is the **simplest option** as it requires no additional hardware beyond what's already installed. It provides:

- **Real-time voltage monitoring** - Track battery state throughout the day
- **Trend analysis** - Daily, weekly, and monthly voltage trends to detect declining batteries
- **Low voltage alerts** - SMS/email warnings when voltage drops below thresholds
- **State of charge estimation** - Approximate SOC percentage from voltage curves
- **USB power detection** - Alert if system falls back to USB power

**Hardware Required:**

Wire the Notecard's VIN/GND directly to your 12V battery (3.8V-17V VIN range is supported). No additional sensors or adapters needed.

> **Note:** If you also have a SunSaver MPPT solar charger, you can enable both systems. The MPPT provides more detailed charging data while the Notecard voltage monitoring is always available as a backup.

**Configuration Parameters:**

```json
{
  "batteryMonitor": {
    "enabled": true,
    "batteryType": "leadAcid12V",
    "pollIntervalSec": 900,
    "alertOnLow": true,
    "voltageHighV": 14.4,
    "voltageLowV": 12.0,
    "voltageCriticalV": 11.8,
    "trendAnalysisHours": 168,
    "includeInDaily": true,
    "alertIntervalMinutes": 60
  }
}
```

**Configuration Fields:**

| Field | Default | Description |
|-------|---------|-------------|
| `enabled` | `false` | Enable Notecard battery monitoring |
| `batteryType` | `"leadAcid12V"` | Battery type: "leadAcid12V", "lifepo4_12V", "lipo", or "custom" |
| `pollIntervalSec` | `900` | How often to poll voltage (seconds, min 60) |
| `alertOnLow` | `true` | Send alerts for low/critical voltage |
| `voltageHighV` | `14.4` | High voltage threshold (for overcharge detection) |
| `voltageLowV` | `12.0` | Low voltage warning threshold |
| `voltageCriticalV` | `11.8` | Critical voltage (alarm immediately) |
| `trendAnalysisHours` | `168` | Hours of data for trend analysis (7 days default) |
| `includeInDaily` | `true` | Include battery data in daily report |
| `alertIntervalMinutes` | `60` | Minimum minutes between repeated alerts |

**Battery Type Defaults:**

| Battery Type | Low V | Critical V | High V | Notes |
|--------------|-------|------------|--------|-------|
| Lead-Acid 12V | 12.0 | 11.8 | 14.4 | AGM, flooded, gel batteries |
| LiFePO4 12V | 12.8 | 12.0 | 14.6 | 4S LiFePO4 pack |
| LiPo | 3.5 | 3.2 | 4.2 | Single cell or pack average |
| Custom | User | User | User | Set all thresholds manually |

**State of Charge Estimation:**

The system estimates state of charge (SOC) from voltage using lookup tables. Note that voltage-based SOC is approximate and varies with load, temperature, and battery age.

| Lead-Acid 12V | LiFePO4 12V |
|---------------|-------------|
| 12.7V+ = 100% | 13.3V+ = 100% |
| 12.5V = 75% | 13.2V = 75% |
| 12.3V = 50% | 13.1V = 50% |
| 12.1V = 25% | 13.0V = 25% |
| 11.9V = 0% | 12.8V = 0% |

**Trend Analysis:**

The Notecard provides trend data showing voltage changes over time:
- **Daily trend** - Voltage change in last 24 hours
- **Weekly trend** - Voltage change in last 7 days
- **Monthly trend** - Voltage change in last 30 days

A negative weekly trend with declining voltage can indicate:
- Battery aging and capacity loss
- Insufficient solar charging
- Increased load on the system

**Data Included in Daily Report:**

When `includeInDaily` is enabled:
- Current battery voltage
- Voltage mode (USB, high, normal, low, critical)
- Min/max/average voltage over analysis period
- Daily/weekly voltage trends
- Estimated state of charge
- Battery health status

**Example Alert Message:**

```
Battery: Voltage LOW (11.95V)
Site: North Tank Farm
SOC: ~20%
Weekly Trend: -0.35V
Action: Check charging system and loads
```

**Choosing Between Solar Monitoring and Notecard Battery Monitoring:**

| Feature | Notecard Battery | SunSaver MPPT |
|---------|------------------|---------------|
| Hardware needed | None extra | MRC-1 + RS485 |
| Battery voltage | ✓ | ✓ |
| Solar voltage | ✗ | ✓ |
| Charge current | ✗ | ✓ |
| Charge state | Via voltage only | Direct reading |
| Trend analysis | ✓ (7-day) | ✓ (daily min/max) |
| Fault detection | ✗ | ✓ |
| Amp-hours | ✗ | ✓ |
| Best for | Simple monitoring | Full solar diagnostics |

**Both can be enabled simultaneously** - the MPPT provides detailed charging data while Notecard monitoring provides backup voltage tracking.

## Operation

### Normal Operation
1. Client wakes up at scheduled interval
2. Reads all configured tank sensors
3. Checks for alarm conditions (high/low thresholds)
4. Sends telemetry to server via Blues Notehub if:
   - Alarm condition detected, OR
   - Level changed by more than threshold, OR
   - Daily report time reached
5. Sleeps until next sample interval

### Alarm Handling
- High alarm: Tank level exceeds high threshold
- Low alarm: Tank level below low threshold
- Alarms sent immediately via Blues Notehub to server
- Server forwards alarms via SMS and/or email

### Daily Reports
- Sent once per day regardless of level changes
- Default time: 7:00 AM (configurable on server)
- Includes all tank levels and status

## Communication

### Data Sent to Server
The client sends three types of notes via Blues Notehub:

1. **Telemetry** (`telemetry.qi` to server fleet)
   - Sent on level change or alarm
   - Contains all tank levels and status
   
2. **Alarms** (`alarm.qi` to server fleet)
   - Immediate notification of threshold breach
   - Includes tank ID, name, level, and threshold
   
3. **Daily Reports** (`daily.qi` to server fleet)
   - Once per day summary
   - All tanks included

### Configuration Received from Server
The client listens for configuration updates (`config.qi`):
- Pushed from server web interface
- Applied immediately on receipt
- Saved to LittleFS for persistence

## Troubleshooting

### No Serial Output
- Verify baud rate is 115200
- Press reset button on Opta
- Check USB cable and connection

### Notecard Not Connecting
- Verify Notecard is activated with Blues Wireless
- Check PRODUCT_UID matches Blues Notehub project
- Verify SIM card is installed and activated
- Check cellular coverage in deployment location

### Configuration Not Loading
- Default config is created automatically on first boot
- Check serial console for config messages
- Use server web interface to push new config
- Verify server fleet name matches

### Sensor Readings Incorrect
- Check analog input connections
- Verify sensor type (voltage vs current)
- Confirm min/max calibration values
- Check sensor power supply

### Cannot Update from Server
- Verify client UID is correct in server interface
- Check that client and server are in correct fleets
- Confirm Blues Notehub shows note traffic
- Verify client is online and syncing

## Development

### Serial Console Commands
Monitor at 115200 baud for diagnostic output:
- Startup sequence and hardware detection
- Configuration loading
- Sensor readings
- Notecard communication
- Alarm conditions

### Memory Usage
- Flash: ~60-80% (varies with configuration)
- RAM: ~40-60% during operation
- LittleFS: <1KB for configuration file

## Documentation

- **[INSTALLATION.md](INSTALLATION.md)** - Complete setup guide with Arduino IDE and library installation
- **[FLEET_IMPLEMENTATION_SUMMARY.md](FLEET_IMPLEMENTATION_SUMMARY.md)** - Architecture and design details
- **[MIGRATION_GUIDE.md](MIGRATION_GUIDE.md)** - Upgrading from route-based to fleet-based setup
- **[DEVICE_TO_DEVICE_API.md](DEVICE_TO_DEVICE_API.md)** - Technical details on Blues Notecard communication

## Additional Resources

- [Arduino Opta Documentation](https://docs.arduino.cc/hardware/opta)
- [Blues Wireless Developer Portal](https://dev.blues.io)
- [ArduinoJson Documentation](https://arduinojson.org/)
- [TankAlarm 112025 Server](../TankAlarm-112025-Server-BluesOpta/)

## Support

For issues or questions:
1. Check [INSTALLATION.md](INSTALLATION.md) troubleshooting section
2. Review serial console output at 115200 baud
3. Verify Blues Notehub note traffic
4. Check GitHub issues in this repository
5. Consult Blues Wireless documentation

## License

See [LICENSE](../LICENSE) file in repository root.
