# TankAlarm-112025-Server-BluesOpta Installation Guide

This guide walks you through setting up the Arduino IDE and installing the TankAlarm 112025 Server sketch on an Arduino Opta with Blues Wireless Notecard.

## Hardware Requirements

- **Arduino Opta Lite** - [Purchase Link](https://store-usa.arduino.cc/products/opta-lite)
- **Blues Wireless for Opta** - [Purchase Link](https://shop.blues.com/collections/accessories/products/wireless-for-opta)
- **Ethernet connection** - RJ45 cable for network connectivity
- **USB-C cable** for programming
- **Power supply** - 12-24V DC power for continuous operation

## Software Requirements

- **Arduino IDE** 2.0 or later (recommended) or 1.8.x
- **Arduino Mbed OS Opta Boards** core
- Required Arduino libraries (detailed below)

## Step 1: Install Arduino IDE

If you don't already have Arduino IDE installed:

1. Download Arduino IDE from [https://www.arduino.cc/en/software](https://www.arduino.cc/en/software)
2. Install for your operating system (Windows, macOS, or Linux)
3. Launch Arduino IDE

## Step 2: Install Arduino Opta Board Support

The Arduino Opta uses the Mbed OS core. You need to install board support:

1. Open Arduino IDE
2. Go to **Tools → Board → Boards Manager**
3. In the search box, type: `Arduino Mbed OS Opta Boards`
4. Find **Arduino Mbed OS Opta Boards** in the list
5. Click **Install** (this may take several minutes)
6. Wait for installation to complete

**Alternative using Boards Manager URL (if needed):**
1. Go to **File → Preferences** (or **Arduino IDE → Settings** on macOS)
2. In "Additional Boards Manager URLs", the Arduino Opta boards should already be included in the default Arduino index
3. Click **OK**
4. Follow steps 2-6 above

## Step 3: Install Required Libraries

The TankAlarm 112025 Server requires several libraries. Install them using the Library Manager:

### Using Library Manager (Recommended)

1. Go to **Tools → Manage Libraries** (or **Sketch → Include Library → Manage Libraries**)
2. Install each of the following libraries by searching for them and clicking **Install**:

#### Required Libraries:

| Library Name | Version | Purpose |
|--------------|---------|---------|
| **ArduinoJson** | 7.x or later | JSON parsing and serialization for configuration and API |
| **Blues Wireless Notecard** | Latest | Communication with Blues Notecard cellular module |
| **Ethernet** | Built-in or latest | Network connectivity for web server |
| **LittleFS** | Built-in with Mbed core | File system for persistent configuration storage |
| **Wire** | Built-in | I2C communication |

#### Installing Each Library:

**1. ArduinoJson**
- Search: `ArduinoJson`
- Install: `ArduinoJson by Benoit Blanchon` (version 7.x or later)
- Note: Version 7.x has breaking changes from v6, ensure you use v7+

**2. Blues Wireless Notecard**
- Search: `Notecard`
- Install: `Blues Wireless Notecard by Blues Inc.`
- This provides the Notecard.h interface

**3. Ethernet**
- Search: `Ethernet`
- Install: `Ethernet by Arduino` (built-in library, should already be available)
- Used for the web server and intranet dashboard

**4. LittleFS**
- This is built into the Arduino Mbed OS core
- No separate installation required
- Included when you install Arduino Opta board support

**5. Wire (I2C)**
- Built-in Arduino library
- No installation required

#### Optional Libraries (for development/debugging):

| Library Name | Purpose |
|--------------|---------|
| **IWatchdog** | Built-in watchdog timer (included with Mbed core) |

### Verifying Library Installation

To verify libraries are installed:
1. Go to **Sketch → Include Library**
2. Check that you see:
   - ArduinoJson
   - Ethernet
   - Notecard
3. Go to **File → Examples**
4. You should see example sketches for ArduinoJson, Ethernet, and Notecard

## Step 4: Open the Server Sketch

1. Download or clone this repository
2. Navigate to `TankAlarm-112025-Server-BluesOpta` folder
3. Open `TankAlarm-112025-Server-BluesOpta.ino` in Arduino IDE
4. The IDE will open the sketch in a new window

## Step 5: Configure Board Settings

Before uploading, configure the correct board:

1. Connect your Arduino Opta to your computer via USB-C cable
2. Go to **Tools → Board → Arduino Mbed OS Opta Boards**
3. Select **Arduino Opta**
4. Go to **Tools → Port**
5. Select the port showing your Arduino Opta (e.g., COM3, /dev/ttyACM0, or similar)

### Board Configuration Summary:
- **Board:** Arduino Opta
- **Core:** Arduino Mbed OS Opta Boards
- **Upload Method:** USB
- **Port:** Select the detected Opta device

## Step 6: Configure the Sketch

The server uses LittleFS (internal flash storage) for configuration. You need to configure network and Blues settings:

### Product UID Configuration

Update the Product UID to match your Blues Notehub project:

```cpp
#define SERVER_PRODUCT_UID "com.your-company.your-product:your-project"
```

Find your Product UID in your Blues Notehub project settings.

### Network Configuration

The server uses DHCP by default. For static IP configuration, edit the code if needed.

### Initial Configuration

On first boot, the server creates default configuration in `/server_config.json`:
- Server name: "Tank Alarm Server"
- Client fleet: "tankalarm-clients"
- Default phone numbers and email settings
- Daily report time: 7:00 AM local time

You can modify these via the web interface after initial setup.

## Step 7: Compile the Sketch

Before uploading, verify the sketch compiles:

1. Click the **Verify** button (✓) in the toolbar
2. Wait for compilation to complete
3. Check the output console for any errors
4. If successful, you'll see "Done compiling" and memory usage statistics

**Expected Memory Usage:**
- Flash: Typically 70-85% of available space (server is larger than client)
- RAM: Typically 50-70% of available space

If you see compilation errors, refer to the **Troubleshooting** section below.

## Step 8: Upload to Arduino Opta

1. Ensure Arduino Opta is connected via USB
2. Click the **Upload** button (→) in the toolbar
3. Wait for upload to complete (progress shown in console)
4. On success, you'll see "Upload complete"

**Note:** First upload may take longer as the bootloader initializes.

## Step 9: Connect Ethernet

1. After upload completes, disconnect USB (or leave connected for debugging)
2. Connect RJ45 Ethernet cable to the Opta's Ethernet port
3. Ensure the cable is connected to your local network
4. Power the Opta with 12-24V DC power supply
5. The Opta will obtain an IP address via DHCP

## Step 10: Verify Operation

### Via Serial Monitor (if USB connected):

1. Open **Tools → Serial Monitor** (or press Ctrl+Shift+M)
2. Set baud rate to **115200**
3. Press the **Reset** button on the Opta
4. You should see startup messages including:
   - "TankAlarm 112025 Server - Blues Opta"
   - "LittleFS initialized"
   - "Notecard UID: dev:xxxxx..."
   - "Ethernet connected"
   - "IP address: xxx.xxx.xxx.xxx"
   - "Server listening on port 80"

### Expected Serial Output:
```
TankAlarm 112025 Server - Blues Opta
Initializing...
LittleFS initialized
Configuration loaded from flash
Notecard UID: dev:xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
Ethernet connected
IP address: 192.168.1.100
MAC address: XX:XX:XX:XX:XX:XX
Server listening on port 80
Storage: LittleFS internal flash for configuration
Polling for client data...
```

### Via Network:

1. Note the IP address from serial monitor
2. From a computer on the same network, open a web browser
3. Navigate to `http://<ip-address>/`
4. You should see the Tank Alarm dashboard

If you don't have serial monitor access:
- Check your router's DHCP client list for "Arduino Opta"
- Try common IP ranges: 192.168.1.x, 192.168.0.x, 10.0.0.x

## Step 11: Access the Web Dashboard

1. Open web browser on a device connected to the same network
2. Navigate to: `http://<server-ip>/`
3. The dashboard shows:
   - Current tank levels from all clients
   - Alarm status
   - Last update times
   - Configuration interface

### Dashboard Features:
- **Real-time tank monitoring**: View levels from all connected clients
- **Alarm display**: See current and recent alarms
- **Client configuration**: Update client settings remotely
- **Daily reports**: Schedule and manage daily email reports
- **System status**: View server and client connectivity

## Step 12: Configure Server Settings

### Via Web Interface:

1. Navigate to server dashboard
2. Scroll to "Server Configuration" section
3. Update settings:
   - Server name
   - Client fleet name (must match Notehub fleet)
   - SMS phone numbers (primary, secondary, tertiary)
   - Email recipients for daily reports
   - Daily report time
4. Click **Save Configuration**

### Via Serial Console (Advanced):

Configuration can also be managed by editing the LittleFS JSON file, but web interface is recommended.

## Troubleshooting

### Compilation Errors

**Error: `ArduinoJson.h: No such file or directory`**
- Solution: Install ArduinoJson library via Library Manager (see Step 3)
- Ensure you have version 7.x or later

**Error: `Notecard.h: No such file or directory`**
- Solution: Install Blues Wireless Notecard library via Library Manager
- Search for "Notecard" and install "Blues Wireless Notecard"

**Error: `LittleFS.h: No such file or directory`**
- Solution: Ensure Arduino Mbed OS Opta Boards core is installed
- LittleFS is built into the Mbed core
- Go to Boards Manager and verify "Arduino Mbed OS Opta Boards" is installed

**Error: `Ethernet.h: No such file or directory`**
- Solution: Ethernet library should be built-in
- Try installing via Library Manager: search for "Ethernet by Arduino"

**Error: Board not found or wrong architecture**
- Solution: Install "Arduino Mbed OS Opta Boards" from Boards Manager
- Select **Tools → Board → Arduino Mbed OS Opta Boards → Arduino Opta**

**Error: Sketch too big**
- Solution: The server sketch is larger than the client
- Ensure you're compiling for Arduino Opta (sufficient flash memory)
- Check that optimization settings are correct

### Upload Errors

**Error: Port not found**
- Ensure USB-C cable is connected
- Try a different USB cable (some cables are charge-only)
- Check Device Manager (Windows) or `ls /dev/tty*` (Linux/Mac) to verify device appears
- Press reset button on Opta and try upload again immediately

**Error: Upload timeout**
- Press and hold the reset button on Opta
- Click Upload in Arduino IDE
- Release reset button when upload starts
- On Opta, the bootloader should respond within a few seconds

**Error: Permission denied (Linux/Mac)**
- Add your user to the dialout group: `sudo usermod -a -G dialout $USER`
- Log out and log back in
- Or use sudo: `sudo arduino-cli upload ...`

### Runtime Errors

**Serial Monitor shows: "LittleFS init failed; halting"**
- Solution: The internal flash may need to be formatted
- Try re-uploading the sketch
- If persistent, the flash may be corrupted - contact support

**Serial Monitor shows: "Failed to initialize Notecard"**
- Check that Blues Wireless for Opta module is properly seated
- Verify I2C connections
- Check that Notecard has valid SIM and is activated
- Verify SERVER_PRODUCT_UID matches your Blues Notehub project

**Serial Monitor shows: "Ethernet failed"**
- Verify Ethernet cable is properly connected
- Check that the cable is connected to a working network port
- Try a different Ethernet cable
- Verify the network has DHCP enabled
- Check for link lights on the Ethernet port

**Cannot access web dashboard**
- Verify the IP address from serial monitor
- Ensure your computer is on the same network
- Check firewall settings (allow port 80)
- Try pinging the IP address
- Verify the Opta obtained an IP via DHCP

**Web dashboard loads but shows no data**
- Clients may not have checked in yet (wait for sample interval)
- Verify clients are configured with correct server fleet name
- Check Blues Notehub for note traffic
- Verify fleet configuration in Notehub

### Network Issues

**DHCP not working**
- Check that your network has a DHCP server (usually your router)
- Try power cycling the Opta
- Check if the MAC address is blocked by network security

**Cannot access from specific computer**
- Check if computer is on the same subnet
- Verify no firewall is blocking port 80
- Try accessing from a different device
- Check for VPN or network isolation

## Blues Notehub Setup

The server requires Blues Notehub configuration to receive data from clients:

1. **Create Notehub Account**: [https://notehub.io](https://notehub.io)
2. **Create a Product**: Set up a product for your tank alarm system
3. **Note the Product UID**: Update SERVER_PRODUCT_UID in sketch
4. **Create Fleets**:
   - Create a fleet named `tankalarm-server` (or your preferred name)
   - Create a fleet named `tankalarm-clients`
   - Assign server Notecard to server fleet
   - Assign client Notecards to client fleet
5. **Provision Notecard**: Claim your Notecards into the product

For detailed Blues Notehub setup, see:
- [FLEET_SETUP.md](FLEET_SETUP.md)
- [Blues Notehub Documentation](https://dev.blues.io/notehub/notehub-walkthrough/)

## Client Configuration via Web Interface

Once the server is running, you can configure clients remotely:

1. Navigate to `http://<server-ip>/`
2. Scroll to "Update Client Config" section
3. Select client from dropdown (populated automatically from received telemetry)
4. Update configuration:
   - Site name and device label
   - Server fleet name
   - Sample interval
   - Level change threshold
   - Tank configurations (ID, name, high/low alarms)
5. Click **Send Config to Client**
6. Configuration is pushed via Blues Notehub to the target client
7. Client will apply and save configuration on next sync

### Configuration Fields:

- **Site Name**: Descriptive location (e.g., "North Tank Farm")
- **Device Label**: Short identifier (e.g., "Tank-01")
- **Server Fleet**: Fleet name for routing data (e.g., "tankalarm-server")
- **Sample Interval**: Seconds between readings (e.g., 1800 for 30 minutes)
- **Level Change Threshold**: Inches of change to trigger telemetry (0 to disable)
- **Tanks**: Up to 8 tanks per client with individual settings

## Advanced Configuration

### Static IP Address

To use a static IP instead of DHCP, modify the code:

```cpp
// Add before Ethernet.begin() in setup():
IPAddress ip(192, 168, 1, 100);
IPAddress dns(192, 168, 1, 1);
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 255, 0);
Ethernet.begin(mac, ip, dns, gateway, subnet);
```

### Changing Web Server Port

Default is port 80. To change:

```cpp
// Find this line and modify:
EthernetServer server(80); // Change 80 to desired port
```

### Customizing Daily Report Time

Via web interface, or edit default config:

```cpp
gServerConfig.dailyReportHour = 7;   // 7 AM
gServerConfig.dailyReportMinute = 0; // On the hour
```

## Security Considerations

The web interface is designed for **intranet use only**:

- No authentication by default
- Should only be accessible on trusted local networks
- Do not expose port 80 to the internet
- Consider firewall rules to restrict access
- For internet access, implement authentication and HTTPS

## Additional Resources

### Hardware Documentation
- [Arduino Opta Overview](https://docs.arduino.cc/hardware/opta)
- [Arduino Opta Networking](https://docs.arduino.cc/tutorials/opta/getting-started#network)
- [Blues Wireless for Opta](https://dev.blues.io/hardware/opta-notecarrier/)

### Software Documentation
- [ArduinoJson Documentation](https://arduinojson.org/)
- [Blues Notecard Arduino Library](https://dev.blues.io/tools-and-sdks/firmware-libraries/arduino-library/)
- [Arduino Ethernet Library](https://www.arduino.cc/reference/en/libraries/ethernet/)
- [Mbed OS LittleFS](https://os.mbed.com/docs/mbed-os/latest/apis/littlefs.html)

### Project Documentation
- [Fleet Setup Guide](FLEET_SETUP.md)
- [Website Preview](WEBSITE_PREVIEW.md)

## Getting Help

If you encounter issues:

1. Check the serial monitor output at 115200 baud
2. Review the troubleshooting section above
3. Verify network connectivity (ping test)
4. Check Blues Notehub for note traffic
5. Consult the Blues Wireless documentation at [dev.blues.io](https://dev.blues.io)
6. Check Arduino Opta documentation at [docs.arduino.cc/hardware/opta](https://docs.arduino.cc/hardware/opta)
7. Review GitHub issues in this repository

## Next Steps

After successful installation:

1. Verify the server appears in Blues Notehub
2. Configure server settings via web interface
3. Set up client devices (see Client Installation Guide)
4. Configure clients via server web interface
5. Test client connectivity and data flow
6. Configure SMS and email settings
7. Set up daily reporting schedule
8. Monitor dashboard for tank levels and alarms

See [FLEET_SETUP.md](FLEET_SETUP.md) for complete fleet configuration and [../TankAlarm-112025-Client-BluesOpta/INSTALLATION.md](../TankAlarm-112025-Client-BluesOpta/INSTALLATION.md) for client setup.
