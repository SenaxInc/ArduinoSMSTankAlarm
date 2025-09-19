# Tank Alarm Server 092025 - Hologram Version

## Overview

The Tank Alarm Server is designed to receive daily tank reports from client Arduino units via the Hologram.io cellular network, log the data to SD card, and provide a web interface for monitoring tank levels across multiple sites.

## Hardware Requirements

- **Arduino MKR NB 1500** (with ublox SARA-R410M cellular modem)
- **Arduino MKR ETH Shield** (for Ethernet connectivity)
- **Hologram.io SIM Card** (for cellular data reception)
- **SD Card** (for data logging)
- **Ethernet connection** (for local network access to web interface)

## Features

### Core Functionality
- **Receives Daily Reports**: Listens for daily tank reports sent via Hologram.io network from client Arduinos
- **Data Logging**: Stores all received tank reports to SD card with timestamps
- **Web Dashboard**: Hosts a web server accessible on the local network via Ethernet
- **Daily Email Summaries**: Composes and sends daily email summaries of tank level changes via Hologram API (default) or SMS gateway (fallback)
- **Monthly CSV Reports**: Generates comprehensive monthly reports grouped by tank location with daily changes and major decreases
- **Real-time Monitoring**: Displays current tank levels and 24-hour changes

### Web Interface Features
- Real-time dashboard showing tank levels from all monitored sites
- Auto-refresh every 30 seconds
- Color-coded status indicators (Normal/Alarm)
- 24-hour change tracking with positive/negative indicators
- Server connection status monitoring
- Mobile-friendly responsive design

## Installation

### 1. Hardware Setup
1. Install the MKR NB 1500 board
2. Attach the MKR ETH Shield
3. Insert the Hologram.io SIM card
4. Insert SD card for data logging
5. Connect Ethernet cable to your local network

### 2. Software Configuration
1. Copy `server_config_template.h` to `server_config.h`
2. Edit `server_config.h` with your specific settings:
   ```cpp
   #define HOLOGRAM_DEVICE_KEY "your_actual_device_key"
   #define USE_HOLOGRAM_EMAIL true  // Use Hologram API for email (default)
   #define HOLOGRAM_EMAIL_RECIPIENT "user@example.com"
   #define DAILY_EMAIL_SMS_GATEWAY "+15551234567@vtext.com"  // Fallback method
   #define MONTHLY_REPORT_ENABLED true  // Enable monthly CSV reports
   ```

### 3. Upload Code
1. Open `TankAlarmServer092025-Hologram.ino` in Arduino IDE
2. Select "Arduino MKR NB 1500" as the board
3. Upload the code to your device

## Configuration Options

### Network Settings
- **HOLOGRAM_DEVICE_KEY**: Your Hologram.io device key
- **Ethernet IP**: Automatic DHCP or static IP fallback (192.168.1.100)
- **Web Server Port**: Default 80 (HTTP)

### Email/Notification Settings
- **USE_HOLOGRAM_EMAIL**: Enable Hologram API for email delivery (default: true)
- **HOLOGRAM_EMAIL_RECIPIENT**: Email address for Hologram API delivery
- **DAILY_EMAIL_HOUR**: Hour to send daily summary (default: 6 AM)
- **DAILY_EMAIL_SMS_GATEWAY**: SMS-to-email gateway (fallback method)
- **ALARM_EMAIL_RECIPIENT**: Email for alarm notifications

### Monthly Report Settings
- **MONTHLY_REPORT_ENABLED**: Enable monthly CSV report generation (default: true)
- **MONTHLY_REPORT_DAY**: Day of month to generate report (default: 1st)
- **MONTHLY_REPORT_HOUR**: Hour to generate monthly report (default: 8 AM)

### Data Storage
- **MAX_REPORTS_IN_MEMORY**: Maximum reports stored in RAM (default: 50)
- **SD Card Files**:
  - `daily_reports.txt`: Tank level reports
  - `alarm_log.txt`: Alarm notifications
  - `server_log.txt`: Server events and status
  - `monthly_report_YYYY-MM.csv`: Monthly CSV reports with tank data grouped by location

## Data Format

### Received Daily Reports
The server expects daily reports in this format from client devices:
```
Daily Tank Report [Site Name] - [Timestamp]
Tank #[Number] Level: [Level in feet/inches]
24hr Change: [+/-Change in feet/inches]
Status: [Normal/ALARM]
```

### Logged Data Format
Data is logged to SD card in CSV format:
```
YYYYMMDD HH:MM,DAILY,SiteLocation,TankNumber,CurrentLevel,24hrChange,Status
```

## Web Interface Access

Once the server is running and connected to your network:

1. **Find the IP Address**: Check the Serial Monitor for the assigned IP address
2. **Access Dashboard**: Open a web browser and navigate to `http://[IP_ADDRESS]`
3. **Monitor Status**: The page auto-refreshes every 30 seconds

### Dashboard Features
- **Tank Cards**: Each monitored tank displays:
  - Site location and tank number
  - Current level in feet and inches
  - 24-hour change (color-coded green/red)
  - Status (Normal/Alarm)
  - Last update timestamp

- **Server Status**: Shows:
  - Hologram network connection status
  - Ethernet connection status
  - Total reports received count

## Daily Email Reports

The server automatically sends daily email summaries with two delivery methods:

### Primary Method: Hologram API (Default)
- **Time**: Configurable (default 6:00 AM)
- **Delivery**: Direct email via Hologram.io API
- **Content**: All tank level changes from the previous 24 hours
- **Configuration**: Set `USE_HOLOGRAM_EMAIL = true` and configure `HOLOGRAM_EMAIL_RECIPIENT`

### Fallback Method: SMS-to-Email Gateway
- **Activation**: Automatic fallback if Hologram API fails
- **Delivery**: Via SMS-to-email gateway services
- **Configuration**: Configure `DAILY_EMAIL_SMS_GATEWAY`

### Supported Email-to-SMS Gateways
- **Verizon**: `+1##########@vtext.com`
- **AT&T**: `+1##########@txt.att.net`
- **T-Mobile**: `+1##########@tmomail.net`
- **Sprint**: `+1##########@messaging.sprintpcs.com`

## Monthly CSV Reports

The server generates comprehensive monthly reports automatically:

### Report Content
- **Grouping**: Data organized by tank location and tank number
- **Daily Data**: Each day's tank level and 24-hour changes
- **Major Decreases**: Flagged entries for significant level drops (>1 foot)
- **Summary Statistics**: Total sites, reports, and time period

### CSV Format Example
```csv
Date,Site Location,Tank Number,Current Level,Daily Change,Major Decrease
20250901,North Site Tank Farm,1,8FT,6.2IN,+0FT,2.1IN,No
20250901,North Site Tank Farm,1,8FT,4.1IN,-0FT,2.1IN,No
20250902,North Site Tank Farm,1,7FT,2.0IN,-1FT,2.1IN,Yes
```

### Report Generation
- **Schedule**: 1st day of each month at 8:00 AM (configurable)
- **Storage**: Saved to SD card as `monthly_report_YYYY-MM.csv`
- **Email Delivery**: Automatically sent via configured email method
- **Configuration**: Enable with `MONTHLY_REPORT_ENABLED = true`

## Troubleshooting

### Connection Issues
1. **Hologram Connection Failed**:
   - Verify SIM card is properly inserted
   - Check signal strength in your location
   - Confirm device key is correct

2. **Ethernet Not Working**:
   - Check cable connection
   - Verify network allows the IP range
   - Try static IP configuration

3. **SD Card Errors**:
   - Ensure SD card is properly formatted (FAT32)
   - Check card is properly seated
   - Verify SD_CARD_CS_PIN setting

### Data Issues
1. **No Reports Received**:
   - Verify client devices are configured with correct Hologram settings
   - Check client device connectivity
   - Monitor server logs for connection attempts

2. **Web Page Not Loading**:
   - Confirm Ethernet connection is active
   - Check firewall settings on your network
   - Try accessing via IP address instead of hostname

## Log Files

The server creates several log files on the SD card:

- **`daily_reports.txt`**: All received tank reports
- **`alarm_log.txt`**: Alarm notifications and critical events
- **`server_log.txt`**: Server status, connections, and operational events
- **`monthly_report_YYYY-MM.csv`**: Monthly CSV reports with tank data grouped by location and major decrease tracking

## Maintenance

### Regular Tasks
- Monitor SD card space (logs can grow over time)
- Check network connectivity status
- Verify daily email delivery
- Review server logs for any errors

### Log Rotation
The server is configured to manage log file sizes automatically. Old logs are rotated when they exceed the configured maximum size.

## Support

For technical support or questions:
1. Check the Serial Monitor output for detailed status information
2. Review log files on the SD card for error details
3. Verify all configuration settings in `server_config.h`

## Version Information

- **Version**: 092025-Hologram
- **Hardware**: MKR NB 1500 + MKR ETH Shield
- **Network**: Hologram.io cellular + Ethernet
- **Created**: September 2025