
import re
import os

file_path = r"c:\Users\Mike\Documents\GitHub\ArduinoSMSTankAlarm\TankAlarm-112025-Server-BluesOpta\TankAlarm-112025-Server-BluesOpta.ino"

# The new HTML/JS content
html_content = r"""<!DOCTYPE html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1"><title>Client Configuration</title><link rel="stylesheet" href="/style.css"></head><body data-theme="light"><header><div class="bar"><div class="brand">TankAlarm</div><div class="header-actions"><button class="pause-btn" id="pauseBtn" aria-label="Resume data flow" style="display:none">Unpause</button><a class="pill secondary" href="/">Dashboard</a><a class="pill secondary" href="/client-console">Client Console</a><a class="pill" href="/config-generator">Client Config</a><a class="pill secondary" href="/contacts">Contacts</a><a class="pill secondary" href="/server-settings">Server Settings</a></div></div></header><main><div class="card"><h2>Client Configuration</h2>
<div class="actions" style="margin-bottom:20px;justify-content:space-between;flex-wrap:wrap;gap:10px;">
  <div style="display:flex;gap:8px;"><button type="button" id="loadFromCloudBtn" class="secondary">Load from Cloud</button>
  <button type="button" id="importBtn" class="secondary">Import JSON</button></div>
  <div style="display:flex;gap:8px;"><button type="submit" form="generatorForm">Save to Device</button>
  <button type="button" id="downloadBtn" class="secondary">Download JSON</button></div>
</div>
<div id="configStatus" style="display:none;padding:12px;margin-bottom:16px;border-radius:var(--radius);background:#e6f2ff;border:1px solid #b3d9ff;color:#0066cc;"></div><form id="generatorForm"><div class="form-grid"><label class="field"><span>Product UID <span class="tooltip-icon" tabindex="0" data-tooltip="Blues Notehub Product UID.">?</span></span><input id="productUid" type="text" placeholder="com.company.product:project" required></label><label class="field"><span>Site Name</span><input id="siteName" type="text" placeholder="Site Name" required></label><label class="field"><span>Device Label</span><input id="deviceLabel" type="text" placeholder="Device Label" required></label><label class="field"><span>Server Fleet</span><input id="serverFleet" type="text" value="tankalarm-server"></label><label class="field"><span>Sample Minutes</span><input id="sampleMinutes" type="number" value="30" min="1" max="1440"></label><label class="field"><span>Report Time</span><input id="reportTime" type="time" value="05:00"></label><label class="field"><span>Daily Report Email Recipient</span><input id="dailyEmail" type="email"></label></div><h3>Power Configuration</h3><div class="form-grid"><label class="field"><span>Power Source<span class="tooltip-icon" tabindex="0" data-tooltip="Select the primary power source.">?</span></span><select id="powerSource" onchange="updatePowerConfigInfo()"><option value="grid">Grid-Tied (AC Power Only)</option><option value="grid_battery">Grid-Tied + Battery Backup</option><option value="solar">Solar + Battery (Basic)</option><option value="solar_mppt">Solar + Battery + MPPT (No Monitor)</option><option value="solar_modbus_mppt">Solar + Modbus MPPT (RS-485 Monitor)</option></select></label></div><div id="powerConfigInfo" style="display:none;background:var(--chip);border:1px solid var(--card-border);padding:12px;margin-bottom:16px;font-size:0.9rem;color:var(--muted);"><strong>Hardware Requirement:</strong> Modbus MPPT requires the Arduino Opta with RS-485 expansion module.</div><h3>Sensors</h3><div id="sensorsContainer"></div><div class="actions" style="margin-bottom: 24px;"><button type="button" id="addSensorBtn" class="secondary">+ Add Sensor</button></div><h3>Inputs (Buttons &amp; Switches)</h3><p style="color: var(--muted); font-size: 0.9rem; margin-bottom: 12px;">Configure physical inputs.</p><div id="inputsContainer"></div><div class="actions" style="margin-bottom: 24px;"><button type="button" id="addInputBtn" class="secondary">+ Add Input</button></div><div class="actions"><button type="submit" id="sendConfigBtn">Save Configuration to Device</button></div></form></div></main><div id="toast"></div><div id="pinModal" class="modal hidden"><div class="modal-card"><div class="modal-badge" id="pinSessionBadge">Locked</div><h2 id="pinModalTitle">Enter Admin PIN</h2><p id="pinModalDescription">Enter the admin PIN.</p><form id="pinForm"><label class="field"><span>PIN</span><input type="password" id="pinInput" inputmode="numeric" pattern="\d*" maxlength="4" autocomplete="off" required placeholder="4 digits"></label><div class="actions"><button type="submit" id="pinSubmit">Unlock</button><button type="button" class="secondary" id="pinCancel">Cancel</button></div></form></div></div><div id="selectClientModal" class="modal hidden"><div class="modal-content"><div class="modal-header"><h2>Load Configuration</h2><button class="modal-close" onclick="closeSelectClientModal()">&times;</button></div><div id="clientList" class="client-list"><div class="empty-state">Loading clients...</div></div></div></div><input type="file" id="importFileInput" accept=".json" style="display:none;" />
<script>
(() => {
const token=localStorage.getItem('tankalarm_token');if(!token){window.location.href='/login?redirect='+encodeURIComponent(window.location.pathname);return;}
const state={pin:token,pinConfigured:false,paused:false,clients:[]};
const els={toast:document.getElementById('toast'),form:document.getElementById('generatorForm'),productUid:document.getElementById('productUid'),clientList:document.getElementById('clientList'),selectClientModal:document.getElementById('selectClientModal'),loadFromCloudBtn:document.getElementById('loadFromCloudBtn'),
siteName:document.getElementById('siteName'),deviceLabel:document.getElementById('deviceLabel'),serverFleet:document.getElementById('serverFleet'),sampleMinutes:document.getElementById('sampleMinutes'),dailyEmail:document.getElementById('dailyEmail'),reportTime:document.getElementById('reportTime'),powerSource:document.getElementById('powerSource')};
function showToast(m,e){if(els.toast){els.toast.innerText=m;els.toast.style.background=e?'#dc2626':'#0284c7';els.toast.classList.add('show');setTimeout(()=>els.toast.classList.remove('show'),3000);}}

/* PIN LOGIC */
let pinMode='unlock';let pendingAction=null;
const pinEls={modal:document.getElementById('pinModal'),form:document.getElementById('pinForm'),input:document.getElementById('pinInput'),cancel:document.getElementById('pinCancel')};
function showPinModal(mode,cb){pinMode=mode;pendingAction=cb;if(pinEls.form)pinEls.form.reset();if(pinEls.modal)pinEls.modal.classList.remove('hidden');if(pinEls.input)pinEls.input.focus();}
function hidePinModal(){if(pinEls.modal)pinEls.modal.classList.add('hidden');pendingAction=null;}
function requestPin(cb){if(state.pinConfigured&&!state.pin){showPinModal('unlock',cb);}else{cb(state.pin);}}
if(pinEls.form)pinEls.form.addEventListener('submit',async(e)=>{e.preventDefault();const p=pinEls.input.value.trim();if(p.length!==4){showToast('Invalid PIN',true);return;}state.pin=p;hidePinModal();if(pendingAction){pendingAction(p);}});
if(pinEls.cancel)pinEls.cancel.addEventListener('click',hidePinModal);

/* PAUSE LOGIC (Shortened) */
const pauseBtn=document.getElementById('pauseBtn');
function renderPauseBtn(){if(pauseBtn){pauseBtn.style.display=state.paused?'':'none';}}
async function loadPauseState(){try{const r=await fetch('/api/clients?summary=1');const d=await r.json();if(d&&d.srv){state.paused=!!d.srv.ps;state.pinConfigured=!!d.srv.pc;renderPauseBtn();}}catch(e){}}
if(pauseBtn)pauseBtn.addEventListener('click',()=>{requestPin(async(pin)=>{try{const r=await fetch('/api/pause',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({paused:false,pin:pin})});if(r.ok){state.paused=false;renderPauseBtn();showToast('Resumed');}else throw new Error('Failed');}catch(e){showToast('Error resuming',true);}});});
loadPauseState();

/* CONFIG GENERATOR CORE */
const sensorTypes=[{value:0,label:'Digital Input'},{value:1,label:'Analog Input'},{value:2,label:'4-20mA'},{value:3,label:'RPM'}];
const monitorTypes=[{value:'tank',label:'Tank Level'},{value:'gas',label:'Gas Pressure'},{value:'rpm',label:'RPM'}];
let sensorCount=0;let inputIdCounter=0;

window.updatePowerConfigInfo=function(){const ps=document.getElementById('powerSource').value;const i=document.getElementById('powerConfigInfo');if(i)i.style.display=(ps==='solar_modbus_mppt')?'block':'none';};

function createSensorHtml(id){
  return `<div class="sensor-card" id="sensor-${id}"><div class="sensor-header"><span class="sensor-title">Sensor #${id+1}</span><button type="button" class="remove-btn" onclick="removeSensor(${id})">Remove</button></div>
  <div class="form-grid">
  <label class="field"><span>Monitor Type</span><select class="monitor-type" onchange="updateMonitorFields(${id})">${monitorTypes.map(t=>`<option value="${t.value}">${t.label}</option>`).join('')}</select></label>
  <label class="field tank-num-field"><span class="tank-num-label">Tank #</span><input type="number" class="tank-num" value="${id+1}"></label>
  <label class="field"><span class="name-label">Name</span><input type="text" class="tank-name"></label>
  <label class="field contents-field"><span>Contents</span><input type="text" class="tank-contents"></label>
  <label class="field"><span>Sensor Type</span><select class="sensor-type" onchange="updateFields(${id})">${sensorTypes.map(t=>`<option value="${t.value}">${t.label}</option>`).join('')}</select></label>
  <label class="field"><span>Pin</span><select class="sensor-pin"><option value="0">I1</option><option value="1">I2</option><option value="2">I3</option><option value="3">I4</option><option value="4">I5</option><option value="5">I6</option><option value="6">I7</option><option value="7">I8</option></select></label>
  <label class="field height-field"><span class="height-label">Height(in)</span><input type="number" class="tank-height" value="120"></label>
  </div>
  <div class="collapsible-section alarm-section visible"><h4 style="margin:10px 0;">Alarms</h4><div class="form-grid">
  <label class="field"><input type="checkbox" class="high-alarm-enabled" checked> High Alarm <input type="number" class="high-alarm" value="100"></label>
  <label class="field"><input type="checkbox" class="low-alarm-enabled" checked> Low Alarm <input type="number" class="low-alarm" value="20"></label>
  </div></div>
  <div class="collapsible-section sms-section" style="margin-top:10px;"><h4 style="margin:10px 0;">SMS Alerts</h4><div class="form-grid">
  <label class="field"><span>Phone Numbers</span><input type="text" class="sms-phones" placeholder="+1555..."></label>
  </div></div>
  </div>`;
}

window.updateMonitorFields=function(id){const card=document.getElementById(`sensor-${id}`);const type=card.querySelector('.monitor-type').value;const numField=card.querySelector('.tank-num-field');const numFieldLabel=card.querySelector('.tank-num-label');const nameLabel=card.querySelector('.name-label');const heightLabel=card.querySelector('.height-label');const contentsField=card.querySelector('.contents-field');if(type==='gas'){numField.style.display='none';nameLabel.textContent='System Name';heightLabel.textContent='Max Pressure(PSI)';contentsField.style.display='flex';}else if(type==='rpm'){numField.style.display='flex';numFieldLabel.textContent='Engine #';nameLabel.textContent='Engine Name';heightLabel.textContent='Max RPM';contentsField.style.display='none';}else{numField.style.display='flex';numFieldLabel.textContent='Tank #';nameLabel.textContent='Name';heightLabel.textContent='Height(in)';contentsField.style.display='flex';}};
window.updateFields=function(id){};
window.removeSensor=function(id){const e=document.getElementById(`sensor-${id}`);if(e)e.remove();};
window.removeInput=function(id){const e=document.getElementById(`input-${id}`);if(e)e.remove();};
function addSensor(){
  const c=document.getElementById('sensorsContainer');const d=document.createElement('div');
  d.innerHTML=createSensorHtml(sensorCount++);c.appendChild(d.firstElementChild);
  // Re-populate pins/types logic would go here
}
document.getElementById('addSensorBtn').addEventListener('click',addSensor);

/* INPUTS LOGIC */
function addInput(){
  const c=document.getElementById('inputsContainer');const d=document.createElement('div');
  d.className='sensor-card';d.id=`input-${inputIdCounter}`;
  d.innerHTML=`<div class="card-header"><h4>Input ${inputIdCounter+1}</h4><button type="button" class="remove-btn" onclick="removeInput(${inputIdCounter})">Remove</button></div>
  <div class="form-grid"><label class="field">Name <input type="text" class="input-name" value="Clear Button"></label>
  <label class="field">Pin <input type="number" class="input-pin" value="0"></label>
  <label class="field">Mode <select class="input-mode"><option value="active_low">Active LOW</option><option value="active_high">Active HIGH</option></select></label>
  <label class="field">Action <select class="input-action"><option value="clear_relays">Clear Alarms</option></select></label></div>`;
  c.appendChild(d); inputIdCounter++;
}
document.getElementById('addInputBtn').addEventListener('click',addInput);

/* COLLECTION LOGIC */
function collectConfig(){
  const sMinutes=parseInt(document.getElementById('sampleMinutes').value)||30;
  const time=document.getElementById('reportTime').value.split(':');
  const ps=document.getElementById('powerSource').value;
  const cfg={
    productUid:document.getElementById('productUid').value.trim(),
    site:document.getElementById('siteName').value.trim(),
    deviceLabel:document.getElementById('deviceLabel').value.trim(),
    serverFleet:document.getElementById('serverFleet').value.trim(),
    sampleSeconds:sMinutes*60,
    reportHour:parseInt(time[0]||5),
    reportMinute:parseInt(time[1]||0),
    dailyEmail:document.getElementById('dailyEmail').value.trim(),
    powerSource:ps,
    solarPowered:ps.includes('solar'),
    mpptEnabled:ps.includes('mppt'),
    solarCharger:{enabled:ps==='solar_modbus_mppt'},
    tanks:[]
  };
  document.querySelectorAll('#sensorsContainer .sensor-card').forEach((c,i)=>{
    const type=parseInt(c.querySelector('.sensor-type').value);
    const pin=parseInt(c.querySelector('.sensor-pin').value);
    const tank={
      id:String.fromCharCode(65+i),
      number:parseInt(c.querySelector('.tank-num').value)||i+1,
      name:c.querySelector('.tank-name').value,
      contents:c.querySelector('.tank-contents').value,
      sensor:type===0?'digital':(type===2?'current':(type===3?'rpm':'analog')),
      primaryPin:parseInt(c.querySelector('.sensor-pin').value),
      maxValue:parseFloat(c.querySelector('.tank-height').value)||120,
      daily:true,upload:true,alarmSms:false
    };
    if(c.querySelector('.high-alarm-enabled').checked) tank.highAlarm=parseFloat(c.querySelector('.high-alarm').value);
    if(c.querySelector('.low-alarm-enabled').checked) tank.lowAlarm=parseFloat(c.querySelector('.low-alarm').value);
    const phones=c.querySelector('.sms-phones').value;
    if(phones) tank.smsAlert={phones:phones.split(','),trigger:'any',message:'Alarm'};
    if(tank.highAlarm||tank.lowAlarm||phones) tank.alarmSms=true;
    cfg.tanks.push(tank);
  });
  // Inputs
  let clrBtn=false;
  document.querySelectorAll('#inputsContainer .sensor-card').forEach(c=>{
    if(c.querySelector('.input-action').value==='clear_relays'){
      cfg.clearButtonPin=parseInt(c.querySelector('.input-pin').value);
      cfg.clearButtonActiveHigh=c.querySelector('.input-mode').value==='active_high';
      clrBtn=true;
    }
  });
  if(!clrBtn){cfg.clearButtonPin=-1;cfg.clearButtonActiveHigh=false;}
  return cfg;
}

/* SUBMIT LOGIC */
async function submitConfig(e){
  e.preventDefault();
  requestPin(async(pin)=>{
    try{
      const cfg=collectConfig();
      const res=await fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({pin:pin,config:cfg})});
      if(res.ok){showToast('Configuration saved to device');}
      else{const t=await res.text();showToast('Error: '+t,true);}
    }catch(err){showToast('Error: '+err.message,true);}
  });
}
if(els.form) els.form.addEventListener('submit',submitConfig);

document.getElementById('downloadBtn').addEventListener('click',()=>{
  const cfg=collectConfig();
  const blob=new Blob([JSON.stringify(cfg,null,2)],{type:'application/json'});
  const url=URL.createObjectURL(blob);
  const a=document.createElement('a');a.href=url;a.download='client_config.json';
  document.body.appendChild(a);a.click();document.body.removeChild(a);
});

/* LOAD LOGIC */
window.loadConfig=function(c){
  if(els.siteName)els.siteName.value=c.site||'';
  if(els.deviceLabel)els.deviceLabel.value=c.deviceLabel||c.label||'';
  if(els.productUid)els.productUid.value=c.productUid||'';
  if(els.sampleMinutes)els.sampleMinutes.value=(c.sampleSeconds||1800)/60;
  if(els.dailyEmail)els.dailyEmail.value=c.dailyEmail||'';
  const h=String(c.reportHour||5).padStart(2,'0');
  const m=String(c.reportMinute||0).padStart(2,'0');
  if(els.reportTime)els.reportTime.value=`${h}:${m}`;
  if(els.powerSource)els.powerSource.value=c.powerSource||'grid';
  updatePowerConfigInfo();
  
  document.getElementById('sensorsContainer').innerHTML='';sensorCount=0;
  if(c.tanks) c.tanks.forEach(t=>{
    addSensor();const card=document.getElementById(`sensor-${sensorCount-1}`);
    if(card){
      card.querySelector('.tank-num').value=t.number;
      card.querySelector('.tank-name').value=t.name||'';
      card.querySelector('.tank-contents').value=t.contents||'';
      card.querySelector('.tank-height').value=t.maxValue||120;
      card.querySelector('.sensor-type').value=t.sensor==='digital'?0:(t.sensor==='current'?2:(t.sensor==='rpm'?3:1));
      card.querySelector('.sensor-pin').value=t.primaryPin||0;
      if(t.highAlarm!==undefined) card.querySelector('.high-alarm').value=t.highAlarm;
      if(t.lowAlarm!==undefined) card.querySelector('.low-alarm').value=t.lowAlarm;
      if(t.smsAlert&&t.smsAlert.phones) card.querySelector('.sms-phones').value=t.smsAlert.phones.join(',');
    }
  });
  showToast('Configuration loaded');
};

/* CLOUD LOAD */
function closeSelectClientModal(){els.selectClientModal.classList.add('hidden');}
window.closeSelectClientModal=closeSelectClientModal;
els.loadFromCloudBtn.addEventListener('click',async()=>{
  els.selectClientModal.classList.remove('hidden');
  els.clientList.innerHTML='Loading...';
  try{
    const r=await fetch('/api/clients?summary=1');const d=await r.json();
    els.clientList.innerHTML=d.clients.map(c=>`<div class="client-item" onclick="fetchClientConfig('${c.client}')"><strong>${c.label||c.client}</strong> (${c.client})<br>${c.site||''}</div>`).join('');
  }catch(e){els.clientList.innerHTML='Error loading clients';}
});
window.fetchClientConfig=async(uid)=>{
  closeSelectClientModal();
  try{
    const r=await fetch('/api/client?uid='+encodeURIComponent(uid));
    if(!r.ok)throw new Error('Failed');
    const c=await r.json();
    if(c.config) loadConfig(c.config);
    else showToast('No config found for client',true);
  }catch(e){showToast('Error loading config',true);}
};

/* IMPORT LOGIC */
const importInput=document.getElementById('importFileInput');
document.getElementById('importBtn').addEventListener('click',()=>importInput.click());
importInput.addEventListener('change',(e)=>{
  const f=e.target.files[0];if(!f)return;
  const r=new FileReader();
  r.onload=(evt)=>{try{loadConfig(JSON.parse(evt.target.result));}catch(err){showToast('Invalid JSON',true);}};
  r.readAsText(f);
});

async function basicInit(){
  // Auto load UID from server settings if available
  try{const r=await fetch('/api/clients?summary=1');const d=await r.json();if(d&&d.srv&&d.srv.pu)els.productUid.value=d.srv.pu;}catch(e){}
  addSensor();
}
basicInit();

})();
</script></body></html>"""

# Helper to chunk the string
chunks = [html_content[i:i+8000] for i in range(0, len(html_content), 8000)]
cpp_parts = ['R"HTML(' + c + ')HTML"' for c in chunks]
new_cpp_val = " ".join(cpp_parts) + ";"

# Read and Replace
with open(file_path, 'r', encoding='utf-8') as f:
    text = f.read()

# Pattern matching start of CONFIG_GENERATOR_HTML to just before start of SERIAL_MONITOR_HTML
# We handle the possibility of white space
# Note: we use MULTILINE to match line starts if needed, but here simple find is better since regex on 500KB string is slow or tricky.
start_marker = 'static const char CONFIG_GENERATOR_HTML[] PROGMEM = R"HTML(<!DOCTYPE html>'
end_marker = 'static const char SERIAL_MONITOR_HTML'

start_idx = text.find(start_marker)
if start_idx == -1:
    print("Error: Could not find start marker")
    exit(1)

end_idx = text.find(end_marker, start_idx)
if end_idx == -1:
    print("Error: Could not find end marker")
    exit(1)

# Find the ACTUAL end of the CONFIG_GENERATOR_HTML definition which is the semicolon before SERIAL_MONITOR_HTML
# We search backwards from end_marker for the semicolon
# But wait, there might be blank lines.
# We just replace everything from start_marker up to end_marker with our new definition + \n\n
new_text = text[:start_idx] + 'static const char CONFIG_GENERATOR_HTML[] PROGMEM = ' + new_cpp_val + '\n\n' + text[end_idx:]

with open(file_path, 'w', encoding='utf-8') as f:
    f.write(new_text)

print("Successfully updated CONFIG_GENERATOR_HTML")
