# Tank Alarm Server Wiring Guide

## Hardware Components

### Required Components
- **Arduino MKR NB 1500** - Main microcontroller with cellular connectivity
- **Arduino MKR ETH Shield** - Ethernet connectivity for local network
- **Hologram.io SIM Card** - Cellular data service
- **MicroSD Card** - Data logging storage
- **Ethernet Cable** - Network connection

### Optional Components
- **Power Supply** - 5V DC adapter or USB power
- **Enclosure** - Weather-resistant case for outdoor installation

## Wiring Connections

### MKR NB 1500 Base Board
```
MKR NB 1500 Main Board
┌─────────────────────────┐
│  [ ]   [ ]   [ ]   [ ]  │  Digital pins 0-3
│  [ ]   [ ]   [ ]   [ ]  │  Digital pins 4-7  
│  [ ]   [ ]   [ ]   [ ]  │  Digital pins 8-11
│  [ ]   [ ]   [ ]   [ ]  │  Digital pins 12-14, SCL
│  [ ]   [ ]   [ ]   [ ]  │  SDA, A0-A2
│  [ ]   [ ]   [ ]   [ ]  │  A3-A6
│                         │
│     [USB]    [RESET]    │  
│                         │
│  [○] [○] [○] [○] [○]   │  Power pins: VIN, 5V, 3.3V, GND, GND
└─────────────────────────┘
```

### MKR ETH Shield Stacking
```
MKR ETH Shield (stacked on top)
┌─────────────────────────┐
│                         │
│    [RJ45 Ethernet]      │  Ethernet connector
│                         │
│    [microSD slot]       │  SD card slot
│                         │
│  PIN CONNECTIONS:       │
│  - Pin 4: SD CS         │  SD card chip select
│  - SPI pins: MISO,      │  SPI communication
│    MOSI, SCK            │  for Ethernet & SD
│                         │
└─────────────────────────┘
```

## Pin Assignments

### Used Pins
| Pin | Function | Component |
|-----|----------|-----------|
| Pin 4 | SD Card Chip Select | MKR ETH Shield |
| Pin 11 | SPI MOSI | MKR ETH Shield |
| Pin 12 | SPI MISO | MKR ETH Shield |
| Pin 13 | SPI SCK | MKR ETH Shield |
| Pin 5 | Ethernet CS | MKR ETH Shield (internal) |

### Available Pins
| Pin | Type | Available For |
|-----|------|---------------|
| 0-3 | Digital | General I/O |
| 6-10 | Digital | General I/O |
| 14 | Digital/PWM | General I/O |
| A0-A6 | Analog | Analog inputs |
| SDA/SCL | I2C | I2C devices |

## Power Requirements

### Power Input Options
1. **USB Power**: 5V via USB connector (for development/testing)
2. **External Power**: 7-12V DC via VIN pin (for permanent installation)
3. **Battery Power**: 3.7V LiPo battery via JST connector

### Power Consumption
- **Active Mode**: ~200-300mA (with cellular and Ethernet active)
- **Idle Mode**: ~150-200mA (maintaining connections)
- **Sleep Mode**: ~50-100mA (with periodic wake-ups)

## Network Connections

### Cellular (Hologram.io)
- **SIM Card**: Insert Hologram.io SIM into MKR NB 1500 SIM slot
- **Antenna**: Use included cellular antenna (connect to u.FL connector)
- **Network**: Automatic connection to Hologram.io network

### Ethernet (Local Network)
- **Cable**: Standard Ethernet cable (Cat5e or better)
- **Connection**: Connect to router/switch on local network
- **IP Assignment**: DHCP automatic or static IP configuration

## Installation Steps

### 1. Hardware Assembly
1. Insert Hologram.io SIM card into MKR NB 1500
2. Insert microSD card into MKR ETH Shield
3. Stack MKR ETH Shield onto MKR NB 1500
4. Attach cellular antenna to MKR NB 1500
5. Connect Ethernet cable to shield

### 2. Software Configuration
1. Upload server code to MKR NB 1500
2. Configure `server_config.h` with your settings
3. Test connections via Serial Monitor

### 3. Network Setup
1. Configure local router to allow device access
2. Note assigned IP address for web interface access
3. Test web interface from local network

## Troubleshooting

### Connection Issues
- **No Cellular Signal**: Check antenna connection and signal strength
- **Ethernet Not Working**: Verify cable and network settings
- **SD Card Errors**: Check card format (FAT32) and connections

### Power Issues
- **Device Resets**: Check power supply capacity (minimum 1A recommended)
- **Unstable Operation**: Use dedicated power supply, not USB power

### Network Access
- **Can't Access Web Interface**: Check IP address and firewall settings
- **DHCP Issues**: Configure static IP in router or use static IP mode

## Physical Installation

### Mounting Options
- Desktop/shelf mounting for indoor use
- Wall mounting bracket for permanent installation
- Weather-resistant enclosure for outdoor installations

### Environmental Considerations
- **Temperature Range**: -40°C to +85°C (industrial grade)
- **Humidity**: Up to 95% non-condensing
- **Power Protection**: Use surge protector for outdoor installations

## Maintenance

### Regular Checks
- Monitor SD card storage space
- Check network connectivity status
- Verify cellular signal strength
- Review server logs for errors

### Periodic Tasks
- Update firmware as needed
- Clean dust from ventilation
- Check cable connections
- Backup important log files

## Support Resources

### Documentation
- [Arduino MKR NB 1500 Guide](https://docs.arduino.cc/hardware/mkr-nb-1500)
- [MKR ETH Shield Guide](https://docs.arduino.cc/hardware/mkr-eth-shield)
- [Hologram.io Documentation](https://hologram.io/docs/)

### Pinout References
- MKR NB 1500 pinout diagram
- MKR ETH Shield schematic
- SPI connection details