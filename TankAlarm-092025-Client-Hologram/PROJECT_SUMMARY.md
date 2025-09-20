# Tank Alarm 092025 - Project Summary

## Overview

The Tank Alarm 092025 is the latest version of the Arduino SMS Tank Alarm system, completely redesigned for the Arduino MKR NB 1500 platform. This implementation leverages modern cellular connectivity, integrated SD card logging, and relay control capabilities.

## Key Features

### Hardware Platform
- **Arduino MKR NB 1500**: Native cellular connectivity with ublox SARA-R410M modem
- **MKR SD PROTO Shield**: SD card data logging and prototyping area
- **MKR RELAY Shield**: Relay control for alarm indication or equipment control
- **Hologram.io Connectivity**: Global cellular data service with SMS capability

### Core Functionality
- **Tank Level Monitoring**: Digital float switch sensor with debouncing
- **SMS Alert System**: Immediate alerts to multiple contacts during alarm conditions
- **Daily Status Reports**: Automated daily reports via SMS
- **Data Logging**: Local SD card logging with timestamps
- **Cloud Integration**: Data transmission to Hologram.io cloud platform
- **Low Power Operation**: Optimized for battery-powered remote installation

## File Structure

```
TankAlarm092025-Hologram/
├── TankAlarm092025-Hologram.ino    # Main Arduino sketch
├── TankAlarm092025-Test.ino        # Hardware test sketch
├── config_template.h               # Configuration template
├── config_example.h                # Example configuration
├── README.md                       # Main documentation
├── INSTALLATION.md                 # Step-by-step setup guide
├── WIRING.md                       # Detailed wiring diagrams
├── BillOfMaterials092025.md        # Complete parts list
└── 092025 Notes.txt               # Original project notes
```

## Quick Start Guide

### 1. Hardware Assembly
1. Stack shields: MKR NB 1500 → MKR SD PROTO → MKR RELAY
2. Insert activated Hologram.io SIM card
3. Insert formatted FAT32 SD card
4. Connect tank level sensor (float switch) to Pin 7 and GND

### 2. Software Setup
1. Install Arduino IDE and required libraries (MKRNB, ArduinoLowPower, RTCZero)
2. Copy `config_template.h` to `config.h`
3. Update configuration with your Hologram.io device key and phone numbers
4. Upload code to MKR NB 1500

### 3. Testing
1. Run `TankAlarm092025-Test.ino` to verify all hardware
2. Test sensor triggering and SMS alerts
3. Verify SD card logging functionality
4. Deploy in field location

## Technical Specifications

### Power Requirements
- **Operating Voltage**: 3.3V (internal regulation from 5V USB or 7-12V external)
- **Current Consumption**: 
  - Active (cellular transmitting): ~200mA
  - Sleep mode: <10mA
  - Typical average: 20-30mA (with 1-hour sleep cycles)

### Environmental Specifications
- **Operating Temperature**: -40°C to +85°C (component limited)
- **Humidity**: 0-95% non-condensing (with proper enclosure)
- **Ingress Protection**: IP65+ (enclosure dependent)

### Communication Specifications
- **Cellular Bands**: LTE Cat M1/NB-IoT (region dependent)
- **SMS**: Via Hologram.io cloud SMS service
- **Data**: TCP/IP over cellular for cloud integration
- **Local Logging**: SD card up to 32GB (FAT32)

## Comparison with Previous Versions

| Feature | 052019 Versions | 092025 Version |
|---------|----------------|----------------|
| Cellular Module | SparkFun LTE Shield | Integrated MKR NB 1500 |
| Libraries | SparkFun LTE Library | Arduino MKRNB |
| SD Card Support | No | Yes (integrated) |
| Relay Control | External module | Integrated shield |
| Power Management | Basic watchdog | ArduinoLowPower library |
| Configuration | Hard-coded | External config.h file |
| Testing Support | Limited | Dedicated test sketch |
| Documentation | Basic | Comprehensive guides |

## Advantages of 092025 Version

### Technical Advantages
- **Simplified Hardware**: Fewer discrete components, integrated design
- **Better Power Management**: Native low-power modes for extended battery life
- **Enhanced Logging**: Local SD card logging with timestamps
- **Improved Reliability**: Fewer connections, integrated cellular modem
- **Better Documentation**: Comprehensive setup and troubleshooting guides

### Operational Advantages
- **Easier Configuration**: Centralized config.h file for all settings
- **Better Testing**: Dedicated test sketch for system validation
- **Enhanced Monitoring**: Multiple logging and reporting options
- **Improved Maintainability**: Modular design with clear documentation

## Cost Analysis

### Initial Hardware Cost
- Core system (MKR NB 1500 + shields + sensor): ~$200-240
- Enclosure and installation materials: ~$50-75
- **Total initial investment**: ~$250-315

### Operating Costs
- Hologram.io cellular service: ~$10-20/month
- **Annual operating cost**: ~$120-240

### Cost Comparison
- Previous 052019 versions: ~$180-220 initial, similar operating costs
- Commercial tank monitoring systems: $500-2000+ initial, $30-100/month operating
- **092025 offers excellent value with professional features**

## Applications

### Primary Use Cases
- **Septic Tank Monitoring**: High level alarm for pump tanks
- **Water Tank Monitoring**: Low/high level alerts for storage tanks
- **Sump Pump Monitoring**: High water level alerts
- **Chemical Tank Monitoring**: Level alerts for process tanks

### Installation Environments
- **Residential**: Septic systems, water wells, sump pumps
- **Commercial**: Process tanks, storage facilities
- **Agricultural**: Irrigation tanks, livestock water systems
- **Industrial**: Chemical storage, waste treatment

## Support and Maintenance

### Regular Maintenance
- **Monthly**: Check system status and SD card space
- **Quarterly**: Test alarm functionality
- **Annually**: Inspect hardware and connections

### Troubleshooting Resources
- Comprehensive documentation in project files
- Hardware test sketch for component validation
- Serial debugging output for system diagnosis
- Hologram.io dashboard for connectivity monitoring

## Future Enhancements

### Potential Improvements
- **GPS Integration**: Location tracking for mobile installations
- **Multiple Sensors**: Support for additional tank level sensors
- **Web Dashboard**: Custom monitoring interface
- **Email Reports**: Enhanced reporting via email
- **OTA Updates**: Over-the-air firmware updates

### Expansion Possibilities
- **Sensor Networks**: Multiple tank monitoring from single unit
- **Environmental Monitoring**: Temperature, pressure, humidity sensors
- **Industrial Integration**: SCADA system compatibility
- **IoT Platform Integration**: AWS IoT, Azure IoT, Google Cloud IoT

## Conclusion

The Tank Alarm 092025 represents a significant advancement in Arduino-based tank monitoring systems. By leveraging the Arduino MKR NB 1500 platform and modern cellular connectivity, it provides a robust, scalable, and cost-effective solution for remote tank monitoring applications.

The comprehensive documentation, modular design, and extensive testing support make this version suitable for both DIY enthusiasts and professional installations. The system's low power consumption and reliable operation make it ideal for remote locations where traditional monitoring systems are impractical or cost-prohibitive.

---

*Project Version: 092025*  
*Documentation Date: September 2025*  
*Platform: Arduino MKR NB 1500*  
*Connectivity: Hologram.io Cellular*