import re
import os

FILE_PATH = r'c:\Users\dorkm\Documents\GitHub\ArduinoSMSTankAlarm\TankAlarm-112025-Server-BluesOpta\TankAlarm-112025-Server-BluesOpta.ino'

def normalize_css(css):
    """Simple minification/normalization to help dedup"""
    # Remove comments
    css = re.sub(r'/\*.*?\*/', '', css, flags=re.DOTALL)
    # Normalize whitespace
    css = re.sub(r'\s+', ' ', css).strip()
    return css

def process_file():
    if not os.path.exists(FILE_PATH):
        print(f"Error: File not found at {FILE_PATH}")
        return

    with open(FILE_PATH, 'r', encoding='utf-8') as f:
        content = f.read()

    # 1. Capture all styles to build the master stylesheet
    # We prioritize the "big" pages to define the order
    priority_pages = ['CLIENT_CONSOLE', 'DASHBOARD', 'CONTACTS_MANAGER', 'SERVER_SETTINGS', 'HISTORICAL_DATA']
    
    style_pattern = re.compile(r'(static const char (\w+)_HTML\[\] PROGMEM = R"HTML\(.*?<style>(.*?)</style>)', re.DOTALL)
    
    extracted_styles = [] ## List of (name, css_content)
    
    for match in style_pattern.finditer(content):
        name = match.group(2)
        css = match.group(3)
        extracted_styles.append((name, css))

    # Build Master CSS
    # Strategy: Start with ClientConsole. Append non-duplicate blocks from others.
    # Because parsing CSS with regex is hard, we will do a primitive block extraction based on "}"
    
    master_blocks = []
    seen_blocks = set()

    # Sort extracted styles so priority pages come first
    def sort_key(item):
        name = item[0]
        if name in priority_pages:
            return priority_pages.index(name)
        return 999
    
    extracted_styles.sort(key=sort_key)

    for name, css_text in extracted_styles:
        # Split by '}' to get rules
        # This is a heuristic. It assumes no } inside strings like content: "}"
        raw_rules = css_text.split('}')
        for rule in raw_rules:
            rule = rule.strip()
            if not rule: 
                continue
            rule = rule + '}' # Re-add the closing brace
            
            # Normalize for comparison
            norm = re.sub(r'\s', '', rule)
            if norm not in seen_blocks:
                seen_blocks.add(norm)
                master_blocks.append(rule)
                
    master_css = '\n'.join(master_blocks)
    ## Remove any leftover comment artifacts
    master_css = re.sub(r'/\* UI STANDARDIZATION \*/', '', master_css)

    # 2. Define STYLE_CSS variable
    style_var_def = f'static const char STYLE_CSS[] PROGMEM = R"CSS({master_css})CSS";\n\n'

    # 3. Inject STYLE_CSS before SERVER_SETTINGS_HTML (usually the first one)
    if 'static const char SERVER_SETTINGS_HTML[]' in content:
        content = content.replace('static const char SERVER_SETTINGS_HTML[]', style_var_def + 'static const char SERVER_SETTINGS_HTML[]')
    else:
        print("Could not find insertion point for STYLE_CSS")
        return

    # 4. Replace <style>...</style> with <link...>
    # We use the same pattern but now we substitute
    
    def replacer(match):
        full = match.group(1) # complete definition
        name = match.group(2)
        # We replace the style tag content
        # Note: We keep the group structure
        start = full.find('<style>')
        end = full.find('</style>') + 8
        
        pre_style = full[:start]
        post_style = full[end:]
        
        # Injected link
        link_tag = '<link rel="stylesheet" href="/style.css">'
        return pre_style + link_tag + post_style

    new_content = style_pattern.sub(replacer, content)
    
    # 5. Add serveCss function
    # Search for serveFile and add serveCss after it
    if 'static void serveFile(EthernetClient &client, const char* htmlContent) {' in new_content:
        serve_css_func = """
static void serveCss(EthernetClient &client) {
  size_t cssLen = strlen_P(STYLE_CSS);
  client.println(F("HTTP/1.1 200 OK"));
  client.println(F("Content-Type: text/css"));
  client.print(F("Content-Length: "));
  client.println(cssLen);
  client.println(F("Cache-Control: public, max-age=31536000")); // Cache for 1 year
  client.println();

  const size_t bufSize = 128;
  uint8_t buffer[bufSize];
  size_t remaining = cssLen;
  const char* ptr = STYLE_CSS;

  while (remaining > 0) {
    size_t chunk = (remaining < bufSize) ? remaining : bufSize;
    for (size_t i = 0; i < chunk; i++) {
        buffer[i] = pgm_read_byte_near(ptr++);
    }
    client.write(buffer, chunk);
    remaining -= chunk;
  }
}
"""
        # Insert after serveFile closing brace
        # We need to find the specific closing brace for serveFile.
        # Simpler to insert BEFORE serveFile
        new_content = new_content.replace('static void serveFile(EthernetClient &client, const char* htmlContent) {', serve_css_func + '\nstatic void serveFile(EthernetClient &client, const char* htmlContent) {')

    # 6. Add route handler
    # Find one of the routes
    route_sig = 'if (method == "GET" && path.startsWith("/login")) {'
    new_route_block = 'if (method == "GET" && path == "/style.css") {\n    serveCss(client);\n  } else ' + route_sig
    
    if route_sig in new_content:
        new_content = new_content.replace(route_sig, new_route_block)
    else:
        print("Could not find route dispatcher")

    # Write back
    if new_content != content:
        with open(FILE_PATH, 'w', encoding='utf-8') as f:
            f.write(new_content)
        print("Success: CSS extracted and routes updated.")
    else:
        print("No changes needed.")

if __name__ == '__main__':
    process_file()
