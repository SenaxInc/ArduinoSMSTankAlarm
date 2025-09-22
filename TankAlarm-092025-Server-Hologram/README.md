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
- **Email Management**: Web-based interface to manage email recipient lists stored on SD card
- **Tank Management**: Remote ping functionality for testing tank client connectivity (foundation for future remote control)
- **Daily Email Summaries**: Composes and sends daily email summaries of tank level changes via Hologram API (default) or SMS gateway (fallback)
- **Power Failure Tracking**: Receives and tracks power failure notifications from client devices, includes power failure events in daily email reports
- **Monthly CSV Reports**: Generates comprehensive monthly reports grouped by tank location with daily changes and major decreases
- **Real-time Monitoring**: Displays current tank levels and 24-hour changes

### Web Interface Features
- Real-time dashboard showing tank levels from all monitored sites
- Auto-refresh every 30 seconds
- Color-coded status indicators (Normal/Alarm)
- 24-hour change tracking with positive/negative indicators
- Server connection status monitoring
- **Email Management Interface**: Add/remove email recipients for daily and monthly reports
- **Tank Management Interface**: Remote ping functionality for testing client connectivity with visual feedback
- Mobile-friendly responsive design

## Installation

### 1. Hardware Setup
1. Install the MKR NB 1500 board
2. Attach the MKR ETH Shield
3. Insert the Hologram.io SIM card
4. Insert SD card for data logging
5. Connect Ethernet cable to your local network

### 2. Software Configuration

**Note**: Starting from this version, the server will compile and run even if `server_config.h` is missing. It will use default configuration values and notify you via serial output. For production use, it's recommended to create and customize the configuration file.

1. Copy `server_config_template.h` to `server_config.h` (optional but recommended)
2. Edit `server_config.h` with your specific settings:
   ```cpp
   #define HOLOGRAM_DEVICE_KEY "your_actual_device_key"
   #define USE_HOLOGRAM_EMAIL true  // Use Hologram API for email (default)
   #define HOLOGRAM_EMAIL_RECIPIENT "user@example.com"
   #define DAILY_EMAIL_SMS_GATEWAY "+15551234567@vtext.com"  // Fallback method
   #define MONTHLY_REPORT_ENABLED true  // Enable monthly CSV reports
   ```

**If server_config.h is not found**: The server will use built-in default values and display "Configuration: Using default values (server_config.h not found)" in the serial output. You can still run the server, but you'll need to update the configuration values directly in the code or create the config file.

### 3. Upload Code
1. Open `TankAlarm-092025-Server-Hologram.ino` in Arduino IDE
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
  - `power_failure_log.txt`: Power failure events from client devices
  - `monthly_report_YYYY-MM.csv`: Monthly CSV reports with tank data grouped by location
  - `daily_emails.txt`: List of daily email recipients (managed via web interface)
  - `monthly_emails.txt`: List of monthly email recipients (managed via web interface)
  - `power_failure_backup.txt`: Backup of power failure events for recovery

## Power Failure Recovery

The server includes comprehensive power failure recovery to ensure continuous operation:

### Automatic Recovery Features
- **State Backup**: Critical system state automatically saved every 5 minutes to SD card
- **Heartbeat Monitoring**: Regular heartbeat timestamps track server operation
- **Tank Report Recovery**: All recent tank reports backed up and restored from SD card
- **Email Date Recovery**: Last email sending dates restored to prevent duplicate emails
- **Network Recovery**: Automatic reconnection to Hologram and Ethernet networks with retry logic
- **Recovery Notifications**: Email alerts sent to all daily recipients when power failure recovery occurs

### Recovery Process
1. **Detection**: Server detects unexpected shutdown by checking last known state on startup
2. **State Restoration**: Automatically loads tank reports, email dates, and system configuration from SD card
3. **Network Recovery**: Reconnects to both cellular and Ethernet networks with enhanced retry logic
4. **Recovery Notification**: Sends detailed recovery email to all configured recipients
5. **Resume Operation**: Continues normal server operation from restored state with no data loss

### Recovery Files on SD Card
- `system_state.txt`: Current system state and shutdown reason tracking
- `tank_reports_backup.txt`: Backup of recent tank reports for restoration
- `email_dates.txt`: Last daily and monthly email sending dates
- `heartbeat.txt`: Last known operational timestamp for failure detection

### Recovery Notifications
Power failure recovery notifications include:
- Server identification and location
- Recovery timestamp and system status
- Reason for previous shutdown (power failure vs normal shutdown)
- Number of tank reports restored from backup
- Confirmation that all network connections are operational
- Email recipient counts for daily and monthly reports

This ensures continuous tank monitoring service even after unexpected power outages with complete preservation of tank data and email schedules.

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
3. **Email Management**: Navigate to `http://[IP_ADDRESS]/emails` to manage email recipients
4. **Tank Management**: Navigate to `http://[IP_ADDRESS]/tanks` to ping tank clients and test connectivity
5. **Monitor Status**: The dashboard page auto-refreshes every 30 seconds

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
  - Email recipient counts for daily and monthly reports

## Email Management

The server provides a web-based interface for managing email recipients:

### Accessing Email Management
- Navigate to `http://[SERVER_IP]/emails` in your web browser
- Interface allows adding/removing recipients for daily and monthly reports
- Changes are automatically saved to SD card

### Email Recipient Lists
- **Daily Recipients**: Receive daily tank level summaries
- **Monthly Recipients**: Receive monthly CSV reports
- **Maximum**: 10 recipients per list
- **Storage**: Lists stored in `daily_emails.txt` and `monthly_emails.txt` on SD card

### Adding Recipients
1. Navigate to the Email Management page
2. Enter email address in the appropriate form (Daily or Monthly)
3. Click "Add Daily Recipient" or "Add Monthly Recipient"
4. Email is validated and added to the list

### Removing Recipients
1. Click the "Remove" button next to any email address
2. Confirm the removal in the popup dialog
3. Recipient is immediately removed from the list

### Default Recipients
- On first startup, the configured `HOLOGRAM_EMAIL_RECIPIENT` is added as default recipient for both daily and monthly reports
- Lists are automatically created if they don't exist

## Tank Management

The server provides remote tank client management capabilities:

### Accessing Tank Management
- Navigate to `http://[SERVER_IP]/tanks` in your web browser
- Interface shows all discovered tank clients with ping controls
- Visual feedback for connectivity testing

### Tank Ping Functionality
- **Purpose**: Test connectivity with remote tank clients (foundation for future remote control features)
- **Discovery**: Tank clients automatically appear when they send reports
- **Ping Process**: 
  1. Click "Ping Tank" button for any discovered tank
  2. Button shows "Pinging..." with in-progress icon (⏳)
  3. After completion, shows success (✅) or failure (❌) status
- **Status Tracking**: Server maintains ping history for each tank client

### Tank Client Display
- **Tank Information**: Shows site location, tank number, current level, and last seen timestamp
- **Ping Controls**: Individual ping button for each tank with real-time status feedback
- **Grid Layout**: Organized display of all discovered tank clients
- **Auto-Discovery**: New tanks automatically appear when they send their first report

### Remote Control Foundation
- Ping functionality serves as the foundation for future remote device control features
- Uses Hologram.io API to send commands to tank clients
- Status tracking enables monitoring of client responsiveness
- Expandable architecture for additional remote commands

## Daily Email Reports

The server automatically sends daily email summaries with two delivery methods:

### Primary Method: Hologram API (Default)
- **Time**: Configurable (default 6:00 AM)
- **Delivery**: Direct email via Hologram.io API
- **Content**: All tank level changes from the previous 24 hours plus power failure events
- **Power Failures**: Includes any power failure recovery events from client devices
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
4. See [SD Card Shield Compatibility Guide](../SD_CARD_SHIELD_COMPATIBILITY.md) for questions about different shield configurations

## Version Information

- **Version**: 092025-Hologram
- **Hardware**: MKR NB 1500 + MKR ETH Shield
- **Network**: Hologram.io cellular + Ethernet
- **Created**: September 2025