# Relay Control Feature

## Overview

The Arduino Opta has 4 built-in relay outputs (D0-D3) that can be controlled remotely via the Blues Notecard cellular connection. This feature enables:

1. **Manual Control** - Toggle relays from the server web dashboard
2. **Alarm-Triggered Control** - Automatically trigger relays on another client when an alarm occurs
3. **Server-Triggered Control** - Trigger relays programmatically from the server

## Hardware

- **Arduino Opta Lite** - Has 4 built-in mechanical relays
- **Relay Outputs**: D0, D1, D2, D3 (controlled via LED_D0-LED_D3 constants)
- **Blues Notecard** - Provides cellular communication for relay commands

## Configuration

### Client Configuration

Add relay trigger settings to individual tanks in the client configuration:

```json
{
  "tanks": [
    {
      "id": "A",
      "name": "Tank A",
      "highAlarm": 100.0,
      "lowAlarm": 20.0,
      "relayTargetClient": "dev:864475044012345",
      "relayMask": 3
    }
  ]
}
```

#### Configuration Fields

- **`relayTargetClient`** (string): UID of the target client whose relays to control
  - Leave empty ("") to disable relay triggering
  - Get the UID from the Blues Notehub device list or server dashboard
  - Format: `dev:IMEI` (e.g., "dev:864475044012345")

- **`relayMask`** (integer): Bitmask indicating which relays to trigger
  - Bit 0 (value 1) = Relay 1 (D0)
  - Bit 1 (value 2) = Relay 2 (D1)
  - Bit 2 (value 4) = Relay 3 (D2)
  - Bit 3 (value 8) = Relay 4 (D3)
  - Combine bits to trigger multiple relays

#### Relay Mask Examples

| Decimal | Binary | Relays Triggered |
|---------|--------|------------------|
| 1       | 0001   | Relay 1 only     |
| 2       | 0010   | Relay 2 only     |
| 3       | 0011   | Relays 1 and 2   |
| 5       | 0101   | Relays 1 and 3   |
| 15      | 1111   | All 4 relays     |

## Usage

### 1. Manual Control from Server Web UI

1. Open the server dashboard at `http://<server-ip>/`
2. Locate the client in the telemetry table
3. Click the relay buttons (R1, R2, R3, R4) to toggle relays
4. Active relays are highlighted in blue
5. Commands are sent immediately via Blues Notecard

### 2. Automatic Control on Client Alarms

When a tank alarm is triggered (high or low threshold):

1. Client sends alarm notification to server (as usual)
2. If `relayTargetClient` is configured and `relayMask` is non-zero:
   - Client sends relay commands to the target client
   - Specified relays are activated on the target client
   - Commands are tagged with source "client-alarm"

**Example Scenario:**
- Tank A on Client 1 goes into high alarm
- Tank A is configured to trigger relays 1 and 2 on Client 2
- Client 1 automatically sends commands to activate Client 2's relays 1 and 2
- Client 2 can control pumps, valves, or alarms connected to those relays

### 3. Programmatic Control via API

Send relay commands from external systems:

```bash
curl -X POST http://<server-ip>/api/relay \
  -H "Content-Type: application/json" \
  -d '{
    "clientUid": "dev:864475044012345",
    "relay": 1,
    "state": true
  }'
```

**Request Parameters:**
- `clientUid` (string): Target client's device UID
- `relay` (integer): Relay number 1-4
- `state` (boolean): true = ON, false = OFF

**Response:**
- `200 OK` - Command sent successfully
- `400 Bad Request` - Invalid parameters
- `500 Internal Server Error` - Failed to send command

## Communication Protocol

### Relay Command Message

Commands are sent via Blues Notecard to `device:<clientUID>:relay.qi`:

```json
{
  "relay": 1,
  "state": true,
  "source": "server",
  "duration": 0
}
```

**Fields:**
- `relay` (integer): Relay number 1-4
- `state` (boolean): true = ON, false = OFF
- `source` (string): Origin of command ("server", "client-alarm", "manual")
- `duration` (integer): Auto-off timer in seconds (0 = manual, feature not yet implemented)

### Client Polling

- Client polls `relay.qi` every 5 seconds
- Commands are deleted after processing
- Invalid commands are logged and ignored

## Use Cases

### Example 1: Cross-Site Alarm Response

**Scenario:** When Tank A at Site 1 goes into high alarm, activate a strobe light at Site 2

**Configuration:**
```json
{
  "tanks": [{
    "id": "A",
    "name": "Site 1 Tank A",
    "relayTargetClient": "dev:864475044056789",
    "relayMask": 1
  }]
}
```

**Result:** Relay 1 on Client 2 (Site 2) activates when Tank A alarm triggers

### Example 2: Pump Control

**Scenario:** Manually control a pump connected to Relay 2 from the server dashboard

**Steps:**
1. Open server web UI
2. Find the client controlling the pump
3. Click "R2" button to toggle the pump
4. Button turns blue when pump is ON

### Example 3: Multi-Relay Coordination

**Scenario:** When Tank B goes low, activate both a pump (Relay 1) and warning light (Relay 3)

**Configuration:**
```json
{
  "tanks": [{
    "id": "B",
    "name": "Tank B",
    "lowAlarm": 20.0,
    "relayTargetClient": "dev:864475044056789",
    "relayMask": 5
  }]
}
```

**Result:** Relays 1 and 3 activate together (relayMask = 5 = binary 0101)

## Troubleshooting

### Relay Commands Not Working

1. **Check Blues Notecard Connection**
   - Verify client has cellular signal
   - Check Blues Notehub shows recent sync
   - Look for "notecard offline" messages in serial output

2. **Verify Configuration**
   - Confirm `relayTargetClient` UID is correct
   - Check `relayMask` is non-zero
   - Ensure target client is online and polling

3. **Check Serial Output**
   - Client: "Relay command received from..."
   - Server: "Queued relay command for client..."
   - Look for error messages about failed commands

### Relays Not Activating Physically

1. **Verify Hardware**
   - Check relay LED indicators on Arduino Opta
   - Test with manual control from web UI
   - Ensure external devices are properly connected

2. **Check Power**
   - Relays may not activate if power supply is insufficient
   - Verify external load is within relay specifications

3. **Review Serial Output**
   - Look for "Relay X (DX) set to ON/OFF" messages
   - Check for platform compatibility warnings

### Web UI Buttons Not Responding

1. **Check Browser Console**
   - Look for JavaScript errors
   - Verify `/api/relay` endpoint is accessible
   - Check network tab for failed requests

2. **Verify Server Configuration**
   - Server must have valid Blues Notecard connection
   - Check that client UIDs are correct in the table

## Security Considerations

- Relay commands use device-specific targeting (device:UID:relay.qi)
- Client UIDs are HTML-escaped in the web UI to prevent XSS
- Only authenticated users with server access can control relays
- Consider physical security for devices controlling critical equipment

## Limitations

- Maximum 4 relays per Arduino Opta client
- No timed auto-off feature yet (documented in code with TODO)
- Relay state is not persisted across power cycles
- No feedback mechanism to confirm relay state to server
- Commands depend on cellular connectivity

## Future Enhancements

Potential improvements for future versions:

1. **Auto-Off Timers** - Automatically turn off relays after a specified duration
2. **Relay State Feedback** - Report relay states back to server dashboard
3. **Scheduling** - Time-based relay control (on at 8am, off at 5pm)
4. **Interlock Logic** - Prevent certain relay combinations
5. **Persistent State** - Remember relay states across power cycles
6. **Relay Groups** - Control multiple relays across multiple clients as a group

## References

- [Arduino Opta Documentation](https://docs.arduino.cc/hardware/opta)
- [Blues Notecard Device-to-Device Communication](https://dev.blues.io/guides-and-tutorials/routing-data-to-cloud/device-to-device/)
- [TankAlarm Fleet Implementation](FLEET_IMPLEMENTATION_SUMMARY.md)
- [Blues Notehub Fleet Management](https://dev.blues.io/reference/notehub-api/fleet-api/)
