import os
import re

file_path = r"c:\Users\dorkm\Documents\GitHub\ArduinoSMSTankAlarm\TankAlarm-112025-Server-BluesOpta\TankAlarm-112025-Server-BluesOpta.ino"

login_html = r"""<!DOCTYPE html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1"><title>Login - Tank Alarm</title><style>:root{font-family:"Segoe UI",Arial,sans-serif;--bg:#f8fafc;--surface:#ffffff;--text:#0f172a;--muted:#475569;--card-border:rgba(15,23,42,0.08);--accent:#2563eb;--accent-contrast:#ffffff;--danger:#dc2626;}body{margin:0;min-height:100vh;display:flex;align-items:center;justify-content:center;background:var(--bg);color:var(--text);}main{background:var(--surface);padding:32px;border:1px solid var(--card-border);box-shadow:0 4px 6px -1px rgba(0,0,0,0.1);border-radius:8px;width:100%;max-width:400px;text-align:center;}h1{margin:0 0 8px;font-size:1.5rem;}p{margin:0 0 24px;color:var(--muted);font-size:0.9rem;}input{width:100%;padding:10px;margin-bottom:16px;border:1px solid var(--card-border);border-radius:4px;font-size:1rem;}button{width:100%;padding:10px;background:var(--accent);color:var(--accent-contrast);border:none;border-radius:4px;font-size:1rem;font-weight:600;cursor:pointer;}button:hover{opacity:0.9;}.error{color:var(--danger);font-size:0.9rem;margin-top:12px;display:none;}</style></head><body><main><h1>Tank Alarm Login</h1><p>Enter your PIN to access the dashboard.</p><form id="loginForm"><input type="password" id="pin" placeholder="Enter PIN" required autocomplete="current-password" pattern="\d{4}" title="4-digit PIN"><button type="submit">Login</button><div id="error" class="error">Invalid PIN</div></form></main><script>document.getElementById('loginForm').addEventListener('submit',async(e)=>{e.preventDefault();const pin=document.getElementById('pin').value;try{const res=await fetch('/api/login',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({pin})});if(res.ok){localStorage.setItem('tankalarm_token',pin);const params=new URLSearchParams(window.location.search);window.location.href=params.get('redirect')||'/';}else{document.getElementById('error').textContent='Invalid PIN';document.getElementById('error').style.display='block';}}catch(err){document.getElementById('error').textContent='Connection failed: '+err.message;document.getElementById('error').style.display='block';}});</script></body></html>"""

auth_check_js = r"const token=localStorage.getItem('tankalarm_token');if(!token){window.location.href='/login?redirect='+encodeURIComponent(window.location.pathname);return;}"

# Update CLIENT_CONSOLE logic to use the token
# Replaces loadStoredPin and logic
console_pin_patch = r"""
const token = localStorage.getItem('tankalarm_token');
if(token) return token;
"""

with open(file_path, 'r', encoding='utf-8') as f:
    lines = f.readlines()

new_lines = []
contacts_replaced = False

for line in lines:
    if line.strip().startswith('static const char CONTACTS_MANAGER_HTML[] PROGMEM ='):
        # Add LOGIN_HTML first
        new_lines.append(f'static const char LOGIN_HTML[] PROGMEM = R"HTML({login_html})HTML";\n\n')
        
        # Inject auth check into CONTACTS_MANAGER
        match = re.search(r'R\"HTML\((.*)\)HTML\"', line, re.DOTALL)
        if match:
            content = match.group(1)
            # Find the first script tag
            content = content.replace('<script>(()=>{', '<script>(()=>{' + auth_check_js)
            new_line = f'static const char CONTACTS_MANAGER_HTML[] PROGMEM = R"HTML({content})HTML";\n'
            new_lines.append(new_line)
            contacts_replaced = True
        else:
            new_lines.append(line) # Fallback
            
    elif line.strip().startswith('static const char DASHBOARD_HTML[] PROGMEM ='):
         match = re.search(r'R\"HTML\((.*)\)HTML\"', line, re.DOTALL)
         if match:
            content = match.group(1)
            # Inject auth check - look for <script>
            # DASHBOARD has <script> (() => { ...
            content = content.replace('<script> (() => {', '<script> (() => {' + auth_check_js)
            # Also need to make sure requests use the PIN/Token?
            # Dashboard doesn't modify data, only 'pause' and 'clear relays' which use state.pin
            # We need to auto-populate state.pin from localStorage token
            # state.pin = token
            content = content.replace('state.pin = null;', "state.pin = token; state.pinConfigured = true; // Auto-validated by token presence")
            
            # The pause/clear functions use state.pin. If we set it, it works.
            # But wait, state.pin is set to null initially.
            # Look for: const state = { ... pin:null, ... }
            content = content.replace('pin:null', "pin:token||null")
            
            new_line = f'static const char DASHBOARD_HTML[] PROGMEM = R"HTML({content})HTML";\n'
            new_lines.append(new_line)
         else:
             new_lines.append(line)

    elif line.strip().startswith('static const char CLIENT_CONSOLE_HTML[] PROGMEM ='):
         match = re.search(r'R\"HTML\((.*)\)HTML\"', line, re.DOTALL)
         if match:
            content = match.group(1)
            # Inject auth check
            content = content.replace('<script>(function(){', '<script>(function(){' + auth_check_js)
            
            # Patch loadStoredPin to use token
            # function loadStoredPin(){ ... }
            # We will replace the body of loadStoredPin or just rely on the token check at top?
            # The client console logic uses `pinState.value`
            # We should initialize `pinState.value` with `token`.
            
            # Find: const pinState ={value:loadStoredPin()|| null,
            # Replace with: const pinState ={value:token||null,
            content = content.replace('value:loadStoredPin()|| null', 'value:token||null')
            
            new_line = f'static const char CLIENT_CONSOLE_HTML[] PROGMEM = R"HTML({content})HTML";\n'
            new_lines.append(new_line)
         else:
             new_lines.append(line)
             
    else:
        new_lines.append(line)

with open(file_path, 'w', encoding='utf-8') as f:
    f.writelines(new_lines)

print("Firmware HTML updated successfully.")
