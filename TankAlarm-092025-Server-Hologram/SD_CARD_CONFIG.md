# SD Card Configuration for Tank Alarm Server (REQUIRED)

The Tank Alarm Server requires configuration via SD card. The server will not start without a properly configured SD card.

## Quick Start

1. Copy `server_config.txt` to the root of your SD card
2. Edit the configuration values as needed
3. Insert SD card into server and power on

**CRITICAL**: The server will halt with error messages if the SD card or configuration file is missing.

## Configuration File Format

The configuration file uses a simple `KEY=VALUE` format:

```
# Comments start with #
HOLOGRAM_DEVICE_KEY=your_device_key_here
DAILY_EMAIL_HOUR=6
SERVER_LOCATION=Main Office
```

## Available Configuration Options

### Required Settings
- `HOLOGRAM_DEVICE_KEY`: Your Hologram.io device key
- `DAILY_EMAIL_RECIPIENT`: Email address for daily reports
- `SERVER_LOCATION`: Descriptive location name

### Network Settings
- `STATIC_IP_ADDRESS`: Static IP (format: 192,168,1,100)
- `STATIC_GATEWAY`: Gateway IP (format: 192,168,1,1)  
- `STATIC_SUBNET`: Subnet mask (format: 255,255,255,0)
- `ETHERNET_MAC_BYTE_1` through `ETHERNET_MAC_BYTE_6`: MAC address bytes

### Email/Notification Settings
- `DAILY_EMAIL_HOUR`: Hour to send daily report (0-23)
- `DAILY_EMAIL_MINUTE`: Minute to send daily report (0-59)
- `ALARM_EMAIL_RECIPIENT`: Email for alarm notifications
- `USE_HOLOGRAM_EMAIL`: Use Hologram API for email (true/false)

### System Settings
- `ENABLE_SERIAL_DEBUG`: Enable debug output (true/false)
- `WEB_PAGE_REFRESH_SECONDS`: Web page auto-refresh interval
- `MAX_REPORTS_IN_MEMORY`: Maximum reports to store in memory
- `DAYS_TO_KEEP_LOGS`: Days to retain log files

### Monthly Reports
- `MONTHLY_REPORT_ENABLED`: Enable monthly reports (true/false)
- `MONTHLY_REPORT_DAY`: Day of month to generate report (1-28)
- `MONTHLY_REPORT_HOUR`: Hour to generate report (0-23)

## Field Updates

To update configuration in the field:

1. **Power off** the server
2. **Remove SD card** from server
3. **Edit** `server_config.txt` on a computer
4. **Insert SD card** back into server
5. **Power on** server - new configuration will be loaded

## Fallback Behavior

**NO FALLBACK**: If `server_config.txt` is not found on the SD card, the server will:
- Display critical error messages on serial console
- Halt execution and refuse to start
- Require operator intervention to insert SD card with proper configuration

The server assumes SD card configuration will always be available and does not fall back to compile-time defaults.

## Troubleshooting

### Configuration not loading
- **CRITICAL**: Check that SD card is properly inserted and detected
- **CRITICAL**: Check that file is named exactly `server_config.txt`
- **CRITICAL**: Verify HOLOGRAM_DEVICE_KEY is set and not the default placeholder
- Check serial output for specific error messages
- Ensure no extra spaces around `=` in config file
- Server will halt execution if configuration is missing or invalid

### Invalid values
- Boolean values must be exactly `true` or `false`
- IP addresses must use comma format: `192,168,1,100`
- No quotes needed around string values
- Comments must start with `#` at beginning of line

## Example Complete Configuration

```
# Tank Alarm Server Configuration
HOLOGRAM_DEVICE_KEY=1234567890abcdef
SERVER_LOCATION=Tank Farm A
DAILY_EMAIL_HOUR=6
DAILY_EMAIL_MINUTE=30
DAILY_EMAIL_RECIPIENT=alerts@company.com
ENABLE_SERIAL_DEBUG=false
STATIC_IP_ADDRESS=192,168,1,100
MONTHLY_REPORT_ENABLED=true
MONTHLY_REPORT_DAY=1
```