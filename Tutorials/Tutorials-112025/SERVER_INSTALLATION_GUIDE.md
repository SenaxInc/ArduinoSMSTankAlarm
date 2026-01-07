# TankAlarm Server Installation Guide

**Setting Up Your Central Tank Monitoring Server**

---

## Introduction

The TankAlarm 112025 Server is a centralized data aggregation and web dashboard server built on the Arduino Opta platform. It receives telemetry from field-deployed client devices via the Blues Notecard cellular network, provides a real-time web interface for monitoring, and manages alarm notifications.

### What You'll Build

The TankAlarm Server acts as the central hub for your tank monitoring fleet:

- **Web Dashboard** - Real-time monitoring via browser on local network
- **Data Aggregation** - Collect telemetry from unlimited client devices
- **Alarm Management** - SMS alerts for high/low tank conditions
- **Remote Configuration** - Push settings to clients via cellular
- **Daily Reports** - Email summaries of fleet status
- **Historical Data** - Track trends and maintenance schedules
- **API Access** - RESTful endpoints for integration

### Required Materials

To complete this installation, you'll need:

**Hardware:**
- [Arduino Opta Lite](https://store-usa.arduino.cc/products/opta-lite) - Industrial controller platform
- [Blues Wireless for Opta](https://shop.blues.com/collections/accessories/products/wireless-for-opta) - Cellular module
- Ethernet cable (Cat5e or Cat6) for network connection
- USB-C cable for programming
- 12-24V DC power supply or PoE (Power over Ethernet)
- Router/switch with available Ethernet port

**Software:**
- Arduino IDE 2.0+ (or 1.8.19+)
- Active Blues Notehub account ([notehub.io](https://notehub.io))
- Web browser for dashboard access
- This repository's source code

### Suggested Reading

Before starting, familiarize yourself with these concepts:

- [What is Arduino?](https://learn.sparkfun.com/tutorials/what-is-an-arduino) - Arduino basics
- [Ethernet Networking](https://learn.sparkfun.com/tutorials/ethernet-basics) - Understanding TCP/IP
- [Blues Notecard Quickstart](https://dev.blues.io/quickstart/) - Cellular IoT basics
- [RESTful APIs](https://learn.sparkfun.com/tutorials/restful-apis) - Web service fundamentals

---

## Hardware Overview

### Arduino Opta Platform

The Arduino Opta is an industrial-grade Micro PLC with:

- **Processor**: STM32H747XI dual-core (Cortex-M7 @ 480 MHz + Cortex-M4 @ 240 MHz)
- **Memory**: 2 MB Flash, 1 MB RAM
- **Connectivity**: 10/100 Mbps Ethernet, USB-C, I2C, RS-485
- **Operating Voltage**: 12-24V DC or PoE (802.3af)
- **Temperature**: -20°C to +50°C operating range
- **Mounting**: DIN rail or panel mount

![Arduino Opta](https://docs.arduino.cc/static/opta-ethernet.png)

### Blues Notecard Cellular Module

The Blues Wireless for Opta provides:

- **Cellular**: LTE-M and NB-IoT global coverage
- **Prepaid Data**: 500 MB for 10 years (included)
- **Inbound Routing**: Receive data from client devices
- **Notehub Integration**: Cloud-based device management
- **Secure**: TLS encryption for all communications

### Ethernet Connectivity

**Network Requirements:**
- DHCP server (automatic IP assignment)
- Open port 80 for web server
- Same subnet as client computers
- Static IP optional (configurable in code)

**Supported Features:**
- Web server on port 80
- TCP/IP client connections
- DNS resolution
- Auto-negotiation (10/100 Mbps)

### System Architecture

```
[Client Devices] 
     ↓ (Cellular via Blues)
[Blues Notehub Cloud]
     ↓ (Cellular via Blues)
[Server Notecard]
     ↓ (I2C)
[Arduino Opta Server]
     ↓ (Ethernet)
[Web Browser Dashboard]
```

---

## Step 1: Install Arduino IDE

### Download and Install

1. Visit [arduino.cc/software](https://www.arduino.cc/en/software)
2. Download Arduino IDE for your operating system:
   - **Windows**: Windows 10 or later (Installer .exe)
   - **macOS**: macOS 10.14 Mojave or later (.dmg)
   - **Linux**: AppImage or ZIP archive
3. Run the installer and follow prompts
4. Launch Arduino IDE after installation

### First Launch Setup

When Arduino IDE starts for the first time:

1. Grant permissions if prompted
2. Enable automatic updates
3. Choose language (English recommended)

**Arduino IDE 2.0 Advantages:**
- Integrated serial monitor/plotter
- Code auto-completion
- Library manager improvements
- Better error diagnostics
- Faster compilation

---

## Step 2: Install Board Support

### Add Arduino Opta to IDE

The Arduino Opta uses the Arduino Mbed OS core:

1. Open Arduino IDE
2. Navigate to **Tools → Board → Boards Manager**
3. In the search field, type: `Arduino Mbed OS Opta Boards`
4. Find **Arduino Mbed OS Opta Boards** by Arduino
5. Click the **Install** button
6. Wait for installation (5-10 minutes, ~400 MB download)
7. Confirm "INSTALLED" badge appears

![Boards Manager](https://docs.arduino.cc/static/boards-manager.png)

### Verify Installation

To confirm successful installation:

1. Go to **Tools → Board**
2. Expand **Arduino Mbed OS Opta Boards**
3. Verify **Arduino Opta** appears in the list

**Troubleshooting Tip**: If installation fails:
- Check internet connection stability
- Verify antivirus isn't blocking downloads
- Ensure ~1 GB free disk space
- Restart Arduino IDE and retry

---

## Step 3: Install Required Libraries

### Using the Library Manager

The TankAlarm Server requires these libraries:

1. Open **Tools → Manage Libraries** (or Ctrl+Shift+I)
2. For each library below, search and install:

#### Required Libraries Table

| Library | Version | Purpose | Install Command |
|---------|---------|---------|-----------------|
| **ArduinoJson** | 7.0.0+ | JSON parsing & REST API | Search "ArduinoJson" → Install |
| **Blues Wireless Notecard** | Latest | Cellular communication | Search "Notecard" → Install |
| **Ethernet** | Built-in | Web server & networking | No install needed |
| **LittleFS** | Built-in | Configuration storage | No install needed |
| **Wire** | Built-in | I2C communication | No install needed |

### Step-by-Step Library Installation

#### 1. Install ArduinoJson

1. In Library Manager, type: `ArduinoJson`
2. Find **ArduinoJson by Benoit Blanchon**
3. **Critical**: Install version **7.0.0 or later**
   - Version 7 has breaking changes from v6
   - Verify dropdown shows 7.x
4. Click **Install**
5. Wait for "INSTALLED" confirmation

**Why ArduinoJson?** Efficient JSON handling for configuration files and REST API responses with minimal memory overhead.

#### 2. Install Blues Wireless Notecard Library

1. In Library Manager, type: `Notecard`
2. Find **Blues Wireless Notecard by Blues Inc.**
3. Click **Install** (latest version)
4. Wait for installation to complete

**Why Notecard Library?** High-level API for cellular communication without complex AT commands.

#### 3. Verify Built-In Libraries

These come with Arduino Mbed OS core:

- **Ethernet**: TCP/IP networking and web server
- **LittleFS**: Flash file system for persistent storage
- **Wire**: I2C protocol for Notecard communication

No separate installation required!

### Verify All Libraries Installed

1. Go to **Sketch → Include Library**
2. Confirm you see:
   - ArduinoJson
   - Ethernet
   - Notecard
3. Go to **File → Examples**
4. Verify example sketches appear for each library

---

## Step 4: Set Up Blues Notehub

The server requires Blues Notehub configuration to receive data from clients:

### Create Notehub Account

1. Visit [notehub.io](https://notehub.io)
2. Click **Sign Up** (free tier available)
3. Verify your email address
4. Log in to Notehub dashboard

### Create a Product

1. Click **Create Product**
2. Fill in details:
   - **Product Name**: `TankAlarm Fleet`
   - **Description**: `Tank and pump monitoring system`
   - **Product Type**: `Device Fleet`
3. Click **Create**
4. **Save the Product UID** (format: `com.company.product:project`)

Example: `com.senax.tankalarm:production`

### Create Fleets

Fleets enable device-to-device routing without manual route configuration:

1. In your product, navigate to **Fleets** tab
2. Create **two fleets**:

**Fleet 1: Server Fleet**
- **Name**: `tankalarm-server`
- **Description**: `Central data aggregation servers`
- **Purpose**: Target for client telemetry

**Fleet 2: Client Fleet**
- **Name**: `tankalarm-clients`
- **Description**: `Field monitoring devices`
- **Purpose**: Target for server configuration broadcasts

### Fleet Architecture

```
Clients → send to → fleet.tankalarm-server (Server receives)
Server  → send to → device:<uid> (Specific client receives)
Server  → send to → fleet.tankalarm-clients (All clients receive)
```

### Provision Your Server Notecard

1. Connect your Blues Notecard via USB (if using Notecarrier) or insert into Opta
2. Visit [notehub.io](https://notehub.io) → **Devices**
3. Click **Claim Device**
4. Follow provisioning wizard
5. Assign to **`tankalarm-server`** fleet

**Alternative**: Set Product UID in firmware; Notecard self-provisions on first connection.

---

## Step 5: Configure Network

### DHCP (Default - Recommended)

The server uses DHCP by default for automatic IP assignment:

**Advantages:**
- Zero configuration required
- Works on any network
- Easy setup for testing

**Disadvantages:**
- IP may change on router reboot
- Requires finding IP via router or serial monitor

### Static IP (Optional)

For permanent installations, configure a static IP:

1. Open the server `.ino` file
2. Find the Ethernet initialization section:
   ```cpp
   // DHCP mode (default)
   if (Ethernet.begin(mac) == 0) {
     Serial.println(F("DHCP failed"));
   }
   ```

3. Replace with static configuration:
   ```cpp
   // Static IP configuration
   IPAddress ip(192, 168, 1, 100);      // Server IP
   IPAddress dns(192, 168, 1, 1);       // DNS server
   IPAddress gateway(192, 168, 1, 1);   // Default gateway
   IPAddress subnet(255, 255, 255, 0);  // Subnet mask
   
   Ethernet.begin(mac, ip, dns, gateway, subnet);
   ```

4. Update IP addresses to match your network
5. Save changes

**Network Planning:**
- Choose IP outside DHCP range
- Document assigned IP for reference
- Reserve IP in router if possible

---

## Step 6: Configure and Upload Firmware

### Open the Server Sketch

1. Download or clone this repository
2. Navigate to `TankAlarm-112025-Server-BluesOpta/`
3. Double-click `TankAlarm-112025-Server-BluesOpta.ino`
4. Arduino IDE opens with all project files

### Update Product UID

Before compiling, configure your Product UID:

1. Open the main `.ino` file
2. Find this line near the top:
   ```cpp
   #define SERVER_PRODUCT_UID "com.your-company.your-product:your-project"
   ```
3. Replace with **your Product UID** from Notehub:
   ```cpp
   #define SERVER_PRODUCT_UID "com.senax.tankalarm:production"
   ```
4. Save the file (Ctrl+S)

**Critical**: Product UID must match exactly or Notecard communication will fail!

### Review Configuration Options

Optional customizations:

```cpp
// Web server port (default: 80)
#define WEB_SERVER_PORT 80

// Dashboard auto-refresh interval (seconds)
#define WEB_REFRESH_SECONDS 21600  // 6 hours

// Watchdog timeout (seconds)
#define WATCHDOG_TIMEOUT_SECONDS 600  // 10 minutes

// Fleet names (must match Notehub)
#define DEFAULT_CLIENT_FLEET "tankalarm-clients"
```

### Select Board and Port

1. Connect Arduino Opta via USB-C cable
2. Go to **Tools → Board → Arduino Mbed OS Opta Boards → Arduino Opta**
3. Go to **Tools → Port**
4. Select port showing `Arduino Opta` (e.g., COM3, /dev/ttyACM0)

### Verify/Compile Firmware

Test compilation before uploading:

1. Click **Verify** button (✓ checkmark icon)
2. Wait for compilation (2-4 minutes first time)
3. Check console for results

**Expected Output:**
```
Sketch uses 612840 bytes (78%) of program storage space. Maximum is 786432 bytes.
Global variables use 184512 bytes (35%) of dynamic memory.
Done compiling
```

**Memory Usage Guidelines:**
- **Flash**: 70-85% is normal (server is larger than client)
- **RAM**: 30-50% typical
- **Warning**: If >90% flash, reduce features or optimize

### Upload to Arduino Opta

1. Ensure Opta is connected via USB
2. Click **Upload** button (→ right arrow icon)
3. Monitor progress bar
4. First upload takes 1-2 minutes
5. Success message: `Upload complete`

**Upload Sequence:**
1. Firmware compilation
2. Binary generation
3. Arduino Opta reset
4. Bootloader activation
5. Firmware upload
6. Automatic restart

---

## Step 7: Connect Ethernet and Power

### Ethernet Connection

1. Disconnect USB cable (or leave connected for debugging)
2. Connect RJ45 Ethernet cable to Opta's Ethernet port
3. Connect other end to network switch/router
4. Verify link LEDs illuminate on Ethernet jack

**LED Indicators:**
- **Green LED**: Link established (solid = connected)
- **Amber LED**: Activity (flashing = data transfer)

### Power Connection

**Option 1: DC Power Supply**
1. Connect 12-24V DC power supply to Opta power terminals
2. Observe polarity: Red (+), Black/Blue (-)
3. Secure wire connections in screw terminals
4. Power on supply

**Option 2: Power over Ethernet (PoE)**
1. Use PoE-compatible switch (802.3af)
2. Connect Ethernet cable (data + power)
3. No separate power supply needed
4. Verify Opta powers on via PoE

**Recommended**: PoE simplifies installation and reduces wiring.

---

## Step 8: Verify Operation

### Open Serial Monitor (if USB connected)

1. Go to **Tools → Serial Monitor** (or Ctrl+Shift+M)
2. Set baud rate to **115200**
3. Press **Reset** button on Opta
4. Watch for startup messages

### Expected Serial Output

```
TankAlarm 112025 Server - Blues Opta v1.0.0 (Jan 7 2026)
Initializing...
LittleFS initialized (524288 bytes free)
Loading configuration from /server_config.json
Configuration loaded successfully

Notecard initialization...
Notecard UID: dev:864475044234567
Product UID: com.senax.tankalarm:production
Assigned to fleet: tankalarm-server

Ethernet initialization...
Ethernet connected via DHCP
IP address: 192.168.1.150
MAC address: 48:E7:29:A3:B1:C4
Subnet: 255.255.255.0
Gateway: 192.168.1.1
DNS: 192.168.1.1

Web server started on port 80
Storage: LittleFS internal flash
Ready to accept client data
Main loop started...
```

### Initial Configuration

On first boot, default configuration is created:

```json
{
  "serverName": "Tank Alarm Server",
  "clientFleet": "tankalarm-clients",
  "smsPrimary": "+12223334444",
  "smsSecondary": "+15556667777",
  "dailyEmail": "reports@example.com",
  "dailyHour": 6,
  "dailyMinute": 0,
  "webRefreshSeconds": 21600,
  "useStaticIp": false
}
```

**Configuration persists** in LittleFS across reboots and power cycles.

### Find Server IP Address

If serial monitor isn't available:

**Method 1: Router Admin Panel**
1. Log into router admin interface
2. Check DHCP client list
3. Look for "Arduino Opta" or MAC starting with `48:E7:29`

**Method 2: Network Scanner**
Use tools like:
- **Windows**: Advanced IP Scanner
- **macOS/Linux**: `nmap` or `arp-scan`
- **Mobile**: Fing app

**Method 3: mDNS (if enabled)**
Try accessing: `http://tankalarm.local/`

---

## Step 9: Access Web Dashboard

### Navigate to Dashboard

1. Open web browser on a computer on the same network
2. Enter server IP in address bar: `http://192.168.1.150/`
3. Dashboard loads and displays monitoring interface

### Dashboard Overview

The TankAlarm dashboard provides:

**Header Section:**
- Server name and version
- Last update timestamp
- Current time
- System status indicators

**Fleet Status Panel:**
- List of all connected clients
- Site names and device labels
- Last communication time
- Connection status indicators

**Tank Monitoring Panel:**
- Real-time tank levels (feet/inches and percentage)
- Visual level indicators (color-coded)
- Alarm status (high/low alerts)
- Last update time per tank
- 24-hour change tracking

**Alarm Panel:**
- Active alarms highlighted in red
- Alarm type (high/low)
- Tank identification
- Time alarm triggered
- Alarm history log

**Configuration Panel:**
- Server settings editor
- Client configuration interface
- SMS/email contact management
- Daily report scheduler

### Dashboard Features

**Auto-Refresh:**
- Default: Every 6 hours
- Configurable via server settings
- Manual refresh button available

**Responsive Design:**
- Works on desktop, tablet, and mobile
- Scales to different screen sizes
- Touch-friendly interface

**Data Visualization:**
- Color-coded tank levels
  - **Green**: Normal range
  - **Yellow**: Approaching alarm threshold
  - **Red**: Alarm condition
- Percentage bars for quick visual reference
- Trend indicators (rising/falling/stable)

---

## Step 10: Configure Server Settings

### Access Configuration

1. On dashboard, scroll to **"Server Configuration"** section
2. Current settings displayed in form

### Update Server Settings

| Setting | Purpose | Example |
|---------|---------|---------|
| **Server Name** | Display name | "North District Server" |
| **Client Fleet** | Target fleet for broadcasts | "tankalarm-clients" |
| **Primary SMS** | First SMS alert contact | "+12223334444" |
| **Secondary SMS** | Backup SMS contact | "+15556667777" |
| **Tertiary SMS** | Third SMS contact | "+19998887777" |
| **Daily Email** | Email for daily reports | "ops@company.com" |
| **Report Hour** | Daily report time (0-23) | 6 (6 AM) |
| **Report Minute** | Report minute (0-59) | 0 |

### Save Configuration

1. Edit desired fields
2. Click **"Save Server Configuration"**
3. Settings saved to LittleFS
4. Confirmation message appears
5. Changes effective immediately

### SMS Configuration

**SMS Alerts via Blues Notehub:**
The server sends alarm requests to Blues Notehub, which delivers SMS via Twilio integration.

**Setup Requirements:**
1. Configure Twilio account in Notehub
2. Add route: Notecard → Twilio
3. Map alarm notes to SMS delivery
4. Test with sample alarm

See [Blues SMS Guide](https://dev.blues.io/guides-and-tutorials/twilio-sms-guide/) for detailed Twilio setup.

---

## Step 11: Configure Clients

Once clients start sending telemetry, configure them remotely:

### View Connected Clients

1. Dashboard shows all clients that have sent data
2. Client dropdown auto-populates with Device UIDs
3. Select client to configure

### Update Client Configuration

1. Scroll to **"Update Client Config"** section
2. Select client from dropdown
3. Edit configuration fields:
   - Site name and device label
   - Server fleet (where to send data)
   - Sample interval (seconds)
   - Level change threshold (inches)
   - Tank configurations (up to 8 per client)

### Tank Configuration

For each tank:

| Field | Description | Example |
|-------|-------------|---------|
| **Tank ID** | Letter identifier (A-H) | "A" |
| **Tank Name** | Descriptive name | "Diesel Storage" |
| **Tank Number** | Numeric ID | 1 |
| **Sensor Type** | analog/float/current_loop | "analog" |
| **Pin** | Analog input pin | 0 |
| **Height** | Tank height in inches | 120.0 |
| **High Alarm** | High level threshold | 110.0 |
| **Low Alarm** | Low level threshold | 20.0 |
| **Daily Report** | Include in daily email | true |
| **Alarm SMS** | Send SMS for this tank | true |

### Send Configuration to Client

1. Click **"Send Config to Client"**
2. Server packages JSON configuration
3. Sends via Blues Notehub to client's Device UID
4. Client receives, applies, and saves config
5. Confirmation shown in server console

**Propagation Time**: Configuration typically reaches client within 1-2 minutes.

---

## Troubleshooting

### Compilation Errors

#### Error: `ArduinoJson.h: No such file or directory`

**Cause**: ArduinoJson library not installed

**Solution:**
1. Open **Tools → Manage Libraries**
2. Search `ArduinoJson`
3. Install version 7.0.0 or later
4. Retry compilation

#### Error: `Notecard.h: No such file or directory`

**Cause**: Blues Notecard library missing

**Solution:**
1. Open **Tools → Manage Libraries**
2. Search `Notecard`
3. Install **Blues Wireless Notecard**
4. Restart Arduino IDE
5. Retry compilation

#### Error: `Ethernet.h: No such file or directory`

**Cause**: Ethernet library not found (should be built-in)

**Solution:**
1. Verify Arduino Mbed OS Opta Boards installed
2. Try installing via Library Manager: "Ethernet by Arduino"
3. Restart IDE and retry

#### Error: `Sketch too big`

**Cause**: Wrong board selected or excessive debug code

**Solution:**
1. Verify: **Tools → Board → Arduino Opta**
2. Remove debug code if present
3. Ensure optimization enabled
4. Server sketch is larger but should fit in 2MB

### Upload Errors

#### Error: `Port not found`

**Solutions:**
1. Check USB-C cable is data-capable (not charge-only)
2. Try different USB port
3. Install CH340 drivers if needed (Windows)
4. Check Device Manager / `ls /dev/tty*`
5. Press reset and retry

#### Error: `Upload timeout`

**Solution:**
1. Press and hold Reset button
2. Click Upload in IDE
3. Release Reset when "Uploading..." appears
4. Bootloader should respond

#### Error: `Verification failed`

**Causes:**
- USB interference
- Corrupted upload
- Hardware issue

**Solutions:**
1. Use different USB cable
2. Connect directly to computer (not via hub)
3. Close programs using serial ports
4. Retry upload

### Runtime Errors

#### Serial Output: `LittleFS init failed; halting`

**Cause**: Flash corruption or unformatted

**Solution:**
1. Re-upload firmware
2. If persistent, add format command:
   ```cpp
   LittleFS.format();  // Add before LittleFS.begin()
   ```
3. Upload, let format, remove line, upload again

#### Serial Output: `Failed to initialize Notecard`

**Causes & Solutions:**

1. **Module not seated**: Press Blues Notecard firmly into socket
2. **I2C issue**: Check connections, verify address 0x17
3. **Firmware outdated**: Update Notecard via Blues CLI

**Test Command:**
```cpp
{"req":"card.version"}
```

Should return version info.

#### Serial Output: `Ethernet failed`

**Causes:**
- Cable not connected
- Bad cable
- No DHCP server
- Network incompatibility

**Solutions:**
1. Verify Ethernet cable firmly connected
2. Try different cable
3. Check link LEDs on Ethernet port
4. Verify router DHCP is enabled
5. Try static IP configuration

#### Cannot Access Dashboard

**Diagnostic Steps:**

1. **Verify IP from serial monitor**
   - Correct IP displayed?
   - Same subnet as computer?

2. **Test ping:**
   ```bash
   ping 192.168.1.150
   ```
   - Should respond if network OK

3. **Check firewall:**
   - Allow port 80 inbound
   - Temporarily disable firewall to test

4. **Browser issues:**
   - Try different browser
   - Clear cache
   - Use private/incognito mode
   - Try from different computer

5. **Network isolation:**
   - Same VLAN/subnet?
   - No AP isolation on WiFi?

#### Dashboard Loads But No Data

**Causes:**
- Clients haven't sent telemetry yet
- Wrong fleet configuration
- Notecard offline

**Solutions:**
1. Wait for client sample interval (default 30 min)
2. Verify client `serverFleet` matches server fleet name
3. Check Blues Notehub Events for note traffic
4. Verify clients assigned to correct fleet
5. Check client serial console for "Telemetry sent" messages

### Network Issues

#### DHCP Not Working

**Solutions:**
1. Verify DHCP server enabled on router
2. Check DHCP pool not exhausted
3. Try power cycling Opta
4. Check MAC address not blocked
5. Configure static IP as workaround

#### Static IP Not Working

**Common Mistakes:**
- Wrong subnet mask
- Gateway not reachable
- IP conflicts with another device
- Outside router's subnet

**Verification:**
```cpp
// Add to setup() after Ethernet.begin():
Serial.print("IP: "); Serial.println(Ethernet.localIP());
Serial.print("Mask: "); Serial.println(Ethernet.subnetMask());
Serial.print("Gateway: "); Serial.println(Ethernet.gatewayIP());
Serial.print("DNS: "); Serial.println(Ethernet.dnsServerIP());
```

#### Cannot Access from Other Subnets

**Cause**: Router not routing between VLANs

**Solutions:**
1. Configure router to allow inter-VLAN traffic
2. Add static routes if needed
3. Place server on main VLAN
4. Use VPN or port forwarding (not recommended for security)

---

## Advanced Configuration

### Custom Web Server Port

Change from default port 80:

```cpp
// Find this line:
EthernetServer server(80);

// Change to:
EthernetServer server(8080);
```

**Note**: Users must access via `http://ip:8080/`

### Adjusting Dashboard Refresh

Modify auto-refresh interval:

```cpp
#define WEB_REFRESH_SECONDS 3600  // 1 hour instead of 6
```

Or configure via dashboard server settings.

### Custom CSS Styling

The HTML is embedded in PROGMEM. To customize:

1. Find `const char DASHBOARD_HTML[] PROGMEM =` section
2. Modify CSS within `<style>` tags
3. Recompile and upload

**Example**: Change theme colors:
```css
:root {
  --bg: #1a1a1a;        /* Dark background */
  --text: #ffffff;       /* White text */
  --card-bg: #2d2d2d;    /* Card background */
}
```

### Adding Custom API Endpoints

The server supports REST API extensions:

```cpp
// In handleWebRequests() function, add:
if (path == "/api/custom/endpoint") {
  String response = "{\"status\":\"ok\"}";
  respondJson(client, response);
  return;
}
```

### Database Integration

For persistent historical data:

**Option 1: SD Card Logging**
- Add SD card module
- Log telemetry to CSV
- Process externally

**Option 2: Cloud Database**
- Use Blues Routes to forward data
- Send to AWS, Azure, or Google Cloud
- Store in time-series database

**Option 3: Local Database**
- Add Ethernet-connected database server
- Use HTTP POST to send data
- Query for historical reports

---

## Security Considerations

### Web Interface Security

**Current State:**
- No authentication by default
- Designed for trusted LAN only
- Port 80 unencrypted HTTP

**Recommendations for Production:**

1. **Network Isolation**
   - Deploy on private VLAN
   - Use firewall rules to restrict access
   - Limit to management subnet only

2. **Add Authentication**
   - Implement PIN/password check
   - Use HTTP Basic Auth
   - Session cookies for web UI

3. **HTTPS/TLS**
   - Add TLS library
   - Generate certificates
   - Serve on port 443

4. **VPN Access**
   - Don't expose to internet
   - Use VPN for remote access
   - Firewall blocks external connections

### SMS Security

**Twilio Integration:**
- API keys stored in Notehub (not on device)
- TLS-encrypted communication
- Rate limiting to prevent abuse

**Best Practices:**
- Limit SMS recipients to verified numbers
- Monitor SMS usage for anomalies
- Set up Twilio alerts for unusual activity

### Notecard Security

**Built-in Features:**
- TLS 1.2+ for all cloud communication
- Device authentication via UID
- Encrypted note storage
- Secure provisioning

**No Additional Config Required** - security is automatic.

---

## Field Deployment

### Installation Checklist

- [ ] **Power**: 12-24V DC or PoE connected and tested
- [ ] **Ethernet**: Cat5e/Cat6 cable run to server location
- [ ] **Network**: DHCP available or static IP configured
- [ ] **Notecard**: Activated in Notehub, assigned to server fleet
- [ ] **Configuration**: Server settings updated via web UI
- [ ] **Fleets**: Both fleets created in Notehub
- [ ] **Clients**: At least one client deployed and sending data
- [ ] **SMS**: Twilio route configured in Notehub (if using SMS)
- [ ] **Testing**: Verified telemetry reception and dashboard access
- [ ] **Documentation**: IP address and credentials recorded
- [ ] **Backup**: Configuration backed up via FTP (if enabled)

### Wiring Guidelines

**Power (DC Supply):**
- 18-22 AWG wire for 12-24V DC
- Polarity: Red (+), Black/Blue (-)
- Fuse: 2A recommended
- Use terminal blocks for easy disconnection

**Ethernet:**
- Cat5e minimum (Cat6 preferred)
- Maximum run: 100 meters (328 feet)
- Use solid-core for permanent installation
- Use stranded for patch cables
- Proper termination (T568B standard)

**Grounding:**
- Connect chassis ground to earth
- Use star grounding topology
- Shield Ethernet cable if outdoor runs

### Environmental Considerations

**Operating Environment:**
- Temperature: -20°C to +50°C
- Humidity: 10-90% RH (non-condensing)
- Altitude: Up to 2000m
- Pollution degree: 2 (IEC 60664-1)

**Enclosure:**
- IP20 minimum (enclosed equipment room)
- IP65 for outdoor/harsh environments
- DIN rail mounting recommended
- Adequate ventilation for heat dissipation
- Cable glands for weather sealing

**Power Protection:**
- Surge suppressor on AC supply
- Isolation transformer if needed
- UPS for continuous operation
- Proper circuit breaker sizing

---

## Backup and Recovery

### Configuration Backup

**Manual Backup:**
1. Access dashboard
2. Note all server settings
3. Export client configurations
4. Save contact lists

**Automatic Backup (if FTP enabled):**
- Server can push configs to FTP server
- Scheduled daily or on-change
- Includes server and all client configs

### Firmware Recovery

If firmware becomes corrupted:

1. Connect via USB
2. Select Arduino Opta board
3. Upload last known good firmware
4. Configuration persists in LittleFS

### Factory Reset

To reset to defaults:

1. Add to setup():
   ```cpp
   LittleFS.format();  // Wipes all configuration
   ```
2. Upload firmware
3. Let device boot and create defaults
4. Remove format line
5. Upload clean firmware
6. Reconfigure via web UI

---

## Monitoring and Maintenance

### Health Checks

**Daily:**
- [ ] Check dashboard loads
- [ ] Verify recent client updates
- [ ] Review active alarms

**Weekly:**
- [ ] Test alarm delivery (SMS/email)
- [ ] Review Notehub consumption data
- [ ] Check for firmware updates
- [ ] Verify client connectivity

**Monthly:**
- [ ] Review historical trends
- [ ] Update client configurations
- [ ] Test backup/recovery procedures
- [ ] Check for hardware issues

### Performance Monitoring

**Metrics to Track:**
- Web server response time
- Note sync frequency
- Client check-in rate
- Cellular data usage
- Memory usage (RAM/Flash)

**Notehub Dashboard:**
- View device activity
- Monitor data consumption
- Track note routing
- Review event logs

### Firmware Updates

Use Blues Notecard DFU for OTA updates:

1. Compile new firmware
2. Upload .bin to Notehub
3. Target server device
4. Server auto-updates on next check

See [Firmware Update Guide](FIRMWARE_UPDATE_GUIDE.md) for details.

---

## Resources and Going Further

### Hardware Documentation

- [Arduino Opta Datasheet](https://docs.arduino.cc/resources/datasheets/ABX00064-datasheet.pdf)
- [Arduino Opta Getting Started](https://docs.arduino.cc/tutorials/opta/getting-started)
- [Arduino Opta Networking Guide](https://docs.arduino.cc/tutorials/opta/getting-started#network)
- [Blues Notecard Datasheet](https://dev.blues.io/datasheets/notecard-datasheet/)
- [Blues for Opta Guide](https://dev.blues.io/hardware/opta-notecarrier/)

### Software Documentation

- [ArduinoJson Documentation](https://arduinojson.org/v7/doc/)
- [Blues Notecard API](https://dev.blues.io/api-reference/notecard-api/introduction/)
- [Arduino Ethernet Library](https://www.arduino.cc/reference/en/libraries/ethernet/)
- [Mbed OS API Reference](https://os.mbed.com/docs/mbed-os/v6.16/apis/index.html)

### Project Documentation

- [TankAlarm Client Installation](CLIENT_INSTALLATION_GUIDE.md)
- [Fleet Setup Guide](FLEET_SETUP_GUIDE.md)
- [Firmware Update Guide](FIRMWARE_UPDATE_GUIDE.md)
- [Quick Start Guide](QUICK_START_GUIDE.md)
- [Historical Data Architecture](../TankAlarm-112025-Server-BluesOpta/HISTORICAL_DATA_ARCHITECTURE.md)

### Getting Help

**Technical Support:**
- **Arduino**: [Arduino Forum - Opta](https://forum.arduino.cc/c/hardware/opta/181)
- **Blues**: [Blues Community Forum](https://community.blues.io)
- **GitHub**: [Project Issues](https://github.com/SenaxInc/ArduinoSMSTankAlarm/issues)
- **Email**: support@senax.com

**Useful API Endpoints:**

```bash
# Get system status
GET http://server-ip/api/status

# Get all tanks
GET http://server-ip/api/tanks

# Get active alarms  
GET http://server-ip/api/alarms

# DFU status (if enabled)
GET http://server-ip/api/dfu/status
```

**Notecard Commands:**

```cpp
// Check connectivity
{"req":"hub.sync.status"}

// View inbound notes
{"req":"note.get", "file":"telemetry.qi"}

// Force sync
{"req":"hub.sync"}
```

---

## Next Steps

After successful installation:

1. **[Install Clients](CLIENT_INSTALLATION_GUIDE.md)** - Deploy field devices
2. **[Configure Fleet](FLEET_SETUP_GUIDE.md)** - Organize devices in Notehub
3. **[Test System](QUICK_START_GUIDE.md)** - End-to-end verification
4. **[Configure Alarms](README.md)** - Set up SMS notifications
5. **[Schedule Reports](README.md)** - Daily email summaries
6. **[Monitor Operations](README.md)** - Ongoing maintenance

---

*Server Installation Guide v1.0 | Last Updated: January 7, 2026*  
*Compatible with TankAlarm Server Firmware 1.0.0+*
