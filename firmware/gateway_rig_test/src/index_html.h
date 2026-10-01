// index_html.h — the page (gzipped into index_html_gz.h at build time by tools/gen_html_gz.py). Kept deliberately small:
// mode · scenes · colour · master · dome shading · network/config. No pixel map, no live mirror.
#pragma once
static const char INDEX_HTML[] = R"SUNHTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="color-scheme" content="light dark">
<meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1,user-scalable=no">
<title>Sun</title>
<style>
:root{--bg:#f6f4ef;--panel:#fff;--line:#d9d5cc;--text:#1d1c1a;--mute:#6f6c66;--acc:#d97706;--bad:#c62f27;--ok:#1d8a4f;--warn:#b7791f;--okb:#9fd6b6;--badb:#efb0aa;--warnb:#ecd39a}
:root[data-theme=dark]{--bg:#0d0d0f;--panel:#17171b;--line:#2a2a31;--text:#e9e7e2;--mute:#8b8a93;--acc:#ffb547;--bad:#ff5a4f;--ok:#4fd38a;--warn:#ffcf5a;--okb:#24583b;--badb:#5e2724;--warnb:#6a5420}
*{box-sizing:border-box;-webkit-tap-highlight-color:transparent}
html,body{margin:0;background:var(--bg);color:var(--text);font:14px/1.35 -apple-system,system-ui,Segoe UI,Roboto,sans-serif}
body{max-width:720px;margin:0 auto;padding:10px 12px 28px}
header{display:flex;align-items:center;gap:6px;flex-wrap:wrap;font-size:12px;color:var(--mute)}
header b{color:var(--text);font-size:15px;margin-right:auto}
.pill{border:1px solid var(--line);border-radius:99px;padding:2px 8px;white-space:nowrap}
.pill.ok{color:var(--ok);border-color:var(--okb)}.pill.bad{color:var(--bad);border-color:var(--badb)}.pill.warn{color:var(--warn);border-color:var(--warnb)}
h4{margin:14px 0 4px;font-size:11px;font-weight:600;letter-spacing:.06em;text-transform:uppercase;color:var(--mute)}
h4 span{font-weight:400;text-transform:none;letter-spacing:0}
.row{display:flex;gap:6px;align-items:center;flex-wrap:wrap;margin:6px 0}
button,select,input[type=text],input[type=number]{background:var(--panel);color:var(--text);border:1px solid var(--line);border-radius:8px;padding:8px 12px;font:inherit}
button{cursor:pointer}button.on{border-color:var(--acc);color:var(--acc)}button:active{transform:translateY(1px)}
button.chip{padding:6px 10px;font-size:13px;border-radius:7px}
.sw{width:30px;height:30px;border-radius:50%;border:2px solid var(--line);padding:0}
.sl{display:grid;grid-template-columns:96px 1fr 48px;gap:8px;align-items:center;margin:3px 0}
.sl span{color:var(--mute);font-size:12px}.sl span:last-child{text-align:right}
input[type=range]{width:100%;accent-color:var(--acc)}
details{background:var(--panel);border:1px solid var(--line);border-radius:10px;padding:8px 12px;margin-top:12px}
summary{cursor:pointer;color:var(--mute)}
label{display:flex;flex-direction:column;gap:3px;font-size:12px;color:var(--mute);flex:1;min-width:130px}
label.inl{flex-direction:row;align-items:center;gap:6px;flex:0 0 auto;min-width:0}
.hide{display:none!important}
.note{font-size:12px;color:var(--mute)}
#grp div.g{font-size:11px;color:var(--mute);margin:8px 0 2px}
</style></head><body>
<header><b>Sun</b>
<span id="ws" class="pill bad">ws —</span><span id="eth" class="pill bad">eth —</span><span id="fps" class="pill">— fps</span>
<span id="pwD" class="pill" title="disc · RSP-200-5">D —</span><span id="pwE" class="pill" title="East wedges 1–6 · RSP-500-5">E —</span><span id="pwW" class="pill" title="West wedges 7–12 · RSP-500-5">W —</span>
<span id="build" class="pill" title="firmware build · theme · free heap · uptime">build —</span></header>

<h4>Mode</h4>
<div class="row" id="modes"><button class="chip" data-m="0">Off</button><button class="chip" data-m="1">Solid</button><button class="chip" data-m="2">Scene</button></div>
<div class="sl"><span>☀ master</span><input id="m" type="range" max="255" value="77"><span id="mv">30%</span></div>

<div id="sceneBox" class="hide">
<h4>Scene <span id="scInfo"></span></h4>
<div id="grp"></div>
<div class="row" style="margin-top:8px"><label>palette<select id="pal"><option value="-1">pattern's own colours</option></select></label>
 <span class="note" style="flex:2">colour below is the tint for Rotating rays / Radial pulse; the palette recolours the pattern's energy like the website's Sun palette</span></div>
<div class="sl"><span>speed</span><input id="ss" type="range" min="1" max="30" value="10"><span id="ssv">1.0×</span></div>
<div class="sl"><span id="p1n">p1</span><input id="p1" type="range" min="0" max="100" value="50"><span id="p1v">0.50</span></div>
<div class="sl"><span>contrast</span><input id="con" type="range" min="25" max="200" value="100"><span id="conv">1.00</span></div>
<div class="sl"><span>warmth</span><input id="wa" type="range" min="-100" max="100" value="0"><span id="wav">0.00</span></div>
</div>

<h4>Colour <span class="note">— solid colour · scene tint</span></h4>
<div class="row">
 <button class="sw" data-c="0,0,0,255" style="background:#fff6e8" title="W only"></button>
 <button class="sw" data-c="255,255,255,255" style="background:#fff" title="RGBW full"></button>
 <button class="sw" data-c="255,255,255,0" style="background:#ddd" title="RGB white"></button>
 <button class="sw" data-c="255,120,0,120" style="background:#ffb060" title="warm"></button>
 <button class="sw" data-c="255,0,0,0" style="background:#f33"></button>
 <button class="sw" data-c="0,255,0,0" style="background:#3f3"></button>
 <button class="sw" data-c="0,0,255,0" style="background:#36f"></button>
 <button class="sw" data-c="255,0,160,0" style="background:#f0a"></button>
 <button class="sw" data-c="0,200,255,0" style="background:#0cf"></button>
</div>
<div class="sl"><span>R</span><input id="r" type="range" max="255"><span id="rv"></span></div>
<div class="sl"><span>G</span><input id="g" type="range" max="255"><span id="gv"></span></div>
<div class="sl"><span>B</span><input id="b" type="range" max="255"><span id="bv"></span></div>
<div class="sl"><span>W</span><input id="w" type="range" max="255"><span id="wv"></span></div>

<details id="shd" open><summary>Dome shading <span id="shInfo" style="font-size:12px"></span></summary>
 <div class="note" style="margin:4px 0 6px">Gain per pixel ring (30 mm pitch), applied to everything sent. Dim the rings where the dome is thin and pixels show through; lift the dark ring inboard. 1.00 = no change, 0 = off. Saved on the ESP as you go.</div>
 <div class="row"><label class="inl">apply<input id="shon" type="checkbox" checked></label><label class="inl" title="while you move a slider, that ring is lit magenta on the rig">light ring being tuned<input id="shhl" type="checkbox" checked></label><button class="chip" id="shReset">Reset</button>
  <canvas id="shcv" width="240" height="48" style="width:240px;height:48px;border-radius:6px;border:1px solid var(--line)"></canvas></div>
 <div id="shk"></div>
</details>

<details><summary>Network / config</summary>
 <div class="row">
  <label>PixLite IP<input id="tip" type="text"></label>
  <label>gateway Ethernet IP (/24)<input id="eip" type="text"></label>
  <label>Art-Net universe of plan universe 1<input id="uni" type="number" min="1" max="32000"></label>
 </div>
 <div class="row"><button id="bSave">Save &amp; apply</button><span style="flex:1"></span><button id="bExport">Export config</button><button id="bImport">Import config…</button><input id="impFile" type="file" accept="application/json,.json" style="display:none"></div>
 <div id="impInfo" class="note"></div>
 <div class="note" style="margin-top:6px">OTA: join SUN-DISC, then <code>pio run -e ota -t upload</code> (192.168.4.1:3232). Output pauses during the upload.</div>
</details>

<script>
const $=id=>document.getElementById(id);
const touch={};document.addEventListener('input',e=>{if(e.target&&e.target.id)touch[e.target.id]=Date.now();},true);document.addEventListener('change',e=>{if(e.target&&e.target.id)touch[e.target.id]=Date.now();},true);
const recent=id=>(touch[id]||0)>Date.now()-1500;
const PSU=[200,500,500];
let st=null,ws=null,first=true,colA=[255,255,255,255],dark=false,SC=null;
const send=o=>{if(ws&&ws.readyState===1)ws.send(JSON.stringify(Object.assign({t:'set'},o)));};

// mode / master
document.querySelectorAll('#modes .chip').forEach(b=>b.onclick=()=>send({mode:+b.dataset.m}));
$('m').oninput=e=>{$('mv').textContent=Math.round(e.target.value/2.55)+'%';send({master:+e.target.value});};

// scenes (catalogue from the ESP)
fetch('/scenes.json').then(r=>r.json()).then(j=>{SC=j;const box=$('grp');let g='',row=null;
  j.scenes.forEach((sc,k)=>{if(sc.g!==g){g=sc.g;const h=document.createElement('div');h.className='g';h.textContent=g;box.appendChild(h);row=document.createElement('div');row.className='row';box.appendChild(row);}
    const b=document.createElement('button');b.className='chip';b.textContent=sc.n;b.dataset.k=k;b.onclick=()=>send({mode:2,sc:k});row.appendChild(b);});
  j.pal.forEach((n,k)=>{const o=document.createElement('option');o.value=k;o.textContent=n;$('pal').appendChild(o);});
  if(st)applyStatus(st);});
$('pal').onchange=e=>send({pal:+e.target.value});
$('ss').oninput=e=>{$('ssv').textContent=(e.target.value/10).toFixed(1)+'×';send({ss:+e.target.value});};
$('p1').oninput=e=>{$('p1v').textContent=(e.target.value/100).toFixed(2);send({p1:+e.target.value});};
$('con').oninput=e=>{$('conv').textContent=(e.target.value/100).toFixed(2);send({con:+e.target.value});};
$('wa').oninput=e=>{$('wav').textContent=(e.target.value/100).toFixed(2);send({wa:+e.target.value});};

// colour
const SL=['r','g','b','w'];
function showCol(){SL.forEach((k,j)=>{if(!recent(k))$(k).value=colA[j];$(k+'v').textContent=colA[j];});}
SL.forEach((k,j)=>$(k).oninput=e=>{colA[j]=+e.target.value;showCol();send({a:colA.join(',')});});
document.querySelectorAll('.sw').forEach(b=>b.onclick=()=>{touch.col=Date.now();colA=b.dataset.c.split(',').map(Number);showCol();send({a:colA.join(',')});});
showCol();

// dome shading (per ring; rings 0–16 = 0–480 mm share one slider)
const SH_FIRST=17,RING_BOARD=i=>i<=7?'A':(i<=22?'B':'C');let shk=new Array(29).fill(100);
const hl=r=>{if(ws&&$('shhl').checked)ws.send(JSON.stringify({t:'shade',hl:r}));};
function shRow(label,id,onin,ring){const row=document.createElement('div');row.className='sl';row.style.gridTemplateColumns='128px 1fr 48px';
  row.innerHTML=`<span>${label}</span><input type="range" id="${id}" min="0" max="200" value="100"><span id="${id}v">1.00</span>`;$('shk').appendChild(row);
  $(id).oninput=onin;$(id).addEventListener('pointerdown',()=>hl(ring));}
shRow('inner · 0–480 mm (A, B)','shin',e=>{const g=+e.target.value;for(let r=0;r<SH_FIRST;r++)shk[r]=g;$('shinv').textContent=(g/100).toFixed(2);shDraw();if(ws)ws.send(JSON.stringify({t:'shade',inner:g,hl:$('shhl').checked?-2:-1}));},-2);
for(let r=28;r>=SH_FIRST;r--)shRow(`${r*30} mm · ${RING_BOARD(r)}${r===28?' (rim)':''}`,'shr'+r,e=>{const g=+e.target.value;shk[r]=g;$('shr'+r+'v').textContent=(g/100).toFixed(2);shDraw();if(ws)ws.send(JSON.stringify({t:'shade',ring:r,g,hl:$('shhl').checked?r:-1}));},r);
$('shhl').onchange=e=>{if(!e.target.checked&&ws)ws.send(JSON.stringify({t:'shade',hl:-1}));};
$('shon').onchange=e=>ws&&ws.send(JSON.stringify({t:'shade',on:e.target.checked?1:0}));
$('shReset').onclick=()=>ws&&ws.send(JSON.stringify({t:'shade',reset:1}));
function shDraw(){const c=$('shcv').getContext('2d');c.clearRect(0,0,240,48);const cs=getComputedStyle(document.documentElement);
  c.strokeStyle=cs.getPropertyValue('--line');c.beginPath();c.moveTo(0,24);c.lineTo(240,24);c.stroke();
  c.fillStyle=cs.getPropertyValue('--acc');for(let r=0;r<29;r++){const h=48*shk[r]/200;c.fillRect(r*240/29+1,48-h,240/29-2,h);}}
function shShow(){$('shin').value=shk[0];$('shinv').textContent=(shk[0]/100).toFixed(2);for(let r=SH_FIRST;r<29;r++){$('shr'+r).value=shk[r];$('shr'+r+'v').textContent=(shk[r]/100).toFixed(2);}shDraw();}
shDraw();

// network / config
$('bSave').onclick=()=>{if(ws&&ws.readyState===1)ws.send(JSON.stringify({t:'cfg',tip:$('tip').value.trim(),uni:+$('uni').value,eip:$('eip').value.trim()}));};
$('bExport').onclick=async()=>{const r=await fetch('/config.json');const txt=await r.text();const d=new Date(),ts=d.toISOString().slice(0,16).replace(/[-:T]/g,'').replace(/(\d{8})(\d{4})/,'$1-$2');
  const a=document.createElement('a');a.href=URL.createObjectURL(new Blob([txt],{type:'application/json'}));a.download=`sun-gateway-config-${ts}.json`;document.body.appendChild(a);a.click();setTimeout(()=>{URL.revokeObjectURL(a.href);a.remove();},2000);$('impInfo').textContent='exported '+a.download;};
$('bImport').onclick=()=>$('impFile').click();
$('impFile').onchange=async e=>{const f=e.target.files[0];if(!f)return;const txt=await f.text();try{JSON.parse(txt);}catch(err){$('impInfo').textContent='not valid JSON';return;}
  const r=await fetch('/config',{method:'POST',headers:{'Content-Type':'application/json'},body:txt});$('impInfo').textContent=r.ok?`imported ${f.name}`:'import failed';first=true;e.target.value='';};

// status → UI
function pw(id,w,lim){const e=$(id);e.textContent=`${id[2]} ${w} W`;e.className='pill '+(w>lim*0.9?'bad':(w>lim*0.7?'warn':''));}
function applyStatus(s){
  st=s;
  const sync=(id,v,fmt)=>{if(!recent(id)){$(id).value=v;$(id+'v').textContent=fmt?fmt(v):v;}};
  $('eth').textContent=s.link?`eth ${s.speed}M · ${s.eip}`:'eth link down';$('eth').className='pill '+(s.link?'ok':'bad');
  $('fps').textContent=s.ota?'OTA…':`${s.fps} fps${s.ren?` · render ${s.ren} ms`:''}${s.err?` · ${s.err} err`:''}`;
  const R={4:'panic',5:'int WDT',6:'task WDT',7:'WDT',9:'brown-out'};
  $('build').textContent=`build ${s.build} · ${s.dark?'dark':'light'} · heap ${Math.round(s.heap/1024)} kB · up ${s.up<120?s.up+' s':Math.round(s.up/60)+' min'}${R[s.rst]?' · last reset: '+R[s.rst]:''}${s.hto?' · helper late ×'+s.hto:''}`;
  if(!!s.dark!==dark){dark=!!s.dark;document.documentElement.dataset.theme=dark?'dark':'light';shDraw();}
  pw('pwD',s.pw[0],PSU[0]);pw('pwE',s.pw[1],PSU[1]);pw('pwW',s.pw[2],PSU[2]);
  document.querySelectorAll('#modes .chip').forEach(b=>b.classList.toggle('on',+b.dataset.m===s.mode));
  $('sceneBox').classList.toggle('hide',s.mode!==2);
  sync('m',s.master,v=>Math.round(v/2.55)+'%');
  if(SC){document.querySelectorAll('#grp .chip').forEach(b=>b.classList.toggle('on',+b.dataset.k===s.sc));const scn=SC.scenes[s.sc];if(scn){$('p1n').textContent=scn.p;$('scInfo').textContent='— '+scn.g+' · '+scn.n;}
    if(!recent('pal'))$('pal').value=s.pal;sync('ss',s.ss,v=>(v/10).toFixed(1)+'×');sync('p1',s.p1,v=>(v/100).toFixed(2));sync('con',s.con,v=>(v/100).toFixed(2));sync('wa',s.wa,v=>(v/100).toFixed(2));}
  const a=s.a.split(',').map(Number);if(first||!(SL.some(recent)||recent('col'))){colA=a;showCol();}
  if(s.shk&&s.shk.length===29){$('shon').checked=!!s.shon;$('shInfo').textContent=s.shon?'— on':'— off';if(!recent('shin')&&![...Array(29).keys()].some(r=>recent('shr'+r))){shk=s.shk.slice();shShow();}}
  if(first){$('tip').value=s.tip;$('uni').value=s.uni;$('eip').value=s.eip;first=false;}
}
function connect(){
  ws=new WebSocket(`ws://${location.host}/ws`);
  ws.onopen=()=>{$('ws').textContent='ws ok';$('ws').className='pill ok';};
  ws.onclose=()=>{$('ws').textContent='ws —';$('ws').className='pill bad';setTimeout(connect,1500);};
  ws.onmessage=e=>{const s=JSON.parse(e.data);if(s.t==='st')applyStatus(s);};
}
connect();
</script></body></html>
)SUNHTML";
