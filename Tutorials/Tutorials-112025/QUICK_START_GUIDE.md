# TankAlarm Quick Start Guide

**Get Your First Tank Monitoring System Running in 30 Minutes**

---

## Introduction

This quick start guide gets you from zero to a working tank monitoring system as fast as possible. We'll set up one server and one client device using default settings. Once you verify everything works, you can expand to a full fleet using the [Fleet Setup Guide](FLEET_SETUP_GUIDE.md).

### What You'll Build

- **1 Server Device**: Central dashboard and data aggregation
- **1 Client Device**: Remote tank monitoring with analog sensors
- **Cloud Communication**: Via Blues Notecard and Notehub
- **Web Dashboard**: Real-time monitoring interface

### Time Required

- **Setup**: 20-30 minutes
- **First Data**: 30 minutes after installation
- **Verification**: 1-2 hours total

### Prerequisites

**Hardware:**
- [ ] 1x Arduino Opta (client)
- [ ] 1x Arduino Opta (server)  
- [ ] 2x Blues Notecard (cellular or WiFi)
- [ ] 2x Blues Notecarrier-F
- [ ] 1x Arduino Opta Analog Expansion
- [ ] 1x 4-20mA tank level sensor
- [ ] Ethernet cable (for server)
- [ ] USB-C cables for programming
- [ ] Power supplies (USB-C or 12-24V)

**Software:**
- [ ] Arduino IDE 2.0 or later
- [ ] Blues Notehub account (free)

**Network:**
- [ ] Ethernet network with DHCP
- [ ] Cellular coverage (if using cellular Notecard)

---

## Quick Start Checklist

### Phase 1: Notehub Setup (5 minutes)

- [ ] Create Notehub account
- [ ] Create product
- [ ] Save product UID
- [ ] Create two fleets: `tankalarm-server` and `tankalarm-clients`

### Phase 2: Server Setup (10 minutes)

- [ ] Install Arduino IDE and libraries
- [ ] Configure server firmware
- [ ] Upload to server Opta
- [ ] Connect to network
- [ ] Verify dashboard access

### Phase 3: Client Setup (10 minutes)

- [ ] Configure client firmware
- [ ] Upload to client Opta
- [ ] Connect sensor
- [ ] Verify Notecard connection

### Phase 4: Verification (5 minutes)

- [ ] Client appears in server dashboard
- [ ] Configure client settings
- [ ] Verify telemetry updates
- [ ] Test alarm

---

## Step-by-Step Instructions

### Step 1: Create Notehub Account and Product

#### 1.1 Sign Up for Blues Notehub

1. Go to [notehub.io](https://notehub.io)
2. Click **Sign Up** (or log in if you have account)
3. Complete registration (email verification required)

#### 1.2 Create Product

1. Click **Create Product**
2. Fill in:
   - **Name**: `TankAlarm QuickStart`
   - **Description**: `Testing tank monitoring system`
3. Click **Create**
4. **Copy Product UID** - you'll need this! Example: `com.company.tankalarm:quickstart`

#### 1.3 Create Fleets

**Server Fleet:**
1. Click **Fleets** tab → **Create Fleet**
2. Name: `tankalarm-server`
3. Description: `Server devices`
4. Click **Create**

**Client Fleet:**
1. Click **Create Fleet** again
2. Name: `tankalarm-clients`
3. Description: `Client devices`
4. Click **Create**

**✓ Checkpoint**: You should see two fleets listed in the Fleets tab.

---

### Step 2: Setup Server Device

#### 2.1 Install Arduino IDE

1. Download Arduino IDE 2.0+ from [arduino.cc](https://www.arduino.cc/en/software)
2. Install and launch
3. Go to **Tools** → **Board** → **Boards Manager**
4. Search: `Arduino Mbed OS Opta Boards`
5. Click **Install** (this takes 5-10 minutes)

#### 2.2 Install Required Libraries

1. Go to **Tools** → **Manage Libraries**
2. Install each library (search and click Install):
   - **ArduinoJson** (version 7.x)
   - **Blues Wireless Notecard**
   - **Arduino_JSON**
   - **Ethernet** (by Various)

#### 2.2.1 Install TankAlarm-112025-Common (Custom Library)

TankAlarm uses a shared custom library in this repository: **TankAlarm-112025-Common**.

**Option A (Recommended): Install from ZIP**

1. Download `TankAlarm-112025-Common.zip` from the repository root
2. In Arduino IDE, go to **Sketch → Include Library → Add .ZIP Library...**
3. Select `TankAlarm-112025-Common.zip`

**Option B (Developer Setup): Install from Folder**

Copy the entire `TankAlarm-112025-Common/` folder to your Arduino libraries folder:

- Windows: `%USERPROFILE%\Documents\Arduino\libraries\`
- macOS: `~/Documents/Arduino/libraries/`
- Linux: `~/Arduino/libraries/`

#### 2.3 Download Server Firmware

1. Navigate to your TankAlarm project folder
2. Open: `TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino`
3. Arduino IDE opens the sketch

#### 2.4 Configure Server Firmware

Find and edit these lines in `server_config.h`:

```cpp
// === REQUIRED: Product UID ===
const char* SERVER_PRODUCT_UID = "com.company.tankalarm:quickstart";  // ← YOUR PRODUCT UID

// === OPTIONAL: Network Settings ===
// For quick start, leave DHCP enabled (default)
#define USE_STATIC_IP false
```

**That's it!** Everything else uses safe defaults.

> 💡 **Tip**: You can also configure or change the Product UID later through the web dashboard at **Server Settings** → **Blues Notehub** → **Product UID**. This is useful if you need to move the device to a different Notehub project without reflashing.

> ⚠️ **Important**: The Product UID must be **identical** on both the server and all client devices. When using the Config Generator to create client configurations, the Product UID is automatically filled in from the server settings to ensure they match.

#### 2.5 Upload Server Firmware

1. Connect server Opta via USB-C
2. **Tools** → **Board**: `Arduino Opta`
3. **Tools** → **Port**: Select your Opta (e.g., COM3)
4. Click **Upload** (arrow button)
5. Wait for "Upload complete" (takes ~60 seconds)

#### 2.6 Connect Server to Network

**Hardware Connections:**
```
Power: USB-C or 12-24V to Opta power input
Network: Ethernet cable to Opta RJ45 port
Notecard: Blues Notecarrier-F connected to Opta expansion port
```

**Verify Connectivity:**
1. Open **Tools** → **Serial Monitor**
2. Set baud rate: **115200**
3. Wait for startup messages
4. Look for:
   ```
   ✓ Ethernet: 192.168.1.XXX
   ✓ Notecard: Connected
   ✓ Server Ready
   ```

#### 2.7 Access Dashboard

1. Note the IP address from serial monitor (e.g., `192.168.1.150`)
2. Open web browser
3. Go to: `http://192.168.1.150/`
4. You should see the TankAlarm dashboard!

**✓ Checkpoint**: Dashboard loads and shows "No clients configured yet"

---

### Step 3: Setup Client Device

#### 3.1 Download Client Firmware

1. Open: `TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino`
2. Arduino IDE opens the sketch

#### 3.2 Configure Client Firmware

Edit `config_template.h` (or create `config.h` from template):

```cpp
// === REQUIRED: Product UID ===
#define PRODUCT_UID "com.company.tankalarm:quickstart"  // ← SAME as server

// === REQUIRED: Server Fleet ===
#define SERVER_FLEET "tankalarm-server"  // ← Must match Notehub fleet name
```

> ⚠️ **Critical**: The `PRODUCT_UID` must be **exactly the same** as the server's Product UID. If they don't match, the client and server will not be able to communicate. You can verify the server's Product UID in the web dashboard under **Server Settings** → **Blues Notehub**.

**Note**: Tank configurations will be done via the server dashboard later.

#### 3.3 Upload Client Firmware

1. Connect client Opta via USB-C
2. **Tools** → **Board**: `Arduino Opta`
3. **Tools** → **Port**: Select your Opta
4. Click **Upload**
5. Wait for "Upload complete"

#### 3.4 Connect Hardware

**Analog Expansion:**
1. Attach Arduino Opta Analog Expansion to client Opta
2. Connect 4-20mA sensor:
   ```
   Sensor 4-20mA output → Analog Expansion Channel 0
   Sensor power: 12-24V (from expansion or external)
   Sensor ground: Common ground
   ```

**Notecard:**
1. Insert Blues Notecard into Notecarrier-F
2. Connect Notecarrier to Opta expansion port
3. For cellular: ensure antenna connected
4. For WiFi: antenna already integrated

**Power:**
1. Connect USB-C or 12-24V power supply
2. Client Opta powers on (LEDs illuminate)

#### 3.5 Verify Notecard Connection

1. Open **Tools** → **Serial Monitor** (baud: 115200)
2. Wait for startup messages (~2 minutes for cellular registration)
3. Look for:
   ```
   ✓ Notecard: Ready
   ✓ Hub: Connected
   ✓ Sampling started
   ```

**✓ Checkpoint**: Serial monitor shows successful Notecard connection.

---

### Step 4: Configure Client via Server

#### 4.1 Wait for First Telemetry

**Default behavior:**
- Client samples every 30 minutes
- Sends telemetry to server fleet
- Server receives and displays in dashboard

**Timeline:**
- **T+0**: Client powers on
- **T+2 min**: Notecard connects to cellular
- **T+30 min**: First sample and telemetry sent
- **T+31 min**: Server receives data, client appears in dropdown

**To speed up initial test:**
1. Temporarily edit client `config.h`:
   ```cpp
   #define DEFAULT_SAMPLE_SECONDS 300  // 5 minutes instead of 30
   ```
2. Re-upload firmware
3. First telemetry arrives in ~5 minutes

#### 4.2 Select Client in Dashboard

1. Access server dashboard: `http://192.168.1.150/`
2. Top dropdown: Select your client device
   - Shows Notecard UID (e.g., `dev:123456789`)
   - If not visible yet, wait for first telemetry
3. Dashboard shows placeholder values (not yet configured)

#### 4.3 Configure Client Settings

**Step 1: Check for Unconfigured Clients**

Look for the **"New Sites (Unconfigured)"** section at the top of the dashboard. New clients appear here automatically with:
- ⚠ Warning indicator
- Client UID
- Last seen timestamp
- Firmware version
- **"Configure →"** button

💡 **Tip:** Click the **📘 Quick Start Guide** link for detailed deployment instructions.

**Step 2: Basic Site Information**

1. Click **"Configure →"** on your unconfigured client
2. The page scrolls to the configuration form
3. Fill in the required fields (marked with *):
   ```
   Site Name*: QuickStart Test Site
   Device Label*: Tank-01
   Route (Fleet): tankalarm-server
   Product UID: [Auto-filled from server]
   ```

⚠️ **Note:** Product UID is fleet-wide and configured in `TankAlarm_Config.h`. It's readonly here.

**Step 3: Sample & Report Settings**

```
Sample Interval: 1800 seconds (30 min)
Daily Report Time: 05:00 (5 AM)
Daily Email: your-email@example.com
```

**Step 4: Power Source**

Select your power configuration:
- **Grid Power** - AC mains (default)
- **Grid + Battery Backup** - AC with UPS
- **Solar** - Solar panel + battery
- **Solar + MPPT** - Solar with charge controller

**Step 5: Add Your First Sensor**

1. Click the blue **"+ Add Sensor"** button
2. Configure sensor details:

   **Monitor Type:**
   - Tank Level (liquid monitoring)
   - Gas Pressure (propane/natural gas)
   - RPM Sensor (engine speed)

   **For Tank Level:**
   ```
   Tank Number: 1
   Name: Diesel Tank
   Contents: Diesel
   Sensor Type: 4-20mA Current Loop
   Pin/Channel: Expansion CH0
   Height: 96 inches (8 feet)
   Level Change Threshold: 6 inches
   ```

3. Click **"+ Add Alarm"** to configure alarms:
   ```
   ✓ High Alarm: 90 inches (triggers at 94%)
   ✓ Low Alarm: 12 inches (triggers at 13%)
   ```

💡 **Optional Features:**
- **+ Add Relay Control**: Trigger relay outputs on other clients
- **+ Add SMS Alert**: Send text messages on alarm conditions

**Step 6: Validate & Send**

1. Review your configuration
2. Click **"Download JSON"** (blue button) to save a backup copy
3. Click **"Send Configuration"** (primary button)
4. Watch the status indicator:
   - 🟡 Validating configuration...
   - 🟡 Sending to server...
   - ✅ Configuration queued for delivery

5. Server sends configuration via Notehub
6. Client receives within ~5 minutes (on next Notecard sync)

#### 4.4 Verify Configuration Applied

**Watch client serial monitor:**
```
Configuration received from server
Applied: Site=QuickStart Test Site
Applied: Tank 1 enabled, CH0, 4-20mA
Next sample: 1800 seconds
```

**✓ Checkpoint**: Client serial monitor confirms configuration update.

---

### Step 5: Verify Data Flow

#### 5.1 Monitor Dashboard Updates

1. Keep server dashboard open: `http://192.168.1.150/`
2. Wait for next telemetry (up to 30 minutes, or 5 min if you shortened interval)
3. Dashboard updates with:
   - **Tank level** (inches and %)
   - **Last update timestamp**
   - **Sensor raw value** (4-20mA)
   - **Connectivity status** (green indicator)

#### 5.2 Check Notehub Event Stream

1. Go to [notehub.io](https://notehub.io)
2. Open your product
3. Click **Events** tab
4. You should see:
   - **From client**: `fleet.tankalarm-server:telemetry.qi`
   - **From server**: `device:<uid>:config.qi`

**Example Telemetry Event:**
```json
{
  "site": "QuickStart Test Site",
  "deviceLabel": "Tank-01",
  "tanks": [
    {
      "id": "TANK-A",
      "l": 45.2,    // level (inches)
      "p": 67,      // percent
      "r": 12.5     // raw (mA)
    }
  ],
  "voltage": 24.1,
  "signal": 85
}
```

**✓ Checkpoint**: Notehub shows telemetry events flowing from client to server.

#### 5.3 Test Alarm System

**Simulate High Level Alarm:**

**Option A: Physical Test (if possible)**
1. Raise tank level above high alarm threshold
2. Client detects change
3. Sends alarm telemetry immediately
4. Server processes and sends SMS/email

**Option B: Modify Threshold Test**
1. Lower high alarm threshold via dashboard
2. Send new configuration
3. On next sample, level exceeds threshold
4. Alarm triggers

**Expected Behavior:**
- Dashboard shows **RED indicator** for alarmed tank
- SMS sent to configured phone numbers
- Email sent to configured addresses
- Alarm event logged in Notehub

**✓ Checkpoint**: Alarm system functions correctly.

---

## Troubleshooting

### Server Issues

#### Dashboard Not Loading

**Check:**
1. Ethernet cable connected?
2. Router/switch powered on?
3. Serial monitor shows IP address?
4. Browser on same network as server?

**Solutions:**
- Ping server IP: `ping 192.168.1.150`
- Try different browser
- Check firewall settings
- Verify DHCP lease

#### "Notecard Not Connected"

**Check:**
1. Notecard fully inserted in Notecarrier?
2. Antenna connected (for cellular)?
3. Serial monitor shows errors?
4. Product UID correctly configured?

**Solutions:**
- Reseat Notecard
- Check Product UID is correct (must match Notehub project exactly)
- Go to **Server Settings** → **Blues Notehub** to verify/update Product UID
- Try `hub.sync` command in serial monitor
- Verify cellular coverage (for cellular Notecard)
- Power cycle the device after changing Product UID

### Client Issues

#### Not Appearing in Server Dashboard

**Check:**
1. Client powered on?
2. Serial monitor shows "Telemetry sent"?
3. Notecard connected (green LED)?
4. Waited 30+ minutes?

**Solutions:**
- Verify Product UID matches server
- Check SERVER_FLEET = "tankalarm-server"
- Force sample: press client reset button
- Check Notehub Events for telemetry

#### Sensor Reading Zero or Max

**Check:**
1. Sensor powered?
2. 4-20mA wiring correct?
3. Analog expansion powered?
4. Correct analog channel configured?

**Solutions:**
- Check sensor power voltage (12-24V)
- Verify sensor is current loop type
- Swap to different analog channel and update config
- Test sensor with multimeter

### Communication Issues

#### Configuration Not Reaching Client

**Check:**
1. Server sent configuration (check serial monitor)?
2. Notecard connected on client?
3. Correct device UID selected in dropdown?

**Solutions:**
- Server serial monitor: should show "Config sent to dev:123456"
- Client serial monitor: should show "Config received"
- Wait 5-10 minutes for Notecard sync
- Force sync via serial monitor: `{"req":"hub.sync"}`

#### Telemetry Delayed

**Normal Behavior:**
- Cellular sync: 5-15 minutes typical
- WiFi sync: 1-5 minutes typical
- Continuous mode: ~60 seconds

**If excessive delays:**
1. Check cellular signal strength
2. Set Notecard to continuous mode:
   ```
   {"req":"hub.set", "mode":"continuous"}
   ```
3. Reduce sample interval for testing

---

## Next Steps

### Congratulations! 🎉

You now have a working tank monitoring system. Here's what to do next:

### 1. Fine-Tune Configuration

**Sensor Calibration:**
- Measure actual tank levels at known points
- Update calibration values via dashboard
- See [Client Installation Guide](CLIENT_INSTALLATION_GUIDE.md#sensor-calibration)

**Alarm Thresholds:**
- Adjust based on operational needs
- Set appropriate high/low levels
- Configure SMS contacts

**Server Settings:**
- Navigate to **Server Settings** from the dashboard
- Configure Blues Notehub Product UID if needed
- Set up SMS contacts for alarm notifications
- Configure daily email reports
- Set up FTP backup if desired

**Sample Intervals:**
- Faster for critical tanks (e.g., 15 min)
- Slower for stable tanks (e.g., 2 hours)
- Balance responsiveness vs battery/data

### 2. Add More Tanks

**To the Same Client:**
1. Connect additional sensors to Analog Expansion (Channels 1-7)
2. Configure via server dashboard
3. Enable tanks 2-8 as needed

**To the Same Site:**
1. Deploy additional client devices
2. Each monitors up to 8 tanks
3. All report to same server

### 3. Expand to Fleet

**Deploy Multiple Sites:**
1. Follow [Fleet Setup Guide](FLEET_SETUP_GUIDE.md)
2. Provision additional Notecards
3. Assign to `tankalarm-clients` fleet
4. Deploy and configure via server

**Best Practices:**
- Start with 3-5 pilot sites
- Validate before full rollout
- Document site-specific configurations
- Plan for cellular coverage

### 4. Advanced Features

**Relay Control:**
- Add relay expansion modules
- Control pumps based on tank levels
- See RELAY_CONTROL_GUIDE.md

**Unload Tracking:**
- Monitor tank fill/empty events
- Track delivery volumes
- See UNLOAD_TRACKING_GUIDE.md

**Device-to-Device:**
- Direct client-to-client messaging
- Distributed logic
- See DEVICE_TO_DEVICE_API.md

### 5. Monitoring and Maintenance

**Regular Tasks:**
- Weekly: Check dashboard for anomalies
- Monthly: Review alarm history
- Quarterly: Verify sensor calibration
- Annually: Update firmware

**Firmware Updates:**
- Blues Notecard DFU (over-the-air)
- Fleet-wide deployment
- See [Firmware Update Guide](FIRMWARE_UPDATE_GUIDE.md)

**Backup Configuration:**
- Export client configurations
- Document site details
- Store in version control

---

## Complete Reference

### Hardware Connections Summary

**Server Device:**
```
Arduino Opta:
  - USB-C: Power or programming
  - Ethernet: RJ45 to network
  - Expansion: Blues Notecarrier-F

Blues Notecarrier-F:
  - Notecard: Cellular or WiFi module
  - Antenna: Connected (cellular only)
  - Power: From Opta
```

**Client Device:**
```
Arduino Opta:
  - USB-C: Power or programming
  - Expansion 1: Analog Expansion
  - Expansion 2: Blues Notecarrier-F

Analog Expansion:
  - CH0-7: 4-20mA sensors
  - Power: 12-24V external

Blues Notecarrier-F:
  - Notecard: Cellular or WiFi module
  - Antenna: Connected (cellular only)
  - Power: From Opta
```

### Default Configuration Values

**Client:**
```cpp
Sample Interval: 1800 seconds (30 min)
Server Fleet: "tankalarm-server"
Level Change Threshold: 0 (report all samples)
Daily Report: Disabled by default
Tanks: All disabled (configure via server)
```

**Server:**
```cpp
Network: DHCP (auto IP)
Web Port: 80
Dashboard Refresh: 6 hours
SMS Contacts: None (configure via dashboard)
Email: None (configure via dashboard)
```

### Firmware File Locations

```
Project Root:
  ├── TankAlarm-112025-Client-BluesOpta/
  │   ├── TankAlarm-112025-Client-BluesOpta.ino (main sketch)
  │   ├── config_template.h (settings template)
  │   └── ... (other files)
  │
  ├── TankAlarm-112025-Server-BluesOpta/
  │   ├── TankAlarm-112025-Server-BluesOpta.ino (main sketch)
  │   ├── server_config.h (settings)
  │   └── ... (other files)
  │
  └── Tutorials/ (documentation)
      ├── QUICK_START_GUIDE.md (this file)
      ├── CLIENT_INSTALLATION_GUIDE.md
      ├── SERVER_INSTALLATION_GUIDE.md
      ├── FLEET_SETUP_GUIDE.md
      └── FIRMWARE_UPDATE_GUIDE.md
```

### Useful Serial Monitor Commands

**Check Notecard Status:**
```json
{"req":"hub.status"}
```

**Force Sync:**
```json
{"req":"hub.sync"}
```

**Check Inbound Queue:**
```json
{"req":"note.get", "file":"config.qi"}
```

**Get Device UID:**
```json
{"req":"card.wireless"}
```

**Set Continuous Mode:**
```json
{"req":"hub.set", "mode":"continuous"}
```

### Common Alarm Codes

| Code | Meaning | Action |
|------|---------|--------|
| `HIGH` | Tank above high threshold | Check for overflow risk |
| `LOW` | Tank below low threshold | Schedule refill |
| `SENSOR` | Sensor malfunction | Check wiring, power |
| `OFFLINE` | Client not reporting | Check power, cellular |

---

## Additional Resources

### Documentation

- [Client Installation Guide](CLIENT_INSTALLATION_GUIDE.md) - Comprehensive client setup
- [Server Installation Guide](SERVER_INSTALLATION_GUIDE.md) - Detailed server configuration
- [Fleet Setup Guide](FLEET_SETUP_GUIDE.md) - Multi-device deployment
- [Firmware Update Guide](FIRMWARE_UPDATE_GUIDE.md) - OTA updates

### External Tutorials

**Arduino Opta:**
- [Getting Started with Opta](https://docs.arduino.cc/hardware/opta)
- [Opta Analog Expansion](https://docs.arduino.cc/hardware/opta-analog)

**Blues Wireless:**
- [Notecard Quickstart](https://dev.blues.io/quickstart/)
- [Notehub Walkthrough](https://dev.blues.io/notehub/notehub-walkthrough/)
- [Device-to-Device Messaging](https://dev.blues.io/guides-and-tutorials/notecard-guides/understanding-device-to-device-communications/)

**4-20mA Sensors:**
- [Current Loop Basics](https://www.omega.com/en-us/resources/4-20-ma-current-loop-output)
- [Troubleshooting 4-20mA](https://blog.beamex.com/4-20ma-troubleshooting)

### Community Support

- **Blues Community**: [community.blues.io](https://community.blues.io)
- **Arduino Forum**: [forum.arduino.cc/opta](https://forum.arduino.cc/c/hardware/opta/181)
- **GitHub Issues**: [Project Repository](https://github.com/SenaxInc/ArduinoSMSTankAlarm)

### Video Walkthroughs

- [TankAlarm Quick Start](coming soon)
- [Blues Notecard Setup](https://www.youtube.com/blues_wireless)
- [Arduino Opta Overview](https://www.youtube.com/arduino)

---

## FAQ

**Q: How long does the first cellular connection take?**  
A: 2-5 minutes typically, up to 10 minutes in poor coverage areas.

**Q: Can I use WiFi instead of cellular?**  
A: Yes! Use Blues Notecard WiFi instead of cellular variant. Configuration is identical.

**Q: How much cellular data does this use?**  
A: ~5-10 MB/month per client with default settings (30 min intervals).

**Q: Can the server and client be on different networks?**  
A: Absolutely! They communicate via Blues Notehub cloud, not directly.

**Q: What if I don't have an Analog Expansion?**  
A: You can use Opta's built-in I/O, but it's optimized for digital/relay. 4-20mA requires analog inputs.

**Q: How do I add more tanks?**  
A: Connect sensors to additional analog channels (CH1-7), then configure via server dashboard.

**Q: Can I monitor tanks at multiple sites?**  
A: Yes! Deploy additional client devices, all report to same server(s). See [Fleet Setup Guide](FLEET_SETUP_GUIDE.md).

**Q: What happens if the server loses power?**  
A: Client data is buffered in Notecard queue. Server retrieves on restart. No data loss (up to queue limits).

**Q: How do I backup my configuration?**  
A: Server stores configs in LittleFS. Export via FTP (if enabled) or note settings manually.

**Q: Can I customize the dashboard?**  
A: Yes! Modify `html_content.h` in server firmware. See SERVER_INSTALLATION_GUIDE.md for details.

---

## Revision History

| Version | Date | Changes |
|---------|------|---------|
| 1.0 | Jan 2026 | Initial release for firmware v1.0.0 |

---

*Quick Start Guide v1.0 | Last Updated: January 7, 2026*  
*Compatible with TankAlarm Firmware 1.0.0+*

**Ready to expand your deployment?** See the [Fleet Setup Guide](FLEET_SETUP_GUIDE.md) for multi-site installations.
