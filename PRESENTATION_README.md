# TankAlarm 112025 Setup Presentation

This directory contains an automatically generated PowerPoint presentation that provides comprehensive setup instructions for the TankAlarm 112025 system using Arduino Opta and Blues Wireless.

## Presentation File

**File:** `TankAlarm_112025_Setup_Guide.pptx`

This PowerPoint presentation includes:

1. **System Overview** - Introduction to TankAlarm 112025
2. **Hardware Requirements** - Client and server components
3. **Arduino IDE Installation** - Setting up the development environment
4. **Required Libraries** - Installing necessary Arduino libraries
5. **Blues Notehub Configuration** - Setting up cloud connectivity
6. **Client Device Wiring** - How to wire tank monitoring devices
7. **Server Device Wiring** - How to wire the data aggregation server
8. **Upload Client Firmware** - Programming client devices
9. **Upload Server Firmware** - Programming the server device
10. **Server Web Dashboard** - Accessing the monitoring interface
11. **Adding Sensors & Alarms** - Configuring tank sensors via web interface
12. **Blues Notehub Device Management** - Managing devices in the cloud
13. **System Testing** - Verifying proper operation
14. **Ongoing Maintenance** - Regular monitoring and updates

## Screenshots Included

The presentation uses existing screenshots from:
- `TankAlarm-112025-Server-BluesOpta/screenshots/`

Screenshots show:
- Server dashboard interface
- Client configuration console
- Sensor pin setup
- SMS contact management
- Calibration interface
- Historical data view
- Server settings

Note: The viewer project has its own dashboard screenshot in `TankAlarm-112025-Viewer-BluesOpta/screenshots/`, which is not currently included in the main setup presentation as it's a separate read-only viewer component.

## Automatic Updates

The presentation is automatically regenerated when:
- Server or client `.ino` files are modified
- Screenshots are updated in the screenshots directories
- The `generate_presentation.py` script is modified

This is handled by the GitHub Action: `.github/workflows/update-presentation.yml`

### Manual Update

To manually regenerate the presentation:

```bash
python3 generate_presentation.py
```

### Force Update via GitHub Actions

You can also trigger an update through GitHub Actions:
1. Go to the Actions tab in the GitHub repository
2. Select "Update Setup Presentation" workflow
3. Click "Run workflow"
4. Check "Force update presentation even without changes" if desired
5. Click "Run workflow"

## Generating the Presentation Locally

### Requirements

- Python 3.8 or later
- Required Python packages:
  ```bash
  pip install python-pptx Pillow requests
  ```

### Steps

1. Clone the repository:
   ```bash
   git clone https://github.com/SenaxInc/ArduinoSMSTankAlarm.git
   cd ArduinoSMSTankAlarm
   ```

2. Install dependencies:
   ```bash
   pip install python-pptx Pillow requests
   ```

3. Run the generation script:
   ```bash
   python3 generate_presentation.py
   ```

4. The presentation will be created as `TankAlarm_112025_Setup_Guide.pptx`

## Customization

To customize the presentation:

1. Edit `generate_presentation.py`
2. Modify slide content, add new sections, or update screenshots
3. Run the script to generate updated presentation
4. The GitHub Action will automatically update when pushed to main/master

### Adding New Screenshots

To add new screenshots to the presentation:

1. Capture screenshots and save to appropriate directory:
   - Server screenshots: `TankAlarm-112025-Server-BluesOpta/screenshots/`
   - Client screenshots: `TankAlarm-112025-Client-BluesOpta/screenshots/`
   - Viewer screenshots: `TankAlarm-112025-Viewer-BluesOpta/screenshots/`

2. Update `generate_presentation.py` to reference new screenshots:
   ```python
   new_screenshot = str(SCREENSHOT_BASE / "path" / "to" / "screenshot.png")
   add_image_slide(prs, "Slide Title", new_screenshot, "Caption text")
   ```

3. Regenerate the presentation

## Use Cases

This presentation is useful for:

- **Installation Teams** - Step-by-step hardware and software setup
- **Training Sessions** - Teaching new users how to deploy the system
- **Documentation** - Visual reference for configuration procedures
- **Troubleshooting** - Screenshots of expected interfaces and outputs
- **Sales/Demos** - Showing system capabilities and ease of setup

## Related Documentation

For detailed text-based documentation, see:

- [Client Installation Guide](TankAlarm-112025-Client-BluesOpta/INSTALLATION.md)
- [Server Installation Guide](TankAlarm-112025-Server-BluesOpta/INSTALLATION.md)
- [Fleet Setup Guide](TankAlarm-112025-Server-BluesOpta/FLEET_SETUP.md)
- [Quick Reference Guide](QUICK_REFERENCE_FLEET_SETUP.md)

## Technical Details

### Script Features

The `generate_presentation.py` script:
- Uses `python-pptx` library for PowerPoint generation
- Automatically locates and includes screenshots
- Creates consistent slide layouts and styling
- Handles missing screenshots gracefully (creates placeholders)
- Generates 50+ slides covering complete setup process

### Slide Types

The script creates several slide types:
- **Title slides** - Section dividers
- **Content slides** - Bullet points with optional images
- **Image slides** - Full-size screenshots with captions
- **Section slides** - Major topic separators

### Styling

- **Title font**: 44pt, bold, Arduino blue (RGB: 0, 32, 96)
- **Section font**: 40pt, bold, blue (RGB: 0, 120, 215)
- **Content font**: 16-18pt, standard formatting
- **Layout**: 10" × 7.5" (standard widescreen)

## Maintenance

The presentation should be reviewed and updated when:
- New hardware components are added to the system
- Software installation procedures change
- Web interface screenshots become outdated
- New features are added to the system
- Documentation improvements are made

## Contributing

To contribute improvements to the presentation:

1. Fork the repository
2. Modify `generate_presentation.py` or add new screenshots
3. Test locally by running the script
4. Submit a pull request
5. The GitHub Action will automatically verify the changes

## License

This presentation and generation script are part of the ArduinoSMSTankAlarm project and follow the same license as the main repository.

## Support

For issues or questions:
- Open a GitHub issue
- Refer to the main project README.md
- Check Blues Wireless documentation: https://dev.blues.io
- Check Arduino Opta documentation: https://docs.arduino.cc/hardware/opta
