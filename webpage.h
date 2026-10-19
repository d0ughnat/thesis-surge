#pragma once
const char PAGE_HTML[] PROGMEM = R"rawliteral(
<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>AC Power Monitor</title><style>
:root{--bg:#f5f5f5;--surface:#fff;--border:#e0e0e0;--text:#1a1a1a;--muted:#999;--normal:#16a34a;--warning:#f59e0b;--fault:#dc2626;--normal-bg:#dcfce7;--warning-bg:#fef3c7;--fault-bg:#fee2e2}
*{box-sizing:border-box;margin:0;padding:0}
body{background:var(--bg);color:var(--text);font-family:-apple-system,BlinkMacSystemFont,Segoe UI,Roboto,sans-serif;min-height:100vh;padding:24px 16px;display:flex;align-items:flex-start;justify-content:center}
.wrapper{width:100%;max-width:720px;display:flex;flex-direction:column;gap:12px;min-width:0}
h1{font-size:1.1rem;font-weight:600;text-align:center;color:var(--text);letter-spacing:.5px;margin-bottom:4px}
.tab-nav{display:flex;border-bottom:1px solid var(--border);flex-wrap:wrap;gap:4px}
.tab-btn{background:none;border:none;border-bottom:2px solid transparent;color:var(--muted);font-size:.75rem;padding:10px 16px;cursor:pointer;transition:color .2s,border-color .2s;white-space:nowrap}
.tab-btn.active{color:var(--text);border-bottom-color:var(--text)}
.tab-pane{display:none}
.tab-pane.active{display:block}
.gauges{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:10px}
.card{background:var(--surface);border:1px solid var(--border);border-radius:8px;padding:16px 12px;text-align:center}
.val{font-family:SF Mono,Cascadia Code,Consolas,monospace;font-size:1.8rem;display:block;margin:6px 0;color:var(--text)}
.val-sm{font-family:SF Mono,Cascadia Code,Consolas,monospace;font-size:1.4rem;display:block;margin:6px 0;color:var(--text)}
.unit{font-size:.65rem;color:var(--muted);text-transform:uppercase;letter-spacing:.5px}
.status-badge{font-family:SF Mono,Cascadia Code,Consolas,monospace;font-size:.6rem;padding:2px 10px;border-radius:99px;border:1px solid;margin-top:6px;display:inline-flex;align-items:center;gap:4px;font-weight:600;text-transform:uppercase}
.status-badge.normal{background:var(--normal-bg);color:var(--normal);border-color:var(--normal)}
.status-badge.warning{background:var(--warning-bg);color:var(--warning);border-color:var(--warning)}
.status-badge.fault{background:var(--fault-bg);color:var(--fault);border-color:var(--fault)}
.status-badge.unknown{background:#f3f4f6;color:var(--muted);border-color:var(--border)}
.conn-status{display:flex;align-items:center;justify-content:center;gap:6px;font-size:.65rem;color:var(--muted);margin-bottom:8px;font-family:SF Mono,Cascadia Code,Consolas,monospace}
.conn-dot{width:8px;height:8px;border-radius:50%;background:var(--muted);transition:background .2s}
.conn-dot.connected{background:var(--normal)}
.conn-dot.reconnecting{background:var(--warning);animation:pulse 1s infinite}
.conn-dot.disconnected{background:var(--fault)}
@keyframes pulse{0%,100%{opacity:1}50%{opacity:.4}}
.chart-card{background:var(--surface);border:1px solid var(--border);border-radius:8px;padding:14px}
.chart-label{font-size:.65rem;color:var(--muted);text-transform:uppercase;letter-spacing:.5px;margin-bottom:8px}
canvas{width:100%;display:block;border-radius:4px}
.uptime{font-family:SF Mono,Cascadia Code,Consolas,monospace;font-size:.6rem;color:var(--muted);text-align:center}
.d-val-stable{color:#999}.d-val-mod{color:#555}.d-val-fast{color:#111;font-weight:600}
.surge-banner{background:var(--surface);border:1px solid var(--border);border-radius:8px;padding:14px 16px;display:flex;align-items:center;justify-content:space-between;flex-wrap:wrap;gap:8px}
.surge-today{font-size:.85rem;color:var(--text)}
.surge-today span{font-family:monospace;font-size:1.4rem;color:var(--text);font-weight:600}
.btn{background:none;border:1px solid var(--border);color:var(--muted);border-radius:6px;padding:8px 16px;cursor:pointer;font-size:.7rem;transition:border-color .2s,color .2s}
.btn:hover{border-color:var(--text);color:var(--text)}
.btn-danger{border-color:var(--fault);color:var(--fault)}.btn-danger:hover{background:var(--fault);color:#fff}
.log-table{width:100%;border-collapse:collapse;font-family:monospace;font-size:.8rem}
.log-table th{color:var(--muted);font-weight:normal;text-align:left;padding:8px 12px;border-bottom:1px solid var(--border);font-size:.7rem;text-transform:uppercase;letter-spacing:.5px}
.log-table td{padding:8px 12px;border-bottom:1px solid #f0f0f0}
.log-table tr:last-child td{border-bottom:none}
.log-v{font-weight:600}
.log-empty{text-align:center;color:var(--muted);padding:32px;font-size:.8rem}
.log-total{font-size:.6rem;color:var(--muted);text-align:right;margin-top:4px}
.alert-banner{display:none;background:#b91c1c;color:#fff;text-align:left;padding:12px 16px;font-size:.8rem;font-weight:600;border-radius:8px;letter-spacing:.3px;overflow-wrap:anywhere}
.alert-banner.show{display:block}
.alert-banner button{float:right;margin-left:12px;background:#fff;color:#7f1d1d;border:0;border-radius:5px;padding:8px 12px;min-height:40px;cursor:pointer}
.modal-bg{display:none;position:fixed;inset:0;background:rgba(0,0,0,.4);z-index:100;align-items:center;justify-content:center}
.modal-bg.open{display:flex}
.modal{background:var(--surface);border:1px solid var(--border);border-radius:12px;padding:24px;max-width:300px;width:90%;text-align:center}
.modal h2{font-size:.95rem;color:var(--text);margin-bottom:8px;font-weight:600}
.modal p{font-size:.75rem;color:var(--muted);margin-bottom:20px;line-height:1.5}
.modal-btns{display:flex;gap:10px;justify-content:center}
.btn-cancel{background:none;border:1px solid var(--border);color:var(--muted);border-radius:6px;padding:8px 20px;cursor:pointer;font-size:.75rem}
.btn-confirm{background:none;border:1px solid var(--text);color:var(--text);border-radius:6px;padding:8px 20px;cursor:pointer;font-size:.75rem}
.event-detail{background:var(--surface);border:1px solid var(--border);border-radius:8px;padding:16px}
.event-detail h3{font-size:.85rem;margin-bottom:8px}
.event-detail .kv{display:grid;grid-template-columns:auto 1fr;gap:6px 12px;font-size:.75rem;font-family:monospace}
.event-detail .kv{overflow-wrap:anywhere;grid-template-columns:minmax(0,1fr) minmax(0,2fr)}
.event-detail .kv span:first-child{color:var(--muted)}
.event-detail .actions{display:flex;gap:8px;margin-top:12px;flex-wrap:wrap}
.hidden{display:none!important}
@media(max-width:600px){.gauges{grid-template-columns:1fr}.tab-btn{padding:10px 12px;font-size:.7rem}.log-table{font-size:.72rem}.log-table th,.log-table td{padding:8px 6px}}
</style></head><body><div class="wrapper">
<h1>AC Power Monitor</h1>
<div class="conn-status" id="connStatus"><span class="conn-dot" id="connDot"></span><span class="conn-text" id="connText">Connecting...</span></div>
<div class="alert-banner" id="alertBanner">SURGE DETECTED — LOAD ISOLATED</div>
<div class="tab-nav" role="tablist">
  <button class="tab-btn active" role="tab" aria-selected="true" onclick="switchTab('isolation',this)">Now</button>
  <button class="tab-btn" role="tab" aria-selected="false" onclick="switchTab('events',this)">Alerts</button>
</div>

<div id="tab-isolation" class="tab-pane active" role="tabpanel">
  <div class="gauges">
    <div class="card"><div class="unit">Voltage</div><span class="val" id="vval">--.-</span><div class="unit">Volts RMS · Latest reading</div><div class="unit" id="voltageRange">Since restart: --</div><span class="status-badge unknown" id="badge">INIT</span></div>
    <div class="card"><div class="unit">Current</div><span class="val" id="ival">N/A</span><div class="unit">ACS712 · GPIO35</div><div class="unit" id="currentDiag">ADC: --</div></div>
  </div>
  <div class="card"><div class="unit">Rate of Change</div><span class="val-sm d-val-stable" id="dvdt">0.00 V/s</span><div class="unit">dV/dt — Voltage Rate</div></div>
  <div class="chart-card"><div class="chart-label">Live Voltage Drop</div><canvas id="liveVChart" height="110"></canvas></div>
  <div class="chart-card"><div class="chart-label">Live Current</div><canvas id="liveIChart" height="110"></canvas></div>
  <div class="card"><div class="unit">Isolation Relay</div><span class="val-sm" id="relayState">--</span><div class="unit" id="relayReason"></div></div>
  <div class="card"><div class="unit">Relay Test</div><button class="btn" id="relayTestBtn" onclick="toggleRelayTest()">TEST OFF</button><div class="unit" id="relayTestNote">Normal relay control</div></div>
  <div class="card"><div class="unit">YCCH6 Surge Demo</div><button class="btn btn-danger" id="surgeDemoBtn" onclick="simulateSurge()">SIMULATE SURGE TRIP</button><div class="unit" id="surgeDemoNote">Opens relay for 10 seconds</div></div>
  <div class="uptime">Uptime: <span id="uptime">--</span>s | Auto-refresh 1s</div>
</div>

<div id="tab-voltage" class="tab-pane" role="tabpanel">
  <div class="chart-card"><div class="chart-label">Voltage RMS — Last 60 Readings</div><canvas id="vChart" height="140"></canvas></div>
  <div class="chart-card"><div class="chart-label">Current history · ACS712</div><canvas id="iChart" height="140"></canvas><div class="uptime" id="currentNote">Waiting for a usable ACS712 signal.</div></div>
  <div class="chart-card"><div class="chart-label">dV/dt — Last 60 Readings</div><canvas id="dChart" height="140"></canvas></div>
  <div class="uptime">Time relative to latest sample. Gaps indicate missing samples.</div>
</div>

<div id="tab-events" class="tab-pane" role="tabpanel">
  <div class="surge-banner"><div class="surge-today">Today's Surges: <span id="todayCnt">0</span></div><button class="btn btn-danger" onclick="openModal()">Clear Event History</button></div>
  <div class="chart-card" style="padding:0;overflow-x:auto;">
    <table class="log-table"><thead><tr><th>#</th><th>Date & Time</th><th>Type</th><th>RMS Voltage</th><th>Action</th></tr></thead><tbody id="logBody"><tr><td colspan="5" class="log-empty">Loading...</td></tr></tbody></table>
  </div>
  <div class="log-total">Total stored: <span id="logTotal">0</span> / 150 entries | Auto-delete: 30 days & 150-entry cap</div>
  <div class="event-detail hidden" id="eventDetail">
    <h3>Alert Details</h3>
    <div class="kv" id="eventKV"></div>
    <p id="graphStatus" class="uptime"></p>
    <div class="chart-card"><div class="chart-label">Saved voltage - V RMS</div><canvas id="savedVChart" style="height:180px" height="180"></canvas></div>
    <div class="chart-card"><div class="chart-label" id="savedILabel">Saved current - A</div><canvas id="savedIChart" style="height:180px" height="180"></canvas></div>
    <div class="actions"><a class="btn" id="eventExport" download>Export Event JSON</a></div>
  </div>
  <p class="log-empty" id="noEventMsg">Select an alert to view its saved graphs</p>
</div>

<div class="modal-bg" id="modalBg"><div class="modal"><h2>Clear All Event History?</h2><p>This will permanently delete all logged surge events from storage. This cannot be undone.</p><div class="modal-btns"><button class="btn-cancel" onclick="closeModal()">Cancel</button><button class="btn-confirm" onclick="confirmClear()">Yes, Clear</button></div></div></div>
</div>

<script>
var selectedEvent=null, eventRequest=0;
var MAX=60, vData=[], iData=[], dData=[], selectedEventId=null, lastProtectionEventId=localStorage.getItem('lastProtectionEventId')||'', dismissedEventId=localStorage.getItem('dismissedEventId')||'';
var liveVC=document.getElementById('liveVChart'), liveIC=document.getElementById('liveIChart'), vC=document.getElementById('vChart'), iC=document.getElementById('iChart'), dC=document.getElementById('dChart');
var liveVX=liveVC.getContext('2d'), liveIX=liveIC.getContext('2d'), vX=vC.getContext('2d'), iX=iC.getContext('2d'), dX=dC.getContext('2d');

function switchTab(name,btn){
  document.querySelectorAll('.tab-pane').forEach(p=>p.classList.remove('active'));
  document.querySelectorAll('.tab-btn').forEach(b=>{b.classList.remove('active');b.setAttribute('aria-selected','false')});
  document.getElementById('tab-'+name).classList.add('active');
  btn.classList.add('active');
  btn.setAttribute('aria-selected','true');
  if(name==='events'){fetchSurges();drawSavedGraphs();}
  if(name==='voltage')fetchHistory();
  if(name==='isolation')fetchData();
}

function openModal(){document.getElementById('modalBg').classList.add('open')}
function closeModal(){document.getElementById('modalBg').classList.remove('open')}
function confirmClear(){closeModal();var token=prompt('Enter admin token to clear history:');if(!token)return;var x=new XMLHttpRequest();x.open('POST','/clearlogs',true);x.setRequestHeader('Content-Type','application/x-www-form-urlencoded');x.onload=function(){if(x.status===200){clearSelectedEvent();fetchSurges();}else alert('Failed: '+x.responseText);};x.send('token='+encodeURIComponent(token));}
function toggleRelayTest(){var b=document.getElementById('relayTestBtn'),active=b.getAttribute('data-active')==='1',x=new XMLHttpRequest();x.open('POST','/relay-test',true);x.setRequestHeader('Content-Type','application/x-www-form-urlencoded');x.onload=function(){if(x.status===200)fetchData();else alert('Relay test failed: '+x.responseText)};x.send('off='+(active?'1':'0'))}
function simulateSurge(){var x=new XMLHttpRequest();x.open('POST','/simulate-surge',true);x.onload=function(){if(x.status===200){fetchData();fetchSurges()}else alert('Surge demo failed: '+x.responseText)};x.send('')}

function safe(v,fb){return(v!==undefined&&v!==null&&!isNaN(v))?v:fb}
function rsz(c){var h=c.getAttribute("data-height")||c.getAttribute("height");c.setAttribute("data-height",h);c.style.height=h+"px";c.width=c.offsetWidth*devicePixelRatio;c.height=Number(h)*devicePixelRatio}

function drawChart(ctx,data,col,minY,maxY){
  var w=ctx.canvas.width,h=ctx.canvas.height;ctx.clearRect(0,0,w,h);
  if(!data||data.length<2)return;
  var pad=h*.08,range=(maxY-minY)||1;
  function toY(v){return h-pad-((v-minY)/range)*(h-pad*2)}
  ctx.strokeStyle='rgba(0,0,0,0.06)';ctx.lineWidth=1;
  for(var i=0;i<=4;i++){var gy=pad+(i/4)*(h-pad*2);ctx.beginPath();ctx.moveTo(0,gy);ctx.lineTo(w,gy);ctx.stroke()}
  ctx.strokeStyle=col;ctx.lineWidth=1.5*devicePixelRatio;ctx.lineJoin='round';ctx.lineCap='round';ctx.beginPath();
  var started=false;
  for(var j=0;j<data.length;j++){var x=(j/(Math.max(data.length-1,1)))*w;if(data[j]===null||data[j]===undefined||isNaN(data[j])){started=false;continue}var y=toY(data[j]);if(!started){ctx.moveTo(x,y);started=true}else ctx.lineTo(x,y)}
  ctx.stroke();ctx.lineTo((data.length-1)/(MAX-1)*w,h);ctx.lineTo(0,h);ctx.closePath();ctx.fillStyle=col+'18';ctx.fill()
}

function voltageRange(data){
  var vals=data.filter(function(v){return v!==null&&v!==undefined&&!isNaN(v)});
  if(!vals.length)return{min:180,max:260};
  var lo=Math.min.apply(null,vals),hi=Math.max.apply(null,vals),span=Math.max(8,hi-lo);
  return{min:Math.max(0,lo-span*.35),max:hi+span*.35}
}

function drawDvdtChart(ctx,data){
  var absMax=Math.max.apply(null,data.map(Math.abs));if(absMax<1)absMax=10;
  var col=absMax>=10?'#111':absMax>=5?'#555':'#999';drawChart(ctx,data,col,-absMax,absMax)
}

function redraw(){rsz(liveVC);rsz(liveIC);rsz(vC);rsz(iC);rsz(dC);var vr=voltageRange(vData);drawChart(liveVX,vData,'#2563eb',vr.min,vr.max);drawChart(liveIX,iData,'#16a34a',0,2);drawChart(vX,vData,'#2563eb',vr.min,vr.max);drawChart(iX,iData,'#666',0,2);drawDvdtChart(dX,dData)}
window.addEventListener('resize',function(){redraw();drawSavedGraphs()});setTimeout(redraw,100);

function dvdtClass(v){var a=Math.abs(v);if(a>=10)return'd-val-fast';if(a>=5)return'd-val-mod';return'd-val-stable'}

function showNoConn(){var b=document.getElementById('badge');b.textContent='NO CONN';b.className='status-badge unknown';setConn('disconnected')}

function setConn(state){
  var dot=document.getElementById('connDot'),text=document.getElementById('connText');
  dot.className='conn-dot '+state;
  if(state==='connected')text.textContent='Connected';
  else if(state==='reconnecting')text.textContent='Reconnecting...';
  else text.textContent='Disconnected'
}

function showProtectionAlert(eventId,message){var ab=document.getElementById('alertBanner');if(!eventId||eventId===dismissedEventId)return;ab.textContent=message||'SURGE DETECTED - LOAD ISOLATED';ab.classList.add('show');if(!document.getElementById('dismissAlert')){var b=document.createElement('button');b.id='dismissAlert';b.textContent='Dismiss';b.onclick=function(){dismissedEventId=eventId;localStorage.setItem('dismissedEventId',eventId);ab.classList.remove('show')};ab.insertBefore(b,ab.firstChild)}}

async function fetchData(){
  try{
    var r=await fetch('/data',{cache:'no-store'});if(!r.ok)throw 0;
    var d=await r.json();
    document.getElementById('vval').textContent=d.sensorOk&&d.voltage!==null?safe(d.voltage,0).toFixed(1):'--.-';
    document.getElementById('voltageRange').textContent=Number.isFinite(d.voltage_min_since_boot)&&Number.isFinite(d.voltage_max_since_boot)?'Since restart: '+d.voltage_min_since_boot.toFixed(1)+'–'+d.voltage_max_since_boot.toFixed(1)+' V RMS':'Since restart: --';
    document.getElementById('ival').textContent=d.current===null?'N/A':safe(d.current,0).toFixed(3);
    document.getElementById('currentDiag').textContent=d.current_estimated?'DEMO LOAD FROM VOLTAGE':'ADC: '+safe(d.current_adc_mean,'--')+' | '+(d.current_sensor_ok?'OK':'CHECK WIRING');
    document.getElementById('uptime').textContent=safe(d.uptime,'--');
    document.getElementById('todayCnt').textContent=safe(d.todaySurges,0);
    var labels={normal:'ON',waiting:'OFF — Recovery Wait',checking:'OFF — Checking',lockout:'OFF — Lockout'};
    var rs=document.getElementById('relayState');rs.textContent=labels[d.relayState]||'--';
    rs.style.fontWeight=d.relayState==='lockout'?'bold':'normal';
    document.getElementById('relayReason').textContent=d.fault_reason?d.fault_reason.replaceAll('_',' '):'';
    var rt=document.getElementById('relayTestBtn'),rn=document.getElementById('relayTestNote');if(rt){var ra=!!d.relay_test_active;rt.setAttribute('data-active',ra?'1':'0');rt.textContent=ra?'NORMAL':'TEST ON';rn.textContent=ra?'Relay forced ON for sensor test':'Normal relay control'}
    var sd=document.getElementById('surgeDemoBtn'),sn=document.getElementById('surgeDemoNote');if(sd){sd.disabled=!!d.demo_trip_active;sd.textContent=d.demo_trip_active?'TRIP ACTIVE':'SIMULATE SURGE TRIP';sn.textContent=d.demo_trip_active?'YCCH6 coil relay open, '+safe(d.demo_trip_remaining_ms,0)+' ms remaining':'Opens relay for 10 seconds'}
    var ab=document.getElementById('alertBanner');ab.classList.remove('show');
    var dv=safe(d.dvdt,0),dvEl=document.getElementById('dvdt');dvEl.textContent=(dv>=0?'+':'')+dv.toFixed(2)+' V/s';dvEl.className='val-sm '+dvdtClass(dv);
    var b=document.getElementById('badge'),sys=d.systemState||'UNKNOWN';b.textContent=sys;b.className='status-badge '+sys.toLowerCase();
    if(d.latest_event&&d.latest_event.id&&d.latest_event.t==='SURGE'){lastProtectionEventId=d.latest_event.id;localStorage.setItem('lastProtectionEventId',lastProtectionEventId);showProtectionAlert(lastProtectionEventId,'SURGE DETECTED - LOAD ISOLATED')}
    if(lastProtectionEventId===dismissedEventId)document.getElementById('alertBanner').classList.remove('show');
    setConn('connected')
  }catch{showNoConn()}
}

async function fetchHistory(){
  try{var r=await fetch('/history',{cache:'no-store'});if(!r.ok)return;var h=await r.json();if(Array.isArray(h.voltage))vData=h.voltage;if(Array.isArray(h.current))iData=h.current;if(Array.isArray(h.dvdt))dData=h.dvdt;var note=document.getElementById('currentNote');if(note)note.textContent=iData.some(function(v){return v!==null&&v!==undefined&&!isNaN(v)})?'Current shown for demo/history.':'Current history is unavailable until voltage is valid.';redraw()}catch{}
}

async function fetchSurges(){
  try{var r=await fetch('/surges?all=1',{cache:'no-store'});if(!r.ok)return;var d=await r.json();document.getElementById('todayCnt').textContent=d.todayCount||0;document.getElementById('logTotal').textContent=d.count||0;if(selectedEventId&&!(d.entries||[]).some(e=>e.id===selectedEventId))clearSelectedEvent();var tb=document.getElementById('logBody');if(!d.entries||!d.entries.length){tb.innerHTML='<tr><td colspan="5" class="log-empty">No surge events recorded.</td></tr>';return}var html='';for(var i=0;i<d.entries.length;i++){var e=d.entries[i],ev=safe(e.v,null),vv=ev===null?'N/A':parseFloat(ev).toFixed(1)+' V';html+='<tr><td>'+(d.entries.length-i)+'</td><td>'+e.dt+'</td><td>'+e.t+'</td><td class="log-v">'+vv+'</td><td><button class="btn" onclick="showEvent(\''+e.id+'\')">View</button></td></tr>';}tb.innerHTML=html}catch{}
}

function drawSavedGraphs(){
  var e=selectedEvent,h=e&&e.history,rows=h&&h.status==='available'?h.samples:[];
  document.getElementById('graphStatus').textContent=!e?'':h&&h.status==='pending'?'Graph pending - select the alert again shortly':!rows||!rows.length?'Graph unavailable':'Saved readings ending at detection. Time in seconds relative to trigger; gaps are missing readings.';
  document.getElementById('savedILabel').textContent='Saved current - A'+(rows&&rows.some(p=>p.current_estimated)?' - Estimated values shown dashed':'');
  ['voltage','current'].forEach(function(key){
    var c=document.getElementById(key==='voltage'?'savedVChart':'savedIChart');rsz(c);var x=c.getContext('2d'),w=c.width,d=devicePixelRatio,height=c.height;
    x.clearRect(0,0,w,height);if(!rows||!rows.length)return;
    var vals=rows.map(p=>p[key]).filter(Number.isFinite),lo=vals.length?Math.min(...vals):0,hi=vals.length?Math.max(...vals):1;
    if(hi===lo){lo=Math.max(0,lo-1);hi+=1}
    var left=44*d,right=w-12*d,top=12*d,bottom=height-32*d;
    var first=rows[0].sample_us,last=e.trigger_us,span=Math.max(1,last-first);
    var px=p=>left+(p.sample_us-first)/span*(right-left),py=v=>bottom-(v-lo)/(hi-lo)*(bottom-top);
    x.font=10*d+'px sans-serif';x.fillStyle='#555';x.fillText(hi.toFixed(1),0,top+4*d);x.fillText(lo.toFixed(1),0,bottom);
    x.fillText(((first-last)/1e6).toFixed(2)+' s',left,height-8*d);x.fillText('0 s',Math.max(left,right-20*d),height-8*d);
    x.strokeStyle='#ddd';x.beginPath();x.moveTo(left,top);x.lineTo(left,bottom);x.lineTo(right,bottom);x.stroke();
    x.strokeStyle=key==='voltage'?'#2563eb':'#16a34a';x.fillStyle=x.strokeStyle;x.lineWidth=1.5*d;
    rows.forEach(function(p,i){if(!Number.isFinite(p[key]))return;
      x.beginPath();x.arc(px(p),py(p[key]),2*d,0,Math.PI*2);x.fill();
      var prev=rows[i-1];if(!prev||!Number.isFinite(prev[key]))return;
      x.setLineDash(key==='current'&&(p.current_estimated||prev.current_estimated)?[4*d,3*d]:[]);
      x.beginPath();x.moveTo(px(prev),py(prev[key]));x.lineTo(px(p),py(p[key]));x.stroke();
    });x.setLineDash([]);
  });
}
function clearSelectedEvent(){eventRequest++;selectedEvent=null;selectedEventId=null;document.getElementById('eventDetail').classList.add('hidden');document.getElementById('noEventMsg').classList.remove('hidden')}
async function showEvent(id){
  selectedEventId=id;selectedEvent=null;var request=++eventRequest;
  document.getElementById('noEventMsg').classList.add('hidden');
  document.getElementById('eventDetail').classList.remove('hidden');
  document.getElementById('eventKV').textContent='Loading...';
  document.getElementById('eventExport').removeAttribute('href');drawSavedGraphs();
  try{
    var r=await fetch('/events?id='+encodeURIComponent(id),{cache:'no-store'});if(!r.ok)throw 0;
    var e=await r.json();if(request!==eventRequest)return;selectedEvent=e;
    var kv=document.getElementById('eventKV');kv.textContent='';
    var fields={'Event ID':e.id,'Type':e.t,'RMS Voltage':Number.isFinite(e.v)?e.v.toFixed(1)+' V':'Unavailable','Time':e.dt,'Boot ID':e.boot_id,'Trigger (us)':e.trigger_us,'Isolation command (us)':e.command_us??'N/A','Software response (us)':e.response_us??'N/A','Physical response':e.physical_response??'unknown'};
    Object.entries(fields).forEach(function(pair){pair.forEach(function(value){var span=document.createElement('span');span.textContent=value;kv.appendChild(span)})});
    document.getElementById('eventExport').href='/events?id='+encodeURIComponent(id);
    document.getElementById('eventExport').download='event-'+id+'.json';drawSavedGraphs();
  }catch{if(request===eventRequest){document.getElementById('eventKV').textContent='Event details unavailable';drawSavedGraphs()}}
}

async function fetchAll(){setConn('reconnecting');await fetchData();await fetchHistory();}fetchAll();setInterval(fetchAll,1000);fetchSurges();setInterval(fetchSurges,5000);
</script></body></html>
)rawliteral";
