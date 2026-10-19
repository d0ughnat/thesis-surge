const {chromium}=require('../build/node_modules/playwright');
const fs=require('fs'),http=require('http'),assert=require('assert');
const html=fs.readFileSync('webpage.h','utf8').match(/R"rawliteral\(([\s\S]*)\)rawliteral"/)[1];
new(require('vm').Script)(html.match(/<script>([\s\S]*?)<\/script>/)[1]);
let clears=0,relay=0,demo=0;
const event=id=>({id,boot_id:'fixture',t:'SURGE',v:245,dt:'Time unsynchronized',trigger_us:6000000,response_us:25,history:{status:id==='legacy'?'unavailable':'available',samples:id==='legacy'?[]:[{sample_us:0,voltage:230,current:1.5,current_estimated:true},{sample_us:1000000,voltage:null,current:null},{sample_us:5000000,voltage:245,current:2,current_estimated:false}]}});
let events=[event('one'),event('legacy')];
const server=http.createServer((req,res)=>{
const url=new URL(req.url,'http://localhost');res.setHeader('Content-Type','application/json');
if(url.pathname==='/'){res.setHeader('Content-Type','text/html');return res.end(html)}
if(url.pathname==='/data')return res.end(JSON.stringify({voltage:230,current:1.5,sensorOk:true,systemState:'NORMAL',relayState:'normal',latest_event:events[0]}));
if(url.pathname==='/history')return res.end(JSON.stringify({voltage:[230,null,231],current:[1.5,null,2],dvdt:[0,null,1]}));
if(url.pathname==='/surges')return res.end(JSON.stringify({entries:events,count:events.length}));
if(url.pathname==='/events')return res.end(JSON.stringify(event(url.searchParams.get('id'))));
if(url.pathname==='/clearlogs'){assert.equal(req.method,'POST');clears++;events=[];}
if(url.pathname==='/relay-test'){assert.equal(req.method,'POST');relay++;}
if(url.pathname==='/simulate-surge'){assert.equal(req.method,'POST');demo++;}
res.end('{"ok":true}');
});
(async()=>{
await new Promise(r=>server.listen(0,'127.0.0.1',r));
const browser=await chromium.launch({channel:'msedge',headless:true});
try{
const page=await browser.newPage({viewport:{width:375,height:1000},deviceScaleFactor:2}),errors=[];page.on('pageerror',e=>errors.push(e.message));
await page.goto('http://127.0.0.1:'+server.address().port);
await page.waitForFunction(()=>document.getElementById('connText').textContent==='Connected');
assert.equal(await page.getByRole('tab').count(),2);
assert(await page.locator('#relayTestBtn').isVisible());assert(await page.locator('#surgeDemoBtn').isVisible());
await page.locator('#relayTestBtn').click();await page.locator('#surgeDemoBtn').click();
await page.getByRole('tab',{name:'Alerts',exact:true}).click();await page.locator('#logBody button').first().click();
await page.waitForFunction(()=>selectedEvent&&selectedEvent.id==='one');
assert.match(await page.locator('#savedILabel').innerText(),/estimated/i);
assert.match(await page.locator('#graphStatus').innerText(),/relative to trigger/);
const before=await page.locator('#savedVChart').evaluate(c=>c.toDataURL());
await page.evaluate(()=>fetchAll());assert.equal(await page.locator('#savedVChart').evaluate(c=>c.toDataURL()),before);
const exported=await page.request.get(new URL(await page.locator('#eventExport').getAttribute('href'),page.url()).href);assert.equal((await exported.json()).history.samples[2].voltage,245);
for(const width of [320,375,600,768]){
await page.setViewportSize({width,height:1000});await page.evaluate(()=>drawSavedGraphs());
assert(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),'overflow '+width);
const height=await page.locator('#savedVChart').evaluate(c=>c.height);
await page.evaluate(()=>{for(let i=0;i<10;i++){redraw();drawSavedGraphs()}});assert.equal(await page.locator('#savedVChart').evaluate(c=>c.height),height);
await page.screenshot({path:`build/dashboard-${width}.png`,fullPage:true});
}
await page.evaluate(()=>showEvent('legacy'));assert.match(await page.locator('#graphStatus').innerText(),/Graph unavailable/);
await page.reload();await page.getByRole('tab',{name:'Alerts',exact:true}).click();await page.evaluate(()=>showEvent('one'));assert.equal(await page.evaluate(()=>selectedEvent.history.samples.length),3);
page.on('dialog',d=>d.accept('token'));await page.getByText('Clear Event History',{exact:true}).click();await page.getByText('Yes, Clear',{exact:true}).click();await page.waitForFunction(()=>selectedEvent===null);
assert.equal(clears,1);assert.equal(relay,1);assert.equal(demo,1);assert.deepEqual(errors,[]);
console.log('PASS: Alerts graphs, missing and estimated values, immutable live refresh, exports, legacy alerts, reload, clear, relocated controls, 320/375/600/768px and stable high-DPI sizing');
}finally{await browser.close();server.close()}
})().catch(e=>{console.error(e);server.close();process.exitCode=1});
