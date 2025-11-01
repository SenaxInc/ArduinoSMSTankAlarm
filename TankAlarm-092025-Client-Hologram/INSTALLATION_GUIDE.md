# Tank Alarm Inches/Feet Implementation - Installation Guide

## Quick Start

### 1. Hardware Configuration (Optional)

**The `config_template.h` file is already configured with sensible defaults.**

Only edit `config_template.h` if you need to:
- Change hardware pin configurations
- Select a different sensor type (`SENSOR_TYPE`)
- Modify sensor calibration ranges
- Enable debug options

**Most settings are configured on the SD card, not in the .h file**

### 2. Prepare SD Card (REQUIRED)

**Download and prepare configuration file:**
```bash
# Download tank_config_example.txt and rename
cp tank_config_example.txt tank_config.txt
# Edit tank_config.txt with your settings
# Copy to SD card root
```

**Edit tank_config.txt on SD card with your specific values:**
```
# Essential settings now configured on SD card
HOLOGRAM_DEVICE_KEY=your_actual_device_key
ALARM_PHONE_PRIMARY=+15551234567
ALARM_PHONE_SECONDARY=+15559876543
DAILY_REPORT_PHONE=+15555551234

SITE_NAME=Your Tank Farm Name
TANK_NUMBER=1
TANK_HEIGHT_INCHES=120
HIGH_ALARM_INCHES=100
LOW_ALARM_INCHES=12

SLEEP_INTERVAL_HOURS=1
DAILY_REPORT_HOURS=24
```

### 3. Upload Firmware

1. Open `TankAlarm092025-Hologram.ino` in Arduino IDE
2. Select board: Arduino MKR NB 1500
3. Compile and upload to device

### 4. Insert SD Card and Test

1. Insert configured SD card into MKR SD PROTO Shield
2. Power on device
3. Check serial monitor for startup messages
4. Verify log files are created on SD card

## Configuration Details

### Hardware Setup

**Required Components:**
- Arduino MKR NB 1500
- MKR SD PROTO Shield  
- MKR RELAY Shield (optional)
- Hologram.io SIM card
- SD card (formatted FAT32)
- Tank level sensor (digital float, analog voltage, or 4-20mA)

**Sensor Connections:**
- Digital Float: Connect to pin 7 (configurable via `TANK_LEVEL_PIN`)
- Analog Voltage: Connect to pin A1 (configurable via `ANALOG_SENSOR_PIN`)
- 4-20mA Current Loop: Connect via I2C (address 0x48 default)

### Software Configuration

**Minimal config.h setup (most settings now on SD card):**
```c
// Sensor type selection
#define SENSOR_TYPE DIGITAL_FLOAT  // or ANALOG_VOLTAGE or CURRENT_LOOP

// Hardware pin configuration (if different from defaults)
#define TANK_LEVEL_PIN 7
#define RELAY_CONTROL_PIN 5
#define SD_CARD_CS_PIN 4

// Sensor calibration (for analog sensors)
#define TANK_EMPTY_VOLTAGE 0.5
#define TANK_FULL_VOLTAGE 4.5
```

**Comprehensive SD card configuration (tank_config.txt):**
```
# Network and Communication (moved from config.h)
HOLOGRAM_DEVICE_KEY=your_actual_device_key
HOLOGRAM_APN=hologram
ALARM_PHONE_PRIMARY=+15551234567
ALARM_PHONE_SECONDARY=+15559876543
DAILY_REPORT_PHONE=+15555551234

# Tank Configuration
SITE_NAME=Main Tank Farm
TANK_NUMBER=1
TANK_HEIGHT_INCHES=120
HIGH_ALARM_INCHES=100
LOW_ALARM_INCHES=12

# System Timing
SLEEP_INTERVAL_HOURS=1
DAILY_REPORT_HOURS=24

# Advanced Options
LARGE_DECREASE_THRESHOLD_INCHES=24
LARGE_DECREASE_WAIT_HOURS=2
CONNECTION_TIMEOUT_MS=30000
SMS_RETRY_ATTEMPTS=3
```

## Log File Formats

### Hourly Log (hourly_log.txt)
```
YYYYMMDD00:00,H,(Tank Number),(Feet)FT,(Inches)IN,+(Change Feet)FT,(Change Inches)IN,
```
Example:
```
2025010106:00,H,1,8FT,2.5IN,+0FT,1.2IN,
2025010107:00,H,1,8FT,1.8IN,-0FT,0.7IN,
```

### Daily Report Log (daily_log.txt)  
```
YYYYMMDD00:00,D,(Site Name),(Tank Number),(Feet)FT,(Inches)IN,+(Change Feet)FT,(Change Inches)IN,
```
Example:
```
2025010100:00,D,Main Tank Farm,1,8FT,2.5IN,+1FT,4.2IN,
```

### Alarm Log (alarm_log.txt)
```
YYYYMMDD00:00,A,(Site Name),(Tank Number),(alarm state)
```
Example:
```
2025010115:30,A,Main Tank Farm,1,high
2025010115:45,A,Main Tank Farm,1,normal
```

### Large Decrease Log (decrease_log.txt)
```
YYYYMMDD00:00,S,(Tank Number),(Decrease Feet)FT,(Decrease Inches)IN
```
Example:
```
2025010112:00,S,1,2FT,6.5IN
```

## SMS Message Examples

### Alarm SMS
```
TANK ALARM Main Tank Farm Tank #1: Level 8FT,5.2IN at 2025-01-01 15:30:00. Immediate attention required.
```

### Daily Report SMS  
```
Daily Tank Report Main Tank Farm - 2025-01-01 06:00:00
Tank #1 Level: 8FT,2.5IN
24hr Change: +0FT,3.2IN
Status: Normal
Next report in 24 hours
```

### Startup SMS
```
Tank Alarm System Started Main Tank Farm - 2025-01-01 08:00:00
Tank #1 Initial Level: 7FT,11.8IN
Status: Normal
System ready for monitoring
```

## Troubleshooting

### SD Card Issues
- Ensure SD card is formatted as FAT32
- Check that card is properly inserted
- Verify tank_config.txt exists and has correct format
- Monitor serial output for configuration loading messages

### Sensor Calibration
- For analog sensors, verify voltage range matches `TANK_EMPTY_VOLTAGE` and `TANK_FULL_VOLTAGE`
- For current loop sensors, check I2C address and channel configuration
- Adjust `INCHES_PER_UNIT` for fine calibration

### Network Connectivity
- Verify Hologram.io device key is correct
- Check SIM card activation and data plan
- Monitor signal strength in installation location
- Test with known working phone numbers

### Alarm Thresholds
- Set `HIGH_ALARM_INCHES` below actual tank overflow point
- Set `LOW_ALARM_INCHES` above pump inlet to prevent damage
- For digital float switches, configure `DIGITAL_HIGH_ALARM` and `DIGITAL_LOW_ALARM` appropriately

## Support

For issues with this implementation, check:
1. Serial monitor output for error messages
2. SD card log files for system events
3. Configuration file format and values
4. Sensor wiring and connections

Common configuration errors are logged to the main log file with timestamps for debugging.

## Height Calibration

After installation, calibrate the tank height sensor for accurate readings:

1. **Read CALIBRATION_GUIDE.md** for detailed instructions
2. **Use SMS commands** to add calibration points: `CAL 48.5`
3. **Use web interface** at `http://[server-ip]/calibration`
4. **Add multiple points** at different tank levels for best accuracy
5. **Verify calibration** with `STATUS` SMS command

The calibration system replaces fixed sensor ranges with precise user-defined measurements for improved accuracy.