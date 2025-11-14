# Tank Height Calibration System

This system allows precise tank height measurement by creating calibration points that map sensor readings to actual measured heights.

## Overview

The calibration system replaces fixed sensor ranges with user-defined calibration points for improved accuracy. When calibration data is available, the system uses linear interpolation between calibration points to calculate tank height.

## Features

- **SMS Command Interface**: Send calibration commands via SMS to tank phone numbers
- **Web Interface**: Use server web page to send calibration commands to tanks
- **Multiple Calibration Points**: Support up to 10 calibration points per tank
- **Linear Interpolation**: Automatic calculation between calibration points
- **Fallback Support**: Falls back to original calculation if insufficient calibration data
- **Persistent Storage**: Calibration data saved to SD card

## SMS Commands

Send these commands to any authorized phone number (primary, secondary, or daily report numbers):

### CAL [height]
Adds a calibration point at the current sensor reading with the specified height.
- **Example**: `CAL 48.5` - Records current sensor reading as 48.5 inches
- **Requirements**: Height must be between 0 and tank height limit
- **Response**: Confirmation SMS with sensor reading and height

### STATUS
Requests current tank status including sensor reading and calculated height.
- **Example**: `STATUS`
- **Response**: SMS with current level and raw sensor reading

### CALSHOW
Shows current calibration points (limited to first 3 points due to SMS length).
- **Example**: `CALSHOW`
- **Response**: SMS with calibration point summary

## Web Interface

Access the calibration page at `http://[server-ip]/calibration` to:

1. View all tanks that have reported data
2. See latest tank levels and timestamps
3. Send calibration commands to specific tanks
4. Request status updates from tanks

### Using the Web Interface

1. **Measure actual tank height** using a measuring stick or tape measure
2. **Enter measured height** in the input field (in inches)
3. **Click "Send Calibration"** to transmit command to tank
4. **Repeat at different levels** for improved accuracy (minimum 2 points required)
5. **Use "Request Status"** to verify current readings

## File Structure

### Per-Tank `.cfg` File
Stored on the tank's SD card, the same file used for all other tank settings. Calibration points are added using the `CAL_POINT` key.
```
# In a file like NorthFarm_101.cfg
SITE_NAME=North Farm
TANK_NUMBER=101
# ... other settings ...
CAL_POINT=0.50,0.0
CAL_POINT=2.25,24.5
CAL_POINT=4.00,48.0
```

## Implementation Details

### Client (Tank) Side
- **Calibration data loading**: Automatic on startup from `CAL_POINT` entries in the tank's `.cfg` file.
- **SMS processing**: `processIncomingSMS()` function handles incoming commands. When a `CAL` command is received, a new `CAL_POINT` line is appended to the active `.cfg` file.
- **Data storage**: Calibration points are stored directly in the `.cfg` file on the SD card.
- **Height calculation**: `interpolateHeight()` performs linear interpolation.
- **Sensor reading**: `getCurrentSensorReading()` gets raw sensor values.

### Server Side
- **Web interface**: `/calibration` route displays calibration page
- **Command forwarding**: `sendCalibrationCommand()` forwards commands to tanks
- **Navigation**: Calibration link added to all server pages

### Authorization
Only SMS from authorized phone numbers are processed:
- Primary alarm phone
- Secondary alarm phone  
- Daily report phone

### Error Handling
- Invalid heights are rejected
- Insufficient calibration points fall back to original calculations
- SD card errors are logged but don't crash the system
- Malformed SMS commands are ignored

## Best Practices

1. **Take measurements at different levels** for better interpolation accuracy
2. **Use at least 2 calibration points** (more points = better accuracy)
3. **Measure at tank extremes** (empty and full positions when possible)
4. **Verify calibration** using STATUS command after adding points
5. **Record calibration dates** for maintenance tracking

## Troubleshooting

### No calibration response
- Check phone number authorization
- Verify cellular connectivity
- Check SD card space and functionality

### Inaccurate readings
- Add more calibration points
- Verify sensor stability
- Check for sensor drift over time

### Web interface not working
- Verify server connectivity
- Check that tanks are reporting data
- Confirm Hologram device-to-device messaging setup