# Code Review Update - 2026-01-14

## Summary of Updates
- Removed broken emoji glyphs and standardized header branding color using the .brand class.
- Refined pause controls: moved Pause Server into Server Settings body and added conditional header Unpause.
- Removed Server Settings SMS alarm checkboxes and enforced SMS flags off for alarm-on-high/low/clear in save payload.
- Fixed encoding issues (e.g., R2 text) and cleaned glyph artifacts; added runtime normalization for bullet glyphs in the Config Generator sensor info.
- Added server-down SMS alert based on power loss and a 24-hour heartbeat gap, with persistent heartbeat storage.
- Added a Server Settings checkbox for server-down SMS, default-on and persisted.
- Replaced daily email hour/minute inputs with a single HH:MM time field and updated save/load parsing.
- Ensured loading overlay blur/spinner appears on every HTML page by injecting it server-side when missing.

## Files Updated
- TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino

## Notes
- Server-down SMS uses existing SMS Primary/Secondary numbers.
- Heartbeat persistence uses server_heartbeat.json in the filesystem.
- Loading overlay injection skips pages that already include the overlay.