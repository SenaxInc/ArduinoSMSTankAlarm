# Tank Alarm Server Installation Guide

## Pre-Installation Requirements

### Hardware Checklist
- [ ] Arduino MKR NB 1500
- [ ] Arduino MKR ETH Shield  
- [ ] Hologram.io SIM card (active data plan)
- [ ] MicroSD card (8GB or larger, FAT32 formatted)
- [ ] Ethernet cable
- [ ] Power supply (5V DC, minimum 1A)
- [ ] Cellular antenna (included with MKR NB 1500)

### Software Requirements
- [ ] Arduino IDE (version 1.8.0 or later)
- [ ] Required Arduino libraries (see Library Installation below)
- [ ] Computer with USB connection for programming

### Network Requirements
- [ ] Local Ethernet network with internet access
- [ ] Router with available Ethernet port
- [ ] Cellular signal coverage for Hologram.io service

## Step 1: Arduino IDE Setup

### Install Arduino IDE
1. Download Arduino IDE from [arduino.cc](https://www.arduino.cc/en/software)
2. Install and launch Arduino IDE
3. Open **Tools → Board → Boards Manager**
4. Search for "Arduino SAMD Boards"
5. Install "Arduino SAMD Boards (32-bits ARM Cortex-M0+)"

### Add MKR NB 1500 Board Support
1. Go to **File → Preferences**
2. In "Additional Boards Manager URLs" add:
   ```
   https://downloads.arduino.cc/packages/package_index.json
   ```
3. Open **Tools → Board → Boards Manager**
4. Search for "Arduino MKR NB 1500"
5. Install the board package

### Install Required Libraries
Install these libraries via **Tools → Manage Libraries**:

| Library Name | Purpose | Required |
|--------------|---------|----------|
| MKRNB | Cellular connectivity | Yes |
| Ethernet | Ethernet connectivity | Yes |
| SD | SD card functionality | Yes |
| RTCZero | Real-time clock | Yes |
| SPI | SPI communication | Yes (usually pre-installed) |

## Step 2: Hardware Assembly

### Assemble the Hardware Stack
1. **Insert SIM Card**:
   - Power off MKR NB 1500
   - Insert Hologram.io SIM card into SIM slot
   - Ensure SIM is properly seated

2. **Prepare SD Card**:
   - Format microSD card as FAT32
   - Insert into MKR ETH Shield SD slot

3. **Stack the Shields**:
   - Carefully align MKR ETH Shield pins with MKR NB 1500
   - Press down gently until fully seated
   - Ensure no pins are bent or misaligned

4. **Connect Antennas**:
   - Attach cellular antenna to u.FL connector on MKR NB 1500
   - Position antenna for optimal signal reception

5. **Connect Ethernet**:
   - Connect Ethernet cable to MKR ETH Shield
   - Connect other end to network router/switch

## Step 3: Software Configuration

### Download Server Code
1. Clone or download the repository
2. Navigate to `TankAlarm-092025-Server-Hologram/` folder
3. Open `TankAlarm-092025-Server-Hologram.ino` in Arduino IDE

### Configure Settings

#### Option 1: SD Card Configuration (Recommended - Field Configurable)
1. **Copy Configuration Template to SD Card**:
   ```bash
   cp server_config.txt /path/to/sd/card/server_config.txt
   ```

2. **Edit SD Card Configuration** (`server_config.txt` on SD card):
   ```
   # Update these values for your setup
   HOLOGRAM_DEVICE_KEY=your_actual_device_key_here
   DAILY_EMAIL_RECIPIENT=+15551234567@vtext.com
   DAILY_EMAIL_HOUR=6
   SERVER_LOCATION=Your Location Name
   ```

3. **Insert SD Card**: Place configured SD card in server before powering on

#### Option 2: Compile-Time Configuration (Legacy)
1. **Edit Configuration** (`server_config.h`) for fallback defaults:
   ```cpp
   // These are used only if SD card config is not available
   #define HOLOGRAM_DEVICE_KEY "your_actual_device_key_here"
   #define DAILY_EMAIL_RECIPIENT "+15551234567@vtext.com" 
   #define DAILY_EMAIL_HOUR 6  // 6 AM for daily reports
   ```

2. **Network Configuration** (optional - can also be set in SD card config):
   ```
   # In server_config.txt on SD card:
   STATIC_IP_ADDRESS=192,168,1,100
   STATIC_GATEWAY=192,168,1,1
   STATIC_SUBNET=255,255,255,0
   ```

### Upload Code to Device
1. **Connect Hardware**:
   - Connect MKR NB 1500 to computer via USB
   - Power on the device

2. **Select Board and Port**:
   - **Tools → Board**: "Arduino MKR NB 1500"
   - **Tools → Port**: Select appropriate COM/USB port

3. **Compile and Upload**:
   - Click **Verify** button to compile
   - Click **Upload** button to program device
   - Monitor progress in console

## Step 4: Initial Testing

### Serial Monitor Setup
1. Open **Tools → Serial Monitor**
2. Set baud rate to **9600**
3. Power cycle the device
4. Watch for startup messages

### Expected Startup Sequence
```
Tank Alarm Server 092025 - Hologram Starting...
SD card initialized successfully
Ethernet IP address: 192.168.1.100
Web server started
Connecting to Hologram network...
Connected to Hologram network
Tank Alarm Server initialized successfully
LOG: 20250919 10:30:15 - Server startup completed
```

### Verify Connections
1. **SD Card**: Should show "SD card initialized successfully"
2. **Ethernet**: Should display assigned IP address
3. **Cellular**: Should show "Connected to Hologram network"

## Step 5: Web Interface Testing

### Access the Dashboard
1. **Find IP Address**: Note IP address from Serial Monitor
2. **Open Web Browser**: Navigate to `http://[IP_ADDRESS]`
3. **Verify Display**: Should show "Tank Alarm Server Dashboard"

### Test Functionality
- [ ] Web page loads without errors
- [ ] Server status shows connections as "Connected"  
- [ ] Page auto-refreshes every 30 seconds
- [ ] No reports initially (normal for new installation)

## Step 6: Network Configuration

### Router Configuration
1. **Reserve IP Address** (recommended):
   - Access router admin interface
   - Find DHCP reservation settings
   - Reserve the assigned IP for server's MAC address

2. **Port Forwarding** (optional):
   - Forward external port to internal port 80
   - Allows remote access to web interface

3. **Firewall Settings**:
   - Ensure local network access is allowed
   - Add exception for server IP if needed

## Step 7: Hologram.io Setup

### Verify Hologram Account
1. **Login** to [Hologram.io Dashboard](https://dashboard.hologram.io/)
2. **Check Device**: Ensure device appears in device list
3. **Verify Data Plan**: Confirm active data plan
4. **Test Connectivity**: Send test message if available

### Configure Client Devices
Ensure tank alarm client devices are configured to send to this server:
- Same Hologram.io account
- Correct device keys
- Matching message format

## Step 8: Testing Data Reception

### Simulate Incoming Data
For testing, you can manually trigger data processing:

1. **Access Serial Monitor**
2. **Send Test Command**: Some implementations may include test functions
3. **Check Web Interface**: Verify test data appears

### Verify Logging
1. **Check SD Card**: Ensure log files are created
   - `daily_reports.txt`
   - `server_log.txt`
   - `alarm_log.txt`

2. **Monitor File Growth**: Files should update as data is received

## Troubleshooting

### Common Issues and Solutions

#### Compilation Errors
- **Missing Libraries**: Install all required libraries
- **Board Not Selected**: Ensure MKR NB 1500 is selected
- **Port Issues**: Try different USB ports or cables

#### Network Connection Issues
- **No IP Address**: Check Ethernet cable and router
- **DHCP Failed**: Verify router DHCP settings
- **Static IP Conflicts**: Ensure static IP is not in use

#### Cellular Connection Issues
- **SIM Not Recognized**: Reseat SIM card, check orientation
- **Network Registration Failed**: Check signal strength and account status
- **Device Key Invalid**: Verify Hologram.io device key

#### SD Card Issues
- **Initialization Failed**: Reformat card as FAT32
- **Write Errors**: Check card health and connections
- **Full Storage**: Monitor available space regularly

### Debug Mode
Enable detailed logging by setting in `server_config.h`:
```cpp
#define ENABLE_SERIAL_DEBUG true
```

## Maintenance Schedule

### Daily Checks
- [ ] Verify web interface accessibility
- [ ] Check cellular and Ethernet connectivity status
- [ ] Monitor for any error messages in logs

### Weekly Checks
- [ ] Review server logs for errors or warnings
- [ ] Check SD card available space
- [ ] Verify received tank reports are being processed

### Monthly Checks
- [ ] Clean/backup old log files
- [ ] Check power supply and connections
- [ ] Verify Hologram.io data usage and billing
- [ ] Test emergency email notifications

## Support and Resources

### Documentation Links
- [Arduino MKR NB 1500 Documentation](https://docs.arduino.cc/hardware/mkr-nb-1500)
- [Hologram.io Developer Documentation](https://hologram.io/docs/)
- [Arduino Ethernet Library Reference](https://www.arduino.cc/en/Reference/Ethernet)

### Common Configuration Files

#### Example server_config.txt (SD Card Configuration)
```
# Tank Alarm Server SD Card Configuration
HOLOGRAM_DEVICE_KEY=1234567890abcdef
DAILY_EMAIL_RECIPIENT=+15551234567@vtext.com
DAILY_EMAIL_HOUR=6
DAILY_EMAIL_MINUTE=0
SERVER_LOCATION=Main Office
ENABLE_SERIAL_DEBUG=true
STATIC_IP_ADDRESS=192,168,1,100
STATIC_GATEWAY=192,168,1,1
STATIC_SUBNET=255,255,255,0
```

#### Example server_config.h (Fallback Defaults)
```cpp
// These are only used if server_config.txt is not found on SD card
#define HOLOGRAM_DEVICE_KEY "1234567890abcdef"
#define DAILY_EMAIL_RECIPIENT "+15551234567@vtext.com"
#define DAILY_EMAIL_HOUR 6
#define DAILY_EMAIL_MINUTE 0
#define STATIC_IP_ADDRESS {192, 168, 1, 100}
#define ENABLE_SERIAL_DEBUG true
```

#### Field Configuration Updates
To update configuration in the field:
1. Remove SD card from server
2. Edit `server_config.txt` on computer
3. Insert SD card back into server
4. Restart server to load new configuration

#### Hologram Device Key Location
Find your device key in the Hologram.io dashboard:
1. Login to dashboard.hologram.io
2. Select your device
3. Copy the device key from device details

This completes the installation process. The server should now be ready to receive tank reports and provide web-based monitoring.