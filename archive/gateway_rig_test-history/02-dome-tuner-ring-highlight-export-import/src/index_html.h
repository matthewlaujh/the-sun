// index_html.h — web page for gateway_rig_test. Pixel positions come from the ESP at /map.bin (built from sun_map.h), so the page
// never carries its own copy of the map. The only map constants here are the per-run start index / first universe (for hover info).
#pragma once
#include <pgmspace.h>
static const char INDEX_HTML[] PROGMEM = R"SUNHTML(
<!doctype html>
<html lang="en"><head>
<meta charset="utf-8"><meta name="color-scheme" content="light dark">
<meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1,user-scalable=no">
<title>Sun · rig test</title>
<style>
:root{--bg:#f6f4ef;--panel:#ffffff;--line:#d9d5cc;--text:#1d1c1a;--mute:#6f6c66;--acc:#d97706;--bad:#c62f27;--ok:#1d8a4f;--warn:#b7791f;--cv:#ecebe6;--okb:#9fd6b6;--badb:#efb0aa;--warnb:#ecd39a}
:root[data-theme=dark]{--bg:#0d0d0f;--panel:#17171b;--line:#2a2a31;--text:#e9e7e2;--mute:#8b8a93;--acc:#ffb547;--bad:#ff5a4f;--ok:#4fd38a;--warn:#ffcf5a;--cv:#101013;--okb:#24583b;--badb:#5e2724;--warnb:#6a5420}
*{box-sizing:border-box;-webkit-tap-highlight-color:transparent}
html,body{margin:0;background:var(--bg);color:var(--text);font:14px/1.35 -apple-system,system-ui,Segoe UI,Roboto,sans-serif}
body{display:flex;flex-direction:column;min-height:100vh;max-width:820px;margin:0 auto;padding:10px 12px 24px}
header{display:flex;align-items:center;gap:6px;flex-wrap:wrap;font-size:12px;color:var(--mute)}
header b{color:var(--text);font-size:15px;margin-right:auto}
.pill{border:1px solid var(--line);border-radius:99px;padding:2px 8px;white-space:nowrap}
.pill.ok{color:var(--ok);border-color:var(--okb)}.pill.bad{color:var(--bad);border-color:var(--badb)}.pill.warn{color:var(--warn);border-color:var(--warnb)}
#wrap{position:relative;margin:8px 0}
canvas{width:100%;height:auto;display:block;touch-action:none;user-select:none;border-radius:50%;background:var(--cv);border:1px solid var(--line);aspect-ratio:1}
#info{font-size:12px;color:var(--mute);min-height:1.2em;margin:2px 0 6px}
.row{display:flex;gap:6px;align-items:center;flex-wrap:wrap;margin:6px 0}
.row.tight{gap:4px}
button,select,input[type=text],input[type=number]{background:var(--panel);color:var(--text);border:1px solid var(--line);border-radius:8px;padding:8px 12px;font:inherit}
button{cursor:pointer}button.on{border-color:var(--acc);color:var(--acc)}button.sm{padding:6px 9px;font-size:13px}
button:active{transform:translateY(1px)}
button.chip{padding:5px 8px;font-size:12px;border-radius:6px;min-width:38px}
.sw{width:30px;height:30px;border-radius:50%;border:2px solid var(--line);padding:0}
.sl{display:grid;grid-template-columns:22px 1fr 44px;gap:8px;align-items:center;margin:3px 0}
.sl span{color:var(--mute);font-size:12px;text-align:right}.sl span:first-child{text-align:left}
#sceneBox .sl{grid-template-columns:96px 1fr 44px}
input[type=range]{width:100%;accent-color:var(--acc)}
#curA,#curB{width:30px;height:30px;border-radius:8px;border:2px solid var(--line);padding:0}
#curA.on,#curB.on{border-color:var(--acc)}
details{background:var(--panel);border:1px solid var(--line);border-radius:10px;padding:8px 12px;margin-top:10px}
summary{cursor:pointer;color:var(--mute)}
label{display:flex;flex-direction:column;gap:3px;font-size:12px;color:var(--mute);flex:1;min-width:130px}
label.inl{flex-direction:row;align-items:center;gap:6px;flex:0 0 auto;min-width:0}
h4{margin:10px 0 2px;font-size:11px;font-weight:600;letter-spacing:.06em;text-transform:uppercase;color:var(--mute)}
#lbl{color:var(--text);font-size:13px;min-width:120px;text-align:center;flex:1}
#nodes{font-size:12px;color:var(--mute);white-space:pre-wrap}
.hide{display:none!important}
</style></head><body>
<header><b>Sun · rig test</b>
<span id="ws" class="pill bad">ws —</span><span id="eth" class="pill bad">eth —</span><span id="fps" class="pill">— fps</span><span id="build" class="pill" title="firmware compile time — changes after every OTA upload">build —</span>
<span id="pwD" class="pill" title="disc · RSP-200-5">D — W</span><span id="pwE" class="pill" title="East wedges 1–6 · RSP-500-5">E — W</span><span id="pwW" class="pill" title="West wedges 7–12 · RSP-500-5">W — W</span></header>

<div id="wrap"><canvas id="cv" width="900" height="900"></canvas></div>
<div id="info">loading map…</div>

<h4>Mode</h4>
<div class="row tight" id="modes"></div>
<div class="row" id="stepRow">
 <button class="sm" id="bPrev">◀</button><span id="lbl">—</span><button class="sm" id="bNext">▶</button>
 <label class="inl">hold<input id="hold" type="checkbox"></label>
</div>
<div class="row hide" id="spotRow">
 <label class="inl">px<input id="spot" type="number" min="0" max="2416" value="0" style="width:90px"></label>
 <button class="sm" id="sPrev">◀</button><button class="sm" id="sNext">▶</button>
 <span style="font-size:12px;color:var(--mute)">tap a pixel on the map</span>
</div>

<div id="sceneBox" class="hide">
<h4>Scene <span id="scInfo" style="font-weight:400;text-transform:none;letter-spacing:0"></span></h4>
<div id="scenes"></div>
<div class="row" style="margin-top:6px"><label>palette<select id="pal"><option value="-1">pattern's own colours</option></select></label>
 <span style="font-size:12px;color:var(--mute);flex:2">colour A is the tint for Rotating rays / Radial pulse · the palette recolours the pattern's energy like the website's "Sun palette"</span></div>
<div class="sl"><span>speed</span><input id="ss" type="range" min="1" max="30" value="10"><span id="ssv">1.0×</span></div>
<div class="sl"><span id="p1n">p1</span><input id="p1" type="range" min="0" max="100" value="50"><span id="p1v">0.50</span></div>
<div class="sl"><span>contrast</span><input id="con" type="range" min="25" max="200" value="100"><span id="conv">1.00</span></div>
<div class="sl"><span>warmth</span><input id="wa" type="range" min="-100" max="100" value="0"><span id="wav">0.00</span></div>
</div>

<h4>Select <span id="selInfo" style="font-weight:400;text-transform:none;letter-spacing:0"></span></h4>
<div class="row tight" id="runs"></div>
<details id="bdet"><summary>boards</summary><div class="row tight" id="boards"></div></details>

<h4>Colour</h4>
<div class="row">
 <button id="curA" class="on" title="colour A"></button><button id="curB" title="colour B (ends · last px)"></button>
 <span style="width:6px"></span>
 <button class="sw" data-c="0,0,0,255" style="background:#fff6e8" title="W only"></button>
 <button class="sw" data-c="255,255,255,255" style="background:#fff" title="RGBW full"></button>
 <button class="sw" data-c="255,255,255,0" style="background:#ddd" title="RGB white"></button>
 <button class="sw" data-c="255,0,0,0" style="background:#f33"></button>
 <button class="sw" data-c="0,255,0,0" style="background:#3f3"></button>
 <button class="sw" data-c="0,0,255,0" style="background:#36f"></button>
 <button class="sw" data-c="255,120,0,120" style="background:#ffb060" title="warm"></button>
 <button class="sw" data-c="255,0,160,0" style="background:#f0a" title="magenta"></button>
 <button class="sw" data-c="0,200,255,0" style="background:#0cf" title="cyan"></button>
</div>
<div class="sl"><span>R</span><input id="r" type="range" max="255"><span id="rv"></span></div>
<div class="sl"><span>G</span><input id="g" type="range" max="255"><span id="gv"></span></div>
<div class="sl"><span>B</span><input id="b" type="range" max="255"><span id="bv"></span></div>
<div class="sl"><span>W</span><input id="w" type="range" max="255"><span id="wv"></span></div>

<h4>Levels</h4>
<div class="sl"><span>☀</span><input id="m" type="range" max="255" value="77"><span id="mv">30%</span></div>
<div class="sl"><span>speed</span><input id="spd" type="range" min="1" max="100" value="20"><span id="spdv">20</span></div>
<div class="sl"><span>tail</span><input id="tail" type="range" min="1" max="64" value="6"><span id="tailv">6</span></div>

<div class="row" style="margin-top:8px">
 <label class="inl">board labels<input id="lbls" type="checkbox" checked></label>
 <label class="inl">tint runs<input id="tint" type="checkbox" checked></label>
 <label class="inl">face-down (bench)<input id="flip" type="checkbox"></label>
</div>

<details id="shd"><summary>Dome shading <span id="shInfo" style="font-size:12px"></span></summary>
 <div style="font-size:12px;color:var(--mute);margin:4px 0 6px">Gain per pixel ring (30 mm pitch), applied to everything sent except Board / Universe / Ends / Spot. Dim the rings where the dome is thin and the pixels show through; lift the dark ring inboard. 1.00 = no change, 0 = off. Saved on the ESP.</div>
 <div class="row"><label class="inl">apply<input id="shon" type="checkbox" checked></label><label class="inl" title="while you move a slider, that ring is lit magenta on the rig">light ring being tuned<input id="shhl" type="checkbox" checked></label><button class="sm" id="shReset">Reset to defaults</button><canvas id="shcv" width="300" height="60" style="width:300px;height:60px;border-radius:6px;border:1px solid var(--line);background:var(--cv)"></canvas></div>
 <div id="shk"></div>
</details>

<details><summary>Network / PixLite</summary>
 <div class="row">
  <label>protocol<select id="proto"><option value="0">Art-Net</option><option value="1">DDP</option></select></label>
  <label>target IP (broadcast ok)<input id="tip" type="text"></label>
 </div>
 <div class="row">
  <label>Art-Net universe of plan universe 1<input id="uni" type="number" min="1" max="32000"></label>
  <label>gateway Ethernet IP (/24)<input id="eip" type="text"></label>
 </div>
 <div class="row"><button id="bSave">Save &amp; apply</button><button id="bPoll">Find Art-Net nodes</button></div>
 <div class="row" style="margin-top:10px"><button id="bExport">Export config</button><button id="bImport">Import config…</button><input id="impFile" type="file" accept="application/json,.json" style="display:none"><span style="font-size:12px;color:var(--mute)">network · dome shading · current test state → one JSON file</span></div>
 <div id="impInfo" style="font-size:12px;color:var(--mute)"></div>
 <div id="nodes"></div>
 <div style="font-size:12px;color:var(--mute);margin-top:6px">OTA: join SUN-DISC, then <code>pio run -e ota -t upload</code> (192.168.4.1:3232). Output pauses during the upload.</div>
</details>

<script>
const $=id=>document.getElementById(id);
const touch={};document.addEventListener('input',e=>{if(e.target&&e.target.id)touch[e.target.id]=Date.now();},true);document.addEventListener('change',e=>{if(e.target&&e.target.id)touch[e.target.id]=Date.now();},true);
const recent=id=>(touch[id]||0)>Date.now()-1500;
const MODES=['Off','Solid','Chase','Wipe','Cycle','Board step','Universe step','Ends','Rainbow','Breathe','Radial','Spot','Scene'];
const MODE_HELP=['everything dark','selection in colour A','one head per selected run, wire order (tail = length)','fill each run head→tail, then drain','R · G · B · W · RGB white · RGBW white · A on the selection','one board at a time (ignores selection)','one universe at a time (ignores selection)','first px of each board = A, last px = B','hue by angle, rotating','A breathing 0→100 % — PSU / thermal test','ring expanding centre→rim','one pixel — tap the map or type its index','simulator patterns and shaders running on the ESP — pick one below'];
const BOARDS=['A','B1','B2','B3','B4','B5','B6','B7','B8','B9','B10','B11','B12','C1','C2','C3','C4','C5','C6','C7','C8','C9','C10','C11','C12'];
const RUN_START=[0,0,282,609,936,1263,1559,1871,2183], RUN_UNI0=[0,1,4,7,10,13,16,19,22];   // from sun_map.h — hover info only
const RUN_HUE=[0,28,60,95,150,190,230,275,320];
const RIM=886.5, PSU=[200,500,500];

let N=0,X=null,Y=null,BD=null,RN=null,cent=[];
let frame=null, st=null, ws=null, first=true;
let colA=[255,255,255,255], colB=[255,0,0,0], edit='A';
let flip=false, tint=true, lbls=true, dark=false;

// --- map + canvas
const cv=$('cv'), ctx=cv.getContext('2d');
const S=430/RIM, C=450, PR=30*S*0.42;
// Map frame = LED side, y-up, x-right (wedge 1 top-right, numbers clockwise, East on the right — as in the wiring plan).
// 'face-down (bench)' negates x: the rig lying connector-side up (wiring notes §9).
function xy(i){return [C+(flip?-1:1)*X[i]*0.1*S, C-Y[i]*0.1*S];}
function css(r,g,b,w){const W=[255,244,229];const f=v=>Math.min(255,Math.round(v));return `rgb(${f(r+w*W[0]/255)},${f(g+w*W[1]/255)},${f(b+w*W[2]/255)})`;}
function draw(){
  if(!X)return;
  ctx.clearRect(0,0,900,900);
  for(let i=0;i<N;i++){
    const [x,y]=xy(i); let f;
    if(frame){const o=i*4; if(frame[o]|frame[o+1]|frame[o+2]|frame[o+3]) f=css(frame[o],frame[o+1],frame[o+2],frame[o+3]);}
    if(!f) f=tint?(dark?`hsl(${RUN_HUE[RN[i]]} 40% 14%)`:`hsl(${RUN_HUE[RN[i]]} 45% 84%)`):(dark?'#1a1a1e':'#dcd9d1');
    ctx.fillStyle=f; ctx.beginPath(); ctx.arc(x,y,PR,0,6.2832); ctx.fill();
  }
  if(st&&st.mode===11&&st.spot<N){const [x,y]=xy(st.spot);ctx.strokeStyle='#ffb547';ctx.lineWidth=2;ctx.beginPath();ctx.arc(x,y,PR+5,0,6.2832);ctx.stroke();}
  if(lbls){ctx.font='600 20px system-ui,sans-serif';ctx.textAlign='center';ctx.textBaseline='middle';
    cent.forEach(([x,y],b)=>{const px=C+(flip?-1:1)*x*S,py=C-y*S;ctx.fillStyle=dark?'rgba(0,0,0,.55)':'rgba(255,255,255,.75)';ctx.fillRect(px-20,py-13,40,26);ctx.fillStyle=dark?'#e9e7e2':'#1d1c1a';ctx.fillText(BOARDS[b],px,py);});}
}
fetch('/map.bin').then(r=>r.arrayBuffer()).then(buf=>{
  const dv=new DataView(buf); N=buf.byteLength/6; X=new Int16Array(N);Y=new Int16Array(N);BD=new Uint8Array(N);RN=new Uint8Array(N);
  const acc=BOARDS.map(()=>[0,0,0]);
  for(let i=0;i<N;i++){X[i]=dv.getInt16(i*6,true);Y[i]=dv.getInt16(i*6+2,true);BD[i]=dv.getUint8(i*6+4);RN[i]=dv.getUint8(i*6+5);const a=acc[BD[i]];a[0]+=X[i]*0.1;a[1]+=Y[i]*0.1;a[2]++;}
  cent=acc.map(a=>[a[0]/a[2],a[1]/a[2]]);
  $('spot').max=N-1;
  $('info').textContent=`${N} px · 25 boards · 8 runs · tap a pixel for its address`;
  draw();
});
function hit(ev){
  const r=cv.getBoundingClientRect(),qx=(ev.clientX-r.left)/r.width*900,qy=(ev.clientY-r.top)/r.height*900;
  let best=-1,bd=(PR*2.2)**2;
  for(let i=0;i<N;i++){const [x,y]=xy(i),d=(x-qx)**2+(y-qy)**2;if(d<bd){bd=d;best=i;}}
  return best;
}
function pxInfo(i){const r=RN[i],ri=i-RUN_START[r],u=RUN_UNI0[r]+(ri>>7),ch=1+4*(ri&127);
  return `px ${i} · ${BOARDS[BD[i]]} · run ${r} (#${ri} on the run) · uni ${u} ch ${ch}–${ch+3} · (${(X[i]*0.1).toFixed(0)}, ${(Y[i]*0.1).toFixed(0)}) mm`;}
cv.addEventListener('pointerdown',e=>{const i=hit(e);if(i<0)return;$('info').textContent=pxInfo(i);
  if(st&&st.mode===11){send({spot:i});$('spot').value=i;}});
cv.addEventListener('pointermove',e=>{if(e.pointerType!=='mouse'||!X)return;const i=hit(e);if(i>=0)$('info').textContent=pxInfo(i);});

// --- controls
const send=o=>{if(ws&&ws.readyState===1)ws.send(JSON.stringify(Object.assign({t:'set'},o)));};
const modes=$('modes');
MODES.forEach((n,k)=>{const b=document.createElement('button');b.className='chip';b.textContent=n;b.title=MODE_HELP[k];b.onclick=()=>send({mode:k});modes.appendChild(b);});
const runs=$('runs');
const bAll=document.createElement('button');bAll.className='chip';bAll.textContent='All';bAll.onclick=()=>send({run:0x1FE,board:0});runs.appendChild(bAll);
const bNone=document.createElement('button');bNone.className='chip';bNone.textContent='None';bNone.onclick=()=>send({run:0,board:0});runs.appendChild(bNone);
const runBtn=[];
for(let r=1;r<=8;r++){const b=document.createElement('button');b.className='chip';b.textContent='run '+r;b.dataset.hue=RUN_HUE[r];
  b.title=['','Out1 CLK · A B1','Out1 DATA · B2 B3 B4','Out2 CLK · B5 B6 B7','Out2 DATA · B8 B9 B10','Out3 CLK · B11 B12 C1','Out3 DATA · C5 C4 C3 C2','Out4 CLK · C9 C8 C7 C6','Out4 DATA · C12 C11 C10'][r];
  b.style.borderColor=`hsl(${RUN_HUE[r]} 50% 65%)`;b.onclick=()=>{if(!st)return;send({run:st.run^(1<<r)});};runs.appendChild(b);runBtn[r]=b;}
const boards=$('boards'),boardBtn=[];
BOARDS.forEach((n,k)=>{const b=document.createElement('button');b.className='chip';b.textContent=n;b.onclick=()=>{if(!st)return;send({board:(st.board^(1<<k))>>>0});};boards.appendChild(b);boardBtn.push(b);});
$('bPrev').onclick=()=>ws&&ws.send(JSON.stringify({t:'step',d:-1}));
$('bNext').onclick=()=>ws&&ws.send(JSON.stringify({t:'step',d:1}));
$('hold').onchange=e=>send({hold:e.target.checked?1:0});
$('spot').onchange=e=>send({spot:+e.target.value});
$('sPrev').onclick=()=>{if(st)send({spot:(st.spot+N-1)%N});};
$('sNext').onclick=()=>{if(st)send({spot:(st.spot+1)%N});};

// colours
const SL=['r','g','b','w'];
function cur(){return edit==='A'?colA:colB;}
function showCol(){$('curA').style.background=css(...colA);$('curB').style.background=css(...colB);
  $('curA').classList.toggle('on',edit==='A');$('curB').classList.toggle('on',edit==='B');
  const c=cur();SL.forEach((k,j)=>{if(!recent(k))$(k).value=c[j];$(k+'v').textContent=c[j];});}
function pushCol(){send(edit==='A'?{a:colA.join(',')}:{b:colB.join(',')});}
SL.forEach((k,j)=>$(k).oninput=e=>{cur()[j]=+e.target.value;showCol();pushCol();});
document.querySelectorAll('.sw').forEach(b=>b.onclick=()=>{touch.col=Date.now();const c=b.dataset.c.split(',').map(Number);if(edit==='A')colA=c;else colB=c;showCol();pushCol();});
$('curA').onclick=()=>{edit='A';showCol();};$('curB').onclick=()=>{edit='B';showCol();};
showCol();

$('m').oninput=e=>{$('mv').textContent=Math.round(e.target.value/2.55)+'%';send({master:+e.target.value});};
$('spd').oninput=e=>{$('spdv').textContent=e.target.value;send({speed:+e.target.value});};
$('tail').oninput=e=>{$('tailv').textContent=e.target.value;send({tail:+e.target.value});};
$('lbls').onchange=e=>{lbls=e.target.checked;draw();};
$('tint').onchange=e=>{tint=e.target.checked;draw();};
$('flip').onchange=e=>{flip=e.target.checked;draw();};

// config
$('bSave').onclick=()=>{if(ws&&ws.readyState===1)ws.send(JSON.stringify({t:'cfg',proto:+$('proto').value,tip:$('tip').value.trim(),uni:+$('uni').value,eip:$('eip').value.trim()}));};
$('bPoll').onclick=()=>{if(ws&&ws.readyState===1)ws.send(JSON.stringify({t:'poll'}));$('nodes').textContent='polling…';};

// --- scenes (catalogue from the ESP)
let SC=null;
fetch('/scenes.json').then(r=>r.json()).then(j=>{SC=j;const box=$('scenes');let g='';
  j.scenes.forEach((sc,k)=>{if(sc.g!==g){g=sc.g;const h=document.createElement('div');h.style.cssText='font-size:11px;color:var(--mute);margin:6px 0 2px';h.textContent=g;box.appendChild(h);const row=document.createElement('div');row.className='row tight';row.id='scg'+k;box.appendChild(row);}
    const b=document.createElement('button');b.className='chip';b.textContent=sc.n;b.dataset.k=k;b.onclick=()=>send({mode:12,sc:k});box.lastChild.appendChild(b);});
  j.pal.forEach((n,k)=>{const o=document.createElement('option');o.value=k;o.textContent=n;$('pal').appendChild(o);});
  if(st)applyStatus(st);});
$('pal').onchange=e=>send({pal:+e.target.value});
$('ss').oninput=e=>{$('ssv').textContent=(e.target.value/10).toFixed(1)+'×';send({ss:+e.target.value});};
$('p1').oninput=e=>{$('p1v').textContent=(e.target.value/100).toFixed(2);send({p1:+e.target.value});};
$('con').oninput=e=>{$('conv').textContent=(e.target.value/100).toFixed(2);send({con:+e.target.value});};
$('wa').oninput=e=>{$('wav').textContent=(e.target.value/100).toFixed(2);send({wa:+e.target.value});};

// --- dome shading (per ring; rings 0–16 = 0–480 mm share one slider)
const RING_MM=i=>i*30, RING_BOARD=i=>i<=7?'A':(i<=22?'B':'C'), SH_FIRST=17;
let shk=new Array(29).fill(100),shT=null;
function shRow(label,id,val,onin){const row=document.createElement('div');row.className='sl';row.style.gridTemplateColumns='130px 1fr 44px';
  row.innerHTML=`<span>${label}</span><input type="range" id="${id}" min="0" max="200" value="${val}"><span id="${id}v">${(val/100).toFixed(2)}</span>`;$('shk').appendChild(row);$(id).oninput=onin;}
const hl=r=>{if(ws&&$('shhl').checked)ws.send(JSON.stringify({t:'shade',hl:r}));};
shRow('inner · 0–480 mm (A, B)','shin',100,e=>{const g=+e.target.value;for(let r=0;r<SH_FIRST;r++)shk[r]=g;$('shinv').textContent=(g/100).toFixed(2);shDraw();if(ws)ws.send(JSON.stringify({t:'shade',inner:g,hl:$('shhl').checked?-2:-1}));});
$('shin').addEventListener('pointerdown',()=>hl(-2));
for(let r=28;r>=SH_FIRST;r--){shRow(`${RING_MM(r)} mm · ${RING_BOARD(r)}${r===28?' (rim)':''}`,'shr'+r,shk[r],e=>{const g=+e.target.value;shk[r]=g;$('shr'+r+'v').textContent=(g/100).toFixed(2);shDraw();if(ws)ws.send(JSON.stringify({t:'shade',ring:r,g,hl:$('shhl').checked?r:-1}));});$('shr'+r).addEventListener('pointerdown',()=>hl(r));}
$('shhl').onchange=e=>{if(!e.target.checked&&ws)ws.send(JSON.stringify({t:'shade',hl:-1}));};
$('shon').onchange=e=>ws&&ws.send(JSON.stringify({t:'shade',on:e.target.checked?1:0}));
$('shReset').onclick=()=>ws&&ws.send(JSON.stringify({t:'shade',reset:1}));
function shDraw(){const c=$('shcv').getContext('2d');c.clearRect(0,0,300,60);const cs=getComputedStyle(document.documentElement);
  c.strokeStyle=cs.getPropertyValue('--line');c.beginPath();c.moveTo(0,30);c.lineTo(300,30);c.stroke();
  c.fillStyle=cs.getPropertyValue('--acc');for(let r=0;r<29;r++){const h=60*shk[r]/200;c.fillRect(r*300/29+1,60-h,300/29-2,h);}}
function shShow(){$('shin').value=shk[0];$('shinv').textContent=(shk[0]/100).toFixed(2);for(let r=SH_FIRST;r<29;r++){$('shr'+r).value=shk[r];$('shr'+r+'v').textContent=(shk[r]/100).toFixed(2);}shDraw();}
shDraw();

// --- config export / import
$('bExport').onclick=async()=>{const r=await fetch('/config.json');const txt=await r.text();const d=new Date(),ts=d.toISOString().slice(0,16).replace(/[-:T]/g,'').replace(/(\d{8})(\d{4})/,'$1-$2');
  const a=document.createElement('a');a.href=URL.createObjectURL(new Blob([txt],{type:'application/json'}));a.download=`sun-gateway-config-${ts}.json`;document.body.appendChild(a);a.click();setTimeout(()=>{URL.revokeObjectURL(a.href);a.remove();},2000);$('impInfo').textContent='exported '+a.download;};
$('bImport').onclick=()=>$('impFile').click();
$('impFile').onchange=async e=>{const f=e.target.files[0];if(!f)return;const txt=await f.text();try{JSON.parse(txt);}catch(err){$('impInfo').textContent='not valid JSON';return;}
  const r=await fetch('/config',{method:'POST',headers:{'Content-Type':'application/json'},body:txt});$('impInfo').textContent=r.ok?`imported ${f.name} — network, shading and state applied`:'import failed';first=true;e.target.value='';};

// --- status → UI
function pw(id,w,lim){const e=$(id);e.textContent=`${id[2]} ${w.toFixed(0)} W`;e.className='pill '+(w>lim*0.9?'bad':(w>lim*0.7?'warn':''));}
function applyStatus(s){
  const prev=st; st=s;
  const sync=(id,v,fmt)=>{if(!recent(id)){$(id).value=v;$(id+'v').textContent=fmt?fmt(v):v;}};
  $('eth').textContent=s.link?`eth ${s.speed}M · ${s.eip}`:'eth link down';$('eth').className='pill '+(s.link?'ok':'bad');
  $('fps').textContent=s.ota?'OTA…':`${s.fps} fps${s.ren?` · render ${s.ren} ms`:''} · ${s.proto?'DDP':'Art-Net'} → ${s.tip}${s.err?` · ${s.err} err`:''}`;
  if(s.build)$('build').textContent='build '+s.build+(s.dark?' · dark':' · light');
  if(s.heap!==undefined)$('build').textContent+=' · heap '+Math.round(s.heap/1024)+' kB';
  if(s.up!==undefined){const R={1:'power-on',3:'sw reset',4:'panic',5:'int WDT',6:'task WDT',7:'WDT',8:'deep sleep',9:'brown-out'};$('build').textContent+=' · up '+(s.up<120?s.up+' s':Math.round(s.up/60)+' min')+(s.rst&&s.rst!==1&&s.rst!==3?' · last reset: '+(R[s.rst]||s.rst):'');}
  if(s.shk&&s.shk.length===29){$('shon').checked=!!s.shon;$('shInfo').textContent=s.shon?'— on':'— off';if(!recent('shin')&&![...Array(29).keys()].some(r=>recent('shr'+r))){shk=s.shk.slice();shShow();}}
  if(!!s.dark!==dark){dark=!!s.dark;document.documentElement.dataset.theme=dark?'dark':'light';for(let r=1;r<=8;r++)runBtn[r].style.borderColor=`hsl(${RUN_HUE[r]} ${dark?'40% 30%':'50% 65%'})`;draw();}
  pw('pwD',s.pw[0],PSU[0]);pw('pwE',s.pw[1],PSU[1]);pw('pwW',s.pw[2],PSU[2]);
  [...modes.children].forEach((b,k)=>b.classList.toggle('on',k===s.mode));
  for(let r=1;r<=8;r++)runBtn[r].classList.toggle('on',!!(s.run>>r&1));
  boardBtn.forEach((b,k)=>b.classList.toggle('on',!!(s.board>>>k&1)));
  const nb=boardBtn.filter(b=>b.classList.contains('on')).length,nr=runBtn.filter(b=>b&&b.classList.contains('on')).length;
  $('selInfo').textContent=s.run===0x1FE?'— all':(nr||nb?`— ${nr} run${nr!==1?'s':''}${nb?` + ${nb} board${nb!==1?'s':''}`:''}`:'— nothing selected');
  if(nb)$('bdet').open=true;
  const stepMode=s.steps>1;$('stepRow').classList.toggle('hide',!stepMode);$('spotRow').classList.toggle('hide',s.mode!==11);
  $('sceneBox').classList.toggle('hide',s.mode!==12);
  if(SC){document.querySelectorAll('#scenes .chip').forEach(b=>b.classList.toggle('on',+b.dataset.k===s.sc));const scn=SC.scenes[s.sc];if(scn){$('p1n').textContent=scn.p;$('scInfo').textContent='— '+scn.g+' · '+scn.n+(scn.np?' (writes its own colours; palette optional)':'');}
    if(!recent('pal'))$('pal').value=s.pal;
    sync('ss',s.ss,v=>(v/10).toFixed(1)+'×');sync('p1',s.p1,v=>(v/100).toFixed(2));sync('con',s.con,v=>(v/100).toFixed(2));sync('wa',s.wa,v=>(v/100).toFixed(2));}
  $('lbl').textContent=s.lbl||MODE_HELP[s.mode]; if(s.mode===11)$('info').textContent=s.lbl;
  $('hold').checked=!!s.hold; if(!recent('spot'))$('spot').value=s.spot;
  sync('m',s.master,v=>Math.round(v/2.55)+'%');sync('spd',s.spd);sync('tail',s.tail);
  const a=s.a.split(',').map(Number),b=s.b.split(',').map(Number);
  if(first||!(SL.some(recent)||recent('col'))){colA=a;colB=b;showCol();}
  if(first){$('proto').value=s.proto;$('tip').value=s.tip;$('uni').value=s.uni;$('eip').value=s.eip;first=false;}
  if(s.nodes)$('nodes').textContent=s.nodes.length?s.nodes.map(n=>`${n.ip}  ${n.name}`).join('\n'):($('nodes').textContent==='polling…'?'polling…':'');
  if(!prev||prev.mode!==s.mode||prev.spot!==s.spot)draw();
}

// --- socket
function connect(){
  ws=new WebSocket(`ws://${location.host}/ws`);ws.binaryType='arraybuffer';
  ws.onopen=()=>{$('ws').textContent='ws ok';$('ws').className='pill ok';};
  ws.onclose=()=>{$('ws').textContent='ws —';$('ws').className='pill bad';setTimeout(connect,1000);};
  ws.onmessage=e=>{
    if(typeof e.data!=='string'){const d=new Uint8Array(e.data);if(d[0]===0x4c&&X&&d.length===1+N*4){frame=d.subarray(1);draw();}return;}
    const s=JSON.parse(e.data); if(s.t==='st')applyStatus(s);
  };
}
connect();
</script></body></html>
)SUNHTML";
