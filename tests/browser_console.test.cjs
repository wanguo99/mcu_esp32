// Optional real-browser checks. Set PLAYWRIGHT_MODULE to an installed playwright package.
const {test,before,after,beforeEach}=require('node:test');
const assert=require('node:assert/strict');
const http=require('node:http');
const fs=require('node:fs');
const path=require('node:path');
let playwright;
try{playwright=require(process.env.PLAYWRIGHT_MODULE||'playwright');}catch{}
if(!playwright){test('browser UI checks require Playwright',{skip:true},()=>{});}else{
const root=path.resolve(__dirname,'..');
const assets={'/':'components/web/web_home.html','/ui/theme.css':'components/web/ui_theme.css','/satellite/':'apps/satellite/main/web/satellite.html','/satellite/styles.css':'apps/satellite/main/web/satellite.css','/satellite/app.js':'apps/satellite/main/web/satellite.js','/components/':'apps/component_demo/main/console.html','/camera/':'components/monitor_web/web/index.html','/camera/styles.css':'components/monitor_web/web/styles.css','/camera/api.js':'components/monitor_web/web/api.js','/camera/monitor.js':'components/monitor_web/web/monitor.js'};
const base={standby:false,low_battery:false,power_unknown:false,pose_fault:false,sample_us:123000000,rssi_dbm:-54,tilt_deg:2.4,chip_temperature_c:28.6,fan_ready:true,fan_duty:35,fan_rpm:1280,fan_error:'ESP_OK',led_ready:true,led_error:'ESP_OK',relay_ready:true,relay_channels:4,relay_error:'ESP_OK',imu_error:'ESP_OK',axes:[60,45,120].map((angle,i)=>({configured:true,ready:true,gpio:[1,2,4][i],angle,target:angle,home:60,max_angle:180,moving:false,output_enabled:true,mode:0,search_phase:0,best_angle:null,best_score:null,error:'ESP_OK'})),power:[{voltage_v:5.24,current_a:.096,power_w:.503,session_energy_wh:.1578},{voltage_v:5.18,current_a:.089,power_w:.461,session_energy_wh:.1431},{voltage_v:3.86,current_a:.12,power_w:.463,session_energy_wh:.234},{voltage_v:null,current_a:null,power_w:null,session_energy_wh:0}].map(p=>({...p,voltage_error:p.voltage_v===null?'ESP_ERR_NOT_SUPPORTED':'ESP_OK',current_error:p.power_w===null?'ESP_ERR_NOT_SUPPORTED':'ESP_OK'}))};
let browser,server,origin,state,posts,failStatus,failCommand,trace;
function reset(){state=structuredClone(base);posts=[];failStatus=false;failCommand=false;trace=[[0,-78],[30,-68],[60,-54],[90,-61],[120,-72],[150,-80],[180,-83]];}
before(async()=>{
 const engine=process.env.PLAYWRIGHT_BROWSER||"chromium";
 // Headless servers may not provide a GPU VSync clock. Rendering must not wait for one.
 // Fixtures are loopback-only; do not inherit a desktop or a system HTTP proxy.
 browser=await playwright[engine].launch({headless:true,
  env:{...process.env,DISPLAY:'',WAYLAND_DISPLAY:'',HTTP_PROXY:'',HTTPS_PROXY:'',http_proxy:'',https_proxy:''},
  ...(engine==='chromium'?{args:['--disable-gpu-vsync','--disable-frame-rate-limit']}:{firefoxUserPrefs:{'network.proxy.type':0}})});
 server=http.createServer((req,res)=>{
  const u=new URL(req.url,'http://localhost');res.setHeader('Cache-Control','no-store');
  const json=(data,status=200)=>{res.writeHead(status,{'Content-Type':'application/json'});res.end(JSON.stringify(data));};
  if(req.method==='POST'){posts.push(u);return json(failCommand?{error:'ESP_ERR_INVALID_STATE'}:{ok:true},failCommand?409:200);}
  if(u.pathname==='/api/status')return json({name:'北斗卫星控制器',board:'YD_ESP32_S3',firmware_version:'0.3.0',uptime_seconds:12345,free_heap_bytes:238592,ip:'192.168.4.1',hostname:'beidou.local',restart_error:0,modules:{network:true,web:true,servo:true,voltage_sensor:true,mpu6050:true,ws2812:true}});
  if(u.pathname==='/api/modules')return json([{name:'卫星控制台',href:'/satellite/'},{name:'摄像头监控',href:'/camera/'},{name:'组件调试',href:'/components/'}]);
  if(u.pathname==='/api/satellite/status')return json(failStatus?{error:'offline'}:state,failStatus?503:200);
  if(u.pathname==='/api/satellite/scan')return json({points:trace});
  if(u.pathname==='/api/components/status')return json({servo:{configured:true,ready:true,max_angle:180,min_angle:0,calibrated_max_angle:180,home_angle:60,commanded_angle:60,output_enabled:true,moving:false,last_error:'ESP_OK'},fan:{configured:true,ready:true,duty_percent:35,tach_available:true,rpm_valid:true,rpm:1280,sample_age_ms:60},relay_uart:{configured:true,ready:true,channel_count:4,readback_supported:true},mpu6050:{configured:true,ready:true,read_error:'',accel_m_s2:[0,0,9.8],gyro_dps:[0,0,0],temperature_c:28.6},ws2812:{configured:true,ready:true,effect:2,frames_sent:1250,last_error:'ESP_OK',led_count:8}});
  if(u.pathname==='/api/camera/status')return json({storage_available:false,recording_available:false,camera_enabled:false,camera_change_pending:false,camera_change_error:0,device_name:'影像监控工作台',framesize:11,stream_port:81,ip:'192.168.4.1',ssid:'ESP32-Satellite',sensor:'OV3660'});
  if(u.pathname==='/favicon.ico'){res.writeHead(204);return res.end();}
  const file=assets[u.pathname];if(!file){res.writeHead(404);return res.end();}
  res.setHeader('Content-Type',file.endsWith('.css')?'text/css':file.endsWith('.js')?'application/javascript':'text/html');res.end(fs.readFileSync(path.join(root,file)));
 });
 await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));origin='http://127.0.0.1:'+server.address().port;
});
after(async()=>{await browser?.close();await new Promise(resolve=>server?.close(resolve));});
beforeEach(reset);
async function pageAt(url='/satellite/',width=1440){const page=await browser.newPage({viewport:{width,height:1000}});page.setDefaultTimeout(8000);page.on('pageerror',e=>console.error('BROWSER',e.message));await page.goto(origin+url);if(url==='/satellite/')await page.waitForFunction(()=>document.getElementById('connection').textContent==='遥测已连接').catch(async e=>{console.error(await page.locator('#updated').innerText());await page.close();throw e;});return page;}
async function sync(page){await page.waitForFunction(()=>window.SatelliteUI);await page.evaluate(()=>window.SatelliteUI.refresh());}
const btn=(page,action)=>page.locator('[data-axis="0"][data-action="'+action+'"]');
test('mobile browsers without AbortSignal.timeout can load home, satellite and components',async()=>{
 for(const [url,selector,text] of [['/','#connection','设备已连接'],['/satellite/','#connection','遥测已连接'],['/components/','#connection','设备已连接，每秒刷新状态']]){
  const page=await browser.newPage({viewport:{width:390,height:844}});
  try{
   await page.addInitScript(()=>{Object.defineProperty(AbortSignal,'timeout',{value:undefined,configurable:true});});
   await page.goto(origin+url);
   await page.waitForFunction(({selector,text})=>document.querySelector(selector).textContent===text,{selector,text});
   if(url==='/satellite/'){
    await btn(page,'set').click();
    await page.waitForFunction(()=>document.getElementById('message').textContent.includes('设备已接受'));
    assert.equal(posts.at(-1).searchParams.get('action'),'angle');
   }
  }finally{await page.close();}
 }
});
test('timed out telemetry disables controls and recovers on retry',async()=>{
 const page=await browser.newPage();
 try{
  await page.addInitScript(()=>{
   const original=window.setTimeout.bind(window);
   window.setTimeout=(fn,ms,...args)=>original(fn,ms===6000?100:ms,...args);
  });
  await page.route('**/api/satellite/status',()=>{});
  await page.goto(origin+'/satellite/');
  await page.waitForFunction(()=>document.getElementById('updated').textContent.includes('请求超时'));
  assert.equal(await btn(page,'set').isDisabled(),true);
  await page.unroute('**/api/satellite/status');
  await sync(page);
  await page.waitForFunction(()=>document.getElementById('connection').textContent==='遥测已连接');
  assert.equal(await btn(page,'set').isDisabled(),false);
 }finally{await page.close();}
});
test('one angle button handles startup, automatic release and explicit stop; invalid inputs send nothing',async()=>{
 state.axes[0]={...state.axes[0],gpio:1,angle:null,target:0,home:null,output_enabled:false};
 for(let i=1;i<3;i++)state.axes[i]={...state.axes[i],configured:false,ready:false,gpio:null};
 const page=await pageAt();try{
  assert.equal(await page.locator('[data-action="move"]').count(),0);
  assert.equal(await page.locator('[id^="duration-"]').count(),0);
  assert.equal(await btn(page,'set').innerText(),'设置角度');
  assert.equal(await btn(page,'set').isDisabled(),false);assert.equal(posts.length,0);
  for(const [angle,released] of [['90',true],['60',false],['120',true]]){
   await page.locator('#angle-input-0').fill(angle);await btn(page,'set').click();
   await page.waitForFunction(()=>document.getElementById('message').textContent.includes('设置角度：设备已接受'));
   const posted=posts.at(-1);assert.equal(posted.searchParams.get('action'),'angle');
   assert.equal(posted.searchParams.get('value'),angle);assert.equal(posted.searchParams.get('axis'),'0');
   assert.equal(posted.searchParams.has('duration_ms'),false);
   state.axes[0]={...state.axes[0],angle:Number(angle),target:Number(angle),auto_released:released,output_enabled:false};
   await sync(page);assert.equal(await btn(page,'set').isDisabled(),false);
  }
  for(const value of ['181','-1','']){
   await page.locator('#angle-input-0').fill(value);await page.evaluate(()=>window.SatelliteUI.axisCommand(0,'set'));assert.equal(posts.length,3);
  }
 }finally{await page.close();}
});
test('ESP32 solar wings route GPIO25 left and GPIO26 right independently',async()=>{
 state.axes[0]={...state.axes[0],configured:false,ready:false,gpio:null};
 state.axes[1]={...state.axes[1],gpio:25,angle:45,target:45};
 state.axes[2]={...state.axes[2],gpio:26,angle:135,target:135};
 const page=await pageAt();try{
  assert.match(await page.locator('#axis-pin-1').innerText(),/GPIO25/);
  assert.match(await page.locator('#axis-pin-2').innerText(),/GPIO26/);
  assert.equal(await btn(page,'set').isDisabled(),true);
  for(const [axis,angle,label] of [[1,'60','左侧太阳翼'],[2,'120','右侧太阳翼']]){
   await page.locator('#angle-input-'+axis).fill(angle);
   await page.locator('[data-axis="'+axis+'"][data-action="set"]').click();
   await page.waitForFunction(label=>document.getElementById('message').textContent.includes(label+' · 设置角度：设备已接受'),label);
   const posted=posts.at(-1);assert.equal(posted.searchParams.get('axis'),String(axis));
   assert.equal(posted.searchParams.get('action'),'angle');assert.equal(posted.searchParams.get('value'),angle);
  }
  assert.equal(posts.length,2);
 }finally{await page.close();}
});
test('auto-released solar wing keeps movement and home controls without automatic commands',async()=>{
 state.axes[0]={...state.axes[0],configured:false,ready:false,gpio:null};
 state.axes[1]={...state.axes[1],gpio:25,angle:90,target:90,home:60,output_enabled:false,auto_released:true};
 const page=await pageAt();try{
  assert.match(await page.locator('#axis-state-1').innerText(),/自动关闭/);
  assert.equal(await page.locator('#target-1').innerText(),'90.0°');
  for(const action of ['set','by','record','home'])assert.equal(await page.locator('[data-axis="1"][data-action="'+action+'"]').isDisabled(),false);
  assert.equal(posts.length,0);
  await page.locator('#delta-1').fill('-10');
  await page.locator('[data-axis="1"][data-action="by"]').click();
  await page.waitForFunction(()=>document.getElementById('message').textContent.includes('设备已接受'));
  assert.equal(posts.length,1);assert.equal(posts[0].searchParams.get('action'),'by');
  assert.equal(posts[0].searchParams.get('axis'),'1');
  state.axes[1].auto_released=false;await sync(page);
  await page.waitForFunction(()=>document.getElementById('axis-state-1').textContent==='PWM 已停止');
  assert.equal(await page.locator('[data-axis="1"][data-action="by"]').isDisabled(),true);
  assert.equal(await page.locator('[data-axis="1"][data-action="set"]').isDisabled(),false);
 }finally{await page.close();}
});
test('satellite initializes with GET only, preserves edits and sends fractional relative movement',async()=>{
 const page=await pageAt();try{
  assert.equal(posts.length,0);await page.locator('#slider-0').focus();await page.keyboard.press('ArrowRight');
  assert.equal(await page.locator('#angle-input-0').inputValue(),'60.1');assert.equal(posts.length,0);await page.locator('#delta-0').fill('-12.5');
  await btn(page,'by').click();await page.waitForFunction(()=>document.getElementById('message').textContent.includes('设备已接受'));
  assert.equal(posts.length,1);assert.equal(posts[0].searchParams.get('value'),'-12.5');assert.equal(posts[0].searchParams.get('action'),'by');
  assert.equal(await page.locator('#delta-0').inputValue(),'-12.5');
 }finally{await page.close();}
});
test('unconfigured and policy-blocked axes reject movement while stop bypasses unrelated validation',async()=>{
 state.axes[1].configured=false;state.axes[1].ready=false;state.low_battery=true;
 const page=await pageAt();try{
  assert.equal(await btn(page,'set').isDisabled(),true);
  assert.equal(await page.locator('[data-axis="1"][data-action="set"]').isDisabled(),true);
  await page.evaluate(()=>{document.getElementById('angle-input-0').value='';});
  await btn(page,'stop').click();await page.waitForFunction(()=>document.getElementById('message').textContent.includes('设备已接受'));
  assert.equal(posts[0].searchParams.get('action'),'stop');assert.equal(posts[0].searchParams.has('duration_ms'),false);
 }finally{await page.close();}
});
test('request failures display error and offline status disables controls',async()=>{
 const page=await pageAt();try{
  failCommand=true;await btn(page,'set').click();await page.waitForFunction(()=>document.getElementById('message').textContent.includes('ESP_ERR_INVALID_STATE'));
  assert.match(await page.locator('#events').innerText(),/设备状态不允许操作/);
  failStatus=true;await sync(page);await page.waitForFunction(()=>document.getElementById('connection').textContent.includes('连接中断'));
  assert.equal(await btn(page,'set').isDisabled(),true);
  failStatus=false;await sync(page);assert.equal(await btn(page,'set').isDisabled(),false);
 }finally{await page.close();}
});
test('scan data is read on demand, exports CSV, clears old series when switching axis',async()=>{
 const page=await pageAt();try{
  assert.equal(await page.locator('#trace-empty').isVisible(),true);
  await page.locator('#trace-refresh').click();await page.waitForFunction(()=>document.getElementById('trace-info').textContent.includes('真实采样点'));
  assert.ok((await page.locator('#trace-line').getAttribute('points')).length>10);
  const download=page.waitForEvent('download');await page.locator('#trace-export').click();assert.equal((await download).suggestedFilename(),'scan-axis-0.csv');
  await page.locator('#trace-axis').selectOption('1');assert.equal(await page.locator('#trace-export').isDisabled(),true);assert.equal(await page.locator('#trace-empty').isVisible(),true);
  assert.equal(posts.length,0);
 }finally{await page.close();}
});
test('null measurements remain unavailable and reset requires a saved home',async()=>{
 state.rssi_dbm=null;state.power[0].power_w=null;state.power[0].session_energy_wh=0;state.axes[0].home=null;
 const page=await pageAt();try{
  assert.equal(await page.locator('#rssi').innerText(),'—');assert.equal(await page.locator('#solar').innerText(),'—');assert.equal(await page.locator('#energy-0').innerText(),'—');
  assert.equal(await btn(page,'home').isDisabled(),true);assert.equal(await btn(page,'auto').isDisabled(),true);
 }finally{await page.close();}
});
test('all pages render without script errors, horizontal overflow or external requests; save desktop/mobile previews',async()=>{
 const output=path.join(root,'build/ui-preview');fs.mkdirSync(output,{recursive:true});
 for(const [url,name] of [['/','home'],['/satellite/','satellite'],['/components/','components'],['/camera/','camera']]){
  for(const width of [1440,1024,390]){
   const page=await browser.newPage({viewport:{width,height:1000}}),errors=[],external=[];
   page.on('pageerror',e=>errors.push(e.message));page.on('request',r=>{if(!r.url().startsWith(origin)&&!r.url().startsWith('blob:'))external.push(r.url());});
   try{
    await page.goto(origin+url);await page.waitForLoadState('networkidle');
    if(url==='/satellite/'){await page.locator('#trace-refresh').click();await page.waitForFunction(()=>document.getElementById('trace-info').textContent.includes('真实采样点'));}
    assert.deepEqual(errors,[],url);assert.deepEqual(external,[],url);
    assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),true,url+' '+width);
    await page.screenshot({path:path.join(output,name+'-'+width+'.png'),fullPage:true});
   }finally{await page.close();}
  }
 }
});
}
