"""
update_html.py - Extract or inject CONFIG_GENERATOR_HTML in the .ino file.

Usage:
  python update_html.py extract   - Extract HTML to config_generator.html
  python update_html.py inject    - Inject HTML from config_generator.html back to .ino

The .ino file is the source of truth. Edit the extracted HTML file, then inject back.
"""
import sys
import os

INO_PATH = os.path.join(os.path.dirname(__file__), "TankAlarm-112025-Server-BluesOpta.ino")
HTML_PATH = os.path.join(os.path.dirname(__file__), "config_generator.html")
BLOCK_NAME = "CONFIG_GENERATOR_HTML"
START_MARKER = f"static const char {BLOCK_NAME}[]"
END_MARKER = "\nstatic const char SERIAL_MONITOR_HTML[]"


def extract():
    """Extract CONFIG_GENERATOR_HTML from .ino to a standalone HTML file."""
    with open(INO_PATH, "r", encoding="utf-8") as f:
        content = f.read()
    start = content.index(START_MARKER)
    end = content.index(END_MARKER, start)
    block = content[start:end]
    # Strip C++ wrapper: everything between R"HTML( and )HTML";
    html_start = block.index('R"HTML(') + len('R"HTML(')
    html_end = block.rindex(')HTML"')
    html = block[html_start:html_end]
    with open(HTML_PATH, "w", encoding="utf-8") as f:
        f.write(html)
    print(f"Extracted {len(html)} chars to {HTML_PATH}")


def inject():
    """Inject HTML from config_generator.html back into the .ino file."""
    with open(HTML_PATH, "r", encoding="utf-8") as f:
        html = f.read()
    with open(INO_PATH, "r", encoding="utf-8") as f:
        content = f.read()
    start = content.index(START_MARKER)
    end = content.index(END_MARKER, start)
    cpp_line = f'static const char {BLOCK_NAME}[] PROGMEM = R"HTML({html})HTML";\n'
    new_content = content[:start] + cpp_line + content[end:]
    with open(INO_PATH, "w", encoding="utf-8") as f:
        f.write(new_content)
    print(f"Injected {len(html)} chars into {INO_PATH}")


if __name__ == "__main__":
    if len(sys.argv) < 2 or sys.argv[1] not in ("extract", "inject"):
        print(__doc__)
        sys.exit(1)
    if sys.argv[1] == "extract":
        extract()
    else:
        inject()
