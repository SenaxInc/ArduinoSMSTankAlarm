# Migration Guide: Route-Based to Fleet-Based Communication

This guide walks you through migrating your existing Tank Alarm system from manual route configuration to fleet-based device-to-device communication.

## Overview of Changes

**Before:** Required manually configuring routes in Blues Notehub web interface  
**After:** Only requires assigning devices to fleets in Blues Notehub

## Prerequisites

- Access to Blues Notehub account
- All devices claimed in the same Notehub project
- Arduino IDE with updated firmware files
- Serial console access to verify changes

## Step 1: Back Up Current Configuration

Before making changes:

1. **Export current client configs** from server web UI
   - Navigate to `http://<server-ip>/`
   - For each client, note its configuration settings
   - Save screenshots or export JSON configs

2. **Note current route settings**
   - Log in to Blues Notehub
   - Navigate to Routes
   - Document existing routes (for rollback if needed)

## Step 2: Create Fleets in Blues Notehub

1. **Log in to Blues Notehub** at https://notehub.io

2. **Create Client Fleet:**
   - Navigate to **Fleets** in left sidebar
   - Click **Create Fleet**
   - Name: `tankalarm-clients` (or your preferred name)
   - Description: "Tank monitoring field devices"
   - Click **Create**

3. **Create Server Fleet:**
   - Click **Create Fleet** again
   - Name: `tankalarm-server` (or your preferred name)
   - Description: "Tank alarm base station"
   - Click **Create**

## Step 3: Assign Devices to Fleets

1. **Assign Server Notecard:**
   - Navigate to **Devices**
   - Locate your server Notecard (Arduino Opta server)
   - Click on the device
   - Under **Fleets**, select `tankalarm-server`
   - Click **Update**

2. **Assign Client Notecards:**
   - For each client Notecard (field devices):
     - Click on the device
     - Under **Fleets**, select `tankalarm-clients`
     - Click **Update**

## Step 4: Update Server Firmware

1. **Open Arduino IDE**

2. **Load server sketch:**
   - File → Open → `TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino`

3. **Verify the following changes are present:**
   ```cpp
   // ServerConfig struct now has clientFleet field
   struct ServerConfig {
     char serverName[32];
     char clientFleet[32];  // NEW: Target fleet name
     ...
   };
   
   // Config update uses device-specific targeting
   char targetFile[80];
   snprintf(targetFile, sizeof(targetFile), "device:%s:config.qi", clientUid);
   ```

4. **Compile and upload:**
   - Select **Arduino Opta** board
   - Connect server Opta via USB
   - Click **Upload**

5. **Verify in Serial Monitor:**
   - Open Serial Monitor (115200 baud)
   - Look for: `"Server setup complete"`
   - Verify Notecard UID is displayed

6. **Update Server Configuration (if needed):**
   - If your client fleet has a different name, update via web UI
   - Navigate to server IP address
   - The server config will default to `tankalarm-clients`

## Step 5: Update Client Firmware

For each client device:

1. **Load client sketch:**
   - File → Open → `TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino`

2. **Verify the following changes are present:**
   ```cpp
   // ClientConfig struct now has serverFleet field
   struct ClientConfig {
     char siteName[32];
     char deviceLabel[24];
     char serverFleet[32];  // CHANGED from serverRoute
     ...
   };
   
   // Notes are sent with fleet targeting
   char targetFile[80];
   snprintf(targetFile, sizeof(targetFile), "fleet.%s:%s", gConfig.serverFleet, fileName);
   ```

3. **Compile and upload:**
   - Select **Arduino Opta** board
   - Connect client Opta via USB
   - Click **Upload**

4. **Verify in Serial Monitor:**
   - Open Serial Monitor (115200 baud)
   - Look for: `"Notecard UID: ..."`
   - Verify hardware requirements are listed

5. **Check configuration:**
   - Client will load existing config from flash
   - The `serverRoute` field will be empty initially
   - You'll need to push updated config from server

## Step 6: Update Client Configurations from Server

For each client:

1. **Navigate to server web UI** at `http://<server-ip>/`

2. **Select client from dropdown**

3. **Update the "Server Fleet" field:**
   - Change from old route value to: `tankalarm-server`
   - (Or your custom server fleet name from Step 2)

4. **Review other settings:**
   - Verify all tank configurations
   - Confirm email recipients (SMS numbers now live on the server)
   - Check sample intervals

5. **Click "Send Config to Client"**

6. **Monitor client serial console:**
   - Should see: `"Received config update from server"`
   - New config will be saved to flash

## Step 7: Verify Communication

1. **Check Telemetry Flow (Client → Server):**
   - Wait for client to take samples (~5 minutes with default 300s interval)
   - In Notehub, navigate to **Events**
   - Filter by client device
   - Look for notes with format: `fleet.tankalarm-server:telemetry.qi`
   - On server device events, verify `telemetry.qi` notes are arriving

2. **Check Server Web Dashboard:**
   - Refresh server web UI
   - Verify tank levels are updating
   - Check "Last Update" timestamps

3. **Test Alarm Flow:**
   - Trigger an alarm condition on a client
   - Verify alarm appears on server dashboard
   - Check for `alarm.qi` notes in Notehub events

4. **Test Config Updates (Server → Client):**
   - Make a minor config change via server UI
   - Send to client
   - In Notehub, filter by server device
   - Look for note with format: `device:<client-uid>:config.qi`
   - Client serial console should show config received

## Step 8: Clean Up Old Routes (Optional)

Once verified working:

1. **Navigate to Notehub Routes**

2. **Disable or delete old routes:**
   - `Telemetry-to-Server` route
   - `Alarm-to-Server` route  
   - `Daily-to-Server` route
   - `Config-to-Clients` route

3. **Keep the routes for a few days** before deleting (in case rollback needed)

## Troubleshooting

### Client not sending data to server

**Check:**
- Client is assigned to correct fleet in Notehub
- Client config has correct `serverFleet` value (use Serial Monitor to verify)
- Client's Notecard is connected (check `card.wireless` status)
- Server is assigned to correct fleet

**Fix:**
- Push config update from server with correct `serverFleet` value
- Verify fleet names match exactly (case-sensitive)
- Restart client Opta (power cycle)

### Server not receiving client data

**Check:**
- Server Notecard is in correct fleet
- In Notehub Events, verify notes are being sent with fleet targeting
- Server is polling notefiles (check serial console for activity)

**Fix:**
- Verify server fleet name matches what clients are targeting
- Check Notehub Events for both devices to see note flow
- Restart server Opta

### Config updates not reaching client

**Check:**
- Client UID is correct in server UI
- Format in Notehub shows `device:<uid>:config.qi`
- Client is online and syncing with Notehub

**Fix:**
- Copy device UID directly from Notehub device page
- Verify client Notecard is in continuous mode
- Try forcing sync on client: `{"req":"hub.sync"}`

### Notehub shows errors

**Common issues:**
- Malformed fleet name (contains spaces or special characters)
- Device not assigned to any fleet
- Note file name doesn't match expected format

**Fix:**
- Fleet names should be lowercase, hyphenated, no spaces
- Ensure all devices are assigned to appropriate fleets
- Check file names in code match expected pattern

## Configuration File Format Changes

### Client Config (LittleFS: /client_config.json)

**Old format:**
```json
{
  "site": "Tank Farm A",
  "deviceLabel": "Tank-01",
  "serverRoute": "default-route",
  "sampleSeconds": 300,
  ...
}
```

**New format:**
```json
{
  "site": "Tank Farm A",
  "deviceLabel": "Tank-01",
  "serverFleet": "tankalarm-server",
  "sampleSeconds": 300,
  ...
}
```

### Server Config (LittleFS: /server_config.json)

**Old format:**
```json
{
  "serverName": "Tank Alarm Server",
  "smsPrimary": "+12223334444",
  ...
}
```

**New format:**
```json
{
  "serverName": "Tank Alarm Server",
  "clientFleet": "tankalarm-clients",
  "smsPrimary": "+12223334444",
  ...
}
```

## Rollback Procedure

If you need to revert to route-based communication:

1. **Re-enable routes in Notehub** (if not deleted)

2. **Restore previous firmware:**
   - Use Arduino IDE to upload previous version
   - Or modify code to revert changes

3. **Update client configs:**
   - Change `serverFleet` back to `serverRoute` in struct
   - Restore route field in config files

4. **Verify routes are active** in Notehub

## Benefits After Migration

✅ **No manual route configuration** - Just assign devices to fleets  
✅ **Easier scaling** - Add new clients by assigning to fleet  
✅ **Simpler deployment** - One-time fleet setup  
✅ **Better organization** - Fleet-based device grouping  
✅ **More resilient** - Fleet membership survives device replacement  

## Support

If you encounter issues during migration:

1. Check Notehub Events for both devices to trace note flow
2. Enable Notecard debug output: `notecard.setDebugOutputStream(Serial)`
3. Verify fleet assignments in Notehub device pages
4. Review DEVICE_TO_DEVICE_API.md for technical details

## Next Steps

After successful migration:

- Consider creating additional fleets for multi-site deployments
- Set up fleet-level environment variables for configuration
- Explore Blues Notehub API for programmatic fleet management
- Monitor note traffic in Notehub to optimize sync intervals
