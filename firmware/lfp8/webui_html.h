// Single-page dashboard served at "/". Everything is embedded (no CDN), so it
// works offline on an isolated LAN. The page aggregates all boards it finds
// via GET /api/peers and polls each board's /api/status directly (CORS *).
#pragma once
#if defined(ARDUINO)
#include <pgmspace.h>
#else
#define PROGMEM
#endif

static const char INDEX_HTML[] PROGMEM = R"LFP8HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>LFP-8 Cell Tester</title>
<style>
:root{--bg:#f3f4f6;--fg:#1d2330;--mut:#667085;--card:#fff;--line:#d9dde3;--acc:#1f6feb;--ok:#1a7f37;--warn:#b26b00;--bad:#c62828;--chg:#1f6feb;--dis:#8250df;--rest:#6e7781;--v:#1f6feb;--i:#d4580a}
@media (prefers-color-scheme:dark){:root{--bg:#0f1218;--fg:#e6e9ef;--mut:#98a2b3;--card:#171b23;--line:#2a303b;--acc:#4c8dff;--ok:#3fb950;--warn:#d29922;--bad:#f85149;--chg:#4c8dff;--dis:#a371f7;--rest:#8b949e;--v:#58a6ff;--i:#f0883e}}
*{box-sizing:border-box}body{margin:0;font:14px/1.4 system-ui,-apple-system,"Segoe UI",Roboto,sans-serif;background:var(--bg);color:var(--fg)}
header{position:sticky;top:0;z-index:5;display:flex;flex-wrap:wrap;gap:6px;align-items:center;padding:8px 12px;background:var(--card);border-bottom:1px solid var(--line)}
header .t{font-weight:700;font-size:16px;margin-right:10px}nav{display:flex;flex-wrap:wrap;gap:4px}
nav button.on{background:var(--acc);color:#fff;border-color:var(--acc)}#conn{margin-left:auto;color:var(--mut)}
main{padding:12px;max-width:1600px;margin:auto}section{display:none}section.on{display:block}
h3{margin:14px 0 8px;font-size:15px}
.card{background:var(--card);border:1px solid var(--line);border-radius:8px;padding:10px}
.boards{display:grid;grid-template-columns:repeat(auto-fill,minmax(340px,1fr));gap:10px;margin-bottom:12px}
.grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(235px,1fr));gap:8px}
.hd{display:flex;justify-content:space-between;align-items:center;gap:6px;margin-bottom:4px}
.badge{font-size:11px;font-weight:600;padding:2px 7px;border-radius:10px;color:#fff;background:var(--rest);white-space:nowrap}
.kv{display:grid;grid-template-columns:auto 1fr auto 1fr;gap:1px 8px;font-variant-numeric:tabular-nums}
.kv>span:nth-child(odd){color:var(--mut)}
.btns{display:flex;flex-wrap:wrap;gap:4px;margin-top:6px}
button{font:inherit;padding:3px 9px;border:1px solid var(--line);border-radius:5px;background:var(--bg);color:var(--fg);cursor:pointer}
button:hover{border-color:var(--acc)}button.pri{background:var(--acc);color:#fff;border-color:var(--acc)}button.dan{color:var(--bad);border-color:var(--bad)}
input,select{font:inherit;padding:3px 6px;border:1px solid var(--line);border-radius:5px;background:var(--bg);color:var(--fg)}
input[type=number]{width:100px}
table{border-collapse:collapse;width:100%;font-variant-numeric:tabular-nums}th,td{padding:4px 6px;border-bottom:1px solid var(--line);text-align:right;white-space:nowrap}
th{color:var(--mut);font-weight:600}th:first-child,td:first-child{text-align:left}
.alarm{color:var(--bad);font-weight:600}.ok{color:var(--ok)}.warn{color:var(--warn)}.mut{color:var(--mut)}.bad{color:var(--bad)}
canvas{display:block;width:100%;height:380px;background:var(--card);border:1px solid var(--line);border-radius:8px}
.msg{font-size:12px;color:var(--mut);min-height:1.3em;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.row{display:flex;flex-wrap:wrap;gap:8px;align-items:center;margin:6px 0}
fieldset{border:1px solid var(--line);border-radius:8px;margin:0 0 12px;padding:8px 12px}legend{color:var(--mut)}
label.f{display:grid;grid-template-columns:230px 130px 1fr;gap:8px;align-items:center;margin:3px 0}
.bar{height:8px;background:var(--acc);border-radius:4px}
#toast{position:fixed;bottom:14px;right:14px;max-width:420px;padding:8px 12px;border-radius:6px;background:var(--fg);color:var(--bg);display:none;z-index:9}
.scroll{overflow-x:auto}.note{color:var(--mut);font-size:13px}code{font-size:12px}
</style></head><body>
<header><span class="t">LFP-8 Cell Tester</span>
<nav id="nav"><button data-t="dash" class="on">Cells</button><button data-t="chart">Chart</button><button data-t="ir">Resistance</button><button data-t="pack">Pack builder</button><button data-t="export">Export</button><button data-t="set">Settings</button><button data-t="ota">Update</button></nav>
<span id="conn">connecting...</span><button class="dan" id="stopAll" title="Stop every job on every board">Stop all</button></header>
<main>
<section id="t-dash" class="on"><div class="boards" id="boards"></div><div class="grid" id="cells"></div></section>

<section id="t-chart"><div class="row"><label>Cell <select id="chSel"></select></label><button id="chLoad" class="pri">Load</button>
<label><input type="checkbox" id="chAuto" checked> auto-refresh (30 s)</label><span id="chInfo" class="mut"></span></div>
<canvas id="chart"></canvas><div class="msg" id="chHover"></div>
<p class="note">One averaged sample per 30 s, 24 h per channel, stored on the board (<code>/api/history?ch=k</code>). Blue: voltage (left axis), orange: current (right axis, &gt;0 = charging).</p></section>

<section id="t-ir"><h3>Measured internal resistance</h3><div class="row"><button id="irAll" class="pri">Measure IR on all idle cells</button><span class="note">IR pulses are serialised per board (~7 s per cell incl. 5 s rest).</span></div>
<div class="scroll card"><table id="irTab"><thead><tr><th>Cell</th><th>State</th><th>V</th><th>R ohmic 10 ms (m&Omega;)</th><th>R DC 1 s (m&Omega;)</th><th>I pulse (A)</th><th>Pulse</th><th>V0 (V)</th><th>B- path (m&Omega;)</th><th></th></tr></thead><tbody></tbody></table></div>
<h3>Last IR pulse</h3><div class="row"><label>Board <select id="trSel"></select></label><button id="trLoad">Load trace</button><span id="trInfo" class="mut"></span></div>
<canvas id="trace" style="height:300px"></canvas>
<h3>Resistance calculator</h3><div class="card"><div class="row">
<label>V0 (V) <input type="number" step="0.0001" id="cV0" value="3.3000"></label><label>I0 (A) <input type="number" step="0.001" id="cI0" value="0"></label>
<label>V1 (V) <input type="number" step="0.0001" id="cV1" value="3.2874"></label><label>I1 (A) <input type="number" step="0.001" id="cI1" value="-1.05"></label>
<b id="cR"></b></div>
<p class="note">R = (V1 &minus; V0) / (I1 &minus; I0). With the board's sign convention (I &gt; 0 charging, I &lt; 0 discharging) both a discharge pulse (&Delta;V &lt; 0, &Delta;I &lt; 0) and a charge pulse (&Delta;V &gt; 0, &Delta;I &gt; 0) give a positive resistance.</p>
<p class="note"><b>Method (INTERFACE.md &sect;7.1):</b> charger and load off, rest &ge; 5 s; V0/I0 = average of 16 samples with the multiplexer locked on the cell. The 2.75 &Omega; load (&asymp;1.1 A) is switched on and V and I are sampled alternately at 860 SPS for 1 s.
<b>R ohmic</b> uses V and I at 10 ms (electronic + contact + electrolyte resistance, close to an AC-IR reading). <b>R DC</b> uses the values at 1 s (IEC 61960-style DC-IR; adds the fast charge-transfer polarisation). Below 2.65 V the hardware UV backstop blocks the load, so a charge pulse (ISET 4, &asymp;1 A) is used instead.
Values at 10 ms and 1 s are taken from a least-squares line through the samples around that time. <b>B- path</b> is the wire + contact resistance of the cell's negative connection, estimated during charge/discharge from AIN1&minus;AIN3 = I&middot;(R + 2&times;28 m&Omega; + 0.1 &Omega;); above 300 m&Omega; it is flagged CONTACT_WARN (clean/tighten the holder, warning only). Typical healthy LFP 15 Ah cells: R ohmic 3&ndash;10 m&Omega;; compare cells measured on the same board and at similar state of charge (capacity tests measure IR automatically at 50 % SoC).</p></div></section>

<section id="t-pack"><div class="card"><div class="row">
<label>S <input type="number" id="pS" value="4" min="1" max="32"></label><label>P <input type="number" id="pP" value="10" min="1" max="64"></label>
<span id="pPre"></span>
<label>Selection <select id="pSel"><option value="top">highest capacity</option><option value="tight">tightest capacity window</option></select></label>
<label>IR weight <input type="number" id="pW" value="0.1" step="0.05" min="0" max="10"></label>
<button class="pri" id="pBuild">Build pack</button><button id="pCsv">CSV</button></div>
<div class="note" id="pAvail"></div></div>
<div id="pOut"></div>
<p class="note">Cells with a measured capacity (discharge or capacity test) are used. Groups are filled by a snake draft on capacity and then improved by pairwise swaps that minimise the spread of group capacity (primary) and of group resistance (secondary, weight above). The pack capacity is limited by the weakest group.</p></section>

<section id="t-export"><div class="card"><div class="row"><button class="pri" id="csvAll">Download CSV (all boards)</button></div><div id="csvLinks" class="row"></div>
<p class="note">Columns: board, ch, cell (global number), state, program, step, V, I, T, capacity (mAh, mWh, s), termination, charge input, storage input, DC-IR, ohmic IR, IR current, V0, pulse type, elapsed, message.</p></div></section>

<section id="t-set"><p class="note">Settings of <b>this</b> board (<span id="sHost"></span>). Other boards: open their own page. All values are clamped to safe ranges by the firmware.</p>
<fieldset><legend>Network</legend>
<label class="f">WiFi SSID <input id="s_wifi_ssid"></label>
<label class="f">WiFi password <input id="s_wifi_pass" type="password" placeholder="(unchanged)"></label>
<label class="f">Board id (1&ndash;5) <input id="s_board_id" type="number" min="1" max="32"><span class="note">cells are numbered (id&minus;1)&middot;8+k</span></label>
<label class="f">Admin password <input id="s_admin_pass" type="password" placeholder="(unchanged)"><span class="note">protects settings and OTA when set (user: admin)</span></label>
</fieldset>
<fieldset><legend>Test policy</legend><div id="sPol"></div></fieldset>
<fieldset><legend>Calibration</legend><div class="scroll"><table id="calTab"><thead><tr><th>Ch</th><th>CAL_V</th><th>CAL_I</th><th>V now</th><th>I now</th><th>Reference V</th><th></th><th>Reference I (A)</th><th></th></tr></thead><tbody></tbody></table></div>
<div class="row">Board temperature now <b id="tbNow"></b> &deg;C &mdash; true value <input type="number" id="tbRef" step="0.1" value="25"> <button id="tbCal">Calibrate</button> <span class="note">V25 = <span id="v25"></span> V</span></div>
<p class="note">Voltage: measure the cell with a calibrated DMM at the cell terminals and enter the value. Current: start a charge or discharge, measure the current with a DMM in series and enter it (negative while discharging). Factors are limited to 0.90&ndash;1.10.</p></fieldset>
<div class="row"><button class="pri" id="sSave">Save</button><button id="sReload">Reload</button><button class="dan" id="sReboot">Reboot board</button><span id="sMsg"></span></div></section>

<section id="t-ota"><div class="card"><p>Upload a firmware image (<code>lfp8.ino.bin</code> from <code>arduino-cli compile --export-binaries</code>) to <b>this</b> board.</p>
<div class="row"><input type="file" id="oFile" accept=".bin"><label><input type="checkbox" id="oForce"> force (aborts running tests)</label><button class="pri" id="oGo">Upload</button></div>
<progress id="oProg" max="100" value="0" style="width:100%"></progress><div id="oMsg" class="msg"></div>
<p class="note">The board refuses updates while tests are running. During the update all outputs are switched off and the heartbeat stops, so the hardware isolates every cell. ArduinoOTA (network port <code>lfp8-&lt;id&gt;</code>, password = admin password or <code>lfp8admin</code>) is only answered while no test is running.</p></div></section>
</main><div id="toast"></div>
<script>
'use strict';
const $=(s,e=document)=>e.querySelector(s);
const esc=s=>String(s==null?'':s).replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
const num=v=>typeof v==='number'&&isFinite(v);
const fx=(v,d)=>num(v)?v.toFixed(d):'–';
const hms=s=>{s=Math.max(0,s|0);const h=Math.floor(s/3600),m=Math.floor(s%3600/60),x=s%60;return h+':'+String(m).padStart(2,'0')+':'+String(x).padStart(2,'0')};
const B={};let selfKey='self';let tab='dash';
const ISET_A=[0,0.265,0.532,0.797,1.064,1.328,1.595,1.860];  // set-point at 5.20 V (passive ceiling ~1.0 A at 3.3 V)
function stColor(s){if(!s)return'var(--rest)';if(s.startsWith('FAULT')||s==='REVERSED')return'var(--bad)';if(s.startsWith('CHARGING'))return'var(--chg)';if(s==='DISCHARGING')return'var(--dis)';if(s==='DONE')return'var(--ok)';if(s==='IR_MEASURE'||s==='PAUSED')return'var(--warn)';if(s==='EMPTY')return'#9aa3ae';return'var(--rest)'}
function toast(m,bad){const t=$('#toast');t.textContent=m;t.style.background=bad?'var(--bad)':'var(--fg)';t.style.display='block';clearTimeout(toast.h);toast.h=setTimeout(()=>t.style.display='none',3500)}
async function getJ(base,path,ms){const c=new AbortController(),t=setTimeout(()=>c.abort(),ms||3000);try{const r=await fetch(base+path,{signal:c.signal,cache:'no-store'});if(!r.ok)throw new Error('HTTP '+r.status);return await r.json()}finally{clearTimeout(t)}}
async function postJ(base,path,obj,ms){const c=new AbortController(),t=setTimeout(()=>c.abort(),ms||8000);try{const r=await fetch(base+path,{method:'POST',body:JSON.stringify(obj),headers:{'Content-Type':'text/plain'},signal:c.signal});let j;try{j=await r.json()}catch(e){j={ok:r.ok,err:'HTTP '+r.status}}return j}catch(e){return{ok:false,err:String(e.message||e)}}finally{clearTimeout(t)}}
async function cmd(b,obj,quiet){const r=await postJ(b.base,'/api/cmd',obj);if(!r.ok)toast('Board '+(b.st?b.st.id:'?')+': '+(r.err||'error'),true);else if(!quiet)toast('OK');setTimeout(poll,300);return r}

// ---------------------------------------------------------------- boards
function upsert(key,ip,base){let b=B[key];if(!b){b=B[key]={key:key,ok:false}}b.ip=ip;b.base=base;b.seen=Date.now()}
async function refreshPeers(){try{const p=await getJ('','/api/peers');upsert(selfKey,p.self.ip,'');for(const x of p.peers)upsert(x.ip,x.ip,'http://'+x.ip);for(const k in B)if(k!==selfKey&&Date.now()-B[k].seen>30000)delete B[k]}catch(e){if(!B[selfKey])upsert(selfKey,location.hostname,'')}}
async function poll(){const bs=Object.values(B);await Promise.all(bs.map(async b=>{try{b.st=await getJ(b.base,'/api/status',2500);b.ok=true;b.err=''}catch(e){b.ok=false;b.err=String(e.message||e)}}));render()}
function boardsSorted(){return Object.values(B).filter(b=>b.st).sort((a,c)=>a.st.id-c.st.id)}
function allCells(){const r=[];for(const b of boardsSorted())for(const c of b.st.ch)r.push({b:b,c:c});return r.sort((x,y)=>x.c.g-y.c.g)}

function boardCard(b){const id='b-'+b.key.replace(/\W/g,'_');let el=document.getElementById(id);if(!el){el=document.createElement('div');el.className='card';el.id=id;
el.innerHTML='<div class="hd"><b class="nm"></b><span class="badge sf"></span></div><div class="kv"><span>VIN</span><span class="vin"></span><span>Board</span><span class="tb"></span><span>Fan</span><span class="fan"></span><span>Heartbeat</span><span class="hb"></span><span>ISET</span><span><select class="iset"></select></span><span>Loop max</span><span class="lp"></span></div><div class="msg al"></div><div class="btns"><button data-b="ack">Acknowledge alarm</button><button data-b="stopall" class="dan">Stop board</button><button data-b="identify">Identify</button><a class="lnk" target="_blank">open</a></div>';
const sel=$('.iset',el);for(let i=0;i<8;i++){const o=document.createElement('option');o.value=i;o.textContent=i+' ('+ISET_A[i].toFixed(2)+' A)';sel.appendChild(o)}
sel.addEventListener('focus',()=>sel.dataset.busy=1);sel.addEventListener('blur',()=>delete sel.dataset.busy);
sel.addEventListener('change',async()=>{await cmd(b,{cmd:'iset',value:+sel.value});delete sel.dataset.busy;sel.blur()});
el.addEventListener('click',async e=>{const t=e.target.closest('button[data-b]');if(!t)return;const a=t.dataset.b;if(a==='stopall'&&!confirm('Stop all jobs on this board?'))return;await cmd(b,{cmd:a})});
$('#boards').appendChild(el)}return el}
function renderBoards(){const seen={};for(const b of Object.values(B)){const el=boardCard(b);seen[el.id]=1;const s=b.st;
$('.lnk',el).href=b.base?b.base+'/':'/';
if(!s){$('.nm',el).textContent=b.ip||'?';$('.al',el).textContent=b.err||'connecting...';continue}
const bd=s.board;$('.nm',el).textContent='Board '+s.id+' – '+s.host+' ('+s.ip+', '+s.mode+(s.rssi?' '+s.rssi+' dBm':'')+') fw '+s.fw;
const sf=$('.sf',el);sf.textContent=!b.ok?'OFFLINE':bd.maint?'MAINTENANCE':bd.safe?(bd.alarm?'ALARM':'SAFE OK'):'SAFE LOW';sf.style.background=(!b.ok||!bd.safe||bd.alarm)?'var(--bad)':'var(--ok)';
$('.vin',el).textContent=fx(bd.vin,2)+' V';$('.tb',el).textContent=fx(bd.tb,1)+' °C'+(bd.hot?' HOT':'');$('.fan',el).textContent=bd.fan+' %';
$('.hb',el).textContent=bd.hb?'running':'STOPPED';$('.lp',el).textContent=(bd.loop_max_us/1000).toFixed(1)+' ms, scan '+bd.scan_ms+' ms';
const sel=$('.iset',el);if(!sel.dataset.busy)sel.value=bd.iset;sel.title='effective ISET '+bd.iset_eff+' = '+fx(bd.iset_a,2)+' A';
let al=bd.alarm?('ALARM '+bd.alarm_text):'';if(bd.cond)al+=(al?' | ':'')+bd.cond;if(bd.latch_reset)al+=(al?' | ':'')+'OV latch release '+bd.latch_reset;if(s.dup_id)al+=(al?' | ':'')+'DUPLICATE BOARD ID '+s.id;if(!b.ok)al='offline: '+b.err+(al?' | '+al:'');
const ae=$('.al',el);ae.textContent=al||'no alarms';ae.className='msg al '+(bd.alarm||!b.ok||s.dup_id?'alarm':'ok')}
for(const el of [...$('#boards').children])if(!seen[el.id])el.remove()}

// ---------------------------------------------------------------- cells
function cellCard(key){let el=document.getElementById(key);if(el)return el;el=document.createElement('div');el.className='card';el.id=key;
el.innerHTML='<div class="hd"><b class="g"></b><span class="badge st"></span></div><div class="msg pr"></div><div class="kv"><span>V</span><span class="v"></span><span>I</span><span class="i"></span><span>T</span><span class="t"></span><span>Time</span><span class="el"></span><span>Step</span><span class="mah"></span><span>Step</span><span class="mwh"></span><span>Cap</span><span class="cap"></span><span>Cap</span><span class="capw"></span><span>IR dc</span><span class="ird"></span><span>IR ohm</span><span class="iro"></span><span>B- path</span><span class="ct"></span><span></span><span></span></div><div class="msg ms"></div><div class="btns"><button data-c="charge">Charge</button><button data-c="discharge">Discharge</button><button data-c="captest">Cap test</button><button data-c="ir">IR</button><button data-c="stop">Stop</button><button data-c="reset">Reset</button><button data-c="chart">Chart</button></div>';
$('#cells').appendChild(el);return el}
function renderCells(){const seen={};const list=allCells();
for(const x of list){const b=x.b,c=x.c,key='c-'+b.key.replace(/\W/g,'_')+'-'+c.k;const el=cellCard(key);seen[key]=1;el.dataset.bk=b.key;el.dataset.k=c.k;el.dataset.g=c.g;el.style.order=c.g;
$('.g',el).textContent='Cell '+c.g+' ';$('.g',el).title='board '+b.st.id+' channel '+c.k;
const st=$('.st',el);st.textContent=c.st+(c.pause?' ('+c.pause+')':'');st.style.background=stColor(c.st);
$('.pr',el).textContent=c.prog!=='none'?(c.prog+' · '+c.step+(c.pulse_pct?' · pulsed '+c.pulse_pct+' %':'')+(c.chg?' · CHG on':'')+(c.dis?' · DIS on':'')):(c.ntc==='none'?'no NTC':'');
const ct=$('.ct',el);ct.textContent=num(c.contact_mohm)?fx(c.contact_mohm,0)+' m\u03a9'+(c.contact_warn?' CONTACT_WARN':''):'-';ct.className='ct'+(c.contact_warn?' warn':'');
$('.v',el).textContent=fx(c.v,3)+' V';$('.i',el).textContent=(num(c.i)&&c.i>0?'+':'')+fx(c.i,3)+' A';$('.t',el).textContent=fx(c.t,1)+(num(c.t)?' °C':'');
$('.el',el).textContent=c.el?hms(c.el):'–';$('.mah',el).textContent=fx(c.mah,0)+' mAh';$('.mwh',el).textContent=fx(c.mwh,0)+' mWh';
$('.cap',el).textContent=fx(c.cap_mah,0)+' mAh';$('.capw',el).textContent=fx(c.cap_mwh,0)+' mWh';$('.ird',el).textContent=fx(c.ir_dc,2)+' mΩ';$('.iro',el).textContent=fx(c.ir_ohm,2)+' mΩ';
const m=$('.ms',el);m.textContent=(c.ov_pending?'OV latch release pending (runs when the board is idle) ':'')+(c.msg||(c.bm_warn?'B- sense warning (open B- wire?)':''));m.title=c.msg||'';m.className='msg ms '+(c.st.startsWith('FAULT')?'bad':'')}
for(const el of [...$('#cells').children])if(!seen[el.id])el.remove()}
$('#cells').addEventListener('click',async e=>{const t=e.target.closest('button[data-c]');if(!t)return;const el=t.closest('.card');const b=B[el.dataset.bk];if(!b)return;const a=t.dataset.c,k=+el.dataset.k;
if(a==='chart'){$('#chSel').value=el.dataset.bk+'|'+k;show('chart');loadChart();return}
if(a==='discharge'&&!confirm('Discharge cell '+el.dataset.g+' to 2.5 V?'))return;if(a==='reset'&&!confirm('Reset cell '+el.dataset.g+'? This clears its fault AND its stored results.'))return;await cmd(b,{ch:k,cmd:a})});
$('#stopAll').addEventListener('click',async()=>{if(!confirm('Stop ALL jobs on ALL boards?'))return;for(const b of Object.values(B))await cmd(b,{cmd:'stop'},true);toast('stop sent to all boards')});

// ---------------------------------------------------------------- chart
function nice(lo,hi,n){const span=hi-lo||1,raw=span/n,p=Math.pow(10,Math.floor(Math.log10(raw))),f=raw/p,st=(f<1.5?1:f<3?2:f<7?5:10)*p;const t=[];for(let v=Math.ceil(lo/st)*st;v<=hi+st*1e-9;v+=st)t.push(+v.toFixed(10));return t}
function ext(a,minSpan){let lo=Infinity,hi=-Infinity;for(const v of a)if(num(v)){if(v<lo)lo=v;if(v>hi)hi=v}if(lo===Infinity){lo=0;hi=1}if(hi-lo<minSpan){const m=(hi+lo)/2;lo=m-minSpan/2;hi=m+minSpan/2}const p=(hi-lo)*0.06;return[lo-p,hi+p]}
function plot(cv,xs,s1,s2,o){const dpr=window.devicePixelRatio||1,W=cv.clientWidth,H=cv.clientHeight;cv.width=W*dpr;cv.height=H*dpr;const g=cv.getContext('2d');g.setTransform(dpr,0,0,dpr,0,0);
const cs=getComputedStyle(document.documentElement),col=n=>cs.getPropertyValue(n).trim();g.clearRect(0,0,W,H);const L=58,R=58,T=12,Bt=32,pw=W-L-R,ph=H-T-Bt;
let x0=o.x0,x1=o.x1;if(x0===undefined){const e=ext(xs,1e-9);x0=e[0];x1=e[1]}const a=ext(s1,o.minA||0.05),b=ext(s2,o.minB||0.1);
const X=v=>L+(v-x0)/(x1-x0)*pw,YA=v=>T+ph-(v-a[0])/(a[1]-a[0])*ph,YB=v=>T+ph-(v-b[0])/(b[1]-b[0])*ph;
g.font='11px system-ui,sans-serif';g.strokeStyle=col('--line');g.fillStyle=col('--mut');g.lineWidth=1;
for(const t of nice(a[0],a[1],6)){const y=YA(t);g.beginPath();g.moveTo(L,y);g.lineTo(L+pw,y);g.stroke();g.textAlign='right';g.fillText(t.toFixed(o.da||2),L-6,y+4)}
g.textAlign='left';for(const t of nice(b[0],b[1],6))g.fillText(t.toFixed(o.db||2),L+pw+6,YB(t)+4);
g.textAlign='center';for(const t of nice(x0,x1,8)){const x=X(t);g.beginPath();g.moveTo(x,T);g.lineTo(x,T+ph);g.stroke();g.fillText(o.fx?o.fx(t):t,x,T+ph+14)}
g.fillText(o.xl||'',L+pw/2,H-4);g.save();g.translate(12,T+ph/2);g.rotate(-Math.PI/2);g.fillStyle=col('--v');g.fillText(o.al||'',0,0);g.restore();
g.save();g.translate(W-8,T+ph/2);g.rotate(Math.PI/2);g.fillStyle=col('--i');g.fillText(o.bl||'',0,0);g.restore();
if(b[0]<0&&b[1]>0){g.strokeStyle=col('--i');g.globalAlpha=.35;g.setLineDash([4,4]);g.beginPath();g.moveTo(L,YB(0));g.lineTo(L+pw,YB(0));g.stroke();g.setLineDash([]);g.globalAlpha=1}
const line=(ys,Y,c)=>{g.strokeStyle=c;g.lineWidth=1.6;g.beginPath();let pen=false;for(let k=0;k<xs.length;k++){const y=ys[k];if(!num(y)){pen=false;continue}const px=X(xs[k]),py=Y(y);if(pen)g.lineTo(px,py);else g.moveTo(px,py);pen=true}g.stroke()};
line(s1,YA,col('--v'));line(s2,YB,col('--i'));
if(o.marks)for(const m of o.marks){const x=X(m);g.strokeStyle=col('--warn');g.setLineDash([3,3]);g.beginPath();g.moveTo(x,T);g.lineTo(x,T+ph);g.stroke();g.setLineDash([])}
g.strokeStyle=col('--line');g.strokeRect(L,T,pw,ph);cv._map={L:L,pw:pw,x0:x0,x1:x1}}
let chartData=null;
function fillCellSelects(){const sel=$('#chSel'),cur=sel.value,opts=allCells().map(x=>[x.b.key+'|'+x.c.k,'Cell '+x.c.g+' (board '+x.b.st.id+' ch '+x.c.k+') '+x.c.st]);
const sig=opts.map(o=>o[0]).join();if(sel.dataset.sig!==sig){sel.innerHTML=opts.map(o=>'<option value="'+esc(o[0])+'">'+esc(o[1])+'</option>').join('');sel.dataset.sig=sig;if(cur)sel.value=cur}
const ts=$('#trSel'),tc=ts.value;const bo=boardsSorted().map(b=>'<option value="'+esc(b.key)+'">Board '+b.st.id+'</option>').join('');if(ts.dataset.sig!==bo){ts.innerHTML=bo;ts.dataset.sig=bo;if(tc)ts.value=tc}}
async function loadChart(){const v=$('#chSel').value;if(!v)return;const p=v.split('|'),b=B[p[0]];if(!b)return;try{const h=await getJ(b.base,'/api/history?ch='+p[1],10000);const n=h.d.length/2,xs=[],vs=[],is=[];
for(let k=0;k<n;k++){xs.push(-((n-1-k)*h.interval+h.age_s)/3600);const mv=h.d[2*k],ma=h.d[2*k+1];vs.push(mv==null?null:mv/1000);is.push(ma==null?null:ma/1000)}
chartData={xs:xs,vs:vs,is:is};$('#chInfo').textContent=n+' samples ('+(n*h.interval/3600).toFixed(1)+' h), cell '+h.g;drawChart()}catch(e){toast('history: '+e.message,true)}}
function drawChart(){if(!chartData)return;const d=chartData;plot($('#chart'),d.xs,d.vs,d.is,{x1:0,x0:Math.min(-1,d.xs.length?d.xs[0]:-1),xl:'hours',al:'V',bl:'A',da:2,db:2,fx:t=>t.toFixed(1)})}
$('#chart').addEventListener('mousemove',e=>{const m=e.target._map;if(!m||!chartData)return;const r=e.target.getBoundingClientRect(),xv=m.x0+(e.clientX-r.left-m.L)/m.pw*(m.x1-m.x0);let best=-1,bd=1e9;
chartData.xs.forEach((x,k)=>{const d=Math.abs(x-xv);if(d<bd){bd=d;best=k}});if(best<0)return;$('#chHover').textContent=chartData.xs[best].toFixed(2)+' h: '+fx(chartData.vs[best],3)+' V, '+fx(chartData.is[best],3)+' A'});
$('#chLoad').addEventListener('click',loadChart);setInterval(()=>{if(tab==='chart'&&$('#chAuto').checked)loadChart()},30000);

// ---------------------------------------------------------------- IR
function renderIr(){const tb=$('#irTab tbody');tb.innerHTML=allCells().map(x=>{const c=x.c;return'<tr><td>Cell '+c.g+'</td><td style="color:'+stColor(c.st)+'">'+esc(c.st)+'</td><td>'+fx(c.v,3)+'</td><td><b>'+fx(c.ir_ohm,2)+'</b></td><td><b>'+fx(c.ir_dc,2)+'</b></td><td>'+fx(c.ir_i,3)+'</td><td>'+esc(c.ir_mode||'')+'</td><td>'+fx(c.ir_v0,4)+'</td><td'+(c.contact_warn?' class="warn"':'')+'>'+fx(c.contact_mohm,0)+(c.contact_warn?' !':'')+'</td><td><button data-bk="'+esc(x.b.key)+'" data-k="'+c.k+'">IR</button></td></tr>'}).join('')}
$('#irTab').addEventListener('click',async e=>{const t=e.target.closest('button[data-k]');if(!t)return;const b=B[t.dataset.bk];if(b)await cmd(b,{ch:+t.dataset.k,cmd:'ir'})});
$('#irAll').addEventListener('click',async()=>{let n=0;for(const x of allCells())if(x.c.st==='IDLE'||x.c.st==='DONE'){const r=await cmd(x.b,{ch:x.c.k,cmd:'ir'},true);if(r.ok)n++}toast('IR started on '+n+' cells')});
function calc(){const v0=+$('#cV0').value,i0=+$('#cI0').value,v1=+$('#cV1').value,i1=+$('#cI1').value,di=i1-i0,dv=v1-v0;
$('#cR').textContent=di===0?'ΔI must not be 0':'R = '+(dv/di*1000).toFixed(3)+' mΩ  (ΔV = '+(dv*1000).toFixed(2)+' mV, ΔI = '+di.toFixed(3)+' A)'}
for(const id of['cV0','cI0','cV1','cI1'])$('#'+id).addEventListener('input',calc);calc();
async function loadTrace(){const b=B[$('#trSel').value];if(!b)return;try{const t=await getJ(b.base,'/api/irtrace',5000);if(!t.valid){$('#trInfo').textContent='no IR pulse recorded since boot';return}
const pts={};for(const p of t.v)(pts[p[0]]=pts[p[0]]||{}).v=p[1];for(const p of t.i)(pts[p[0]]=pts[p[0]]||{}).i=p[1];const xs=Object.keys(pts).map(Number).sort((a,c)=>a-c);
plot($('#trace'),xs,xs.map(x=>pts[x].v),xs.map(x=>pts[x].i),{x0:0,x1:1050,xl:'ms after load on',al:'V',bl:'A',da:4,db:2,marks:[10,1000],minA:0.005,minB:0.2,fx:t=>t.toFixed(0)});
$('#trInfo').textContent='cell '+t.g+' ('+t.mode+' pulse): V0 '+fx(t.v0,4)+' V, R ohmic '+fx(t.r_ohmic_mohm,2)+' mΩ, R dc '+fx(t.r_dc_mohm,2)+' mΩ'+(t.ok?'':' – failed: '+t.err)}catch(e){toast('trace: '+e.message,true)}}
$('#trLoad').addEventListener('click',loadTrace);

// ---------------------------------------------------------------- pack
let packRes=null;
for(const pr of[[4,10],[8,5],[16,2],[4,5],[2,20]]){const bt=document.createElement('button');bt.textContent=pr[0]+'S'+pr[1]+'P';bt.onclick=()=>{$('#pS').value=pr[0];$('#pP').value=pr[1]};$('#pPre').appendChild(bt)}
function packCells(){return allCells().filter(x=>num(x.c.cap_mah)&&x.c.cap_mah>0)}
function renderPackAvail(){const n=packCells().length,need=(+$('#pS').value)*(+$('#pP').value);$('#pAvail').textContent=n+' cells with a measured capacity available, '+need+' needed.'}
$('#pBuild').addEventListener('click',async()=>{const cs=packCells(),flat=[];for(const x of cs)flat.push(x.c.g,x.c.cap_mah,num(x.c.ir_dc)?x.c.ir_dc:0);
const r=await postJ('','/api/pack',{s:+$('#pS').value,p:+$('#pP').value,sel:$('#pSel').value,irw:+$('#pW').value,cells:flat},15000);
if(!r.ok){$('#pOut').innerHTML='<p class="alarm">'+esc(r.err||'failed')+'</p>';packRes=null;return}packRes=r;
const mean=r.groups.reduce((a,g)=>a+g.cap_mah,0)/r.groups.length,mx=Math.max(...r.groups.map(g=>g.cap_mah));
let h='<div class="card"><p><b>'+r.s+'S'+r.p+'P</b>: pack capacity '+(r.pack_cap_mah/1000).toFixed(2)+' Ah (weakest group), group capacity spread <b>'+r.cap_spread_pct.toFixed(2)+' %</b>, group IR spread '+r.ir_spread_pct.toFixed(1)+' %, pack DC-IR &asymp; '+r.pack_ir_mohm.toFixed(1)+' m&Omega; ('+r.swaps+' swaps)</p><div class="scroll"><table><thead><tr><th>Group</th><th>Cells (cell: mAh / m&Omega;)</th><th>&Sigma; mAh</th><th>&Delta; vs mean</th><th>R group m&Omega;</th><th style="width:25%"></th></tr></thead><tbody>';
for(const g of r.groups)h+='<tr><td>'+g.n+'</td><td style="text-align:left;white-space:normal">'+g.cells.map(c=>c[0]+': '+c[1].toFixed(0)+(c[2]==null?'':' / '+c[2].toFixed(1))).join(', ')+'</td><td>'+g.cap_mah.toFixed(0)+'</td><td>'+((g.cap_mah-mean)/mean*100).toFixed(2)+' %</td><td>'+g.ir_mohm.toFixed(2)+'</td><td><div class="bar" style="width:'+(g.cap_mah/mx*100).toFixed(1)+'%"></div></td></tr>';
h+='</tbody></table></div>'+(r.unused.length?'<p class="note">Unused cells: '+r.unused.join(', ')+'</p>':'')+'</div>';$('#pOut').innerHTML=h});
$('#pCsv').addEventListener('click',()=>{if(!packRes)return toast('build a pack first',true);let s='group,cell,cap_mah,ir_mohm\r\n';for(const g of packRes.groups)for(const c of g.cells)s+=g.n+','+c[0]+','+c[1]+','+(c[2]==null?'':c[2])+'\r\n';dl('lfp8-pack-'+packRes.s+'S'+packRes.p+'P.csv',s)});
for(const id of['pS','pP'])$('#'+id).addEventListener('input',renderPackAvail);

// ---------------------------------------------------------------- export
function dl(name,text){const a=document.createElement('a');a.href=URL.createObjectURL(new Blob([text],{type:'text/csv'}));a.download=name;document.body.appendChild(a);a.click();setTimeout(()=>{URL.revokeObjectURL(a.href);a.remove()},500)}
const CSV_COLS=['board','ch','cell','state','program','step','v','i','t_cell','cap_mah','cap_mwh','cap_s','dis_term','chg_mah','chg_mwh','chg_term','store_mah','ir_dc_mohm','ir_ohmic_mohm','ir_i_a','ir_v0','ir_mode','elapsed_s','msg'];
function csvq(v){if(v==null||(typeof v==='number'&&!isFinite(v)))return'';const s=String(v);return/[",\r\n]/.test(s)?'"'+s.replace(/"/g,'""')+'"':s}
$('#csvAll').addEventListener('click',()=>{let s=CSV_COLS.join(',')+'\r\n';for(const x of allCells()){const c=x.c;s+=[x.b.st.id,c.k,c.g,c.st,c.prog,c.step,c.v,c.i,c.t,c.cap_mah,c.cap_mwh,c.cap_s||null,c.dis_term,c.chg_mah,c.chg_mwh,c.chg_term,c.store_mah,c.ir_dc,c.ir_ohm,c.ir_i,c.ir_v0,c.ir_mode,c.el,c.msg].map(csvq).join(',')+'\r\n'}
dl('lfp8-all-'+new Date().toISOString().slice(0,16).replace(/[:T]/g,'-')+'.csv',s)});
function renderCsvLinks(){$('#csvLinks').innerHTML=boardsSorted().map(b=>'<a href="'+esc(b.base)+'/api/export.csv">board '+b.st.id+' CSV</a>').join(' ')}

// ---------------------------------------------------------------- settings
const POL=[['v_max_chg','V_MAX_CHG','V'],['v_abs_max','V_ABS_MAX (OV fault)','V'],['i_term','I_TERM','A'],['t_term_s','I_TERM duration','s'],['v_min_dis','V_MIN_DIS (cut-off)','V'],['v_precharge','V_PRECHARGE (20 % duty below)','V'],['v_cc_min','continuous CC from (50 % duty below)','V'],['v_dead','V_DEAD (refuse below)','V'],
['t_cell_max','T_CELL_MAX (pause)','°C'],['t_cell_resume','resume below','°C'],['t_cell_fault','cell fault above','°C'],['t_cell_min_chg','no charging below','°C'],['t_board_fan','T_BOARD_FAN','°C'],['t_board_max','T_BOARD_MAX (derate)','°C'],
['max_chg_h','MAX_CHG_TIME','h'],['max_dis_h','MAX_DIS_TIME','h'],['rest_chg_min','cap test: rest after charge','min'],['rest_dis_min','cap test: rest after discharge','min'],['storage_pct','cap test: storage charge','% of capacity'],['rest_ir_s','cap test: rest before IR','s'],['captest_ir','cap test: measure IR (1/0)',''],
['ir_rest_s','IR: rest before pulse','s'],['nominal_mah','nominal capacity','mAh'],['xchk_tol_v','ADC cross-check tolerance','V'],['vin_min','VIN min','V'],['vin_max','VIN max','V'],['iset','default ISET (0-7)','']];
let cfg=null;
function buildPolicy(){$('#sPol').innerHTML=POL.map(p=>'<label class="f">'+esc(p[1])+' <input type="number" step="any" id="s_'+p[0]+'"><span class="note">'+esc(p[2])+'</span></label>').join('')}
async function loadCfg(){try{cfg=await getJ('','/api/config',4000);for(const p of POL)$('#s_'+p[0]).value=cfg[p[0]];$('#s_wifi_ssid').value=cfg.wifi_ssid;$('#s_board_id').value=cfg.board_id;
$('#s_wifi_pass').placeholder=cfg.wifi_pass_set?'(unchanged)':'(not set)';$('#s_admin_pass').placeholder=cfg.admin_pass_set?'(unchanged)':'(not set)';$('#v25').textContent=fx(cfg.v25,4);renderCal()}catch(e){toast('config: '+e.message,true)}}
function renderCal(){if(!cfg)return;const sb=B[selfKey]&&B[selfKey].st;const tb=$('#calTab tbody');if(!tb.dataset.built){let h='';for(let k=1;k<=8;k++)h+='<tr><td>'+k+'</td><td><input type="number" step="0.0001" id="cv'+k+'"></td><td><input type="number" step="0.0001" id="ci'+k+'"></td><td id="vn'+k+'"></td><td id="in'+k+'"></td><td><input type="number" step="0.0001" id="rv'+k+'"></td><td><button data-cal="calv" data-k="'+k+'">Cal V</button></td><td><input type="number" step="0.001" id="ri'+k+'"></td><td><button data-cal="cali" data-k="'+k+'">Cal I</button></td></tr>';tb.innerHTML=h;tb.dataset.built=1}
for(let k=1;k<=8;k++){const a=$('#cv'+k),c=$('#ci'+k);if(document.activeElement!==a)a.value=cfg.cal_v[k-1];if(document.activeElement!==c)c.value=cfg.cal_i[k-1];if(sb){const ch=sb.ch[k-1];$('#vn'+k).textContent=fx(ch.v,4);$('#in'+k).textContent=fx(ch.i,4)}}
if(sb)$('#tbNow').textContent=fx(sb.board.tb,1)}
$('#calTab').addEventListener('click',async e=>{const t=e.target.closest('button[data-cal]');if(!t)return;const k=+t.dataset.k,ref=+$((t.dataset.cal==='calv'?'#rv':'#ri')+k).value;if(!ref)return toast('enter a reference value',true);
const r=await cmd(B[selfKey],{cmd:t.dataset.cal,ch:k,value:ref});if(r.ok)setTimeout(loadCfg,600)});
$('#tbCal').addEventListener('click',async()=>{const r=await cmd(B[selfKey],{cmd:'caltb',value:+$('#tbRef').value});if(r.ok)setTimeout(loadCfg,600)});
$('#sSave').addEventListener('click',async()=>{const o={};for(const p of POL){const v=$('#s_'+p[0]).value;if(v!=='')o[p[0]]=+v}o.wifi_ssid=$('#s_wifi_ssid').value;o.board_id=+$('#s_board_id').value;
const wp=$('#s_wifi_pass').value,ap=$('#s_admin_pass').value;if(wp)o.wifi_pass=wp;if(ap)o.admin_pass=ap;o.cal_v=[];o.cal_i=[];for(let k=1;k<=8;k++){o.cal_v.push(+$('#cv'+k).value);o.cal_i.push(+$('#ci'+k).value)}
const r=await postJ('','/api/config',o);$('#sMsg').textContent=r.ok?('applied ('+r.changed+' fields)'+(r.persisted===false?', written to flash when no test is running':', saved')+(r.reboot_required?' – reboot to apply network/board id':'')):('error: '+(r.err||'?'));$('#sMsg').className=r.ok?'ok':'alarm';if(r.ok)loadCfg()});
$('#sReload').addEventListener('click',loadCfg);
$('#sReboot').addEventListener('click',async()=>{if(confirm('Reboot this board? Running jobs are aborted.'))await cmd(B[selfKey],{cmd:'reboot'})});

// ---------------------------------------------------------------- OTA
$('#oGo').addEventListener('click',()=>{const f=$('#oFile').files[0];if(!f)return toast('choose a .bin file',true);const fd=new FormData();fd.append('fw',f,f.name);const x=new XMLHttpRequest();
x.open('POST','/update'+($('#oForce').checked?'?force=1':''));x.upload.onprogress=e=>{if(e.lengthComputable)$('#oProg').value=e.loaded/e.total*100};
x.onload=()=>{let m=x.responseText;try{const j=JSON.parse(m);m=j.ok?'Update OK – the board restarts now.':'Error: '+j.err}catch(e){}$('#oMsg').textContent=m};x.onerror=()=>{$('#oMsg').textContent='upload failed'};x.send(fd);$('#oMsg').textContent='uploading...'});

// ---------------------------------------------------------------- main
function show(t){tab=t;for(const b of document.querySelectorAll('#nav button'))b.classList.toggle('on',b.dataset.t===t);for(const s of document.querySelectorAll('main section'))s.classList.toggle('on',s.id==='t-'+t);
if(t==='set'){loadCfg();$('#sHost').textContent=location.host}if(t==='chart'&&!chartData)loadChart();if(t==='ir'&&!$('#trSel').value)fillCellSelects();render()}
$('#nav').addEventListener('click',e=>{const b=e.target.closest('button[data-t]');if(b)show(b.dataset.t)});
function render(){renderBoards();renderCells();fillCellSelects();const on=Object.values(B).filter(b=>b.ok).length,tot=Object.keys(B).length;const cells=allCells();
$('#conn').textContent=on+'/'+tot+' boards online, '+cells.filter(x=>x.c.st!=='EMPTY').length+' cells';if(tab==='ir')renderIr();if(tab==='pack')renderPackAvail();if(tab==='export')renderCsvLinks();if(tab==='set')renderCal()}
window.addEventListener('resize',()=>{if(tab==='chart')drawChart()});
buildPolicy();(async()=>{await refreshPeers();await poll()})();setInterval(poll,2000);setInterval(refreshPeers,5000);
</script></body></html>
)LFP8HTML";
