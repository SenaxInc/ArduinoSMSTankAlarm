# Blues Notehub Routing Setup (112025 Opta Release)

The 112025 client and server sketches exchange data exclusively through Blues Notehub. Complete the following steps before deploying the Opta hardware so telemetry, alarms, and configuration updates flow to the correct devices.

## 1. Prerequisites

- Blues Notehub account with a product already created for the Tank Alarm project.
- At least one provisioned client Notecard (field unit) and one server Notecard (base station) claimed into the product.
- The product UID flashed into both sketches (see `PRODUCT_UID` in the client code and `SERVER_PRODUCT_UID` in the server code).
- The server Notecard mounted to the server Opta and powered so Notehub can identify its device UID.

## 2. Identify Device UIDs

1. In Notehub, open **Devices** and locate the Opta server's Notecard. Copy its 32-character UID (for example, `dev:<xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx>`).
2. Optional but recommended: assign the server Notecard to its own fleet (for example, `tankalarm-server`). Assign all client Notecards to a separate fleet (for example, `tankalarm-clients`). Fleet scoping keeps the routes targeted.

You will reference the exact server UID in the route targets below.

## 3. Define Environment Variables (Optional)

If you prefer to keep the server UID out of the route definitions, create a product-level environment variable:

- Key: `SERVER_UID`
- Value: the server Notecard UID from step 2.

This lets you reuse the same UID token inside multiple routes (`{{env.SERVER_UID}}`). Skip this section if you want to paste the UID directly into each route.

## 4. Create Device-to-Device Routes

You need four routes to shuttle traffic between the client `.qo` files and the server `.qi` inboxes (and one route in the opposite direction for configuration pushes).

### 4.1 Telemetry (client ➜ server)

1. Navigate to **Routes → Create Route**.
2. Choose **Route type: Notehub Device File** (also labeled *Device-to-Device* in some regions).
3. Name: `Telemetry-to-Server`.
4. Filters:
   - Files: `telemetry.qo`
   - Optional fleet filter: select the client fleet.
5. Destination:
   - Device UID: paste the server UID or use `{{env.SERVER_UID}}`.
   - File: `telemetry.qi`
6. Save.

### 4.2 Alarms (client ➜ server)

Repeat the steps above with these changes:

- Name: `Alarm-to-Server`
- File filter: `alarm.qo`
- Destination file: `alarm.qi`

### 4.3 Daily Reports (client ➜ server)

Repeat the steps with:

- Name: `Daily-to-Server`
- File filter: `daily.qo`
- Destination file: `daily.qi`

### 4.4 Configuration Pushes (server ➜ client)

This route goes the other direction so the server can fan out configuration changes.

1. Create a new **Notehub Device File** route.
2. Name: `Config-to-Clients`.
3. Filters:
   - Source device: specify the server UID or fleet.
   - File: `config.qo`
4. Destination:
   - Device selector: choose **Fleet** and select the client fleet (preferred) or leave blank to broadcast to every device.
   - File: `config.qi`
5. Save.

## 5. Optional: SMS and Email Routes

The server sketch publishes `sms.qo` and `email.qo` records to trigger downstream services.

- To integrate with external services (Twilio, SendGrid, etc.), create additional HTTP or MQTT routes that watch those files and forward the JSON payloads to your provider.
- If you rely on a separate automation platform (Zapier, n8n, custom webhook), configure the route there and secure it with the chosen authentication method.

## 6. Verify Traffic Flow

1. Power the server Opta and confirm Notehub shows recent check-ins.
2. Power a client Opta. After it samples the tanks, check the **Events** tab to see `telemetry.qo` notes arriving.
3. Confirm the corresponding `telemetry.qi` file increments on the server device (Events view for the server UID).
4. Trigger an alarm condition on a client to ensure `alarm.qo` routes correctly and that `sms.qo` is emitted for downstream processing.
5. From the server dashboard (`/api/config`), push a small config change and verify clients receive the `config.qi` note.

Once these routes are active, the 112025 Opta client and server sketches will stay in sync via Notehub without manual SD card exchanges.

## 7. Update Client Configurations from the Server UI

The server’s intranet dashboard lets you push JSON configuration updates directly to any client via Notehub.

1. Ensure the server Opta is powered, connected to Ethernet, and reachable from your laptop (default port 80). Note the IP address printed in the server’s serial log during startup.
2. Open a browser and navigate to `http://<server-ip>/`. You should see the Tank Alarm dashboard with current tank telemetry.
3. Scroll to the **Update Client Config** section at the bottom of the page. It displays a reminder that POST requests must target `/api/config`.
4. Prepare a JSON payload containing:
   ```json
   {
     "client": "dev:xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx",
     "config": {
       "sampleSeconds": 600,
       "tanks": [
         {
           "id": "A",
           "name": "North Tank",
           "highAlarm": 110.0,
           "lowAlarm": 18.0
         }
       ]
     }
   }
   ```
   Replace the `client` value with the target Notecard UID and edit fields as required (any keys omitted remain unchanged on the client).
5. Use a REST client (curl, Postman, RESTED, etc.) from your network to POST the JSON to the server:
   ```bash
   curl -X POST http://<server-ip>/api/config \
     -H "Content-Type: application/json" \
     -d @payload.json
   ```
6. Confirm the response is `200 OK`. Check the server serial console for “Queued config update” and verify the Notehub event log shows a new `config.qo` note leaving the server.
7. After the client checks in (typically within the configured sample interval), it will consume the `config.qi` record, persist the changes to LittleFS, and log “Configuration updated from server” on its serial console.
8. Refresh the dashboard to confirm the updated thresholds or labels are reflected in subsequent telemetry uploads.
