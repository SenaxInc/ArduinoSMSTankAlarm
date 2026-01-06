# Setup Presentation Quick Guide

## Overview

The `TankAlarm_112025_Setup_Guide.pptx` is a comprehensive 55-slide PowerPoint presentation covering the complete setup process for TankAlarm 112025 with Arduino Opta and Blues Wireless.

## What's Included

### 📋 Topics Covered (55 slides)

1. **System Overview** (3 slides)
   - Introduction to TankAlarm 112025
   - System components
   - Architecture overview

2. **Hardware Requirements** (2 slides)
   - Client hardware (Arduino Opta + Blues + sensors)
   - Server hardware (Arduino Opta + Blues + Ethernet)

3. **Arduino IDE Installation** (2 slides)
   - IDE download and setup
   - Opta board support installation

4. **Required Libraries** (1 slide)
   - ArduinoJson v7.x
   - Blues Wireless Notecard
   - Built-in libraries

5. **Blues Notehub Configuration** (3 slides)
   - Account creation
   - Fleet setup
   - Device provisioning

6. **Client Device Wiring** (2 slides)
   - Hardware connections
   - Analog sensor wiring

7. **Server Device Wiring** (1 slide)
   - Network and power connections

8. **Upload Client Firmware** (3 slides)
   - Opening and configuring sketch
   - Compiling and uploading
   - Verification

9. **Upload Server Firmware** (3 slides)
   - Opening and configuring sketch
   - Compiling and uploading
   - Verification and IP address

10. **Server Web Dashboard** (2 slides)
    - Dashboard screenshot
    - Feature overview

11. **Adding Sensors & Alarms** (6 slides)
    - Client configuration console
    - Pin setup
    - Adding new tanks
    - Alarm configuration
    - Sending config to devices

12. **Web Interface Screenshots** (4 slides)
    - Contact management
    - Sensor calibration
    - Historical data view
    - Server settings

13. **Blues Notehub Device Management** (3 slides)
    - Device verification
    - Note traffic monitoring
    - Troubleshooting connectivity

14. **System Testing** (3 slides)
    - Sensor readings
    - Alarm functionality
    - Daily reports

15. **Ongoing Maintenance** (2 slides)
    - Regular monitoring tasks
    - Configuration updates

16. **Resources** (1 slide)
    - Documentation links
    - External resources

## How to Use

### For Presentations
1. Open `TankAlarm_112025_Setup_Guide.pptx` in PowerPoint
2. Present in order for new installations
3. Jump to specific sections for targeted training

### For Documentation
1. Print slides as handouts (6 slides per page recommended)
2. Use as visual reference during installation
3. Share PDF version with installation teams

### For Training
1. Present live during training sessions
2. Allow time for questions on each section
3. Use screenshots to demonstrate web interface features

## Auto-Update System

### How It Works
The presentation is automatically regenerated when:
- Server or client `.ino` files change
- Screenshots are updated
- The generation script is modified

### GitHub Action
- **Workflow**: `.github/workflows/update-presentation.yml`
- **Trigger**: Push to main/master or manual dispatch
- **Output**: Updated `.pptx` file committed automatically

### Manual Trigger
Via GitHub Actions UI:
1. Go to repository → Actions tab
2. Select "Update Setup Presentation"
3. Click "Run workflow"
4. Optional: Force update even without changes

## Customization

### Adding Content

Edit `generate_presentation.py`:

```python
# Add a new content slide
add_content_slide(prs, "Your Title Here", [
    "Bullet point 1",
    "Bullet point 2",
    "Bullet point 3"
])

# Add a slide with screenshot
add_image_slide(prs, "Screenshot Title", 
                "path/to/screenshot.png",
                "Optional caption text")
```

### Adding Screenshots

1. Capture new screenshots
2. Save to appropriate directory:
   - `TankAlarm-112025-Server-BluesOpta/screenshots/`
   - `TankAlarm-112025-Client-BluesOpta/screenshots/`
3. Reference in `generate_presentation.py`
4. Regenerate presentation

### Styling

Current styling:
- **Slide size**: 10" × 7.5" (widescreen)
- **Title color**: Arduino blue (RGB 0, 32, 96)
- **Section color**: Blue (RGB 0, 120, 215)
- **Font sizes**: 16-44pt depending on content type

To change styling, edit functions in `generate_presentation.py`:
- `add_title_slide()` - Title slide styling
- `add_section_slide()` - Section header styling
- `add_content_slide()` - Content slide styling

## Maintenance Schedule

### Regular Updates
- **After UI changes**: When web interface screenshots change
- **After feature additions**: When new functionality is added
- **After documentation updates**: When installation procedures change

### Review Frequency
- **Monthly**: Check if content is still current
- **Quarterly**: Full review of all slides
- **Per release**: Update with each major version

## Distribution

### Internal Use
- Keep on shared network drive
- Link in internal documentation
- Include in new employee onboarding

### External Use
- Share with installation contractors
- Provide to customers for self-installation
- Include in product documentation

### Formats
- **PowerPoint (.pptx)**: Full editing capability
- **PDF**: For read-only distribution
- **Handouts**: Print 6 slides per page for reference

## Converting to PDF

Using PowerPoint:
1. File → Save As
2. Choose PDF format
3. Options: "Optimize for Standard (publishing online and printing)"

Using command line (requires LibreOffice):
```bash
libreoffice --headless --convert-to pdf TankAlarm_112025_Setup_Guide.pptx
```

## Troubleshooting

### Presentation won't open
- Ensure you have PowerPoint or compatible software
- Try LibreOffice Impress (free alternative)
- Convert to Google Slides format

### Screenshots appear blurry
- Original screenshots are high resolution
- Ensure you're viewing at 100% zoom
- Check if PowerPoint is compressing images

### Regeneration fails
- Check Python dependencies: `pip install python-pptx Pillow`
- Verify screenshot paths in script
- Check for Python syntax errors

### GitHub Action fails
- Check workflow logs in Actions tab
- Verify Python package installation succeeded
- Check for commit permission issues

## Related Documentation

- **[PRESENTATION_README.md](PRESENTATION_README.md)** - Detailed documentation
- **[generate_presentation.py](generate_presentation.py)** - Generation script
- **[Client Installation](TankAlarm-112025-Client-BluesOpta/INSTALLATION.md)** - Text-based client guide
- **[Server Installation](TankAlarm-112025-Server-BluesOpta/INSTALLATION.md)** - Text-based server guide
- **[Fleet Setup](TankAlarm-112025-Server-BluesOpta/FLEET_SETUP.md)** - Blues Notehub configuration

## Quick Commands

Generate presentation locally:
```bash
python3 generate_presentation.py
```

Validate presentation:
```bash
python3 -c "from pptx import Presentation; prs = Presentation('TankAlarm_112025_Setup_Guide.pptx'); print(f'Slides: {len(prs.slides)}')"
```

Check file size:
```bash
ls -lh TankAlarm_112025_Setup_Guide.pptx
```

## Support

For issues or questions:
- Check [PRESENTATION_README.md](PRESENTATION_README.md) for detailed info
- Review [generate_presentation.py](generate_presentation.py) source code
- Open GitHub issue for bugs or feature requests
- Contact repository maintainers

---

**Last Updated**: January 2026  
**Presentation Version**: 112025  
**Total Slides**: 55
