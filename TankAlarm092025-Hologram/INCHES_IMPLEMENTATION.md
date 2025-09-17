# Tank Alarm - Inches/Feet Implementation

This update switches the tank alarm system from percentage-based measurements to inches and feet as specified in the requirements.

## New Features

### Configuration
- Tank measurements now use inches and feet instead of percentages
- Tank configuration can be loaded from SD card (`tank_config.txt`)
- Site location name and tank number are configurable
- Alarm thresholds are set in inches rather than percentages

### Logging Formats

#### Hourly Log (`hourly_log.txt`)
Format: `YYYYMMDD00:00,H,(Tank Number),(Number of Feet)FT,(Number of Inches)IN,+(Number of feet added in last 24hrs)FT,(Number of inches added in last 24hrs)IN,`

Example: `2025010100:00,H,1,8FT,4.2IN,+0FT,2.1IN,`

#### Daily Report Log (`daily_log.txt`)
Format: `YYYYMMDD00:00,D,(site location name),(Tank Number),(Number of Feet)FT,(Number of Inches)IN,+(Number of feet added in last 24hrs)FT,(Number of inches added in last 24hrs)IN,`

Example: `2025010100:00,D,Example Tank Farm,1,8FT,4.2IN,+0FT,2.1IN,`

#### Alarm Log (`alarm_log.txt`)
Format: `YYYYMMDD00:00,A,(site location name),(Tank Number),(alarm state, high or low or change)`

Example: `2025010100:00,A,Example Tank Farm,1,high`

#### Large Decrease Log (`decrease_log.txt`)
Format: `YYYYMMDD00:00,S,(Tank Number),(total Number of Feet decreased)FT,(total Number of Inches decreased)IN`

Example: `2025010100:00,S,1,2FT,6.0IN`

### SD Card Configuration

Copy `tank_config_example.txt` to your SD card as `tank_config.txt` and modify the values:

```
# Tank Alarm Configuration File
SITE_NAME=Your Tank Farm
TANK_NUMBER=1
TANK_HEIGHT_INCHES=120
INCHES_PER_UNIT=1.0
HIGH_ALARM_INCHES=100
LOW_ALARM_INCHES=12
DIGITAL_HIGH_ALARM=true
DIGITAL_LOW_ALARM=false
LARGE_DECREASE_THRESHOLD_INCHES=24
LARGE_DECREASE_WAIT_HOURS=2
```

### Large Decrease Detection

The system now monitors for large decreases in tank level:
- Configurable threshold (default: 24 inches)
- Waits 2 hours (configurable) before logging to ensure the decrease is stable
- Logs to separate file for analysis

### SMS Messages

SMS messages now include:
- Site location name
- Tank number
- Level in feet and inches format
- 24-hour change tracking

Example alarm SMS:
```
TANK ALARM Example Tank Farm Tank #1: Level 8FT,4.2IN at 2025-01-01 12:00:00. Immediate attention required.
```

Example daily report SMS:
```
Daily Tank Report Example Tank Farm - 2025-01-01 06:00:00
Tank #1 Level: 8FT,4.2IN
24hr Change: +0FT,2.1IN
Status: Normal
Next report in 24 hours
```

## Configuration Parameters

### In config.h or config_example.h:
- `TANK_NUMBER` - Tank identifier (1-99)
- `SITE_LOCATION_NAME` - Site location for reports
- `INCHES_PER_UNIT` - Calibration factor for sensor readings
- `TANK_HEIGHT_INCHES` - Total tank height in inches
- `HIGH_ALARM_INCHES` - High alarm threshold in inches
- `LOW_ALARM_INCHES` - Low alarm threshold in inches
- `DIGITAL_HIGH_ALARM` - Enable high alarm for digital float sensors
- `DIGITAL_LOW_ALARM` - Enable low alarm for digital float sensors
- `LARGE_DECREASE_THRESHOLD_INCHES` - Threshold for large decrease detection
- `LARGE_DECREASE_WAIT_HOURS` - Wait time before logging large decreases

### SD Card Log Files:
- `SD_HOURLY_LOG_FILE` - Hourly data log file name
- `SD_DAILY_LOG_FILE` - Daily report log file name  
- `SD_ALARM_LOG_FILE` - Alarm event log file name
- `SD_DECREASE_LOG_FILE` - Large decrease log file name

## Installation

1. Copy `config_template.h` to `config.h` and configure your settings
2. Create `tank_config.txt` on SD card with your specific values
3. Upload the updated firmware to your Arduino MKR NB 1500
4. Insert configured SD card

## Compatibility

This implementation maintains backward compatibility with existing sensor types:
- Digital float switches
- Analog voltage sensors (Dwyer 626 series)
- 4-20mA current loop sensors

The system automatically converts sensor readings to inches based on the configured tank height and sensor range.