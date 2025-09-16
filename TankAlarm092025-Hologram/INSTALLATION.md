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
1. **Float Switch Wiring**:
   - Connect the Common (C) terminal to any Ground pin
   - Connect the Normally Open (NO) terminal to Pin 7
   - No external resistor needed (internal pullup is used)

### Step 4: Test Connections
Upload and run the test sketch first:
```
File: TankAlarm092025-Test.ino
```

## Software Configuration

### Step 1: Configure the Code
1. **Copy Configuration Template**:
   - Copy `config_template.h` to `config.h`
   - Edit `config.h` with your specific settings

2. **Update Configuration Values**:
   ```cpp
   // Replace with your actual Hologram.io device key
   #define HOLOGRAM_DEVICE_KEY "your_device_key_here"
   
   // Update with your phone numbers (include country code)
   #define ALARM_PHONE_PRIMARY "+12223334444"
   #define ALARM_PHONE_SECONDARY "+15556667777"
   #define DAILY_REPORT_PHONE "+18889990000"
   ```

3. **Adjust Timing (Optional)**:
   ```cpp
   // How often to check tank level (default: 1 hour)
   #define SLEEP_INTERVAL_HOURS 1
   
   // How often to send daily report (default: 24 hours)
   #define DAILY_REPORT_HOURS 24
   ```

### Step 2: Update Main Code
1. In `TankAlarm092025-Hologram.ino`, change this line:
   ```cpp
   // Change from:
   #include "config_template.h"
   // To:
   #include "config.h"
   ```

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
1. Connect to computer via USB
2. Modify config.h as needed
3. Re-upload sketch
4. Test new configuration

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