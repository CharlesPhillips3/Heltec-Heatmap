// Control page served by the Heltec at http://192.168.4.1
#include <Arduino.h>

extern const char CONTROL_PAGE[] PROGMEM = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<title>Heltec Heatmap</title>
<style>
:root{--bg:#0f1514;--panel:#172120;--ink:#e4ece9;--muted:#8fa19c;--line:#26332f;--accent:#3fd0b4;--rec:#ff5a4f;color-scheme:dark}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--ink);font:16px/1.45 -apple-system,system-ui,sans-serif;padding:max(14px,env(safe-area-inset-top)) max(16px,env(safe-area-inset-right)) 32px max(16px,env(safe-area-inset-left))}
h1{font-size:20px;margin:4px 0 2px;letter-spacing:.02em}
.sub{color:var(--muted);font-size:13px;margin-bottom:16px}
.card{background:var(--panel);border:1px solid var(--line);border-radius:12px;padding:14px;margin-bottom:12px}
.state{display:flex;align-items:center;gap:10px;font-weight:600}
.dot{width:12px;height:12px;border-radius:50%;background:var(--muted)}
.rec .dot{background:var(--rec);animation:p 1s infinite}
@keyframes p{50%{opacity:.3}}
.grid{display:grid;grid-template-columns:repeat(3,1fr);gap:8px;margin-top:12px;font-variant-numeric:tabular-nums}
.grid div{background:var(--bg);border-radius:8px;padding:8px;text-align:center}
.grid b{display:block;font-size:20px}
.grid span{font-size:11px;color:var(--muted);text-transform:uppercase;letter-spacing:.06em}
button,.btn{display:block;width:100%;padding:14px;border-radius:10px;border:0;font:600 16px system-ui;margin-top:10px;text-align:center;text-decoration:none;cursor:pointer}
.go{background:var(--accent);color:#06201b}
.stop{background:var(--rec);color:#fff}
.plain{background:transparent;color:var(--ink);border:1px solid var(--line)}
.danger{background:transparent;color:var(--rec);border:1px solid var(--line)}
input{width:100%;padding:12px;border-radius:10px;border:1px solid var(--line);background:var(--bg);color:var(--ink);font-size:16px}
label{font-size:13px;color:var(--muted)}
ol{padding-left:20px;margin:6px 0;color:var(--muted);font-size:14px}
li{margin:4px 0}
.net{display:flex;justify-content:space-between;gap:8px;padding:6px 0;border-bottom:1px solid var(--line);font-size:14px}
.net:last-child{border:0}
.net small{color:var(--muted)}
.r{font-variant-numeric:tabular-nums;font-weight:600}
.warn{color:#ffb547;font-size:13px}
[hidden]{display:none!important}
</style></head><body>
<h1>Heltec Heatmap</h1>
<div class="sub" id="board">Wi-Fi survey logger</div>

<div class="card" id="statecard">
  <div class="state"><span class="dot"></span><span id="state">Connecting…</span></div>
  <div class="grid">
    <div><b id="scans">–</b><span>Scans</span></div>
    <div><b id="known">–</b><span>Networks</span></div>
    <div><b id="free">–</b><span>Free KB</span></div>
  </div>
  <div class="warn" id="warn" hidden></div>
</div>

<div class="card">
  <label for="label">Name this walk (optional)</label>
  <input id="label" placeholder="e.g. Block walk">
  <button class="go" id="start">Start logging &amp; sync clock</button>
  <button class="stop" id="stop" hidden>Stop logging</button>
  <ol>
    <li>Tap <b>Start</b> here. It copies your phone's time to the board.</li>
    <li>Open the Heltec Heatmap app and tap <b>Record GPS</b>.</li>
    <li>Walk or drive. Keep the phone screen on.</li>
    <li>Come back here, tap <b>Stop</b>, then <b>Download log</b>.</li>
  </ol>
</div>

<div class="card">
  <a class="btn plain" href="/export.txt" download="heltec-heatmap-log.txt">Download log</a>
  <button class="danger" id="clear">Erase all logs</button>
  <button class="danger" id="really" hidden>Tap again to erase everything</button>
</div>

<div class="card">
  <div class="state" style="margin-bottom:6px">Hearing now</div>
  <div id="list"><div class="net"><small>Waiting for first scan…</small></div></div>
</div>

<script>
const $=id=>document.getElementById(id);
function esc(s){return s.replace(/[&<>"]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]))}
function show(s){
  $('statecard').classList.toggle('rec',s.logging);
  $('state').textContent=s.logging?(s.synced?'Logging · clock synced':'Logging · no clock sync'):'Not logging';
  $('scans').textContent=s.logging?s.sessionScans:s.totalScans;
  $('known').textContent=s.known;
  $('free').textContent=s.freeKB;
  $('board').textContent='Heltec '+s.board+' · '+s.totalKB+' KB log space';
  $('start').hidden=s.logging; $('stop').hidden=!s.logging;
  const w=$('warn');
  if(s.fsFull){w.hidden=false;w.textContent='Log space is full. Download the log, then erase it.'}
  else if(s.freeKB<200){w.hidden=false;w.textContent='Log space is getting low.'}
  else w.hidden=true;
  $('list').innerHTML=s.top.length?s.top.map(n=>'<div class="net"><span>'+(n.ssid?esc(n.ssid):'<small>(hidden)</small>')+(n.open?' <small>· open</small>':'')+'<br><small>ch '+n.ch+'</small></span><span class="r">'+n.rssi+' dBm</span></div>').join(''):'<div class="net"><small>No networks heard</small></div>';
}
async function call(p){try{const r=await fetch(p);show(await r.json())}catch(e){$('state').textContent='Lost connection to board'}}
$('start').onclick=()=>call('/api/start?t='+Date.now()+'&label='+encodeURIComponent($('label').value));
$('stop').onclick=()=>call('/api/stop');
$('clear').onclick=()=>{$('clear').hidden=true;$('really').hidden=false;setTimeout(()=>{$('clear').hidden=false;$('really').hidden=true},4000)};
$('really').onclick=async()=>{$('really').hidden=true;$('clear').hidden=false;try{const r=await fetch('/api/clear',{method:'POST'});show(await r.json())}catch(e){}};
call('/api/status');setInterval(()=>call('/api/status'),2000);
</script></body></html>)HTML";
