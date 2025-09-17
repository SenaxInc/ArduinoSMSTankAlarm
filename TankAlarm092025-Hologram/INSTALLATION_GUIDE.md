# Tank Alarm Inches/Feet Implementation - Installation Guide

## Quick Start

### 1. Prepare Configuration Files

**Copy template to config.h:**
```bash
cp config_template.h config.h
```

**Edit config.h with your specific values:**
- Update `HOLOGRAM_DEVICE_KEY` with your actual device key
- Update phone numbers for alarm contacts
- Set `TANK_NUMBER` and `SITE_LOCATION_NAME`
- Configure tank physical parameters:
  - `TANK_HEIGHT_INCHES` - Total tank height
  - `HIGH_ALARM_INCHES` - High level alarm threshold
  - `LOW_ALARM_INCHES` - Low level alarm threshold

### 2. Prepare SD Card

**Copy configuration template:**
```bash
cp tank_config_example.txt [SD_CARD]/tank_config.txt
```

**Edit tank_config.txt on SD card:**
```
SITE_NAME=Your Tank Farm Name
TANK_NUMBER=1
TANK_HEIGHT_INCHES=120
HIGH_ALARM_INCHES=100
LOW_ALARM_INCHES=12
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

**In config.h:**
```c
// Essential settings
#define HOLOGRAM_DEVICE_KEY "your_key_here"
#define ALARM_PHONE_PRIMARY "+15551234567"
#define ALARM_PHONE_SECONDARY "+15559876543"
#define DAILY_REPORT_PHONE "+15555551234"

// Tank configuration  
#define TANK_NUMBER 1
#define SITE_LOCATION_NAME "Your Site"
#define TANK_HEIGHT_INCHES 120
#define HIGH_ALARM_INCHES 100
#define LOW_ALARM_INCHES 12

// Sensor type selection
#define SENSOR_TYPE DIGITAL_FLOAT  // or ANALOG_VOLTAGE or CURRENT_LOOP
```

**On SD Card (tank_config.txt):**
```
# Tank configuration loaded at startup
SITE_NAME=Main Tank Farm
TANK_NUMBER=1
TANK_HEIGHT_INCHES=120
INCHES_PER_UNIT=1.0
HIGH_ALARM_INCHES=100
LOW_ALARM_INCHES=12
LARGE_DECREASE_THRESHOLD_INCHES=24
LARGE_DECREASE_WAIT_HOURS=2
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