
import re

html_content = """<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Tank Alarm Server</title>
  <style>
    :root {
      font-family: "Segoe UI", Arial, sans-serif;
      color-scheme: light dark;
    }
    * {
      box-sizing: border-box;
    }
    body {
      margin: 0;
      min-height: 100vh;
      background: var(--bg);
      color: var(--text);
      transition: background 0.2s ease, color 0.2s ease;
    }
    body[data-theme="light"] {
      --bg: #f8fafc;
      --surface: #ffffff;
      --muted: #475569;
      --header-bg: #e2e8f0;
      --card-border: rgba(15,23,42,0.08);
      --card-shadow: rgba(15,23,42,0.08);
      --accent: #2563eb;
      --accent-strong: #1d4ed8;
      --accent-contrast: #f8fafc;
      --chip: #eceff7;
      --table-border: rgba(15,23,42,0.08);
      --pill-bg: rgba(37,99,235,0.12);
      --alarm: #b91c1c;
      --ok: #0f766e;
    }
    body[data-theme="dark"] {
      --bg: #0f172a;
      --surface: #1e293b;
      --muted: #94a3b8;
      --header-bg: #16213d;
      --card-border: rgba(15,23,42,0.55);
      --card-shadow: rgba(0,0,0,0.55);
      --accent: #38bdf8;
      --accent-strong: #22d3ee;
      --accent-contrast: #0f172a;
      --chip: rgba(148,163,184,0.15);
      --table-border: rgba(255,255,255,0.12);
      --pill-bg: rgba(56,189,248,0.18);
      --alarm: #f87171;
      --ok: #34d399;
    }
    header {
      background: var(--header-bg);
      padding: 28px 24px;
      box-shadow: 0 20px 45px var(--card-shadow);
    }
    header .bar {
      display: flex;
      justify-content: space-between;
      gap: 16px;
      flex-wrap: wrap;
      align-items: flex-start;
    }
    header h1 {
      margin: 0;
      font-size: 1.9rem;
    }
    header p {
      margin: 8px 0 0;
      color: var(--muted);
      max-width: 640px;
      line-height: 1.4;
    }
    .header-actions {
      display: flex;
      gap: 12px;
      flex-wrap: wrap;
      align-items: center;
    }
    .pill {
      border-radius: 999px;
      padding: 10px 20px;
      text-decoration: none;
      font-weight: 600;
      background: var(--pill-bg);
      color: var(--accent);
      border: 1px solid transparent;
      transition: transform 0.15s ease;
    }
    .pill.secondary {
      background: transparent;
      border-color: var(--card-border);
      color: var(--muted);
    }
    .pill:hover {
      transform: translateY(-1px);
    }
    .icon-button {
      width: 42px;
      height: 42px;
      border-radius: 50%;
      border: 1px solid var(--card-border);
      background: var(--surface);
      color: var(--text);
      font-size: 1.2rem;
      cursor: pointer;
      transition: transform 0.15s ease;
    }
    .icon-button:hover {
      transform: translateY(-1px);
    }
    .meta-grid {
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(220px, 1fr));
      gap: 12px;
      margin-top: 20px;
    }
    .meta-card {
      background: var(--surface);
      border-radius: 16px;
      border: 1px solid var(--card-border);
      padding: 16px;
      box-shadow: 0 15px 35px var(--card-shadow);
    }
    .meta-card span {
      display: block;
      font-size: 0.8rem;
      letter-spacing: 0.08em;
      text-transform: uppercase;
      color: var(--muted);
    }
    .meta-card strong {
      display: block;
      margin-top: 6px;
      font-size: 1.05rem;
      word-break: break-all;
    }
    main {
      padding: 24px;
      max-width: 1400px;
      margin: 0 auto;
      width: 100%;
    }
    .stats-grid {
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(180px, 1fr));
      gap: 16px;
      margin-bottom: 20px;
    }
    .stat-card {
      background: var(--surface);
      border-radius: 16px;
      padding: 18px;
      border: 1px solid var(--card-border);
      box-shadow: 0 12px 30px var(--card-shadow);
    }
    .stat-card span {
      font-size: 0.85rem;
      color: var(--muted);
      text-transform: uppercase;
      letter-spacing: 0.08em;
    }
    .stat-card strong {
      display: block;
      margin-top: 8px;
      font-size: 1.8rem;
    }
    .btn {
      border: none;
      border-radius: 999px;
      padding: 10px 20px;
      font-weight: 600;
      cursor: pointer;
      background: linear-gradient(135deg, var(--accent), var(--accent-strong));
      color: var(--accent-contrast);
      box-shadow: 0 18px 40px rgba(37,99,235,0.35);
      transition: transform 0.15s ease, box-shadow 0.15s ease;
    }
    .btn.secondary {
      background: transparent;
      border: 1px solid var(--card-border);
      color: var(--text);
      box-shadow: none;
    }
    .btn:disabled {
      opacity: 0.5;
      cursor: not-allowed;
      transform: none;
      box-shadow: none;
    }
    .btn:not(:disabled):hover {
      transform: translateY(-1px);
    }
    .pause-btn {
      border: 1px solid var(--card-border);
      border-radius: 999px;
      padding: 10px 16px;
      background: transparent;
      color: var(--text);
      font-weight: 700;
      cursor: pointer;
      transition: transform 0.12s ease, background 0.12s ease, color 0.12s ease;
    }
    .pause-btn:hover {
      transform: translateY(-1px);
    }
    .pause-btn.paused {
      background: #b91c1c;
      color: #fff;
      border-color: #b91c1c;
    }
    .pause-btn.paused:hover {
      background: #991b1b;
      border-color: #991b1b;
    }
    .card {
      background: var(--surface);
      border-radius: 24px;
      border: 1px solid var(--card-border);
      padding: 20px;
      box-shadow: 0 25px 55px var(--card-shadow);
    }
    .card-head {
      display: flex;
      align-items: baseline;
      justify-content: space-between;
      gap: 12px;
      flex-wrap: wrap;
    }
    .badge {
      display: inline-flex;
      align-items: center;
      gap: 6px;
      border-radius: 999px;
      padding: 6px 12px;
      background: var(--chip);
      color: var(--muted);
      font-size: 0.8rem;
      font-weight: 600;
    }
    table {
      width: 100%;
      border-collapse: collapse;
      margin-top: 18px;
    }
    th, td {
      text-align: left;
      padding: 12px 10px;
      border-bottom: 1px solid var(--table-border);
      font-size: 0.9rem;
    }
    th {
      text-transform: uppercase;
      letter-spacing: 0.05em;
      font-size: 0.75rem;
      color: var(--muted);
    }
    tr:last-child td {
      border-bottom: none;
    }
    tr.alarm {
      background: rgba(220,38,38,0.08);
    }
    body[data-theme="dark"] tr.alarm {
      background: rgba(248,113,113,0.08);
    }
    .status-pill {
      display: inline-flex;
      align-items: center;
      gap: 6px;
      border-radius: 999px;
      padding: 4px 10px;
      font-size: 0.8rem;
      font-weight: 600;
    }
    .status-pill.ok {
      background: rgba(16,185,129,0.15);
      color: var(--ok);
    }
    .status-pill.alarm {
      background: rgba(220,38,38,0.15);
      color: var(--alarm);
    }
    .timestamp {
      font-size: 0.9rem;
      color: var(--muted);
    }
    #toast {
      position: fixed;
      left: 50%;
      bottom: 24px;
      transform: translateX(-50%);
      background: #0284c7;
      color: #fff;
      padding: 12px 18px;
      border-radius: 999px;
      box-shadow: 0 10px 30px rgba(15,23,42,0.25);
      opacity: 0;
      pointer-events: none;
      transition: opacity 0.3s ease;
      font-weight: 600;
    }
    #toast.show {
      opacity: 1;
    }
  </style>
</head>
<body data-theme="light">
  <header>
    <div class="bar">
      <div>
        <p class="timestamp">Tank Alarm Fleet · Live server telemetry</p>
        <h1 id="serverName">Tank Alarm Server</h1>
        <p>
          Monitor every field unit in one place. Filter by site, highlight alarms, and jump into the client console when you need to push configuration updates.
        </p>
      </div>
      <div class="header-actions">
        <button class="icon-button" id="themeToggle" aria-label="Switch to dark mode">&#9789;</button>
        <button class="pause-btn" id="pauseBtn" aria-label="Pause data flow">Pause</button>
        <a class="pill" href="/client-console">Client Console</a>
        <a class="pill secondary" href="/config-generator">Config Generator</a>
        <a class="pill secondary" href="/contacts">Contacts</a>
        <a class="pill secondary" href="/serial-monitor">Serial Monitor</a>
        <a class="pill secondary" href="/calibration">Calibration</a>
      </div>
    </div>
    <div class="meta-grid">
      <div class="meta-card">
        <span>Server UID</span>
        <strong id="serverUid">--</strong>
      </div>
      <div class="meta-card">
        <span>Client Fleet</span>
        <strong id="fleetName">--</strong>
      </div>
      <div class="meta-card">
        <span>Next Daily Email</span>
        <strong id="nextEmail">--</strong>
      </div>
      <div class="meta-card">
        <span>Last Time Sync</span>
        <strong id="lastSync">--</strong>
      </div>
      <div class="meta-card">
        <span>Last Dashboard Refresh</span>
        <strong id="lastRefresh">--</strong>
      </div>
    </div>
  </header>
  <main>
    <div class="stats-grid">
      <div class="stat-card">
        <span>Total Clients</span>
        <strong id="statClients">0</strong>
      </div>
      <div class="stat-card">
        <span>Active Tanks</span>
        <strong id="statTanks">0</strong>
      </div>
      <div class="stat-card">
        <span>Active Alarms</span>
        <strong id="statAlarms">0</strong>
      </div>
      <div class="stat-card">
        <span>Stale Tanks (&gt;60m)</span>
        <strong id="statStale">0</strong>
      </div>
    </div>
    <section class="card">
      <div class="card-head">
        <h2 style="margin:0;">Fleet Telemetry</h2>
        <span class="timestamp">Rows update automatically while this page remains open.</span>
      </div>
      <table>
        <thead>
          <tr>
            <th>Client</th>
            <th>Site</th>
            <th>Tank</th>
            <th>Level</th>
            <th>VIN Voltage</th>
            <th>Status</th>
            <th>Updated</th>
            <th>Relay Control</th>
            <th>Refresh</th>
          </tr>
        </thead>
        <tbody id="tankBody"></tbody>
      </table>
    </section>
  </main>
  <div id="toast"></div>
  <script>
    (() => {
      const THEME_KEY = 'tankalarmTheme';
      const DEFAULT_REFRESH_SECONDS = 60;
      const STALE_MINUTES = 60;

      const els = {
        themeToggle: document.getElementById('themeToggle'),
        pauseBtn: document.getElementById('pauseBtn'),
        serverName: document.getElementById('serverName'),
        serverUid: document.getElementById('serverUid'),
        fleetName: document.getElementById('fleetName'),
        nextEmail: document.getElementById('nextEmail'),
        lastSync: document.getElementById('lastSync'),
        lastRefresh: document.getElementById('lastRefresh'),
        tankBody: document.getElementById('tankBody'),
        statClients: document.getElementById('statClients'),
        statTanks: document.getElementById('statTanks'),
        statAlarms: document.getElementById('statAlarms'),
        statStale: document.getElementById('statStale'),
        toast: document.getElementById('toast')
      };

      const state = {
        clients: [],
        tanks: [],
        refreshing: false,
        timer: null,
        uiRefreshSeconds: DEFAULT_REFRESH_SECONDS,
        paused: false,
        pin: null,
        pinConfigured: false
      };

      function applyTheme(next) {
        const theme = next === 'dark' ? 'dark' : 'light';
        document.body.dataset.theme = theme;
        els.themeToggle.textContent = theme === 'dark' ? '☀' : '☾';
        els.themeToggle.setAttribute('aria-label', theme === 'dark' ? 'Switch to light mode' : 'Switch to dark mode');
        localStorage.setItem(THEME_KEY, theme);
      }
      applyTheme(localStorage.getItem(THEME_KEY) || 'light');
      els.themeToggle.addEventListener('click', () => {
        const next = document.body.dataset.theme === 'dark' ? 'light' : 'dark';
        applyTheme(next);
      });

      els.pauseBtn.addEventListener('click', togglePause);
      els.pauseBtn.addEventListener('mouseenter', () => {
        if (state.paused) {
          els.pauseBtn.textContent = 'Resume';
        }
      });
      els.pauseBtn.addEventListener('mouseleave', () => {
        renderPauseButton();
      });

      function showToast(message, isError) {
        els.toast.textContent = message;
        els.toast.style.background = isError ? '#dc2626' : '#0284c7';
        els.toast.classList.add('show');
        setTimeout(() => els.toast.classList.remove('show'), 2500);
      }

      function formatNumber(value) {
        return (typeof value === 'number' && isFinite(value)) ? value.toFixed(1) : '--';
      }

      function formatLevel(inches) {
        if (typeof inches !== 'number' || !isFinite(inches) || inches <= 0) {
          return '';
        }
        const feet = Math.floor(inches / 12);
        const remainingInches = inches % 12;
        if (feet === 0) {
          return `${remainingInches.toFixed(1)}"`;
        }
        return `${feet}' ${remainingInches.toFixed(1)}"`;
      }

      function formatEpoch(epoch) {
        if (!epoch) return '--';
        const date = new Date(epoch * 1000);
        if (isNaN(date.getTime())) return '--';
        return date.toLocaleString(undefined, {
          year: 'numeric',
          month: 'numeric',
          day: 'numeric',
          hour: 'numeric',
          minute: '2-digit',
          hour12: true
        });
      }

      function renderPauseButton() {
        const btn = els.pauseBtn;
        if (!btn) return;
        if (state.paused) {
          btn.classList.add('paused');
          btn.textContent = 'Paused';
          btn.title = 'Paused – hover to resume';
        } else {
          btn.classList.remove('paused');
          btn.textContent = 'Pause';
          btn.title = 'Pause data flow';
        }
      }

      function describeCadence(seconds) {
        if (!seconds) return '6 h';
        if (seconds < 3600) {
          return `${Math.round(seconds / 60)} m`;
        }
        const hours = (seconds / 3600).toFixed(1).replace(/\.0$/, '');
        return `${hours} h`;
      }

      // Flatten client/tank hierarchy into rows for display.
      // VIN voltage is a per-client value (from Blues Notecard), so it's only
      // shown on the first tank row for each client to avoid redundancy.
      function flattenTanks(clients) {
        const rows = [];
        clients.forEach(client => {
          const tanks = Array.isArray(client.tanks) ? client.tanks : [];
          if (!tanks.length) {
            rows.push({
              client: client.client,
              site: client.site,
              label: client.label || 'Tank',
              tank: client.tank || '--',
              tankIdx: 0,
              levelInches: client.levelInches,
              percent: client.percent,
              alarm: client.alarm,
              alarmType: client.alarmType,
              lastUpdate: client.lastUpdate,
              vinVoltage: client.vinVoltage
            });
            return;
          }
          tanks.forEach((tank, idx) => {
            rows.push({
              client: client.client,
              site: client.site,
              label: tank.label || client.label || 'Tank',
              tank: tank.tank || '--',
              tankIdx: idx,
              levelInches: tank.levelInches,
              percent: tank.percent,
              alarm: tank.alarm,
              alarmType: tank.alarmType || client.alarmType,
              lastUpdate: tank.lastUpdate,
              vinVoltage: idx === 0 ? client.vinVoltage : null  // Only show VIN on first tank per client
            });
          });
        });
        return rows;
      }

      function formatVoltage(voltage) {
        if (typeof voltage !== 'number' || !isFinite(voltage) || voltage <= 0) {
          return '--';
        }
        return voltage.toFixed(2) + ' V';
      }

      function renderTankRows() {
        const tbody = els.tankBody;
        tbody.innerHTML = '';
        const rows = state.tanks;
        if (!rows.length) {
          const tr = document.createElement('tr');
          tr.innerHTML = '<td colspan="9">No telemetry available</td>';
          tbody.appendChild(tr);
          return;
        }
        rows.forEach(row => {
          const tr = document.createElement('tr');
          if (row.alarm) tr.classList.add('alarm');
          tr.innerHTML = `
            <td><code>${row.client || '--'}</code></td>
            <td>${row.site || '--'}</td>
            <td>${row.label || 'Tank'} #${row.tank || '?'}</td>
            <td>${formatLevel(row.levelInches)}</td>
            <td>${formatVoltage(row.vinVoltage)}</td>
            <td>${statusBadge(row)}</td>
            <td>${formatEpoch(row.lastUpdate)}</td>
            <td>${relayButtons(row)}</td>
            <td>${refreshButton(row)}</td>`;
          tbody.appendChild(tr);
        });
      }

      function statusBadge(row) {
        if (!row.alarm) {
          return '<span class="status-pill ok">Normal</span>';
        }
        return '<span class="status-pill alarm">ALARM</span>';
      }

      function relayButtons(row) {
        if (!row.client || row.client === '--') return '--';
        const escapedClient = escapeHtml(row.client);
        const tankIdx = row.tankIdx !== undefined ? row.tankIdx : 0;
        const disabled = state.refreshing ? 'disabled' : '';
        const btnStyle = 'padding:4px 8px;font-size:0.75rem;border-radius:4px;border:1px solid var(--card-border);background:var(--card-bg);cursor:pointer;margin:2px;';
        return `<button style="${btnStyle}" onclick="clearRelays('${escapedClient}', ${tankIdx})" title="Clear all relays for this tank" ${disabled}>🔕 Clear</button>`;
      }

      function refreshButton(row) {
        if (!row.client || row.client === '--') return '--';
        const escapedClient = escapeHtml(row.client);
        const disabled = state.refreshing ? 'disabled' : '';
        const opacity = state.refreshing ? 'opacity:0.4;' : '';
        return `<button class="icon-button refresh-btn" onclick="refreshTank('${escapedClient}')" title="Refresh Tank" style="width:32px;height:32px;font-size:1rem;${opacity}" ${disabled}>🔄</button>`;
      }

      function escapeHtml(unsafe) {
        if (!unsafe) return '';
        return String(unsafe)
          .replace(/&/g, '&amp;')
          .replace(/</g, '&lt;')
          .replace(/>/g, '&gt;')
          .replace(/"/g, '&quot;')
          .replace(/'/g, '&#039;');
      }

      async function refreshTank(clientUid) {
        if (state.refreshing) return;
        state.refreshing = true;
        renderTankRows();
        try {
          const res = await fetch('/api/refresh', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ client: clientUid })
          });
          if (!res.ok) {
            const text = await res.text();
            throw new Error(text || 'Refresh failed');
          }
          const data = await res.json();
          applyServerData(data);
          showToast('Tank refreshed');
        } catch (err) {
          showToast(err.message || 'Refresh failed', true);
        } finally {
          state.refreshing = false;
          renderTankRows();
        }
      }
      window.refreshTank = refreshTank;

      async function clearRelays(clientUid, tankIdx) {
        if (state.refreshing) return;
        
        // Require PIN if configured
        if (state.pinConfigured && !state.pin) {
          const pinInput = prompt('Enter admin PIN to control relays');
          if (!pinInput) return;
          state.pin = pinInput.trim();
        }
        
        state.refreshing = true;
        renderTankRows();
        try {
          const res = await fetch('/api/relay/clear', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ 
              clientUid: clientUid, 
              tankIdx: tankIdx,
              pin: state.pin || ''
            })
          });
          if (!res.ok) {
            if (res.status === 403) {
              state.pin = null;
              throw new Error('PIN required or invalid');
            }
            const text = await res.text();
            throw new Error(text || 'Clear relay failed');
          }
          showToast('Relay clear command sent');
          // Refresh data after a short delay to show updated state
          setTimeout(() => refreshData(), 1000);
        } catch (err) {
          showToast(err.message || 'Clear relay failed', true);
        } finally {
          state.refreshing = false;
          renderTankRows();
        }
      }
      window.clearRelays = clearRelays;

      function updateStats() {
        const clientIds = new Set();
        state.tanks.forEach(t => {
          if (t.client) {
            clientIds.add(t.client);
          }
        });
        els.statClients.textContent = clientIds.size;
        els.statTanks.textContent = state.tanks.length;
        els.statAlarms.textContent = state.tanks.filter(t => t.alarm).length;
        const cutoff = Date.now() - STALE_MINUTES * 60 * 1000;
        const stale = state.tanks.filter(t => !t.lastUpdate || (t.lastUpdate * 1000) < cutoff).length;
        els.statStale.textContent = stale;
      }

      function scheduleUiRefresh() {
        if (state.timer) {
          clearInterval(state.timer);
        }
        state.timer = setInterval(() => {
          refreshData();
        }, state.uiRefreshSeconds * 1000);
      }

      async function togglePause() {
        if (!state.pinConfigured) {
          // No PIN configured; allow without prompt
        } else if (!state.pin) {
          const pinInput = prompt('Enter admin PIN to toggle pause');
          if (!pinInput) {
            return;
          }
          state.pin = pinInput.trim();
        }

        const targetPaused = !state.paused;
        try {
          const res = await fetch('/api/pause', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ paused: targetPaused, pin: state.pin || '' })
          });
          if (!res.ok) {
            if (res.status === 403) {
              state.pin = null;
              throw new Error('PIN required or invalid');
            }
            const text = await res.text();
            throw new Error(text || 'Pause toggle failed');
          }
          const data = await res.json();
          state.paused = !!data.paused;
          renderPauseButton();
          showToast(state.paused ? 'Paused for maintenance' : 'Resumed');
        } catch (err) {
          showToast(err.message || 'Pause toggle failed', true);
        }
      }

      function applyServerData(data) {
        state.clients = data.clients || [];
        state.tanks = flattenTanks(state.clients);
        const serverInfo = data.server || {};
        els.serverName.textContent = serverInfo.name || 'Tank Alarm Server';
        els.serverUid.textContent = data.serverUid || '--';
        els.fleetName.textContent = serverInfo.clientFleet || 'tankalarm-clients';
        els.nextEmail.textContent = formatEpoch(data.nextDailyEmailEpoch);
        els.lastSync.textContent = formatEpoch(data.lastSyncEpoch);
        els.lastRefresh.textContent = new Date().toLocaleString(undefined, {
          year: 'numeric',
          month: 'numeric',
          day: 'numeric',
          hour: 'numeric',
          minute: '2-digit',
          hour12: true
        });
        state.paused = !!serverInfo.paused;
        state.pinConfigured = !!serverInfo.pinConfigured;
        state.uiRefreshSeconds = DEFAULT_REFRESH_SECONDS;
        renderTankRows();
        renderPauseButton();
        updateStats();
        scheduleUiRefresh();
      }

      async function refreshData() {
        try {
          const res = await fetch('/api/clients');
          if (!res.ok) {
            throw new Error('Failed to fetch fleet data');
          }
          const data = await res.json();
          applyServerData(data);
        } catch (err) {
          showToast(err.message || 'Fleet refresh failed', true);
        }
      }

      refreshData();
    })();
  </script>
</body>
</html>"""

def minify_css(css):
    css = re.sub(r'/\*.*?\*/', '', css, flags=re.DOTALL) # remove comments
    css = re.sub(r'\s+', ' ', css) # collapse whitespace
    css = re.sub(r'\s*([:;{}])\s*', r'\1', css) # remove whitespace around punctuation
    css = css.replace(';}', '}') # remove last semicolon
    return css.strip()

def minify_html(html):
    html = re.sub(r'<!--.*?-->', '', html, flags=re.DOTALL) # remove comments
    html = re.sub(r'>\s+<', '><', html) # remove whitespace between tags
    html = re.sub(r'\s+', ' ', html) # collapse whitespace
    return html.strip()

def update_js(js):
    # Replacements
    js = js.replace('client.tanks', 'client.ts')
    js = js.replace('client.client', 'client.c')
    js = js.replace('client.site', 'client.s')
    js = js.replace('client.label', 'client.n')
    js = js.replace('client.tank', 'client.k')
    js = js.replace('client.levelInches', 'client.l')
    js = js.replace('client.percent', 'client.p')
    js = js.replace('client.alarm', 'client.a')
    js = js.replace('client.alarmType', 'client.at')
    js = js.replace('client.lastUpdate', 'client.u')
    js = js.replace('client.vinVoltage', 'client.v')
    
    js = js.replace('tank.label', 'tank.n')
    js = js.replace('tank.tank', 'tank.k')
    js = js.replace('tank.levelInches', 'tank.l')
    js = js.replace('tank.percent', 'tank.p')
    js = js.replace('tank.alarm', 'tank.a')
    js = js.replace('tank.alarmType', 'tank.at')
    js = js.replace('tank.lastUpdate', 'tank.u')
    
    js = js.replace('data.clients', 'data.cs')
    js = js.replace('data.server', 'data.srv')
    js = js.replace('serverInfo.name', 'serverInfo.n')
    js = js.replace('data.serverUid', 'data.si')
    js = js.replace('serverInfo.clientFleet', 'serverInfo.cf')
    js = js.replace('data.nextDailyEmailEpoch', 'data.nde')
    js = js.replace('data.lastSyncEpoch', 'data.lse')
    js = js.replace('serverInfo.paused', 'serverInfo.ps')
    js = js.replace('serverInfo.pinConfigured', 'serverInfo.pc')
    
    # Specific replacements for request bodies
    js = js.replace("body: JSON.stringify({ client: clientUid })", "body: JSON.stringify({ c: clientUid })")
    js = js.replace("body: JSON.stringify({ paused: targetPaused, pin: state.pin || '' })", "body: JSON.stringify({ ps: targetPaused, pin: state.pin || '' })")
    js = js.replace("state.paused = !!data.paused", "state.paused = !!data.ps")
    
    return js

def minify_js(js):
    js = re.sub(r'//.*', '', js) # remove single line comments
    js = re.sub(r'/\*.*?\*/', '', js, flags=re.DOTALL) # remove block comments
    js = re.sub(r'\s+', ' ', js) # collapse whitespace
    js = re.sub(r'\s*([=+\-*/%&|<>!?:;,{}()\[\]])\s*', r'\1', js) # remove whitespace around operators
    return js.strip()

# Split content
style_start = html_content.find('<style>') + 7
style_end = html_content.find('</style>')
css_content = html_content[style_start:style_end]

script_start = html_content.find('<script>') + 8
script_end = html_content.find('</script>')
js_content = html_content[script_start:script_end]

html_before_style = html_content[:style_start-7]
html_between = html_content[style_end+8:script_start-8]
html_after = html_content[script_end+9:]

# Process
minified_css = minify_css(css_content)
updated_js = update_js(js_content)
minified_js = minify_js(updated_js)

# Reassemble
final_html = minify_html(html_before_style + '<style>' + minified_css + '</style>' + html_between + '<script>' + minified_js + '</script>' + html_after)

print(final_html)
