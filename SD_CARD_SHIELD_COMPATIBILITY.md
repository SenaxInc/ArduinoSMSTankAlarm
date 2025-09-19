# SD Card Shield Compatibility Guide

## Question
Does the code need to be different if the server is using a MKR SD PROTO shield versus using the SD card slot on the Ethernet shield?

## Answer
**No, the code does not need to be different.** Both shield configurations use the same pin assignment and initialization code.

## Shield Configurations in This Repository

### Tank Alarm Client (TankAlarm-092025-Client-Hologram)
- **Primary Shield**: Arduino MKR SD PROTO Shield
- **Secondary Shield**: Arduino MKR RELAY Shield (stacked on top)
- **SD Card Location**: Dedicated SD card slot on MKR SD PROTO shield
- **Pin Assignment**: Pin 4 (SD_CARD_CS_PIN)

### Tank Alarm Server (TankAlarm-092025-Server-Hologram)
- **Primary Shield**: Arduino MKR ETH Shield
- **SD Card Location**: Built-in SD card slot on MKR ETH shield
- **Pin Assignment**: Pin 4 (SD_CARD_CS_PIN)

## Technical Details

### SPI Pin Usage
Both shield configurations use the same SPI pins:
- **Pin 11**: SPI MOSI (Master Out Slave In)
- **Pin 12**: SPI MISO (Master In Slave Out)
- **Pin 13**: SPI SCK (Serial Clock)
- **Pin 4**: SD Card Chip Select (CS)

### Additional Pin Usage (MKR ETH Shield Only)
- **Pin 5**: Ethernet Controller Chip Select (internal, handled automatically)

### Code Initialization
Both configurations use identical SD card initialization:
```cpp
#define SD_CARD_CS_PIN 4
if (!SD.begin(SD_CARD_CS_PIN)) {
    // Handle SD card initialization failure
}
```

## Key Differences

### Hardware Stack Order

**Client Configuration (MKR SD PROTO + MKR RELAY):**
```
┌─────────────────────────┐
│     MKR RELAY SHIELD    │  ← Top
├─────────────────────────┤
│    MKR SD PROTO SHIELD  │  ← Middle (SD card here)
├─────────────────────────┤
│      MKR NB 1500        │  ← Bottom
└─────────────────────────┘
```

**Server Configuration (MKR ETH):**
```
┌─────────────────────────┐
│     MKR ETH SHIELD      │  ← Top (SD card here)
├─────────────────────────┤
│      MKR NB 1500        │  ← Bottom
└─────────────────────────┘
```

### Functional Differences

**MKR SD PROTO Shield:**
- Dedicated SD card slot
- Additional prototyping area
- Single SPI device (SD card only)

**MKR ETH Shield:**
- Shared SPI bus between SD card and Ethernet controller
- Built-in Ethernet connectivity
- Automatic chip select management for Ethernet

## Code Compatibility

### No Changes Required
The Arduino SD library automatically handles the SPI bus sharing between devices when using different chip select pins. Both configurations work with the same code because:

1. **Same CS Pin**: Both use Pin 4 for SD card chip select
2. **Automatic SPI Management**: Arduino library handles SPI bus arbitration
3. **Standard Interface**: Both shields implement the standard SD card SPI interface

### Verified Working Code
The existing code in both projects demonstrates compatibility:
- `TankAlarm-092025-Client-Hologram.ino` works with MKR SD PROTO shield
- `TankAlarm-092025-Server-Hologram.ino` works with MKR ETH shield

## Migration Between Shields

If you need to switch between shield types:

1. **Hardware**: Simply swap the shields (ensure proper stacking order)
2. **Software**: No code changes required
3. **Configuration**: Use the same `SD_CARD_CS_PIN 4` setting

## Troubleshooting

### If SD Card Fails to Initialize
1. Check physical connections
2. Verify SD card is properly inserted
3. Ensure SD card is formatted as FAT32
4. Check for SPI bus conflicts (shouldn't occur with these shields)

### Performance Considerations
- **MKR SD PROTO**: Dedicated SPI bus may have slightly better SD performance
- **MKR ETH**: Shared SPI bus is still sufficient for logging applications

## Conclusion

Both shield configurations are fully compatible with the same code. The Arduino SD library and hardware design ensure that Pin 4 works as the SD card chip select for both the MKR SD PROTO shield and the built-in SD slot on the MKR ETH shield.