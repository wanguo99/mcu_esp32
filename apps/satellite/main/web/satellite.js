'use strict';
const $=id=>document.getElementById(id);
const numeric=value=>typeof value==='number'&&Number.isFinite(value);
const fmt=(value,digits=1)=>numeric(value)?value.toFixed(digits):'—';
const names=['通信天线','左侧太阳翼','右侧太阳翼'];
const actionNames={set:'设置角度',by:'相对转动',record:'记录初始位置',home:'复位',limit:'设置限位',hold:'取消运动',stop:'停止 PWM',scan:'单次扫描',auto:'周期寻优',demo:'往返演示',manual:'退出自动模式',standby:'进入待机',resume:'解除待机',fan:'风扇调速',led:'设置灯光亮度',relay:'继电器控制'};
const errors={ESP_ERR_INVALID_STATE:'设备状态不允许操作，请检查起始角度、运动状态及供电/姿态策略',ESP_ERR_INVALID_ARG:'参数超出允许范围或格式错误',ESP_ERR_NOT_SUPPORTED:'该功能尚未配置或未编入固件',ESP_ERR_NOT_FOUND:'没有可用的有效测量或搜索结果',ESP_ERR_TIMEOUT:'设备操作超时，请检查连接和传感器',ESP_ERR_INVALID_RESPONSE:'传感器或设备返回了无效数据',restarting:'设备正在准备重启'};
let latest=null,online=false,busy=false,polling=false,revision=0,traceBusy=false,traceData=[],traceAxis=0;
const initialized=new Set();
function errorText(code){return errors[code]||code||'未知错误';}
async function request(path,options){
  const controller=new AbortController();
  const timer=setTimeout(()=>controller.abort(),6000);
  try{
    const response=await fetch(path,{...options,signal:controller.signal});
    const body=await response.json();
    if(!response.ok)throw new Error(errorText(body.error)+' · '+(body.error||'HTTP '+response.status));
    return body;
  }catch(error){
    if(controller.signal.aborted)throw new Error('请求超时，请检查手机 Wi-Fi 与访问地址');
    throw error;
  }finally{clearTimeout(timer);}
}
function policyBlocked(s){return !!(s.standby||s.low_battery||s.power_unknown||s.pose_fault);}
function canAct(i,action){
  const a=latest?.axes?.[i];
  if(!online||busy||!a?.ready)return false;
  if(['hold','stop','manual'].includes(action))return true;
  if(policyBlocked(latest))return false;
  if(action==='set')return true;
  if(action==='limit')return !a.moving;
  if((!a.output_enabled&&!a.auto_released)||!numeric(a.angle))return false;
  if(action==='record')return !a.moving;
  if(action==='home')return numeric(a.home);
  if(action==='auto'||action==='scan')return !a.moving&&(i===0?numeric(latest.rssi_dbm):numeric(latest.power?.[i-1]?.power_w));
  return true;
}
function controls(){
  for(const button of document.querySelectorAll('[data-action]'))button.disabled=!canAct(Number(button.dataset.axis),button.dataset.action);
  for(let i=0;i<3;i++)for(const key of ['angle-input','slider','delta','limit'])$(key+'-'+i).disabled=!online||busy||!latest?.axes?.[i]?.ready||policyBlocked(latest);
  $('standby').disabled=!online||busy||!!latest?.standby;$('resume').disabled=!online||busy||!latest?.standby;
  for(const id of ['fan-duty','fan-apply'])$(id).disabled=!online||busy||!latest?.fan_ready||!!latest?.standby;
  for(const id of ['led-brightness','led-apply'])$(id).disabled=!online||busy||!latest?.led_ready;
  for(const id of ['relay-channel','relay-on','relay-off'])$(id).disabled=!online||busy||!latest?.relay_ready;
  $('trace-refresh').disabled=!online||traceBusy;$('trace-export').disabled=traceBusy||!traceData.length;
}
function render(s){
  latest=s;
  $('rssi').textContent=fmt(s.rssi_dbm,0);$('rssi-note').textContent=numeric(s.rssi_dbm)?'已关联 AP 的接收信号':'未关联 STA 或未配置网络';
  const powers=s.power||[];
  $('solar').textContent=numeric(powers[0]?.power_w)&&numeric(powers[1]?.power_w)?fmt(powers[0].power_w+powers[1].power_w,3):'—';
  $('battery').textContent=fmt(powers[2]?.voltage_v,2);$('tilt').textContent=fmt(s.tilt_deg);
  $('temperature').textContent='芯片温度 '+fmt(s.chip_temperature_c)+' °C';
  const flags=[];
  if(s.standby)flags.push(['待机 · 输出已停止',true]);
  if(s.low_battery)flags.push(['低电量 · 运动受限',true]);
  if(s.power_unknown)flags.push(['供电数据异常',true]);
  if(s.pose_fault)flags.push(['姿态异常 · 运动受限',true]);
  if(!flags.length)flags.push(['未触发运动限制',false]);
  $('policy').replaceChildren(...flags.map(([text,warn])=>{const e=document.createElement('span');e.className='status-pill'+(warn?' warn':'');e.textContent=text;return e;}));
  for(let i=0;i<3;i++){
    const a=s.axes?.[i]||{},max=numeric(a.max_angle)&&a.max_angle>0?a.max_angle:180;
    const label=!a.configured?'未配置':!a.ready?'初始化失败':a.mode===1?(a.search_phase===5?'周期等待':'自动寻优'):a.mode===3?'单次扫描':a.mode===2?'往返演示':a.moving?'移动中':a.output_enabled?'保持指令':a.auto_released?'PWM 已自动关闭':'PWM 已停止';
    $('axis-state-'+i).textContent=label;$('axis-state-'+i).className='tag'+(a.ready?' online':'');
    $('axis-pin-'+i).textContent=a.configured&&Number.isInteger(a.gpio)?'PWM · GPIO'+a.gpio+' · 0–'+max+'°':'PWM 接线未配置';
    $('angle-'+i).textContent=fmt(a.angle)+(numeric(a.angle)?'°':'');$('scale-'+i).textContent=a.ready?max+'°':'—';
    const needle=$('needle-'+i);needle.setAttribute('visibility',numeric(a.angle)?'visible':'hidden');
    needle.setAttribute('transform','rotate('+(numeric(a.angle)?Math.max(0,Math.min(1,a.angle/max))*180-90:-90)+' 110 110)');
    $('target-'+i).textContent=a.output_enabled||a.auto_released?fmt(a.target)+'°':'—';$('home-'+i).textContent=numeric(a.home)?fmt(a.home)+'°':'未记录';
    for(const id of ['angle-input','slider'])$(id+'-'+i).max=max;
    $('delta-'+i).min=-max;$('delta-'+i).max=max;
    if(a.ready&&!initialized.has(i)){
      $('angle-input-'+i).value=numeric(a.angle)?a.angle:max/2;$('slider-'+i).value=$('angle-input-'+i).value;$('limit-'+i).value=max;initialized.add(i);
    }
    const phases=['空闲','准备移动','等待稳定','采集样本','返回最佳位置','扫描完成','扫描失败'];
    $('axis-note-'+i).textContent=!a.configured?'未配置接线。启用该轴后即可操作。':!a.ready?'初始化失败：'+errorText(a.error):
      a.error&&a.error!=='ESP_OK'?'最近错误：'+errorText(a.error):a.auto_released?'动作结束，PWM 已自动关闭；可继续设置角度、记录位置或复位。角度为指令值，无机械反馈。':!a.output_enabled?'输入目标角度，点击“设置角度”执行。':
      (phases[a.search_phase]||'')+(numeric(a.best_angle)?' · 最佳指令角度 '+fmt(a.best_angle)+'°':'')+' · 指令角度，无机械反馈';
  }
  for(let i=0;i<4;i++){
    const p=powers[i]||{};
    $('voltage-'+i).textContent=fmt(p.voltage_v,3);$('current-'+i).textContent=fmt(p.current_a,3)+' A';$('power-'+i).textContent=fmt(p.power_w,3)+' W';
    $('energy-'+i).textContent=numeric(p.power_w)||(numeric(p.session_energy_wh)&&p.session_energy_wh!==0)?fmt(p.session_energy_wh,4)+' Wh':'—';
    $('power-note-'+i).textContent=!numeric(p.voltage_v)?(p.voltage_error==='ESP_ERR_NOT_SUPPORTED'?'采样通道未配置':errorText(p.voltage_error)):!numeric(p.power_w)?'电压有效 · 电流/功率不可用':'电压与功率采样有效';
  }
  $('fan-status').textContent=s.fan_ready?'当前占空比 '+fmt(s.fan_duty,0)+'% · 转速 '+fmt(s.fan_rpm,0)+' RPM':'风扇未就绪：'+errorText(s.fan_error);
  $('led-status').textContent=s.led_ready?'灯色由设备策略决定，亮度手动调整。':'状态灯未就绪：'+errorText(s.led_error);
  $('relay-status').textContent=s.relay_ready?'设备应答不代表机械触点反馈。':'继电器未就绪：'+errorText(s.relay_error);
  $('relay-channel').max=Math.max(0,(s.relay_channels||1)-1);
  $('updated').textContent='最近同步 '+new Date().toLocaleTimeString();controls();
}
async function refresh(){
  if(polling||busy||document.hidden)return;
  polling=true;const stamp=revision;
  try{const s=await request('/api/satellite/status');if(stamp!==revision)return;online=true;render(s);$('connection').textContent='遥测已连接';$('connection').className='tag online';}
  catch(e){if(stamp!==revision)return;online=false;$('connection').textContent='连接中断 · 数据已过期';$('connection').className='tag offline';$('updated').textContent='同步失败：'+e.message;controls();}
  finally{polling=false;}
}
function log(text,failed){
  $('events-empty')?.remove();const row=document.createElement('li');if(failed)row.className='failure';
  const time=document.createElement('time');time.textContent=new Date().toLocaleTimeString();const detail=document.createElement('span');detail.textContent=text;row.append(time,detail);$('events').prepend(row);
  while($('events').children.length>12)$('events').lastElementChild.remove();
}
async function send(params,label){
  if(busy||!online)return;busy=true;revision++;controls();$('message').className='banner fixed-feedback';$('message').textContent='正在提交：'+label+'…';
  try{await request('/api/satellite/command?'+new URLSearchParams(params),{method:'POST'});$('message').textContent=label+'：设备已接受指令，请以遥测状态确认执行进度。';log(label+' · 指令已接受',false);}
  catch(e){$('message').className='banner fixed-feedback error';$('message').textContent=label+'失败：'+e.message;log(label+' · '+e.message,true);}
  finally{busy=false;controls();await refresh();}
}
function axisCommand(i,action){
  if(!canAct(i,action))return;
  const field=action==='by'?'delta':action==='limit'?'limit':action==='set'?'angle-input':null;
  if(field&&!$(field+'-'+i).reportValidity())return;
  const params={axis:i,action:action==='set'?'angle':action};if(field)params.value=String(Number($(field+'-'+i).value));
  return send(params,names[i]+' · '+actionNames[action]);
}
function clearTrace(){traceData=[];$('trace-line').setAttribute('points','');$('trace-empty').hidden=false;$('trace-info').textContent='0 个采样点';for(const id of ['trace-high','trace-low','trace-min','trace-max'])$(id).textContent='—';controls();}
function drawTrace(points){
  traceData=points.filter(p=>Array.isArray(p)&&numeric(p[0])&&numeric(p[1])).slice(0,256);
  if(!traceData.length){clearTrace();return;}
  const sorted=traceData.slice().sort((a,b)=>a[0]-b[0]),xs=sorted.map(p=>p[0]),ys=sorted.map(p=>p[1]);
  const xmin=Math.min(...xs),xmax=Math.max(...xs),ymin=Math.min(...ys),ymax=Math.max(...ys),margin=Math.max((ymax-ymin)*.15,traceAxis===0?1:.001);
  const low=ymin-margin,high=ymax+margin;
  $('trace-line').setAttribute('points',sorted.map(([x,y])=>((x-xmin)/(xmax-xmin||1)*550+60).toFixed(1)+','+(220-(y-low)/(high-low)*190).toFixed(1)).join(' '));
  $('trace-high').textContent=fmt(high,traceAxis===0?0:3);$('trace-low').textContent=fmt(low,traceAxis===0?0:3);
  $('trace-min').textContent=fmt(xmin)+'°';$('trace-max').textContent=fmt(xmax)+'°';$('trace-empty').hidden=true;$('trace-info').textContent=traceData.length+' 个真实采样点';controls();
}
async function loadTrace(){
  if(traceBusy||!online)return;traceBusy=true;const axis=Number($('trace-axis').value);controls();
  try{const data=await request('/api/satellite/scan?axis='+axis);if(axis!==Number($('trace-axis').value))return;traceAxis=axis;drawTrace(data.points||[]);}
  catch(e){$('trace-info').textContent='读取失败：'+e.message;}
  finally{traceBusy=false;controls();}
}
for(const button of document.querySelectorAll('[data-action]'))button.addEventListener('click',()=>axisCommand(Number(button.dataset.axis),button.dataset.action));
for(let i=0;i<3;i++){
  $('slider-'+i).addEventListener('input',()=>{$('angle-input-'+i).value=$('slider-'+i).value;});
  $('angle-input-'+i).addEventListener('input',()=>{$('slider-'+i).value=$('angle-input-'+i).value;});
}
$('standby').addEventListener('click',()=>send({action:'standby'},actionNames.standby));$('resume').addEventListener('click',()=>send({action:'resume'},actionNames.resume));
for(const [button,field,action] of [['fan-apply','fan-duty','fan'],['led-apply','led-brightness','led'],['relay-on','relay-channel','relay'],['relay-off','relay-channel','relay']])$(button).addEventListener('click',()=>{
  if(!$(field).reportValidity())return;const p={action,value:$(field).value};if(action==='relay'){p.channel=p.value;p.value=button==='relay-on'?1:0;}send(p,actionNames[action]);
});
$('trace-refresh').addEventListener('click',loadTrace);$('trace-axis').addEventListener('change',()=>{clearTrace();$('trace-unit').textContent=names[Number($('trace-axis').value)]+' · '+($('trace-axis').value==='0'?'RSSI / dBm':'功率 / W');});
$('trace-export').addEventListener('click',()=>{if(!traceData.length)return;const blob=new Blob(['angle_deg,'+(traceAxis===0?'rssi_dbm':'power_w')+'\n'+traceData.map(p=>p.join(',')).join('\n')],{type:'text/csv;charset=utf-8'});const url=URL.createObjectURL(blob);const a=document.createElement('a');a.href=url;a.download='scan-axis-'+traceAxis+'.csv';a.click();setTimeout(()=>URL.revokeObjectURL(url),1000);});
request('/api/status').then(s=>{$('device-meta').textContent=s.name+' · '+s.board+' · v'+s.firmware_version;}).catch(()=>{});
window.SatelliteUI={render,refresh,axisCommand,loadTrace,drawTrace};refresh();setInterval(refresh,2000);
