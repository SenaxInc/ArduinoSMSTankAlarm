# TankAlarm-112025-Client-BluesOpta Installation Guide

This guide walks you through setting up the Arduino IDE and installing the TankAlarm 112025 Client sketch on an Arduino Opta with Blues Wireless Notecard.

## Hardware Requirements

- **Arduino Opta Lite** - [Purchase Link](https://store-usa.arduino.cc/products/opta-lite)
- **Blues Wireless for Opta** - [Purchase Link](https://shop.blues.com/collections/accessories/products/wireless-for-opta)
- **Arduino Pro Opta Ext A0602** (for analog sensor inputs) - [Purchase Link](https://store-usa.arduino.cc/products/opta-ext-a0602)
- **USB-C cable** for programming
- **Sensors** - Compatible analog sensors for tank level monitoring

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

The TankAlarm 112025 Client requires several libraries. Install them using the Library Manager:

### Using Library Manager (Recommended)

1. Go to **Tools → Manage Libraries** (or **Sketch → Include Library → Manage Libraries**)
2. Install each of the following libraries by searching for them and clicking **Install**:

#### Required Libraries:

| Library Name | Version | Purpose |
|--------------|---------|---------|
| **ArduinoJson** | 7.x or later | JSON parsing and serialization for configuration |
| **Blues Wireless Notecard** | Latest | Communication with Blues Notecard cellular module |
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

**3. LittleFS**
- This is built into the Arduino Mbed OS core
- No separate installation required
- Included when you install Arduino Opta board support

**4. Wire (I2C)**
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
   - Notecard
3. Go to **File → Examples**
4. You should see example sketches for ArduinoJson and Notecard

## Step 4: Open the Client Sketch

1. Download or clone this repository
2. Navigate to `TankAlarm-112025-Client-BluesOpta` folder
3. Open `TankAlarm-112025-Client-BluesOpta.ino` in Arduino IDE
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

## Step 6: Configure the Sketch (Optional)

The client uses LittleFS (internal flash storage) for configuration instead of SD cards. Default configuration will be created on first boot.

**Initial Configuration (Advanced Users):**
- Configuration is stored in `/client_config.json` on the device's internal flash
- On first boot, default configuration is created
- You can update configuration remotely via the server web interface (recommended)
- Manual configuration via serial console is also supported

**Product UID Configuration:**
- Ensure `PRODUCT_UID` is set correctly for your Blues Notehub project
- Find this in your Blues Notehub project settings
- Update at the top of the sketch if needed:
  ```cpp
  #define PRODUCT_UID "com.your-company.your-product:your-project"
  ```

## Step 7: Compile the Sketch

Before uploading, verify the sketch compiles:

1. Click the **Verify** button (✓) in the toolbar
2. Wait for compilation to complete
3. Check the output console for any errors
4. If successful, you'll see "Done compiling" and memory usage statistics

**Expected Memory Usage:**
- Flash: Typically 60-80% of available space
- RAM: Typically 40-60% of available space

If you see compilation errors, refer to the **Troubleshooting** section below.

## Step 8: Upload to Arduino Opta

1. Ensure Arduino Opta is connected via USB
2. Click the **Upload** button (→) in the toolbar
3. Wait for upload to complete (progress shown in console)
4. On success, you'll see "Upload complete"

**Note:** First upload may take longer as the bootloader initializes.

## Step 9: Verify Operation

1. Open **Tools → Serial Monitor** (or press Ctrl+Shift+M)
2. Set baud rate to **115200**
3. Press the **Reset** button on the Opta
4. You should see startup messages including:
   - "TankAlarm 112025 Client - Blues Opta"
   - "LittleFS initialized"
   - "Notecard UID: dev:xxxxx..."
   - Hardware requirements and pin assignments
   - Initial sensor readings

### Expected Serial Output:
```
TankAlarm 112025 Client - Blues Opta
Initializing...
LittleFS initialized
Notecard UID: dev:xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
Hardware Requirements:
  Analog inputs needed: X
  Required expansion: Arduino Opta Ext A0602
Configuration loaded from flash
Starting main loop...
```

## Step 10: Configuration Setup

### Option A: Configure via Server Web Interface (Recommended)

If you have the TankAlarm 112025 Server running:

1. Access the server web interface at `http://<server-ip>/`
2. Navigate to the configuration section
3. Select your client device from the dropdown
4. Update configuration settings:
   - Site name and device label
   - Tank configurations (ID, name, high/low alarms)
   - Sample interval
   - Server fleet name
5. Click **Send Config to Client**
6. Monitor the client serial console for "Configuration updated from server"

### Option B: Default Configuration

On first boot, the client creates a default configuration:
- Sample interval: 1800 seconds (30 minutes)
- Single tank: "Tank A"
- Default alarm thresholds
- Server fleet: "tankalarm-server"

You can update this later via the server web interface.

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

**Error: Board not found or wrong architecture**
- Solution: Install "Arduino Mbed OS Opta Boards" from Boards Manager
- Select **Tools → Board → Arduino Mbed OS Opta Boards → Arduino Opta**

**Error: Sketch too big**
- Solution: The sketch size is optimized for Opta
- Ensure you're compiling for Arduino Opta, not a different board
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
- Verify PRODUCT_UID matches your Blues Notehub project

**No output on Serial Monitor**
- Verify baud rate is set to **115200**
- Press reset button on Opta
- Try a different USB port
- Check that correct COM port is selected

**Configuration not loading**
- On first boot, default config is created automatically
- Check serial console for config creation messages
- Use server web interface to push new configuration

## Blues Notehub Setup

The client requires a Blues Notehub account and proper fleet configuration:

1. **Create Notehub Account**: [https://notehub.io](https://notehub.io)
2. **Create a Product**: Set up a product for your tank alarm system
3. **Note the Product UID**: Update in sketch if different from default
4. **Create Fleets**:
   - Create a fleet named `tankalarm-clients` (or your preferred name)
   - Assign this client Notecard to that fleet
5. **Provision Notecard**: Claim your Notecard into the product
6. **Server Setup**: Deploy the TankAlarm 112025 Server to receive data

For detailed Blues Notehub setup, see:
- [FLEET_IMPLEMENTATION_SUMMARY.md](FLEET_IMPLEMENTATION_SUMMARY.md)
- [MIGRATION_GUIDE.md](MIGRATION_GUIDE.md)
- [Blues Notehub Documentation](https://dev.blues.io/notehub/notehub-walkthrough/)

## Advanced Configuration

### Custom Product UID

Edit near the top of the `.ino` file:
```cpp
#define PRODUCT_UID "com.your-company.your-product:your-project"
```

### Adjusting Sample Intervals

Via server web interface (recommended) or by editing the default config in code:
```cpp
// Default sample interval in seconds
gConfig.sampleSeconds = 1800; // 30 minutes
```

### Adding Additional Tanks

Configure via the server web interface, which supports up to 8 tanks per client.

## Additional Resources

### Hardware Documentation
- [Arduino Opta Overview](https://docs.arduino.cc/hardware/opta)
- [Arduino Opta Ext A0602 Analog Expansion](https://store-usa.arduino.cc/products/opta-ext-a0602)
- [Blues Wireless for Opta](https://dev.blues.io/hardware/opta-notecarrier/)

### Software Documentation
- [ArduinoJson Documentation](https://arduinojson.org/)
- [Blues Notecard Arduino Library](https://dev.blues.io/tools-and-sdks/firmware-libraries/arduino-library/)
- [Mbed OS LittleFS](https://os.mbed.com/docs/mbed-os/latest/apis/littlefs.html)

### Project Documentation
- [Device-to-Device API](DEVICE_TO_DEVICE_API.md)
- [Fleet Implementation Summary](FLEET_IMPLEMENTATION_SUMMARY.md)
- [Migration Guide](MIGRATION_GUIDE.md)

## Getting Help

If you encounter issues:

1. Check the serial monitor output at 115200 baud
2. Review the troubleshooting section above
3. Consult the Blues Wireless documentation at [dev.blues.io](https://dev.blues.io)
4. Check Arduino Opta documentation at [docs.arduino.cc/hardware/opta](https://docs.arduino.cc/hardware/opta)
5. Review GitHub issues in this repository
6. Post questions on the Arduino Forum or Blues Wireless forum

## Next Steps

After successful installation:

1. Verify the client appears in Blues Notehub
2. Set up the TankAlarm 112025 Server to receive data
3. Configure client settings via server web interface
4. Connect sensors to the Opta analog inputs
5. Monitor serial console to verify sensor readings
6. Test alarm conditions and daily reports

See [FLEET_IMPLEMENTATION_SUMMARY.md](FLEET_IMPLEMENTATION_SUMMARY.md) for complete system setup.
