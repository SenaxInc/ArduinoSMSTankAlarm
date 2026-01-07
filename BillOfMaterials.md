# TankAlarm 112025 - Bill of Materials

**Version:** 1.0.0  
**Platform:** Arduino Opta + Blues Wireless  
**Last Updated:** January 7, 2026

---

## Core Components

### Client System (Remote Tank Monitoring)

Qty | Component | Part Number | Link | Notes
--- | --------- | ----------- | ---- | -----
1 | Arduino Opta Lite | AFX00003 | [Arduino USA Store](https://store-usa.arduino.cc/products/opta-lite) | Industrial controller (STM32H747XI) - $151
1 | Blues Wireless for Opta | N/A | [Blues Store](https://shop.blues.com/collections/accessories/products/wireless-for-opta) | Cellular Notecard carrier - $170
1 | Blues Notecard (North America) | NOTE-NBGL | [Blues Store](https://shop.blues.com/collections/notecard/products/notecard-cellular) | LTE-M/NB-IoT cellular - $45-49
1 | Arduino Opta Ext A0602 (Optional) | AFX00007 | [Arduino USA Store](https://store-usa.arduino.cc/products/opta-ext-a0602) | 4-20mA analog expansion - $229
1 | 24V DC Power Supply | N/A | Various | DIN rail mount recommended - $30-50
1 | Solar Charge Controller (Optional) | SunKeeper-6 | [Solar Electric](https://www.solar-electric.com/mosupamoco6a.html) | For remote solar installations - ~$60
1 | Battery (Optional) | Optima D31T | [Walmart](https://www.walmart.com/ip/OPTIMA-YellowTop-Dual-Purpose-Battery-Group-d31t/579876980) | For backup/solar power - ~$250

### Server System (Office/Headquarters)

Qty | Component | Part Number | Link | Notes
--- | --------- | ----------- | ---- | -----
1 | Arduino Opta Lite | AFX00003 | [Arduino USA Store](https://store-usa.arduino.cc/products/opta-lite) | Industrial controller - $151
1 | Blues Wireless for Opta | N/A | [Blues Store](https://shop.blues.com/collections/accessories/products/wireless-for-opta) | Cellular Notecard carrier - $170
1 | Blues Notecard (North America) | NOTE-NBGL | [Blues Store](https://shop.blues.com/collections/notecard/products/notecard-cellular) | LTE-M/NB-IoT cellular - $45-49
1 | Ethernet Cable | Cat5e/Cat6 | Various | RJ45 to local network - $5-10
1 | 24V DC Power Supply | N/A | Various | DIN rail mount recommended - $30-50

### Viewer System (Optional - Remote Display)

Qty | Component | Part Number | Link | Notes
--- | --------- | ----------- | ---- | -----
1 | Arduino Opta Lite | AFX00003 | [Arduino USA Store](https://store-usa.arduino.cc/products/opta-lite) | Industrial controller - $151
1 | Blues Wireless for Opta | N/A | [Blues Store](https://shop.blues.com/collections/accessories/products/wireless-for-opta) | Cellular Notecard carrier - $170
1 | Blues Notecard (North America) | NOTE-NBGL | [Blues Store](https://shop.blues.com/collections/notecard/products/notecard-cellular) | LTE-M/NB-IoT cellular - $45-49
1 | Ethernet Cable | Cat5e/Cat6 | Various | RJ45 to local network - $5-10
1 | 24V DC Power Supply | N/A | Various | DIN rail mount recommended - $30-50

---

## Sensors & Accessories

### Tank Level Sensors

Qty | Component | Part Number | Link | Notes
--- | --------- | ----------- | ---- | -----
1+ | Pressure Transmitter 4-20mA (0-5 PSI) | 626-06-CB-P1-E5-S1 | [Dwyer](http://www.dwyer-inst.com/configurator/index.cfm?Group_ID=98) | Bottom-mounted pressure sensor
1+ | Float Switch (N.C.) | 3BY75 | [Grainger](https://www.grainger.com/product/DAYTON-Float-Switch-3BY75) | High/low level detection
N/A | Analog Voltage Sensor (0-10V) | Various | N/A | General purpose analog sensors

### M12 Connectors (for field wiring)

Qty | Component | Part Number | Link | Notes
--- | --------- | ----------- | ---- | -----
2+ | M12 Connector | 21033112400 | [Mouser](https://www.mouser.com/ProductDetail/617-21033112400) | Panel mount connector
2+ | M12 Plug Male | 21032211405 | [Mouser](https://www.mouser.com/ProductDetail/617-21032211405) | Field cable connector
2+ | M12 Plug Female | 21032212405 | [Mouser](https://www.mouser.com/ProductDetail/617-21032212405) | Field cable connector

### Relay & Control

Qty | Component | Part Number | Link | Notes
--- | --------- | ----------- | ---- | -----
1 | 2-Channel 5V Relay Module (Optional) | N/A | [RobotShop](https://www.robotshop.com/en/2-channel-5v-relay-module-elf.html) | For valve control (if needed)
1 | Terminal Block | GTB-406 | [Home Depot](https://www.homedepot.com/p/Gardner-Bender-22-10-AWG-6-Circuit-Terminal-Block-1-Pack-GTB-406/202522482) | For field wiring

### Wiring & Cables

Qty | Component | Part Number | Link | Notes
--- | --------- | ----------- | ---- | -----
1 | Jumper Wire Kit (M-M) | MIKROE-513 | [Mouser](https://www.mouser.com/ProductDetail/932-MIKROE-513) | Prototyping connections
1 | Jumper Wire Kit (M-F) | MIKROE-512 | [Mouser](https://www.mouser.com/ProductDetail/932-MIKROE-512) | Prototyping connections

---

## Enclosures & Mounting

### Recommended Enclosures

Qty | Component | Link | Notes
--- | --------- | ---- | -----
1 | DIN Rail Mount Enclosure | Various | NEMA 4X rated for outdoor use
1 | DIN Rail (35mm) | Various | For mounting Opta controllers
1 | Cable Glands | Various | IP67 rated for outdoor installations

---

## Optional Components

### Solar Power System (Remote Sites)

Qty | Component | Link | Notes
--- | --------- | ---- | -----
1 | Solar Panel (50-100W) | Various | 12V nominal output
1 | Solar Charge Controller | [Solar Electric](https://www.solar-electric.com/mosupamoco6a.html) | SunKeeper-6 or equivalent
1 | Deep Cycle Battery | [Walmart](https://www.walmart.com/ip/OPTIMA-YellowTop-Dual-Purpose-Battery-Group-d31t/579876980) | Optima D31T or equivalent
1 | Solar Panel Mounting Hardware | Various | Ground or pole mount

---

## Blues Wireless Service

Item | Link | Notes
---- | ---- | -----
Blues Notehub Account | [notehub.io](https://notehub.io) | Free tier available; 500MB/month included
Cellular Data Plan | [Blues Wireless](https://blues.io/pricing/) | Pay-as-you-go or prepaid options

---

## Cost Estimates (USA Pricing, January 2026)

### Basic Client System
- Arduino Opta Lite: $151
- Blues Wireless for Opta: $170
- Blues Notecard Cellular: $45-49
- Power Supply: $30-50
- **Subtotal: ~$396-420**

### Basic Server System
- Arduino Opta Lite: $151
- Blues Wireless for Opta: $170
- Blues Notecard Cellular: $45-49
- Power Supply: $30-50
- Ethernet Cable: $5-10
- **Subtotal: ~$401-430**

### Sensors (per tank)
- Pressure Transmitter (4-20mA): $150-250
- Float Switch: $50-100
- M12 Connectors/Cables: $50
- **Subtotal: ~$250-400 per tank**

### Analog Expansion (optional per client)
- Arduino Opta Ext A0602: $229

### Optional Solar System (per remote site)
- Solar Panel: $80-150
- Charge Controller: $60
- Battery: $250
- Mounting Hardware: $50
- **Subtotal: ~$440-510**

**Note:** Prices shown are from USA vendors and do not include shipping or sales tax. International pricing may vary. Check vendor websites for current pricing and availability.

---

## Compatibility Notes

### Arduino Opta Models
- **Opta Lite** (AFX00003) - Recommended for most installations
- **Opta WiFi** (AFX00001) - Can be used but WiFi not utilized in this design
- **Opta RS485** (AFX00002) - Can be used for Modbus sensor integration

### Blues Notecard Models
- **NOTE-NBGL-500** - North America (LTE-M/NB-IoT)
- **NOTE-WBEX-500** - Global (LTE-M/NB-IoT)
- **NOTE-CELL-500** - Global (2G/3G fallback)

### Sensor Compatibility
The system supports multiple sensor types:
- **4-20mA Current Loop** - Requires Arduino Opta Ext A0602
- **0-10V Analog** - Requires Arduino Opta Ext A0602
- **Digital On/Off** - Direct connection to Opta digital inputs
- **Pulse/Frequency** - Direct connection to Opta digital inputs

---

## Where to Buy

### Official Distributors (USA)
- **Arduino USA Store:** [store-usa.arduino.cc](https://store-usa.arduino.cc) - Arduino Opta products
- **Blues Wireless Shop:** [shop.blues.com](https://shop.blues.com) - Notecards and accessories
- **Mouser Electronics:** [mouser.com](https://mouser.com) - Electronic components, connectors
- **Digi-Key:** [digikey.com](https://digikey.com) - Electronic components
- **Newark:** [newark.com](https://newark.com) - Industrial electronics

### Industrial Suppliers (USA)
- **Grainger:** [grainger.com](https://grainger.com) - Sensors, switches, enclosures, power supplies
- **Automation Direct:** [automationdirect.com](https://automationdirect.com) - Industrial automation components
- **McMaster-Carr:** [mcmaster.com](https://mcmaster.com) - Mounting hardware, enclosures

---

**For detailed installation and wiring instructions, see:**
- Client: [TankAlarm-112025-Client-BluesOpta/README.md](TankAlarm-112025-Client-BluesOpta/README.md)
- Server: [TankAlarm-112025-Server-BluesOpta/README.md](TankAlarm-112025-Server-BluesOpta/README.md)
