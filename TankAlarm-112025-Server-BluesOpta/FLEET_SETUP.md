# Fleet-Based Setup Guide (No Manual Routes Required)

This simplified setup guide uses **fleet-based device-to-device communication**, eliminating the need for manual route configuration in Blues Notehub.

## Quick Start

### 1. Create Fleets in Blues Notehub

1. Log in to https://notehub.io
2. Navigate to your project
3. Go to **Fleets** → **Create Fleet**
4. Create two fleets:
   - **Fleet name:** `tankalarm-server`  
     **Description:** Base station server
   - **Fleet name:** `tankalarm-clients`  
     **Description:** Field monitoring devices

### 2. Assign Devices to Fleets

1. Go to **Devices**
2. Select your server Notecard → assign to `tankalarm-server` fleet
3. Select each client Notecard → assign to `tankalarm-clients` fleet

**That's it!** No route configuration needed.

## How It Works

### Client → Server Communication
- Clients send notes using fleet-based targeting: `fleet.tankalarm-server:telemetry.qi`
- Blues Notehub automatically delivers to all devices in the `tankalarm-server` fleet
- No manual routes required

### Server → Client Communication  
- Server sends config updates using device-specific targeting: `device:<client-uid>:config.qi`
- Blues Notehub delivers directly to the specific client device
- No manual routes required

## Configuration

### Client Configuration

The client configuration file (`/client_config.json` on LittleFS) includes:

```json
{
  "site": "Tank Farm A",
  "deviceLabel": "Tank-01",
  "serverFleet": "tankalarm-server",
  "sampleSeconds": 300,
  "reportHour": 5,
  "reportMinute": 0,
  "dailyEmail": "reports@example.com",
  "tanks": [
    {
      "id": "A",
      "name": "Primary Tank",
      "number": 1,
      "sensor": "analog",
      "primaryPin": 0,
      "secondaryPin": -1,
      "loopChannel": -1,
      "heightInches": 120.0,
      "highAlarm": 100.0,
      "lowAlarm": 20.0,
      "daily": true,
      "alarmSms": true,
      "upload": true
    }
  ]
}
```

Server-managed SMS contacts have been removed from the client schema; per-tank `alarmSms` flags now simply request that the server escalate alerts via its own contact list.

**Key field:** `serverFleet` - Must match the server's fleet name in Notehub

### Server Configuration

The server configuration file (`/server_config.json` on LittleFS) includes:

```json
{
  "serverName": "Tank Alarm Server",
  "clientFleet": "tankalarm-clients",
  "smsPrimary": "+12223334444",
  "smsSecondary": "+15556667777",
  "dailyEmail": "reports@example.com",
  "dailyHour": 6,
  "dailyMinute": 0,
   "webRefreshSeconds": 21600,
  "useStaticIp": true
}
```

**Key field:** `clientFleet` - Used for fleet-wide operations (future use)

## Deployment Workflow

### Initial Setup

1. **Flash server Opta:**
   - Upload `TankAlarm-112025-Server-BluesOpta.ino`
   - Connect to Ethernet
   - Note IP address from Serial Monitor

2. **Flash client Optas:**
   - Upload `TankAlarm-112025-Client-BluesOpta.ino`
   - Connect sensors and power
   - Note device UID from Serial Monitor

3. **Configure fleets in Notehub:**
   - Create fleets as described above
   - Assign devices to appropriate fleets

4. **Configure clients via server web UI:**
   - Navigate to `http://<server-ip>/`
   - Select each client from dropdown
   - Set `serverFleet` to `tankalarm-server`
   - Configure tank parameters
   - Click "Send Config to Client"

### Verification

1. **Check client serial console:**
   - Should see: `"Received config update from server"`
   - Configuration saved to flash

2. **Monitor telemetry:**
   - Wait for sample interval (~5 minutes default)
   - Server web UI should show tank levels
   - Check Notehub Events to see note traffic

3. **Test alarms:**
   - Trigger an alarm condition
   - Verify alarm appears on server dashboard
   - SMS alerts should be sent (if configured)

## Notefile Structure

| Communication Path | Notefile Format | Description |
|-------------------|----------------|-------------|
| Client → Server (telemetry) | `fleet.tankalarm-server:telemetry.qi` | Tank level readings |
| Client → Server (alarm) | `fleet.tankalarm-server:alarm.qi` | Alarm state changes |
| Client → Server (daily) | `fleet.tankalarm-server:daily.qi` | Daily summary reports |
| Server → Client (config) | `device:<client-uid>:config.qi` | Configuration updates |
| Server → Viewer (summary) | `viewer_summary.qo` (route to viewer fleet `viewer_summary.qi`) | 6-hour fleet snapshot |
| Server → SMS Gateway | `sms.qo` | SMS alert requests |
| Server → Email Gateway | `email.qo` | Email report requests |

## Troubleshooting

### Clients not appearing on server dashboard

**Symptoms:** Server web UI shows no clients

**Causes:**
- Clients not assigned to fleet
- Clients haven't sent first telemetry
- Server not polling Notecard

**Solutions:**
1. Verify fleet assignments in Notehub
2. Check client serial console for transmission
3. Restart server Opta
4. Check Notehub Events for note traffic

### Config updates not reaching clients

**Symptoms:** Client configuration not updating after sending from server

**Causes:**
- Incorrect client UID in server
- Client not online
- Client not assigned to fleet

**Solutions:**
1. Copy device UID from Notehub (not from serial console)
2. Verify client Notecard is connected
3. Check client fleet assignment
4. Force sync: send `{"req":"hub.sync"}` via Notecard playground

### Telemetry not flowing

**Symptoms:** Tank levels stuck, timestamps not updating

**Causes:**
- Wrong `serverFleet` value in client config
- Server not in correct fleet
- Network connectivity issues

**Solutions:**
1. Update client config with correct `serverFleet` value
2. Verify server fleet assignment in Notehub
3. Check both devices show recent activity in Notehub
4. Review Notecard status: `{"req":"card.wireless"}`

## Advanced Configuration

### Multiple Server Deployment

For systems with multiple servers monitoring different zones:

1. Create separate server fleets:
   - `tankalarm-server-zone1`
   - `tankalarm-server-zone2`

2. Assign servers to their respective fleets

3. Configure clients with appropriate `serverFleet` value

### Backup Server

To send telemetry to multiple servers:

1. Add secondary server fleet in client config
2. Modify `publishNote()` to send to multiple fleets
3. Both servers will receive all client data

### Fleet-Wide Config Updates

To broadcast config to all clients (use with caution):

```cpp
// In server code, modify dispatchClientConfig:
char targetFile[80];
snprintf(targetFile, sizeof(targetFile), "fleet.%s:config.qi", gConfig.clientFleet);
JAddStringToObject(req, "file", targetFile);
// Remove device-specific targeting
```

## SMS and Email Integration

The server publishes `sms.qo` and `email.qo` notefiles for downstream processing.

### Option 1: Blues Routes to External Services

Create routes in Notehub to forward to:
- Twilio (SMS)
- SendGrid (Email)
- Custom webhook

### Option 2: Direct API Integration

Modify server code to use:
- Twilio REST API (via Ethernet)
- SMTP email (via Ethernet)
- Custom SMS gateway

## Migrating from Route-Based Setup

If you have an existing system using manual routes, see **MIGRATION_GUIDE.md** for detailed migration steps.

**Quick summary:**
1. Create fleets
2. Assign devices to fleets  
3. Update firmware to new version
4. Push updated configs to clients
5. Disable old routes

## Hardware Requirements

### Client (per unit)
- Arduino Opta Lite
- Blues Wireless for Opta adapter
- Arduino Pro Opta Ext A0602 (for analog/4-20mA sensors)
- Level sensors (analog voltage, digital pulse, or 4-20mA)
- Power supply (24VDC recommended)

### Server
- Arduino Opta Lite
- Blues Wireless for Opta adapter  
- Ethernet connection
- Power supply

## Additional Resources

- **DEVICE_TO_DEVICE_API.md** - Technical details on fleet-based communication
- **MIGRATION_GUIDE.md** - Step-by-step migration from route-based setup
- **Blues Notehub Documentation** - https://dev.blues.io
- **Arduino Opta Documentation** - https://docs.arduino.cc/hardware/opta

## Support

For technical support:
1. Check serial console output for error messages
2. Review Notehub Events to trace note flow
3. Enable Notecard debug: `notecard.setDebugOutputStream(Serial)`
4. Verify fleet assignments in Notehub device pages
