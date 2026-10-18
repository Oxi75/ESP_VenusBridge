#pragma once

#include <Arduino.h>

// Shared stylesheet
static const char PAGE_CSS[] PROGMEM = R"rawliteral(
:root{--bg:#f4f5f7;--card:#fff;--fg:#1d2330;--mut:#6b7280;--acc:#1f7ae0;--ok:#16a34a;--bad:#dc2626;--bd:#e3e6eb}
@media(prefers-color-scheme:dark){:root{--bg:#14171c;--card:#1d2128;--fg:#e6e8eb;--mut:#9aa1ab;--acc:#4c9bf0;--bd:#2c313a}}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--fg);font:15px/1.45 system-ui,sans-serif}
main{max-width:720px;margin:0 auto;padding:16px}h1{font-size:20px;margin:4px 0 16px}h2{font-size:15px;margin:0 0 10px;color:var(--mut);font-weight:600}
.card{background:var(--card);border:1px solid var(--bd);border-radius:10px;padding:14px 16px;margin-bottom:14px}
.grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(150px,1fr));gap:10px}
.v{font-size:22px;font-weight:600;font-variant-numeric:tabular-nums}.l{color:var(--mut);font-size:13px}
table{width:100%;border-collapse:collapse}td{padding:4px 0;border-bottom:1px solid var(--bd)}td:last-child{text-align:right;font-variant-numeric:tabular-nums}
label{display:block;margin:10px 0 4px;color:var(--mut);font-size:13px}input,select{width:100%;padding:8px;border:1px solid var(--bd);border-radius:6px;background:var(--bg);color:var(--fg);font:inherit}
button{background:var(--acc);color:#fff;border:0;border-radius:6px;padding:9px 16px;font:inherit;cursor:pointer;margin:12px 8px 0 0}button.sec{background:var(--mut)}
a{color:var(--acc)}.ok{color:var(--ok)}.bad{color:var(--bad)}nav{display:flex;gap:16px;margin-bottom:12px}
pre{white-space:pre-wrap;word-break:break-all;font-size:12px;color:var(--mut)}
)rawliteral";

// Status page
static const char PAGE_INDEX[] PROGMEM = R"rawliteral(<!doctype html><html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>VenusBridge</title><link rel="stylesheet" href="/style.css"></head>
<body><main><h1>VenusBridge</h1><nav><a href="/">Status</a><a href="/config">Settings</a></nav>
<div class="card"><h2>Battery <span id="dev"></span></h2><div class="grid">
<div><div class="l">State of charge</div><div class="v" id="soc">–</div></div>
<div><div class="l">Battery power</div><div class="v" id="bp">–</div></div>
<div><div class="l">Grid (AC)</div><div class="v" id="grid">–</div></div>
<div><div class="l">Mode</div><div class="v" id="mode">–</div></div>
</div></div>
<div class="card"><h2>Details</h2><table id="det"></table></div>
<div class="card"><h2>System</h2><table id="sys"></table></div>
<div class="card"><h2>Last response from the battery</h2><pre id="raw"></pre></div>
</main><script>
const $=id=>document.getElementById(id);
const MODES={Auto:'Self-consumption',AI:'AI optimization',UPS:'UPS',Manual:'Manual',Passive:'Passive'};
function rows(t,r){$(t).innerHTML=r.map(x=>'<tr><td>'+x[0]+'</td><td>'+x[1]+'</td></tr>').join('')}
async function upd(){try{const s=await(await fetch('/api/status')).json(),v=s.venus;
$('dev').textContent=v.device?('· '+v.device):'';
$('soc').textContent=v.esValid?v.soc+' %':'–';
$('bp').textContent=v.esValid?v.batPower+' W':'–';
$('grid').textContent=v.esValid?v.ongridPower+' W':'–';
$('mode').textContent=v.modeValid?(MODES[v.mode]||v.mode):'–';
rows('det',[['Reachable',v.online?'<span class="ok">yes</span>':'<span class="bad">no</span>'],
['Remaining energy',v.batCapWh+' Wh'],['Charge/discharge power',v.batPower+' W'+(v.batPowerCalc?' (calculated)':'')],
['PV power',v.pvPower+' W'],['Off-grid power',v.offgridPower+' W'],
['Energy to grid',(v.gridOutWh/1000).toFixed(2)+' kWh'],['Energy from grid',(v.gridInWh/1000).toFixed(2)+' kWh'],
['CT meter total',v.emValid?v.emTotalPower+' W (A '+v.emA+' / B '+v.emB+' / C '+v.emC+')':'–'],
['Battery temperature',v.batValid?v.batTemp.toFixed(1)+' °C':'–'],['Rated capacity',v.batValid?v.ratedCapWh+' Wh':'–'],
['Firmware',v.fw||'–'],['Responses / errors',v.ok+' / '+v.err]]);
rows('sys',[['Version',s.version],['IP',s.ip],['WiFi',s.ssid+' ('+s.rssi+' dBm)'],['Access point',s.ap||'off'],
['homee connections',s.homeeClients],['Uptime',Math.floor(s.uptime/3600)+' h '+Math.floor(s.uptime%3600/60)+' min'],
['Free heap',s.heap+' B (largest block '+s.maxBlock+' B)']]);
$('raw').textContent=s.raw||'';}catch(e){}}
upd();setInterval(upd,5000);
</script></body></html>)rawliteral";

// Settings
static const char PAGE_CONFIG[] PROGMEM = R"rawliteral(<!doctype html><html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>VenusBridge</title><link rel="stylesheet" href="/style.css"></head>
<body><main><h1>VenusBridge · Settings</h1><nav><a href="/">Status</a><a href="/config">Settings</a></nav>
<form id="f"><div class="card"><h2>WiFi</h2>
<label>SSID</label><input name="ssid" maxlength="32" required>
<label>WiFi password (leave empty = unchanged)</label><input name="password" type="password" maxlength="64">
<label>Hostname</label><input name="hostname" maxlength="32">
<label>IP address</label><select name="dhcp"><option value="1">automatic (DHCP)</option><option value="0">static</option></select>
<div id="st"><label>IP</label><input name="ip"><label>Gateway</label><input name="gateway"><label>Subnet mask</label><input name="subnet"><label>DNS (optional)</label><input name="dns"></div>
</div><div class="card"><h2>Marstek Venus</h2>
<label>Battery IP address</label><input name="marstekIp" placeholder="e.g. 192.168.1.50">
<label>UDP port (Open API)</label><input name="marstekPort" type="number" min="1" max="65535">
<label>Polling interval (seconds, min. 10)</label><input name="pollInterval" type="number" min="10" max="3600">
</div><div class="card"><h2>homee</h2>
<label>virtual homee name (homee ID when pairing)</label><input name="homeeId" maxlength="32">
<label>homee device name</label><input name="nodeName" maxlength="40">
</div><div class="card"><h2>Security</h2>
<label>Admin password for saving and firmware update (leave empty = unchanged, min. 5 characters)</label><input name="adminPw" type="password" maxlength="32" autocomplete="new-password">
<label>Repeat admin password</label><input name="adminPw2" type="password" maxlength="32" autocomplete="new-password">
<div id="clr" style="display:none"><label><input name="adminPwClear" type="checkbox" style="width:auto"> Remove admin password (no protection)</label></div>
</div><div id="au" class="card" style="display:none"><h2>Authentication</h2>
<label>Current admin password (required for saving and firmware update)</label><input name="auth" type="password" maxlength="32" autocomplete="current-password">
</div><button type="submit">Save &amp; restart</button><button type="button" id="rs" class="sec">Restart</button>
<button type="button" id="dl" class="sec">Download config</button><button type="button" id="ul" class="sec">Upload config</button>
<input type="file" id="cf" accept=".json,application/json" style="display:none"></form>
<p id="msg"></p>
<div class="card" style="margin-top:14px"><h2>Firmware update</h2>
<input type="file" id="fw" accept=".bin"><button id="up" class="sec">Upload</button><span id="ups"></span></div>
</main><script>
const $=id=>document.getElementById(id),f=$('f'),st=$('st'),msg=$('msg');
function tg(){st.style.display=f.dhcp.value=='1'?'none':'block'}f.dhcp.onchange=tg;
function hdr(){return f.auth.value?{'X-Auth':encodeURIComponent(f.auth.value)}:{}}
// Fills the form fields from an object; unknown keys and passwords are ignored
function fill(c){let n=0;for(const k in c){const e=f.elements[k],v=c[k];
if(!e||!e.name||['password','checkbox','file'].includes(e.type))continue;
if(k=='dhcp'){if(typeof v!='boolean'&&v!==0&&v!==1)continue;e.value=v?'1':'0'}
else{if(typeof v!='string'&&typeof v!='number')continue;e.value=v}n++}tg();return n}
fetch('/api/config').then(r=>r.json()).then(c=>{fill(c);$('clr').style.display=c.adminPwSet?'block':'none';$('au').style.display=c.authRequired?'block':'none'});
f.onsubmit=async e=>{e.preventDefault();const p=f.adminPw.value;
if(p&&p.length<5){msg.textContent='Admin password must have at least 5 characters';return}
if(p!=f.adminPw2.value){msg.textContent='Admin passwords do not match';return}
const d=new FormData(f);d.delete('auth');d.delete('adminPw2');d.set('adminPwClear',f.adminPwClear.checked?'1':'0');
msg.textContent='Saving...';
const r=await fetch('/api/config',{method:'POST',headers:hdr(),body:new URLSearchParams(d)});
msg.textContent=r.ok?'Saved. Device is restarting...':'Error: '+await r.text();};
$('rs').onclick=async()=>{if(!confirm('Restart the bridge?'))return;
const r=await fetch('/api/restart',{method:'POST'});msg.textContent=r.ok?'Device is restarting...':'Error: '+await r.text()};
$('dl').onclick=async()=>{try{const c=await(await fetch('/api/config')).json();delete c.authRequired;delete c.adminPwSet;
try{c._version=(await(await fetch('/api/status')).json()).version}catch(e){}
const a=document.createElement('a');a.href=URL.createObjectURL(new Blob([JSON.stringify(c,null,2)],{type:'application/json'}));
a.download='venusbridge-config.json';document.body.appendChild(a);a.click();a.remove();setTimeout(()=>URL.revokeObjectURL(a.href),1000);
msg.textContent='Configuration downloaded (without passwords).'}catch(e){msg.textContent='Download failed'}};
$('ul').onclick=()=>$('cf').click();
$('cf').onchange=()=>{const fl=$('cf').files[0];if(!fl)return;const rd=new FileReader();
rd.onload=()=>{let c=null;try{c=JSON.parse(rd.result)}catch(e){}
if(!c||typeof c!='object'||Array.isArray(c))msg.textContent='Invalid file - no fields changed';
else msg.textContent=fill(c)+' values loaded - click "Save & restart" to apply them';$('cf').value=''};
rd.onerror=()=>{msg.textContent='File cannot be read - no fields changed';$('cf').value=''};rd.readAsText(fl)};
$('up').onclick=()=>{const fl=$('fw').files[0];if(!fl)return;
const d=new FormData();d.append('firmware',fl);const x=new XMLHttpRequest();x.open('POST','/update');
const h=hdr();for(const k in h)x.setRequestHeader(k,h[k]);
x.upload.onprogress=e=>{$('ups').textContent=' '+Math.round(e.loaded*100/e.total)+' %'};
x.onload=()=>{$('ups').textContent=' '+x.responseText};x.send(d)};
</script></body></html>)rawliteral";
