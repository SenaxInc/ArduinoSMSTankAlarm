$path = "c:\Users\dorkm\Documents\GitHub\ArduinoSMSTankAlarm\TankAlarm-112025-Server-BluesOpta\TankAlarm-112025-Server-BluesOpta.ino"
$content = Get-Content -Path $path -Raw

# 1. Define LOGIN_HTML
$loginHtml = '<!DOCTYPE html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1"><title>Login - Tank Alarm</title><style>:root{font-family:"Segoe UI",Arial,sans-serif;--bg:#f8fafc;--surface:#ffffff;--text:#0f172a;--muted:#475569;--card-border:rgba(15,23,42,0.08);--accent:#2563eb;--accent-contrast:#ffffff;--danger:#dc2626;}body{margin:0;min-height:100vh;display:flex;align-items:center;justify-content:center;background:var(--bg);color:var(--text);}main{background:var(--surface);padding:32px;border:1px solid var(--card-border);box-shadow:0 4px 6px -1px rgba(0,0,0,0.1);border-radius:8px;width:100%;max-width:400px;text-align:center;}h1{margin:0 0 8px;font-size:1.5rem;}p{margin:0 0 24px;color:var(--muted);font-size:0.9rem;}input{width:100%;padding:10px;margin-bottom:16px;border:1px solid var(--card-border);border-radius:4px;font-size:1rem;}button{width:100%;padding:10px;background:var(--accent);color:var(--accent-contrast);border:none;border-radius:4px;font-size:1rem;font-weight:600;cursor:pointer;}button:hover{opacity:0.9;}.error{color:var(--danger);font-size:0.9rem;margin-top:12px;display:none;}</style></head><body><main><h1>Tank Alarm Login</h1><p>Enter your PIN to access the dashboard.</p><form id="loginForm"><input type="password" id="pin" placeholder="Enter PIN" required autocomplete="current-password" pattern="\d{4}" title="4-digit PIN"><button type="submit">Login</button><div id="error" class="error">Invalid PIN</div></form></main><script>document.getElementById("loginForm").addEventListener("submit",async(e)=>{e.preventDefault();const pin=document.getElementById("pin").value;try{const res=await fetch("/api/login",{method:"POST",headers:{"Content-Type":"application/json"},body:JSON.stringify({pin})});if(res.ok){localStorage.setItem("tankalarm_token",pin);const params=new URLSearchParams(window.location.search);window.location.href=params.get("redirect")||"/";}else{document.getElementById("error").textContent="Invalid PIN";document.getElementById("error").style.display="block";}}catch(err){document.getElementById("error").textContent="Connection failed: "+err.message;document.getElementById("error").style.display="block";}});</script></body></html>'

$loginDef = 'static const char LOGIN_HTML[] PROGMEM = R"HTML(' + $loginHtml + ')HTML";'
$authCheck = "const token=localStorage.getItem('tankalarm_token');if(!token){window.location.href='/login?redirect='+encodeURIComponent(window.location.pathname);return;}"

# 2. CONTACTS_MANAGER_HTML
$pattern = '(?s)(static const char CONTACTS_MANAGER_HTML\[\] PROGMEM = R"HTML\()(.*?)(\)HTML";)'
$content = [Regex]::Replace($content, $pattern, {
    param($m) 
    $inner = $m.Groups[2].Value
    $inner = $inner -replace '<script>\(\(\)=>\{', ('<script>(()=>{' + $authCheck)
    return $loginDef + "`r`n`r`n" + 'static const char CONTACTS_MANAGER_HTML[] PROGMEM = R"HTML(' + $inner + ')HTML";'
})

# 3. DASHBOARD_HTML
$pattern = '(?s)(static const char DASHBOARD_HTML\[\] PROGMEM = R"HTML\()(.*?)(\)HTML";)'
$content = [Regex]::Replace($content, $pattern, {
    param($m) 
    $inner = $m.Groups[2].Value
    $inner = $inner -replace '<script> \(\(\) => \{', ('<script> (() => {' + $authCheck)
    $inner = $inner -replace 'state.pin = null;', "state.pin = token; state.pinConfigured = true;"
    $inner = $inner -replace 'pin:null', "pin:token||null"
    return 'static const char DASHBOARD_HTML[] PROGMEM = R"HTML(' + $inner + ')HTML";'
})

# 4. CLIENT_CONSOLE_HTML
$pattern = '(?s)(static const char CLIENT_CONSOLE_HTML\[\] PROGMEM = R"HTML\()(.*?)(\)HTML";)'
$content = [Regex]::Replace($content, $pattern, {
    param($m)
    $inner = $m.Groups[2].Value
    # Note: escape special chars in pattern for internal replace if needed, but simple strings work
    $inner = $inner -replace '<script>\(function\(\)\{', ('<script>(function(){' + $authCheck)
    $inner = $inner -replace 'value:loadStoredPin\(\)\|\| null', 'value:token||null'
    return 'static const char CLIENT_CONSOLE_HTML[] PROGMEM = R"HTML(' + $inner + ')HTML";'
})

Set-Content -Path $path -Value $content -Encoding UTF8
Write-Host "Replaced HTML content."
