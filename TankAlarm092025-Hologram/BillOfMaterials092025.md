# Bill of Materials - Tank Alarm 092025

This document lists all the components required for the September 2025 version of the Arduino SMS Tank Alarm system.

## Main Components

| Qty | Component | Part Number | Description | Supplier Link |
|-----|-----------|-------------|-------------|---------------|
| 1 | Arduino MKR NB 1500 | ABX00019 | Main board with built-in cellular modem (ublox SARA-R410M) | [Arduino Store](https://store.arduino.cc/products/arduino-mkr-nb-1500) |
| 1 | Arduino MKR SD PROTO | ASX00008 | SD card shield with prototyping area | [Arduino Store](https://store.arduino.cc/products/arduino-mkr-sd-proto-shield) |
| 1 | Arduino MKR RELAY | ASX00013 | Relay control shield | [Arduino Store](https://store.arduino.cc/products/arduino-mkr-relay-proto-shield) |
| 1 | Hologram.io SIM Card | - | Cellular connectivity SIM card | [Hologram.io](https://hologram.io) |

## Storage & Memory

| Qty | Component | Specification | Description | Notes |
|-----|-----------|---------------|-------------|-------|
| 1 | MicroSD Card | 8-32GB, Class 10, FAT32 | Data logging storage | Must be formatted as FAT32 |

## Sensors & Switches

| Qty | Component | Type | Specification | Application |
|-----|-----------|------|---------------|-------------|
| 1 | Tank Level Sensor | Float Switch | SPST, N.O., 12-24V rating | Tank level detection |
| 1 | Float Switch | Miniature | 10A @ 250VAC contacts | Alternative sensor option |

### Recommended Tank Level Sensors

| Supplier | Part Number | Description | Link |
|----------|-------------|-------------|------|
| Grainger | 3BY75 | N.C. Float Switch | [Grainger](https://www.grainger.com/product/DAYTON-Float-Switch-3BY75) |
| McMaster-Carr | 4036K31 | Side-Mount Float Switch | [McMaster](https://www.mcmaster.com/4036K31/) |

## Power Supply Options

### Option 1: USB Power (Development/Testing)
| Component | Specification | Notes |
|-----------|---------------|-------|
| USB Cable | Micro USB | For programming and testing |
| USB Power Adapter | 5V, 1A minimum | Wall adapter for bench testing |

### Option 2: Battery Power (Field Deployment)
| Component | Specification | Application |
|-----------|---------------|-------------|
| LiPo Battery | 3.7V, 2000mAh+ | Portable operation |
| Solar Panel | 6V, 1W minimum | Solar charging (optional) |
| Battery Management | Built into MKR NB 1500 | Integrated charging circuit |

### Option 3: External DC Power
| Component | Specification | Application |
|-----------|---------------|-------------|
| DC Power Supply | 7-12V, 500mA | Continuous operation |
| DC Jack Adapter | 2.1mm center positive | Power input connection |

## Enclosure & Mounting

| Qty | Component | Specification | Purpose |
|-----|-----------|---------------|---------|
| 1 | Weatherproof Enclosure | IP65, minimum 6"x4"x2" | Outdoor installation |
| 1 | Cable Glands | PG13.5 or 1/2" NPT | Sensor cable entry |
| 4 | Mounting Screws | Stainless steel | Enclosure mounting |
| 1 | Antenna Mount | SMA connector | External antenna (if needed) |

### Recommended Enclosures

| Supplier | Part Number | Description | Features |
|----------|-------------|-------------|----------|
| Hammond | 1554G2GY | Polycarbonate Enclosure | Clear lid, IP65 rated |
| Pelican | 1120 | Protective Case | Waterproof, impact resistant |

## Wiring & Connections

| Qty | Component | Specification | Purpose |
|-----|-----------|---------------|---------|
| 1 | Sensor Cable | 18-22 AWG, 2-conductor | Float switch connection |
| 1 | Power Cable | 18 AWG, 2-conductor | External power (if used) |
| 10 | Jumper Wires | M-M, M-F varieties | Internal connections |
| 1 | Terminal Block | 2-position, 12A rating | Sensor connections |

## Tools Required

| Tool | Purpose |
|------|---------|
| Phillips Screwdriver | Assembly |
| Wire Strippers | Cable preparation |
| Multimeter | Testing and troubleshooting |
| Drill & Bits | Enclosure mounting holes |
| Computer | Programming and setup |

## Optional Components

### Alarm Indication
| Qty | Component | Purpose |
|-----|-----------|---------|
| 1 | LED Indicator | Visual alarm status |
| 1 | Buzzer | Audible alarm |
| 1 | External Relay | High-power switching |

### Monitoring Enhancements
| Qty | Component | Purpose |
|-----|-----------|---------|
| 1 | Temperature Sensor | Environmental monitoring |
| 1 | Pressure Sensor | Alternative level measurement |
| 1 | GPS Module | Location tracking |

## Cost Estimate

### Core Components (Required)
- Arduino MKR NB 1500: ~$70
- MKR SD PROTO Shield: ~$20
- MKR RELAY Shield: ~$25
- Hologram.io SIM: ~$0 (monthly service fees apply)
- MicroSD Card: ~$10
- Float Switch: ~$30-50
- Enclosure: ~$25-40
- Cables & Misc: ~$20

**Total Core System: ~$200-240**

### Monthly Operating Costs
- Hologram.io Service: ~$10-20/month (depending on data usage)

## Assembly Notes

1. **Shield Stacking Order** (bottom to top):
   - Arduino MKR NB 1500 (base)
   - MKR SD PROTO Shield
   - MKR RELAY Shield

2. **Power Considerations**:
   - Total system current: ~200mA active, <10mA sleep
   - Battery life calculation: Capacity(mAh) ÷ Average Current × 0.8
   - For 2000mAh battery: ~400+ hours continuous, weeks with sleep mode

3. **Antenna Placement**:
   - Keep cellular antenna away from metal objects
   - External antenna recommended for metal enclosures
   - Minimum 6" separation from other electronics

4. **Environmental Considerations**:
   - Operating temperature: -40°C to +85°C (MKR NB 1500 spec)
   - Humidity: Use desiccant packets in outdoor enclosures
   - Vibration: Secure all connections and components

## Supplier Information

### Primary Suppliers
- **Arduino Official Store**: https://store.arduino.cc
- **Adafruit**: https://www.adafruit.com
- **SparkFun**: https://www.sparkfun.com
- **Hologram.io**: https://hologram.io

### Industrial Suppliers
- **Grainger**: https://www.grainger.com (sensors, enclosures)
- **McMaster-Carr**: https://www.mcmaster.com (hardware, cables)
- **Digi-Key**: https://www.digikey.com (electronic components)
- **Mouser**: https://www.mouser.com (electronic components)

---

*Last Updated: September 2025*
*Version: 092025*