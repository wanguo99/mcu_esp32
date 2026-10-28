'use strict';
const {test}=require('node:test');
const assert=require('node:assert/strict');
const fs=require('node:fs');
const vm=require('node:vm');
const html=fs.readFileSync('apps/component_demo/main/console.html','utf8');
const script=html.match(/<script>([\s\S]*?)<\/script>/)[1];
const ready={configured:true,ready:true,error:'ESP_OK'};
const servo={...ready,max_angle:90,commanded_angle:null,output_enabled:false,moving:false,last_error:'ESP_OK'};
const flush=()=>new Promise(resolve=>setImmediate(resolve));
async function setup(status={}) {
  const nodes=new Map(),buttons=[],calls=[];
  let currentCard=null;
  for(const match of html.matchAll(/<(section|input|select|button|div)\b([^>]*)>/g)) {
    const [,tag,attrs]=match,id=attrs.match(/\bid="([^"]+)"/)?.[1],cmd=attrs.match(/data-cmd="([^"]+)"/)?.[1];
    const node={value:attrs.match(/\bvalue="([^"]*)"/)?.[1]||'',hidden:false,disabled:false,checked:false,
      textContent:'',valid:true,children:[],dataset:{cmd},listeners:{},reportValidity(){return this.valid;},
      querySelectorAll(){return this.children;},addEventListener(event,callback){this.listeners[event]=callback;}};
    if(id)nodes.set(id,node);
    if(tag==='section')currentCard=node;
    if(['input','select','button'].includes(tag)&&currentCard)currentCard.children.push(node);
    if(cmd)buttons.push(node);
  }
  nodes.get('led-effect').value='3';
  let responder=async(url,options)=>({ok:true,json:async()=>options.method==='POST'?{ok:true}:status});
  const context={document:{getElementById:id=>nodes.get(id),querySelectorAll:()=>buttons},window:{},
    fetch:async(url,options)=>{calls.push({url,options});return responder(url,options);},
    AbortController,setTimeout,clearTimeout,URLSearchParams,setInterval:()=>0};
  vm.runInNewContext(script,context);await flush();
  return {nodes,buttons,calls,api:context.window.ComponentConsole,setResponder:fn=>{responder=fn;}};
}
test('initial load only reads status and disables unconfigured devices',async()=>{
  const h=await setup({servo:{configured:false,ready:false}});
  assert.equal(h.calls.length,1);assert.equal(h.calls[0].url,'/api/components/status');
  assert.equal(h.nodes.get('fan').hidden,true);
  assert.ok(h.nodes.get('servo').children.every(n=>n.disabled));
});
test('calibrated bounds initialize inputs and fixed relay disables readback',async()=>{
  const h=await setup({servo,relay_uart:{...ready,channel_count:2,readback_supported:false}});
  assert.equal(Number(h.nodes.get('servo-max').value),75);
  assert.equal(h.nodes.get('relay-read').disabled,true);
  assert.match(h.nodes.get('servo-state').textContent,/尚未设置/);
});
test('stop bypasses invalid movement fields; move sends only its own parameters',async()=>{
  const h=await setup({servo});h.nodes.get('servo-angle').valid=false;
  h.buttons.find(b=>b.dataset.cmd==='servo-stop').listeners.click();await flush();
  const stop=h.calls.find(c=>c.options.method==='POST');
  assert.ok(stop);assert.equal(new URLSearchParams(stop.url.split('?')[1]).get('action'),'stop');
  assert.equal(new URLSearchParams(stop.url.split('?')[1]).has('angle'),false);
  h.nodes.get('servo-angle').valid=true;h.nodes.get('servo-min').valid=false;
  h.buttons.find(b=>b.dataset.cmd==='servo-move').listeners.click();await flush();
  const params=new URLSearchParams(h.calls.filter(c=>c.options.method==='POST').at(-1).url.split('?')[1]);
  assert.equal(params.get('action'),'move');assert.equal(params.has('min'),false);
});
test('command failure retains error code and permits retry',async()=>{
  const h=await setup({servo});
  h.setResponder(async(url,options)=>({ok:options.method!=='POST',json:async()=>options.method==='POST'?{message:'未设置初始角度',error:'ESP_ERR_INVALID_STATE'}:{servo}}));
  await h.api.command('servo-move');
  assert.match(h.nodes.get('message').textContent,/操作失败.*ESP_ERR_INVALID_STATE/);
  assert.equal(h.nodes.get('servo-angle').disabled,false);
});
test('LED command transmits color, direction and animation settings',async()=>{
  const h=await setup({ws2812:{...ready,led_count:16,effect:0,frames_sent:0,last_error:'ESP_OK'}});
  h.nodes.get('led-reverse').checked=true;await h.api.command('led-set');
  const params=new URLSearchParams(h.calls.find(c=>c.options.method==='POST').url.split('?')[1]);
  assert.equal(params.get('r'),'0');assert.equal(params.get('g'),'128');assert.equal(params.get('b'),'255');
  assert.equal(params.get('reverse'),'1');assert.equal(params.get('period_ms'),'2000');
});
test('slow status requests do not accumulate polling calls',async()=>{
  const h=await setup({servo});let release;
  h.setResponder(()=>new Promise(resolve=>{release=resolve;}));
  const first=h.api.refresh();await h.api.refresh();await h.api.refresh();
  assert.equal(h.calls.length,2);
  release({ok:true,json:async()=>({servo})});await first;
});

test('servo accepts and transmits fractional angle targets',async()=>{
  for(const id of ['servo-angle','servo-min','servo-max']) {
    const tag=html.match(new RegExp('<input[^>]*id="'+id+'"[^>]*>'))[0];
    assert.match(tag,/step="any"/);
  }
  const h=await setup({servo});h.nodes.get('servo-angle').value='45.5';
  await h.api.command('servo-move');
  const params=new URLSearchParams(h.calls.find(c=>c.options.method==='POST').url.split('?')[1]);
  assert.equal(params.get('angle'),'45.5');
});

test('relative move, home and limits use independent command parameters',async()=>{
  const h=await setup({servo:{...servo,home_angle:45,output_enabled:true,calibrated_max_angle:180}});
  h.nodes.get('servo-delta').value='-20.5';await h.api.command('servo-relative');
  let params=new URLSearchParams(h.calls.filter(c=>c.options.method==='POST').at(-1).url.split('?')[1]);
  assert.equal(params.get('action'),'relative');assert.equal(params.get('delta'),'-20.5');
  assert.equal(params.has('angle'),false);
  h.nodes.get('servo-angle').valid=false;
  h.buttons.find(b=>b.dataset.cmd==='servo-record_home').listeners.click();await flush();
  params=new URLSearchParams(h.calls.filter(c=>c.options.method==='POST').at(-1).url.split('?')[1]);
  assert.equal(params.get('action'),'record_home');assert.equal(params.has('angle'),false);
  assert.equal(h.nodes.get('servo-home').disabled,false);
  await h.api.command('servo-home');
  params=new URLSearchParams(h.calls.filter(c=>c.options.method==='POST').at(-1).url.split('?')[1]);
  assert.equal(params.get('action'),'home');assert.equal(params.get('duration_ms'),'1000');
  h.nodes.get('servo-limit').value='120';await h.api.command('servo-limits');
  params=new URLSearchParams(h.calls.filter(c=>c.options.method==='POST').at(-1).url.split('?')[1]);
  assert.equal(params.get('max'),'120');assert.equal(params.has('duration_ms'),false);
});
test('reset stays disabled until home is recorded and output is enabled',async()=>{
  const h=await setup({servo:{...servo,home_angle:null,output_enabled:true}});
  assert.equal(h.nodes.get('servo-home').disabled,true);
  h.api.update({servo:{...servo,home_angle:45,output_enabled:false}});
  assert.equal(h.nodes.get('servo-home').disabled,true);
});
