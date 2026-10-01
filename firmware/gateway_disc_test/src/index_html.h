// index_html.h — web page for gateway_disc_test (generated from page.html + simulator/sun-map.csv board A, 173 px)
#pragma once
#include <pgmspace.h>
static const char INDEX_HTML[] PROGMEM = R"SUNHTML(
<!doctype html>
<html lang="en"><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1,user-scalable=no">
<title>Sun · centre disc</title>
<style>
:root{--bg:#0d0d0f;--panel:#17171b;--line:#2a2a31;--text:#e9e7e2;--mute:#8b8a93;--acc:#ffb547;--bad:#ff5a4f;--ok:#4fd38a}
*{box-sizing:border-box;-webkit-tap-highlight-color:transparent}
html,body{margin:0;background:var(--bg);color:var(--text);font:14px/1.35 -apple-system,system-ui,Segoe UI,Roboto,sans-serif}
body{display:flex;flex-direction:column;min-height:100vh;max-width:760px;margin:0 auto;padding:10px 12px 24px}
header{display:flex;align-items:center;gap:8px;flex-wrap:wrap;font-size:12px;color:var(--mute)}
header b{color:var(--text);font-size:15px;margin-right:auto}
.pill{border:1px solid var(--line);border-radius:99px;padding:2px 8px;white-space:nowrap}
.pill.ok{color:var(--ok);border-color:#24583b}.pill.bad{color:var(--bad);border-color:#5e2724}
#wrap{position:relative;margin:8px 0;display:flex;gap:10px;align-items:flex-start}
#pane,#pane2{flex:1;min-width:0}
svg,canvas{width:100%;height:auto;display:block;touch-action:none;user-select:none}
canvas{border-radius:50%;background:#111114;border:1px solid #2a2a31;aspect-ratio:1}
.cap{font-size:11px;color:var(--mute);text-align:center;margin-top:4px}
circle.px{stroke:#333;stroke-width:1}
text.ix{font-size:7px;fill:#999;text-anchor:middle;dominant-baseline:central;pointer-events:none}
.row{display:flex;gap:8px;align-items:center;flex-wrap:wrap;margin:6px 0}
button,select,input[type=text],input[type=number]{background:var(--panel);color:var(--text);border:1px solid var(--line);border-radius:8px;padding:8px 12px;font:inherit}
button{cursor:pointer}button.on{border-color:var(--acc);color:var(--acc)}
button:active{transform:translateY(1px)}
.sw{width:34px;height:34px;border-radius:50%;border:2px solid var(--line);padding:0}
.sl{display:grid;grid-template-columns:18px 1fr 40px;gap:8px;align-items:center;margin:4px 0}
.sl span{color:var(--mute);font-size:12px;text-align:right}
input[type=range]{width:100%;accent-color:var(--acc)}
#cur{width:34px;height:34px;border-radius:8px;border:1px solid var(--line)}
details{background:var(--panel);border:1px solid var(--line);border-radius:10px;padding:8px 12px;margin-top:10px}
summary{cursor:pointer;color:var(--mute)}
label{display:flex;flex-direction:column;gap:3px;font-size:12px;color:var(--mute);flex:1;min-width:130px}
#info{font-size:12px;color:var(--mute);min-height:1.2em}
#nodes{font-size:12px;color:var(--mute);white-space:pre-wrap}
</style></head><body>
<header><b>Sun · centre disc</b>
<span id="ws" class="pill bad">ws —</span><span id="eth" class="pill bad">eth —</span><span id="fps" class="pill">— fps</span></header>

<div id="wrap"><div id="pane"><svg id="svg" viewBox="-236 -236 472 472"><circle cx="0" cy="0" r="226" fill="#111114" stroke="#2a2a31"/><g id="g"></g></svg><div class="cap">LEDs (ESP frame)</div></div>
<div id="pane2" style="display:none"><canvas id="gl" width="472" height="472"></canvas><div class="cap">shader · drag = focus · wheel = zoom</div></div></div>
<div id="info">tap or drag to paint · 173 px · run 1 · universes 1–2</div>

<div class="row">
 <button id="tPaint" class="on">Paint</button><button id="tErase">Erase</button><button id="tPick">Pick</button>
 <span style="flex:1"></span>
 <button id="bFill">Fill all</button><button id="bClear">Clear</button>
</div>
<div class="row">
 <div id="cur"></div>
 <button class="sw" data-c="0,0,0,255" style="background:#fff6e8" title="W only"></button>
 <button class="sw" data-c="255,120,0,120" style="background:#ffb060" title="warm"></button>
 <button class="sw" data-c="255,0,0,0" style="background:#f33"></button>
 <button class="sw" data-c="0,255,0,0" style="background:#3f3"></button>
 <button class="sw" data-c="0,0,255,0" style="background:#36f"></button>
 <button class="sw" data-c="255,255,255,0" style="background:#ddd" title="RGB white"></button>
 <button class="sw" data-c="0,0,0,0" style="background:#000" title="off"></button>
</div>
<div class="sl"><span>R</span><input id="r" type="range" max="255" value="0"><span id="rv">0</span></div>
<div class="sl"><span>G</span><input id="gg" type="range" max="255" value="0"><span id="ggv">0</span></div>
<div class="sl"><span>B</span><input id="b" type="range" max="255" value="0"><span id="bv">0</span></div>
<div class="sl"><span>W</span><input id="w" type="range" max="255" value="255"><span id="wv">255</span></div>
<div class="sl"><span>☀</span><input id="m" type="range" max="255" value="77"><span id="mv">30%</span></div>

<div class="row">
 <button id="bChase">Chase test</button>
 <label style="flex:2">chase speed (px/s)<input id="cs" type="range" min="1" max="60" value="8"></label>
 <label style="flex:0 0 auto;min-width:0">labels<input id="lbl" type="checkbox"></label>
</div>

<details id="fld"><summary>Fluid shader</summary>
 <div class="row"><button id="bFluid">Fluid off</button><select id="shsel"><option value="0">Neon fluid</option><option value="1">Black-hole sunset</option></select><span id="flinfo" style="font-size:12px;color:var(--mute)">shader runs per-pixel on the ESP · sliders are live</span></div>
 <div id="fls"></div>
 <div class="row"><button id="bFlReset">Reset</button></div>
</details>

<details><summary>Network / PixLite</summary>
 <div class="row">
  <label>protocol<select id="proto"><option value="0">Art-Net</option><option value="1">DDP</option></select></label>
  <label>target IP (broadcast ok)<input id="tip" type="text"></label>
 </div>
 <div class="row">
  <label>Art-Net universe for px 0<input id="uni" type="number" min="0" max="32767"></label>
  <label>gateway Ethernet IP (/24)<input id="eip" type="text"></label>
 </div>
 <div class="row"><button id="bSave">Save &amp; apply</button><button id="bPoll">Find Art-Net nodes</button></div>
 <div id="nodes"></div>
</details>

<script>
const PX=[[0,0],[-15,25.981],[-30,0],[-15,-25.981],[15,-25.981],[30,0],[15,25.981],[15.529,57.955],[42.426,42.426],[57.956,15.529],[57.956,-15.529],[42.426,-42.426],[15.529,-57.956],[-15.529,-57.956],[-42.426,-42.426],[-57.956,-15.529],[-57.956,15.529],[-42.426,42.426],[-15.529,57.955],[-15.628,88.633],[-45,77.942],[-68.944,57.851],[-84.572,30.782],[-90,0],[-84.572,-30.782],[-68.944,-57.851],[-45,-77.942],[-15.628,-88.633],[15.628,-88.633],[45,-77.942],[68.944,-57.851],[84.572,-30.782],[90,0],[84.572,30.782],[68.944,57.851],[45,77.942],[15.628,88.633],[15.04,119.054],[44.175,111.573],[70.534,97.082],[92.462,76.491],[108.579,51.094],[117.874,22.486],[119.763,-7.535],[114.127,-37.082],[101.319,-64.299],[82.146,-87.476],[57.81,-105.157],[29.843,-116.23],[0,-120],[-29.843,-116.23],[-57.81,-105.157],[-82.146,-87.476],[-101.319,-64.299],[-114.127,-37.082],[-119.763,-7.535],[-117.875,22.486],[-108.579,51.094],[-92.462,76.491],[-70.534,97.082],[-44.175,111.573],[-15.04,119.054],[-15.175,149.23],[-44.905,143.121],[-72.795,131.152],[-97.706,113.814],[-118.616,91.816],[-134.671,66.059],[-145.212,37.598],[-149.808,7.597],[-148.27,-22.714],[-140.663,-52.096],[-127.297,-79.345],[-108.719,-103.345],[-85.69,-123.115],[-59.153,-137.844],[-30.195,-146.929],[0,-150],[30.195,-146.929],[59.153,-137.844],[85.69,-123.115],[108.719,-103.345],[127.297,-79.345],[140.663,-52.096],[148.27,-22.714],[149.807,7.597],[145.212,37.598],[134.671,66.059],[118.616,91.816],[97.706,113.814],[72.795,131.152],[44.904,143.121],[15.175,149.23],[15.265,179.351],[45.356,174.192],[74.142,164.021],[100.795,149.132],[124.549,129.952],[144.72,107.034],[160.727,81.037],[172.11,52.708],[178.542,22.863],[179.838,-7.639],[175.96,-37.922],[167.02,-67.114],[153.275,-94.375],[135.121,-118.921],[113.08,-140.046],[87.785,-157.142],[59.965,-169.718],[30.42,-177.411],[0,-180],[-30.42,-177.411],[-59.965,-169.718],[-87.785,-157.142],[-113.08,-140.046],[-135.121,-118.921],[-153.275,-94.375],[-167.02,-67.114],[-175.96,-37.922],[-179.838,-7.639],[-178.542,22.863],[-172.11,52.708],[-160.727,81.037],[-144.72,107.034],[-124.549,129.952],[-100.796,149.132],[-74.142,164.021],[-45.356,174.192],[-15.265,179.351],[-15.329,209.44],[-45.66,204.976],[-75.019,196.143],[-102.778,183.13],[-128.346,166.214],[-151.18,145.756],[-170.791,122.19],[-186.762,96.021],[-198.752,67.805],[-206.507,38.144],[-209.86,7.67],[-208.74,-22.968],[-203.172,-53.116],[-193.273,-82.132],[-179.255,-109.397],[-161.416,-134.331],[-140.137,-156.402],[-115.871,-175.139],[-89.136,-190.144],[-60.501,-201.096],[-30.576,-207.762],[0,-210],[30.576,-207.762],[60.501,-201.096],[89.136,-190.144],[115.871,-175.139],[140.137,-156.402],[161.416,-134.331],[179.254,-109.397],[193.273,-82.132],[203.172,-53.116],[208.74,-22.968],[209.86,7.67],[206.507,38.144],[198.752,67.805],[186.762,96.021],[170.791,122.19],[151.18,145.756],[128.346,166.214],[102.778,183.13],[75.018,196.143],[45.66,204.976],[15.329,209.44]];            // [x,y] mm, y-up, index = output order
const N=PX.length;
const $=id=>document.getElementById(id);
const g=$('g'),svg=$('svg');
const frame=new Uint8Array(N*4);
let cur=[0,0,0,255],tool='paint',ws=null,pending=new Set(),labels=false;

// --- build disc
const circ=[],txt=[];
PX.forEach(([x,y],i)=>{
  const c=document.createElementNS('http://www.w3.org/2000/svg','circle');
  c.setAttribute('cx',x);c.setAttribute('cy',-y);c.setAttribute('r',11.5);c.setAttribute('class','px');
  c.setAttribute('fill','#050505');g.appendChild(c);circ.push(c);
  const t=document.createElementNS('http://www.w3.org/2000/svg','text');
  t.setAttribute('x',x);t.setAttribute('y',-y);t.setAttribute('class','ix');t.textContent=i;t.style.display='none';
  g.appendChild(t);txt.push(t);
});
function css(r,g_,b,w){ // preview of RGBW on screen
  const W=[255,244,229];const f=v=>Math.min(255,Math.round(v));
  return `rgb(${f(r+w*W[0]/255)},${f(g_+w*W[1]/255)},${f(b+w*W[2]/255)})`;
}
function paintLocal(i){const o=i*4;circ[i].setAttribute('fill',(frame[o]|frame[o+1]|frame[o+2]|frame[o+3])?css(frame[o],frame[o+1],frame[o+2],frame[o+3]):'#050505');}
function redraw(){for(let i=0;i<N;i++)paintLocal(i);}

// --- colour controls
const sl=['r','gg','b','w'];
function setCur(c){cur=c.slice();sl.forEach((k,j)=>{$(k).value=cur[j];$(k+'v').textContent=cur[j];});$('cur').style.background=css(...cur);}
sl.forEach((k,j)=>$(k).oninput=e=>{cur[j]=+e.target.value;$(k+'v').textContent=cur[j];$('cur').style.background=css(...cur);});
document.querySelectorAll('.sw').forEach(b=>b.onclick=()=>setCur(b.dataset.c.split(',').map(Number)));
setCur(cur);
['Paint','Erase','Pick'].forEach(t=>$('t'+t).onclick=()=>{tool=t.toLowerCase();['Paint','Erase','Pick'].forEach(u=>$('t'+u).classList.toggle('on',u===t));});
$('lbl').onchange=e=>{labels=e.target.checked;txt.forEach(t=>t.style.display=labels?'':'none');};

// --- painting (nearest pixel under the finger)
function hit(ev){
  const p=svg.createSVGPoint();p.x=ev.clientX;p.y=ev.clientY;
  const q=p.matrixTransform(g.getScreenCTM().inverse());
  let best=-1,bd=15*15;
  for(let i=0;i<N;i++){const dx=PX[i][0]-q.x,dy=-PX[i][1]-q.y,d=dx*dx+dy*dy;if(d<bd){bd=d;best=i;}}
  return best;
}
function apply(i){
  if(i<0)return;
  $('info').textContent=`px ${i}  ·  uni ${Math.floor(i/128)+1} ch ${(i%128)*4+1}  ·  (${PX[i][0].toFixed(1)}, ${PX[i][1].toFixed(1)}) mm`;
  if(tool==='pick'){setCur(Array.from(frame.slice(i*4,i*4+4)));return;}
  const c=tool==='erase'?[0,0,0,0]:cur;
  frame.set(c,i*4);paintLocal(i);pending.add(i);
}
let down=false;
svg.addEventListener('pointerdown',e=>{down=true;svg.setPointerCapture(e.pointerId);apply(hit(e));});
let lastPt=null;
svg.addEventListener('pointermove',e=>{
  if(!down||tool==='pick')return;
  const p={clientX:e.clientX,clientY:e.clientY};
  if(lastPt){const dx=p.clientX-lastPt.clientX,dy=p.clientY-lastPt.clientY,n=Math.ceil(Math.hypot(dx,dy)/6);
    for(let k=1;k<n;k++)apply(hit({clientX:lastPt.clientX+dx*k/n,clientY:lastPt.clientY+dy*k/n}));}
  apply(hit(p));lastPt=p;});
svg.addEventListener('pointerdown',e=>{lastPt={clientX:e.clientX,clientY:e.clientY};});
['pointerup','pointercancel'].forEach(t=>svg.addEventListener(t,()=>down=false));
function flush(){
  if(pending.size&&ws&&ws.readyState===1){
    const idx=[...pending].slice(0,200);idx.forEach(i=>pending.delete(i));
    const b=new Uint8Array(1+idx.length*6);b[0]=0x50;
    idx.forEach((i,k)=>{const o=1+k*6;b[o]=i>>8;b[o+1]=i&255;b.set(frame.slice(i*4,i*4+4),o+2);});
    ws.send(b);
  }
  requestAnimationFrame(flush);
}
requestAnimationFrame(flush);
const send=a=>{if(ws&&ws.readyState===1)ws.send(new Uint8Array(a));};
$('bFill').onclick=()=>{for(let i=0;i<N;i++)frame.set(cur,i*4);redraw();send([0x46,...cur]);};
$('bClear').onclick=()=>{frame.fill(0);redraw();send([0x46,0,0,0,0]);};
$('m').oninput=e=>{$('mv').textContent=Math.round(e.target.value/2.55)+'%';send([0x4d,+e.target.value]);};
let chase=false;
$('bChase').onclick=()=>{chase=!chase;send([0x43,chase?1:0,+$('cs').value,...cur]);};
$('cs').oninput=()=>{if(chase)send([0x43,1,+$('cs').value,...cur]);};

// --- config
$('bSave').onclick=()=>{if(ws&&ws.readyState===1)ws.send(JSON.stringify({t:'cfg',proto:+$('proto').value,tip:$('tip').value.trim(),uni:+$('uni').value,eip:$('eip').value.trim()}));};
$('bPoll').onclick=()=>{if(ws&&ws.readyState===1)ws.send(JSON.stringify({t:'poll'}));$('nodes').textContent='polling…';};
let cfgShown=false;

// --- fluid shader
const FL=[['speed','speed',0,3,0.01,0.6],['zoom','zoom',-3,3,0.01,0],['flow','flow (phase)',0,6.28,0.01,0],
 ['fx','focus x',-1,1,0.01,0],['fy','focus y',-1,1,0.01,0],['glow','glow near focus',0,2,0.01,0.8],
 ['bright','brightness',0.2,3,0.01,1.6],['sat','saturation',0,2,0.01,1.5],['wmix','white mix',0,1,0.01,0.5],['iter','iterations',1,8,1,8]];
// per-shader label + default overrides
const SH=[{name:'Neon fluid'},
 {name:'Black-hole sunset',lab:{zoom:'size',flow:'spiral phase',glow:'pull strength',iter:'noise octaves',fx:'pull x',fy:'pull y'},def:{speed:1,glow:1,iter:5,bright:1.4,sat:1.2}}];
let shId=0;
function shDefaults(id){const o={};FL.forEach(([k,,,,,d])=>o[k]=d);Object.assign(o,SH[id].def||{});return o;}
function shLabels(id){FL.forEach(([k,n])=>{const l=(SH[id].lab||{})[k]||n;$('fl_'+k).previousElementSibling.textContent=l;});}
const flv={};let flOn=false,flSync=false,flTimer=null,flQueue={};
const fls=$('fls');
FL.forEach(([k,n,lo,hi,st,d])=>{flv[k]=d;
  const row=document.createElement('div');row.className='sl';
  row.innerHTML=`<span style="width:auto;min-width:18px">${n}</span><input type="range" id="fl_${k}" min="${lo}" max="${hi}" step="${st}" value="${d}"><span id="flv_${k}">${d}</span>`;
  row.style.gridTemplateColumns='110px 1fr 40px';fls.appendChild(row);
  $('fl_'+k).oninput=e=>{flv[k]=+e.target.value;$('flv_'+k).textContent=flv[k];flQueue[k]=flv[k];flSend();};
});
function flSend(){if(flTimer)return;flTimer=setTimeout(()=>{flTimer=null;if(ws&&ws.readyState===1&&Object.keys(flQueue).length){ws.send(JSON.stringify(Object.assign({t:'fl'},flQueue)));flQueue={};}},50);}
function flSet(o){FL.forEach(([k])=>{if(o[k]!==undefined){flv[k]=o[k];$('fl_'+k).value=o[k];$('flv_'+k).textContent=o[k];}});}
function flShow(){$('bFluid').textContent=flOn?'Fluid on':'Fluid off';$('bFluid').classList.toggle('on',flOn);if(!flOn)redraw();}
$('bFluid').onclick=()=>{flOn=!flOn;flShow();if(ws&&ws.readyState===1)ws.send(JSON.stringify({t:'fl',on:flOn?1:0}));};
$('bFlReset').onclick=()=>{const o=shDefaults(shId);flSet(o);flQueue=Object.assign({},o);flSend();};
$('shsel').onchange=e=>{shId=+e.target.value;shLabels(shId);const o=shDefaults(shId);flSet(o);flQueue=Object.assign({sh:shId},o);flSend();glUse(shId);};

// --- WebGL twin of fluid.h (same math; shows RGB before the RGBW split)
const VS=`attribute vec2 a;varying vec2 v;void main(){v=a;gl_Position=vec4(a,0.,1.);}`;
const FS=`precision highp float;varying vec2 v;uniform float t,zoom,flow,glow,bright,sat;uniform vec2 fo;uniform int iter;
vec4 th(vec4 x){vec4 x2=x*x;return clamp(x*(27.+x2)/(27.+9.*x2),-1.,1.);}
void main(){vec2 p=v*(236./226.);if(dot(p,p)>1.09)discard;
 float z=4.-4.*abs(.7-dot(p,p))+zoom;vec2 f=p*z;vec4 O=vec4(0.);
 float md=length(p-fo)*2.;float boost=1.+(1.-smoothstep(0.,.5,md))*glow;float tt=t+flow;
 for(int j=1;j<=8;j++){if(j>iter)break;float jf=float(j);vec2 sF=sin(f)+1.;
  O+=vec4(sF.x,sF.y,sF.x,sF.y)*abs(f.x-f.y)*1.5*boost;
  f+=cos(f.yx*jf+vec2(0.,jf)+tt)/jf+.7;}
 vec4 c=th(7.*exp(z-4.-p.y*vec4(-1.,1.,2.,0.))/max(O,1e-3));
 float l=dot(c.rgb,vec3(.299,.587,.114));vec3 s=clamp(mix(vec3(l),c.rgb,sat),0.,1.)*bright;
 gl_FragColor=vec4(clamp(s,0.,1.),1.);}`;
const FS2=`precision highp float;varying vec2 v;uniform float t,zoom,flow,glow,bright,sat;uniform vec2 fo;uniform int iter;
vec3 permute(vec3 x){return mod(((x*34.0)+1.0)*x,289.0);}
float snoise(vec2 v){const vec4 C=vec4(0.211324865405187,0.366025403784439,-0.577350269189626,0.024390243902439);
 vec2 i=floor(v+dot(v,C.yy));vec2 x0=v-i+dot(i,C.xx);vec2 i1=(x0.x>x0.y)?vec2(1.,0.):vec2(0.,1.);
 vec4 x12=x0.xyxy+C.xxzz;x12.xy-=i1;i=mod(i,289.0);
 vec3 p=permute(permute(i.y+vec3(0.,i1.y,1.))+i.x+vec3(0.,i1.x,1.));
 vec3 m=max(0.5-vec3(dot(x0,x0),dot(x12.xy,x12.xy),dot(x12.zw,x12.zw)),0.);m=m*m;m=m*m;
 vec3 x=2.*fract(p*C.www)-1.;vec3 h=abs(x)-0.5;vec3 ox=floor(x+0.5);vec3 a0=x-ox;
 m*=1.79284291400159-0.85373472095314*(a0*a0+h*h);vec3 g;g.x=a0.x*x0.x+h.x*x0.y;g.yz=a0.yz*x12.xz+h.yz*x12.yw;return 130.*dot(m,g);}
float fbm(vec2 p,int o){float v=0.,a=.5,f=1.;for(int i=0;i<8;i++){if(i>=o)break;v+=a*snoise(p*f);a*=.5;f*=2.;}return v;}
void main(){vec2 p=v*(236./226.);if(dot(p,p)>1.09)discard;float sc=.5*exp(-zoom*.35);vec2 uv=p*sc;vec2 m=fo*sc;
 float dC=length(uv),dM=length(uv-m);float bhr=.1+.03*sin(t*.5),ehr=.25+.05*sin(t*.3);
 float mi=smoothstep(.5,0.,dM)*.5;float st=2.*glow+mi*3.;
 float ang=atan(uv.y,uv.x)+t*.2+flow+st/(dC+.001);vec2 d=vec2(cos(ang),sin(ang))*dC;
 float n=fbm(d*3.+t*.1,iter);float vg=smoothstep(-.5,.5,d.y);
 vec3 c=mix(vec3(.9,.6,.3),vec3(.2,.4,.8),vg);c=mix(c,vec3(.05,.05,.2),smoothstep(.5,1.,vg));c+=n*.1;
 if(dC>bhr*1.5){float sn=fbm(d*20.,3);c+=smoothstep(.75,.8,sn)*smoothstep(bhr*2.,ehr*3.,dC)*vec3(.9,.95,1.);}
 c=mix(c,vec3(0.),smoothstep(.01,-.01,dC-bhr));c+=smoothstep(ehr,bhr,dC)*vec3(.1,.2,.4)*.8;c*=smoothstep(1.,.3,dC*1.5);
 c=clamp(c,0.,1.);float l=dot(c,vec3(.299,.587,.114));c=clamp(mix(vec3(l),c,sat),0.,1.)*bright;gl_FragColor=vec4(clamp(c,0.,1.),1.);}`;
const cv=$('gl');let gl=cv.getContext('webgl'),U={},tOff=0,glOk=false,PR=[];
function glUse(id){if(!glOk)return;const pr=PR[id];gl.useProgram(pr);['t','zoom','flow','glow','bright','sat','fo','iter'].forEach(k=>U[k]=gl.getUniformLocation(pr,k));
 const a=gl.getAttribLocation(pr,'a');gl.enableVertexAttribArray(a);gl.vertexAttribPointer(a,2,gl.FLOAT,false,0,0);}
if(gl){const mk=(ty,src)=>{const sh=gl.createShader(ty);gl.shaderSource(sh,src);gl.compileShader(sh);if(!gl.getShaderParameter(sh,gl.COMPILE_STATUS))console.error(gl.getShaderInfoLog(sh));return sh;};
 const bf=gl.createBuffer();gl.bindBuffer(gl.ARRAY_BUFFER,bf);gl.bufferData(gl.ARRAY_BUFFER,new Float32Array([-1,-1,1,-1,-1,1,1,1]),gl.STATIC_DRAW);
 [FS,FS2].forEach(src=>{const pr=gl.createProgram();gl.attachShader(pr,mk(gl.VERTEX_SHADER,VS));gl.attachShader(pr,mk(gl.FRAGMENT_SHADER,src));gl.linkProgram(pr);PR.push(pr);});
 glOk=true;glUse(0);$('pane2').style.display='';}
function glFrame(){if(glOk&&flOn){const t=(performance.now()/1000+tOff)*flv.speed;
 gl.uniform1f(U.t,t);gl.uniform1f(U.zoom,flv.zoom);gl.uniform1f(U.flow,flv.flow);gl.uniform1f(U.glow,flv.glow);gl.uniform1f(U.bright,flv.bright);gl.uniform1f(U.sat,flv.sat);
 gl.uniform2f(U.fo,flv.fx,flv.fy);gl.uniform1i(U.iter,flv.iter|0);gl.drawArrays(gl.TRIANGLE_STRIP,0,4);}
 requestAnimationFrame(glFrame);}
requestAnimationFrame(glFrame);
// mouse / touch on the canvas → focus point (both sides), wheel → zoom
function cvPt(e){const r=cv.getBoundingClientRect();return [((e.clientX-r.left)/r.width*2-1)*236/226,-((e.clientY-r.top)/r.height*2-1)*236/226];}
let cvDown=false;
function cvFocus(e){const [x,y]=cvPt(e);flSet({fx:+x.toFixed(2),fy:+y.toFixed(2)});flQueue.fx=flv.fx;flQueue.fy=flv.fy;flSend();}
cv.addEventListener('pointerdown',e=>{cvDown=true;cv.setPointerCapture(e.pointerId);cvFocus(e);});
cv.addEventListener('pointermove',e=>{if(cvDown||e.pointerType==='mouse'&&e.buttons===0&&matchMedia('(hover:hover)').matches)cvFocus(e);});
['pointerup','pointercancel'].forEach(t=>cv.addEventListener(t,()=>cvDown=false));
cv.addEventListener('wheel',e=>{e.preventDefault();const z=Math.max(-3,Math.min(3,flv.zoom-e.deltaY*0.003));flSet({zoom:+z.toFixed(2)});flQueue.zoom=flv.zoom;flSend();},{passive:false});

// --- socket
function connect(){
  ws=new WebSocket(`ws://${location.host}/ws`);ws.binaryType='arraybuffer';
  ws.onopen=()=>{$('ws').textContent='ws ok';$('ws').className='pill ok';};
  ws.onclose=()=>{$('ws').textContent='ws —';$('ws').className='pill bad';setTimeout(connect,1000);};
  ws.onmessage=e=>{
    if(typeof e.data!=='string'){const d=new Uint8Array(e.data);
      if(d[0]===0x4c&&d.length===1+N*4){for(let i=0;i<N;i++){const o=1+i*4;circ[i].setAttribute('fill',(d[o]|d[o+1]|d[o+2]|d[o+3])?css(d[o],d[o+1],d[o+2],d[o+3]):'#050505');}return;}
      if(d[0]===0x53&&d.length===1+N*4&&!pending.size&&!down&&!flOn){frame.set(d.subarray(1));redraw();}return;}
    const s=JSON.parse(e.data);
    if(s.t==='st'){
      $('eth').textContent=s.link?`eth ${s.speed}M · ${s.eip}`:'eth link down';$('eth').className='pill '+(s.link?'ok':'bad');
      $('fps').textContent=`${s.fps} fps · ${s.proto?'DDP':'Art-Net'} → ${s.tip}${s.err?` · ${s.err} err`:''}`;
      $('m').value=s.master;$('mv').textContent=Math.round(s.master/2.55)+'%';
      chase=!!s.chase;$('bChase').classList.toggle('on',chase);$('bChase').textContent=chase?`Chase · px ${s.ci}`:'Chase test';
      if(s.fl){const on=!!s.fl.on;if(on!==flOn){flOn=on;flShow();}if(s.fl.sh!==undefined&&s.fl.sh!==shId){shId=s.fl.sh;$('shsel').value=shId;shLabels(shId);glUse(shId);}
        if(!flSync){flSet(s.fl);flSync=true;$('fld').open=on;}
        if(s.fl.t!==undefined)tOff=s.fl.t-performance.now()/1000;}
      if(!cfgShown){$('proto').value=s.proto;$('tip').value=s.tip;$('uni').value=s.uni;$('eip').value=s.eip;cfgShown=true;}
      if(s.nodes)$('nodes').textContent=s.nodes.length?s.nodes.map(n=>`${n.ip}  ${n.name}`).join('\n'):($('nodes').textContent==='polling…'?'polling…':'no replies yet');
    }
  };
}
connect();
</script></body></html>

)SUNHTML";
