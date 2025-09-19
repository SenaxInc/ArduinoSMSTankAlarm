# Tank Alarm 092025 - MKR NB 1500 Version

This is the September 2025 version of the Arduino SMS Tank Alarm system, designed for the Arduino MKR NB 1500 with cellular connectivity via Hologram.io.

## Hardware Components

### Main Board
- **Arduino MKR NB 1500** - Main microcontroller board with built-in cellular modem (ublox SARA-R410M)

### Shields
- **Arduino MKR SD PROTO** - Provides SD card slot and prototyping area
- **Arduino MKR RELAY** - Provides relay control capability

### Additional Components
- **Hologram.io SIM Card** - Cellular connectivity for SMS and data
- **SD Card** - Local data logging (formatted as FAT32)
- **Tank Level Sensor** - Choose from multiple sensor types:
  - Digital float switch (normally open)
  - Analog voltage sensor (0.5-4.5V ratiometric pressure sensor)
  - 4-20mA current loop sensor (via I2C module)

## Pin Assignments

### MKR NB 1500 Pin Usage
- **Pin 4**: SD Card Chip Select (CS)
- **Pin 5**: Relay Control Output
- **Pin 7**: Digital Tank Level Sensor Input (float switch with internal pullup)
- **Pin A1**: Analog Tank Level Sensor Input (0.5-4.5V pressure sensor)
- **Pins A1-A4**: Available analog inputs with screw terminals on MKR RELAY shield
- **SDA/SCL**: I2C communication for 4-20mA current loop sensors
- **LED_BUILTIN**: Status indication LED

### Shield Connections
- **MKR SD PROTO**: Stacked on MKR NB 1500, provides SD card interface
- **MKR RELAY**: Stacked on MKR SD PROTO, provides relay control

## Wiring Diagram

```
Tank Level Sensor (Float Switch):
- Common (C) terminal → Ground
- Normally Open (NO) terminal → Pin 7
- Internal pullup resistor enabled in software

Relay Output:
- Controlled via MKR RELAY shield
- Can be used for alarm indication, pump control, etc.

SD Card:
- Connected via MKR SD PROTO shield
- Used for local data logging
```

## Software Features

### Tank Level Sensor Support
The system supports three types of tank level sensors:

1. **Digital Float Switch (DIGITAL_FLOAT)**
   - Simple on/off detection
   - Connected to digital pin with pullup resistor
   - Suitable for alarm-only applications
   - Low cost and reliable

2. **Analog Voltage Sensors (ANALOG_VOLTAGE)**
   - Ratiometric 0.5-4.5V pressure sensors (e.g., Dwyer 626 series)
   - Provides continuous level measurement
   - Configurable alarm thresholds as percentage
   - Higher accuracy for level monitoring
   - **Convenient screw terminals**: Use A1-A4 pins on MKR RELAY shield for easy wiring

3. **4-20mA Current Loop Sensor (CURRENT_LOOP)**
   - Industrial standard current loop sensors
   - Uses NCD.io 4-channel current loop I2C module
   - Excellent noise immunity for long cable runs
   - Professional-grade accuracy and reliability

### Core Functionality
1. **Tank Level Monitoring**: Continuously monitors digital tank level sensor
2. **SMS Alerts**: Sends immediate SMS alerts when alarm condition detected
3. **Daily Reports**: Sends daily status reports via SMS at configurable time
4. **Data Logging**: Logs all events to SD card with timestamps
5. **Hologram.io Integration**: Sends data to Hologram.io cloud platform
6. **Server Communication**: Supports remote commands from Tank Alarm Server
7. **Low Power Operation**: Uses sleep modes to conserve battery power
8. **Time Synchronization**: Syncs with cellular network time for accurate scheduling

### Time Management Features
- **Enhanced Cellular Time Sync**: Automatically synchronizes time with cellular network during startup using robust date calculation
- **Leap Year Support**: Proper handling of leap years and Gregorian calendar rules including century boundaries  
- **Configurable Daily Report Time**: Set specific time (HH:MM format) for daily reports via SD card config
- **Dual Timing System**: Falls back to hour-based counting if time sync fails
- **Automatic Re-sync**: Time synchronizes once per day to maintain accuracy
- **Time-based Scheduling**: Sleep cycles aligned with real-time for precise report timing

### Alert System
- **Alarm Condition**: Tank level sensor reads HIGH (float switch triggered)
- **Primary Contact**: Immediate SMS to primary phone number
- **Secondary Contact**: Immediate SMS to secondary phone number
- **Daily Contact**: Regular daily status reports

### Enhanced Power Management
- **Adaptive Sleep Modes**: Intelligent sleep duration based on current activity
  - **Normal Sleep**: 1 hour intervals for routine monitoring
  - **Active Monitoring**: 10 minute intervals when alarms active or server commands received
  - **Deep Sleep Mode**: Optional maximum power savings for extended deployments
- **Wake Triggers**: 
  - **Timer-based**: Automatic wake at configured intervals
  - **Wake-on-Ping**: Device can be woken by incoming cellular data (server commands)
  - **Alarm Events**: Immediate wake when tank levels change
- **Power Optimization**:
  - **RTC-based Timing**: Precise sleep timing using real-time clock
  - **Cellular Wake**: Modem configured to wake device on incoming data
  - **Configurable Intervals**: Sleep durations adjustable via SD card configuration
  - **Battery Life**: Optimized for months of operation on battery power

### Server Communication
- **Server Device ID**: Configure `SERVER_DEVICE_KEY` to communicate with Tank Alarm Server
- **Remote Commands**: Listens for ping and control commands from server (every 10 minutes)
- **Command Types**: Supports PING, RELAY_ON, RELAY_OFF (future expansion for more commands)
- **Response System**: Sends acknowledgments back to server for command execution
- **Configuration Storage**: Server device ID stored on SD card for easy updates

## Configuration

### Required Setup Steps

1. **Install Required Libraries**:
   ```
   - MKRNB (Arduino official)
   - ArduinoLowPower
   - RTCZero
   - SD (Arduino official)
   ```

2. **Configure Hologram.io Account**:
   - Sign up at https://hologram.io
   - Get device key and update `HOLOGRAM_DEVICE_KEY` in code
   - If using with server, also get server device key and update `SERVER_DEVICE_KEY`
   - Activate SIM card and assign to device

3. **Update Phone Numbers**:
   ```cpp
   String ALARM_PHONE_1 = "12223334444";    // Primary alarm contact
   String ALARM_PHONE_2 = "15556667777";    // Secondary alarm contact
   String DAILY_PHONE = "18889990000";      // Daily report recipient
   ```

4. **Network Configuration**:
   - APN is pre-configured for Hologram.io ("hologram")
   - No additional network setup required

5. **Daily Report Time Configuration**:
   ```
   DAILY_REPORT_TIME=05:00
   ```
   - Set specific time for daily reports in HH:MM format (24-hour)
   - Default is 05:00 (5:00 AM)
   - Configure in SD card config file (tank_config.txt)
   - System automatically syncs with cellular network time

6. **Power Management Configuration**:
   ```
   SHORT_SLEEP_MINUTES=10     # Active monitoring sleep duration
   NORMAL_SLEEP_HOURS=1       # Normal operation sleep duration
   ENABLE_WAKE_ON_PING=true   # Enable wake-on-ping functionality
   DEEP_SLEEP_MODE=false      # Use deep sleep for maximum power savings
   ```
   - **Wake-on-Ping**: Device wakes when server sends ping commands
   - **Adaptive Sleep**: Shorter intervals during active monitoring
   - **Deep Sleep**: Maximum power savings for extended deployments
   - **Battery Optimization**: Configurable sleep durations for optimal battery life

### Hardware Setup

1. **Stack the Shields**:
   - Place MKR SD PROTO on MKR NB 1500
   - Place MKR RELAY on top of MKR SD PROTO
   - Ensure all pins are properly aligned

2. **Connect Tank Sensor**:
   - Connect float switch common terminal to GND
   - Connect float switch NO terminal to Pin 7
   - No external resistor needed (internal pullup used)

3. **Insert SIM and SD Cards**:
   - Install activated Hologram.io SIM card in MKR NB 1500
   - Insert formatted SD card in MKR SD PROTO

4. **Power Connection**:
   - Connect via USB for programming and testing
   - Use external power supply or battery for field deployment

## Operation

### Normal Operation Cycle
1. System wakes up every hour
2. Reads tank level sensor
3. If alarm condition (HIGH), sends immediate alerts
4. Logs status to SD card
5. Checks if current time matches configured daily report time
6. If time matches, sends daily report and syncs time
7. Returns to sleep mode

### Time Synchronization
- **Startup Sync**: Time synchronized from cellular network during startup using enhanced algorithm
- **Leap Year Support**: Robust date calculation handles all leap year rules and century boundaries
- **Daily Re-sync**: Automatic time sync once per 24 hours
- **Fallback Mode**: Uses hour-counting if time sync fails
- **Accuracy**: Reports sent within 1-hour window of configured time

### LED Status Indicators
- **Solid ON during alarm**: System detected high tank level
- **Brief flash during wake**: Normal operation check

### Data Logging
- All events logged to "tanklog.txt" on SD card
- Format: "YYYY-MM-DD HH:MM:SS - Event description"
- Logs include: startup, alarms, daily reports, network status
- **Daily report transmission log**: Separate "report_log.txt" tracks successful reports
- **Persistent duplicate prevention**: System uses log files to prevent duplicate reports after restarts

## Troubleshooting

### Common Issues

1. **No Cellular Connection**:
   - Check SIM card installation
   - Verify Hologram.io account status
   - Check signal strength in installation location

2. **SD Card Issues**:
   - Ensure SD card is formatted as FAT32
   - Check card capacity (32GB max recommended)
   - Verify card is properly inserted

3. **False Alarms**:
   - Check float switch wiring
   - Verify sensor mechanical operation
   - Review tank level thresholds

4. **SMS Not Received**:
   - Verify phone numbers are correct (include country code)
   - Check Hologram.io account balance
   - Confirm SMS service is enabled

### Testing Procedures

1. **Initial Setup Test**:
   - Upload code and monitor serial output
   - Verify network connection
   - Test SMS functionality with startup notification

2. **Sensor Test**:
   - Manually trigger float switch
   - Verify alarm SMS is sent
   - Check LED status indication

3. **Daily Report Test**:
   - Modify timing variables for testing
   - Verify daily SMS is sent
   - Check SD card logging

## Maintenance

### Regular Maintenance
- Check SD card space monthly
- Verify cellular signal strength
- Test alarm system quarterly
- Update Hologram.io account as needed

### Battery Life Optimization
- System designed for low power operation
- Expected battery life depends on usage and battery capacity
- Consider solar charging for remote installations

## Version History

- **092025**: Initial MKR NB 1500 implementation
  - Migration from SparkFun LTE Shield to MKR NB 1500
  - Added SD card logging capability
  - Integrated MKR RELAY shield support
  - Hologram.io cloud integration
  - Low power sleep modes

## Support

For technical support or questions:
- Review this documentation
- Check Arduino MKR NB 1500 documentation
- Contact Hologram.io support for cellular connectivity issues
- Refer to shield documentation for hardware-specific questions