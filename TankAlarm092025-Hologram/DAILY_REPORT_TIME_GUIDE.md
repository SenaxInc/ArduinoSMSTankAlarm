# Daily Report Time Configuration Guide

## Overview

The Tank Alarm system now supports configurable daily report times with automatic cellular network time synchronization. This allows you to set a specific time of day for daily reports to be sent.

## Configuration

### Setting Daily Report Time

Add or modify the following line in your SD card configuration file (`tank_config.txt`):

```
DAILY_REPORT_TIME=06:00
```

- Use 24-hour format (HH:MM)
- Valid hours: 00-23
- Valid minutes: 00-59
- Default: 06:00 (6:00 AM)

### Examples

```
DAILY_REPORT_TIME=06:00    # 6:00 AM
DAILY_REPORT_TIME=18:30    # 6:30 PM
DAILY_REPORT_TIME=12:00    # 12:00 PM (noon)
DAILY_REPORT_TIME=00:00    # 12:00 AM (midnight)
```

## How It Works

### Time Synchronization

1. **Startup**: System connects to cellular network and syncs time
2. **Daily Re-sync**: Time is re-synchronized once every 24 hours
3. **Fallback**: If time sync fails, system uses the old hour-counting method

### Report Scheduling

- Reports are sent when the current time matches the configured time
- 1-minute tolerance window (report sent between HH:MM and HH:MM+1)
- One report per day maximum (prevents duplicate reports)
- Time comparison only active when time is successfully synchronized

### Status Information

The system status messages now include:
- Current synchronized time
- Next scheduled report time
- Time synchronization status

## Benefits

1. **Predictable Timing**: Reports sent at consistent, configured times
2. **Network Accuracy**: Uses cellular network time for precision
3. **Flexibility**: Easily change report time without code modifications
4. **Reliability**: Falls back to hour-counting if time sync fails

## Troubleshooting

### Time Not Synchronized

If you see "Time: NOT SYNCED" in status messages:

1. Check cellular network connection
2. Verify SIM card is working
3. Check signal strength
4. System will fall back to hour-counting method

### Reports Not Sent at Expected Time

1. Verify `DAILY_REPORT_TIME` format in config file
2. Check that time synchronization is working
3. Ensure system is awake during the scheduled time window
3. Check SD card for error logs

### Time Zone Considerations

- The system uses UTC time from the cellular network
- For local time zones, adjust the configured time accordingly
- Example: For EST (UTC-5), set 11:00 to get 6:00 AM local time

## Configuration File Example

```
# Tank Alarm Configuration File
SITE_NAME=Example Tank Farm
TANK_NUMBER=1
HOLOGRAM_DEVICE_KEY=your_key_here
ALARM_PHONE_PRIMARY=+15551234567
DAILY_REPORT_PHONE=+15559876543
SLEEP_INTERVAL_HOURS=1
DAILY_REPORT_HOURS=24
DAILY_REPORT_TIME=06:00
```

## Version Compatibility

This feature is available in Tank Alarm 092025 and later versions.