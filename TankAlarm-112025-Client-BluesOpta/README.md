# TankAlarm 112025 Client - Blues Opta

Tank level monitoring client using Arduino Opta with Blues Wireless Notecard cellular connectivity.

## Overview

The TankAlarm 112025 Client monitors tank levels using analog sensors and reports data to a central server via Blues Wireless Notecard. Key features include:

- **Cellular connectivity** via Blues Wireless Notecard
- **Multi-tank support** - Monitor up to 8 tanks per device
- **Configurable alarms** - High and low thresholds per tank
- **Remote configuration** - Update settings from server web interface
- **Persistent storage** - LittleFS internal flash (no SD card needed)
- **Fleet-based communication** - Simplified device-to-device routing
- **Watchdog protection** - Auto-recovery from hangs

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
4. Connect tank level sensors to analog inputs

### 2. Software Setup
1. Install Arduino IDE 2.x or later
2. Install Arduino Mbed OS Opta Boards core via Boards Manager
3. Install required libraries:
   - **ArduinoJson** (version 7.x or later)
   - **Blues Wireless Notecard** (latest)
   - LittleFS (built into Mbed core)
4. Open `TankAlarm-112025-Client-BluesOpta.ino` in Arduino IDE
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
4. Update settings (site name, tanks, thresholds, etc.)
5. Click "Send Config to Client"

**Default Configuration:**
- Sample interval: 1800 seconds (30 minutes)
- Single tank: "Tank A" 
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
- **Level Change Threshold**: Inches of change to trigger telemetry (0 to disable)

### Tank Configuration (per tank)
- **Tank ID**: Single letter identifier (A-H)
- **Tank Name**: Descriptive name
- **High Alarm**: Threshold in inches for high level alert
- **Low Alarm**: Threshold in inches for low level alert
- **Analog Pin**: Arduino Opta analog input (A0-A7, I1-I8)
- **Sensor Type**: "voltage" (0-10V), "current" (4-20mA), or "digital" (float switch)
- **Min Value**: Minimum sensor value (e.g., 0.0V or 4.0mA)
- **Max Value**: Maximum sensor value (e.g., 10.0V or 20.0mA)
- **Min Inches**: Tank level in inches at minimum sensor value
- **Max Inches**: Tank level in inches at maximum sensor value

### Float Switch Configuration (Digital Sensors)
Float switches can be configured as either normally-open (NO) or normally-closed (NC):

- **Digital Switch Mode**: "NO" (normally-open) or "NC" (normally-closed)
  - **NO (Normally-Open)**: Switch is open by default, closes when fluid reaches the switch position
  - **NC (Normally-Closed)**: Switch is closed by default, opens when fluid reaches the switch position
- **Digital Trigger**: When to trigger the alarm
  - "activated": Alarm when switch is activated (fluid present)
  - "not_activated": Alarm when switch is not activated (fluid absent)

**Wiring Note**: For both NO and NC float switches, connect the switch between the digital input pin and GND. The Arduino uses an internal pull-up resistor, and the software interprets the signal based on your configured switch mode. The wiring is the same for both modes - only the software interpretation changes.

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
