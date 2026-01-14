$path = "c:\Users\dorkm\Documents\GitHub\ArduinoSMSTankAlarm\TankAlarm-112025-Server-BluesOpta\TankAlarm-112025-Server-BluesOpta.ino"
$content = [System.IO.File]::ReadAllText($path)

$priorityPages = @("CLIENT_CONSOLE", "DASHBOARD", "CONTACTS_MANAGER", "SERVER_SETTINGS", "HISTORICAL_DATA")

# Regex to find HTML blocks with style
$pattern = '(?s)(static const char (\w+)_HTML\[\] PROGMEM = R"HTML\(.*?<style>(.*?)</style>)'
$matches = [Regex]::Matches($content, $pattern)

$extracted = @()
foreach ($m in $matches) {
    $extracted += @{
        Name = $m.Groups[2].Value
        Css = $m.Groups[3].Value
        Priority = 999
    }
}

# Update priority
foreach ($item in $extracted) {
    $idx = $priorityPages.IndexOf($item.Name)
    if ($idx -ge 0) { $item.Priority = $idx }
}

# Sort
$extracted = $extracted | Sort-Object Priority

$masterBlocks = @()
$seenBlocks = @{} # Hashset-like

foreach ($item in $extracted) {
    $rawRules = $item.Css -split '}'
    foreach ($rule in $rawRules) {
        $r = $rule.Trim()
        if ([string]::IsNullOrWhiteSpace($r)) { continue }
        $r = $r + "}"
        
        $norm = $r -replace '\s', ''
        if (-not $seenBlocks.ContainsKey($norm)) {
            $seenBlocks[$norm] = $true
            # Fix potential double-closing brace if split was weird, but usually split consumes delimiter.
            # We added "}" back.
            $masterBlocks += $r
        }
    }
}

$masterCss = $masterBlocks -join "`n"
$masterCss = $masterCss -replace '/\* UI STANDARDIZATION \*/', ''

# Define STYLE_CSS
$styleVarDef = "static const char STYLE_CSS[] PROGMEM = R`"CSS($masterCss)CSS`";`n`n"

# Inject STYLE_CSS
if ($content -match '(static const char SERVER_SETTINGS_HTML\[\])') {
   $content = $content.Replace("static const char SERVER_SETTINGS_HTML[]", $styleVarDef + "static const char SERVER_SETTINGS_HTML[]")
} else {
   Write-Host "Could not find insertion point for STYLE_CSS."
   exit
}

# Replace Style Tags
$newContent = [Regex]::Replace($content, $pattern, {
   param($m)
   $full = $m.Value
   $idxStart = $full.IndexOf("<style>")
   $idxEnd = $full.IndexOf("</style>") + 8
   
   $pre = $full.Substring(0, $idxStart)
   # Keep post part
   $post = $full.Substring($idxEnd)
   
   return $pre + '<link rel="stylesheet" href="/style.css">' + $post
})

# Add serveCss
$serveCssFunc = @'
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

'@

if ($newContent.Contains("static void serveFile(EthernetClient &client, const char* htmlContent) {")) {
    $newContent = $newContent.Replace("static void serveFile(EthernetClient &client, const char* htmlContent) {", $serveCssFunc + "`nstatic void serveFile(EthernetClient &client, const char* htmlContent) {")
} else {
    Write-Host "Could not find serveFile to insert serveCss."
    exit
}

# Add Route
$routeSig = 'if (method == "GET" && path.startsWith("/login")) {'
$newRouteBlock = 'if (method == "GET" && path == "/style.css") {' + "`n    serveCss(client);`n  } else " + $routeSig

if ($newContent.Contains($routeSig)) {
    $newContent = $newContent.Replace($routeSig, $newRouteBlock)
} else {
    Write-Host "Could not find route dispatcher."
    exit
}

$newContent | Set-Content -Path $path -Encoding utf8 -NoNewline
Write-Host "Success: CSS extracted."

