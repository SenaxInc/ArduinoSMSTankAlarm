# Installation Guide - Tank Alarm 092025

This guide walks you through setting up the Arduino MKR NB 1500 Tank Alarm system step by step.

## Prerequisites

### Arduino IDE Setup
1. **Install Arduino IDE**: Download from https://www.arduino.cc/en/software
2. **Install MKR NB 1500 Board Package**:
   - Open Arduino IDE
   - Go to Tools → Board → Boards Manager
   - Search for "Arduino SAMD Boards"
   - Install the package
3. **Install Required Libraries**:
   - Go to Tools → Manage Libraries
   - Install these libraries:
     - `MKRNB` (by Arduino)
     - `ArduinoLowPower` (by Arduino)
     - `RTCZero` (by Arduino)
     - `SD` (should be included)

### Hologram.io Account Setup
1. **Create Account**: Go to https://hologram.io and sign up
2. **Activate SIM Card**: Follow Hologram.io instructions to activate your SIM
3. **Get Device Key**: 
   - Create a new device in Hologram.io dashboard
   - Note down your device key (you'll need this for configuration)

## Hardware Assembly

### Step 1: Stack the Shields
1. Start with the Arduino MKR NB 1500 as the base
2. Carefully align and attach the MKR SD PROTO shield on top
3. Align and attach the MKR RELAY shield on top of the SD shield
4. Ensure all pins are properly seated

### Step 2: Insert Cards
1. **SIM Card**: Insert the activated Hologram.io SIM into the MKR NB 1500
2. **SD Card**: Format a microSD card as FAT32 and insert into MKR SD PROTO

### Step 3: Connect Sensor
Choose the appropriate wiring based on your sensor type:

1. **Digital Float Switch Wiring** (SENSOR_TYPE = DIGITAL_FLOAT):
   - Connect the Common (C) terminal to any Ground pin
   - Connect the Normally Open (NO) terminal to Pin 7
   - No external resistor needed (internal pullup is used)

2. **Analog Voltage Sensor Wiring** (SENSOR_TYPE = ANALOG_VOLTAGE):
   - **Option A - Direct connection to MKR NB 1500:**
     - Connect sensor Signal+ to Pin A1
     - Connect sensor Signal- to Ground (GND)
     - Connect sensor VCC to +3.3V pin
   - **Option B - Using MKR RELAY shield screw terminals (RECOMMENDED):**
     - Connect sensor Signal+ to A1 screw terminal on MKR RELAY shield
     - Connect sensor Signal- to GND screw terminal on MKR RELAY shield
     - Connect sensor VCC to +3.3V screw terminal on MKR RELAY shield
     - **Alternative pins**: Can use A2, A3, or A4 screw terminals (update ANALOG_SENSOR_PIN in config.h)
   - Ensure sensor output range is 0.5-4.5V

3. **Current Loop Sensor Wiring** (SENSOR_TYPE = CURRENT_LOOP):
   - Connect NCD.io I2C module SDA to Pin 11 (SDA)
   - Connect NCD.io I2C module SCL to Pin 12 (SCL)
   - Connect NCD.io I2C module VCC to +3.3V
   - Connect NCD.io I2C module GND to Ground
   - Connect 4-20mA sensor to NCD.io module Channel 0
   - Configure I2C address (default 0x48) if needed

### Step 4: Test Connections
Upload and run the test sketch first:
```
File: TankAlarm092025-Test.ino
```

## Software Configuration

### Required Configuration Files

**Download these files to get started:**

1. **SD Card Configuration** (REQUIRED): [`tank_config_example.txt`](tank_config_example.txt)
   - [Download tank_config_example.txt](tank_config_example.txt)
   - Runtime configuration stored on SD card
   - Contains all user-configurable settings

2. **Hardware Configuration**: [`config_template.h`](config_template.h)
   - Already in repository - no download needed
   - Contains compile-time hardware constants only
   - No user changes required

3. **Calibration Data** (Optional): [`calibration_example.txt`](calibration_example.txt)
   - [Download calibration_example.txt](calibration_example.txt)
   - Tank height calibration for analog sensors
   - Created automatically by calibration system

### Step 1: Configure SD Card (REQUIRED)

1. **Download and Edit Configuration**:
   - Download [`tank_config_example.txt`](tank_config_example.txt) to your computer
   - Rename to `tank_config.txt`
   - Edit with your specific settings:
   ```
   # Update these values for your setup - ALL SETTINGS REQUIRED
   SITE_NAME=Your Tank Farm Name
   TANK_NUMBER=1
   HOLOGRAM_DEVICE_KEY=your_actual_device_key_here
   ALARM_PHONE_PRIMARY=+15551234567
   ALARM_PHONE_SECONDARY=+15559876543
   DAILY_REPORT_PHONE=+15555551234
   TANK_HEIGHT_INCHES=120
   HIGH_ALARM_INCHES=100
   LOW_ALARM_INCHES=12
   ```

2. **Copy to SD Card**:
   - Copy the edited `tank_config.txt` to the root of your FAT32-formatted SD card

3. **Insert SD Card**: Place configured SD card in device before powering on

**IMPORTANT**: The device will not start without a properly configured `tank_config.txt` file on the SD card. The device will halt with error messages if:
- SD card is not inserted
- `tank_config.txt` file is missing
- HOLOGRAM_DEVICE_KEY is not set
- ALARM_PHONE_PRIMARY is not set

### Step 2: Hardware Configuration (config_template.h)

The `config_template.h` file contains only essential hardware constants (pin assignments, sensor types, buffer sizes). No user changes are required unless customizing hardware.

**Sensor Type Selection** (only if using different sensor):
If you need to change the sensor type from the default DIGITAL_FLOAT, edit `config_template.h`:
```cpp
// Choose sensor type: DIGITAL_FLOAT, ANALOG_VOLTAGE, or CURRENT_LOOP
#define SENSOR_TYPE DIGITAL_FLOAT
```

**Note**: Most sensor-specific settings (pins, voltage ranges, current ranges) are pre-configured with sensible defaults and don't need changes unless you have custom hardware.

### Step 3: Upload Code
1. Connect MKR NB 1500 to computer via USB
2. Select correct board: Tools → Board → Arduino MKR NB 1500
3. Select correct port: Tools → Port → (your port)
4. Upload the sketch: Sketch → Upload

## Initial Testing

### Step 1: Serial Monitor Test
1. Open Serial Monitor (Tools → Serial Monitor)
2. Set baud rate to 9600
3. Reset the Arduino and watch for startup messages
4. Verify cellular connection is established

### Step 2: Sensor Test
1. Manually trigger the float switch
2. Verify alarm SMS is sent to configured numbers
3. Check that LED turns on during alarm
4. Verify relay activation (listen for clicking)

### Step 3: SD Card Test
1. Check that log file is created on SD card
2. Verify events are being logged with timestamps
3. Remove SD card and check `tanklog.txt` file on computer

## Field Installation

### Step 1: Enclosure Preparation
1. **Drill Holes**:
   - Cable entry for sensor wires
   - Mounting holes for enclosure
   - Optional: Ventilation holes with weatherproofing

2. **Install Cable Glands**:
   - Use appropriate size for your sensor cable
   - Ensure waterproof seal

### Step 2: System Installation
1. **Mount Enclosure**:
   - Choose location with good cellular signal
   - Protect from direct weather exposure
   - Allow access for maintenance

2. **Connect Sensor**:
   - Route sensor cable through cable gland
   - Connect to terminals inside enclosure
   - Test sensor operation after connection

3. **Power Connection**:
   - For battery operation: Connect LiPo battery
   - For external power: Connect 7-12V DC supply
   - For solar: Add solar panel with charge controller

### Step 3: Final Testing
1. **Signal Strength Test**:
   - Use serial monitor to check signal strength
   - Relocate if signal is poor (<-90 dBm)

2. **End-to-End Test**:
   - Trigger sensor manually
   - Verify SMS alerts are received
   - Check Hologram.io dashboard for data

3. **Daily Operation Test**:
   - Modify timing for testing
   - Verify daily reports are sent
   - Reset to normal timing after test

## Troubleshooting

### Common Issues

| Problem | Possible Cause | Solution |
|---------|---------------|----------|
| No cellular connection | SIM not activated | Check Hologram.io dashboard |
| No SMS received | Wrong phone numbers | Verify numbers include country code |
| SD card errors | Card not formatted | Format as FAT32 |
| Sensor false triggers | Poor mechanical setup | Check float switch installation |
| Battery drains quickly | Sleep mode not working | Verify ArduinoLowPower library |

### Diagnostic Steps

1. **Check Serial Output**:
   - Enable debug mode in config.h
   - Monitor startup sequence
   - Look for error messages

2. **Test Components Individually**:
   - Use TankAlarm092025-Test.ino
   - Verify each component works separately
   - Check connections if tests fail

3. **Network Diagnostics**:
   - Check cellular signal strength
   - Verify APN settings
   - Test with different location

## Maintenance

### Regular Checks
- **Monthly**: Check SD card space, verify system operation
- **Quarterly**: Test alarm system, check battery level
- **Annually**: Clean enclosure, inspect connections

### Updating Configuration
**NEW APPROACH - Field Configurable:**
1. Power off device
2. Remove SD card from device
3. Edit `tank_config.txt` on computer
4. Insert SD card back into device
5. Power on device - new configuration will be loaded and validated

**CRITICAL**: Device will not start without valid SD card configuration.

### Data Retrieval
1. Remove SD card from system
2. Insert into computer
3. Copy tanklog.txt file
4. Analyze data as needed

## Support

If you encounter issues:
1. Check this installation guide
2. Review the troubleshooting section
3. Check Arduino MKR NB 1500 documentation
4. Contact Hologram.io support for cellular issues
5. Refer to component datasheets for hardware problems

---

*Installation Guide Version: 092025*
*Last Updated: September 2025*