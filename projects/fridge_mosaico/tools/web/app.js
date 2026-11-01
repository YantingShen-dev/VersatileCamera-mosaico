'use strict';
const $ = id => document.getElementById(id);
let state, camera, mic, socket, context, processor, sourceNode;
let voiceEnabled = false, audioSession = -1, connecting = false;
let paused = false, generation = 0;
let captureToken = '', vadFrames = 0, lastVad = 0, polling = false;
let permissionFailed = false;
let entrySession = -1, entrySpeaking = false, frameUpload = null;
let soundTest = null;
function notice(text) { $('notice').textContent = text; }
async function api(path, body, raw = false) {
  const r = await fetch('/api/' + path, body === undefined ? {} : {
    method: 'POST', headers: raw ? {} : {'Content-Type':'application/json'}, body: raw ? body : JSON.stringify(body)});
  let result; try { result = await r.json(); } catch { throw Error(`本地服务暂不可用 (${r.status})`); }
  if (!r.ok) throw Error(result.error || `操作失败 (${r.status})`);
  return result;
}
async function command(action, extra = {}) {
  state = await api('command', {action, session:state?.session, ...extra});
  return state;
}
function stopSpeech() { generation++; window.speechSynthesis.cancel(); entrySpeaking = false; }
function reconcileEntry(s) {
  if (s.page !== 1) { if (entrySpeaking) stopSpeech(); return; }
  // One greeting per operation, including recognition retries in that operation.
  if (entrySession === s.operation) return;
  stopSpeech(); entrySession = s.operation;
  const utterance = new SpeechSynthesisUtterance(s.direction === 2 ? '想取出些什么？' : '买了些什么？');
  utterance.lang = 'zh-CN'; utterance.rate = 1.05;
  utterance.volume = (s.volume ?? 45) / 100;
  const voice = speechSynthesis.getVoices().find(v => /^zh[-_]/i.test(v.lang));
  if (voice) utterance.voice = voice;
  const myGeneration = generation;
  entrySpeaking = true;
  utterance.onend = () => { if (myGeneration === generation) entrySpeaking = false; };
  utterance.onerror = e => {
    if (myGeneration !== generation) return;
    entrySpeaking = false;
    if (e.error !== 'canceled' && e.error !== 'interrupted') notice('启动播报不可用，请检查浏览器声音权限');
  };
  speechSynthesis.speak(utterance);
}
function interrupt() {
  if (paused || state?.page !== 1 || !state?.undo) return;
  paused = true; stopSpeech();
  command('interrupt').catch(e => notice(e.message));
}
function reconcileCorrection(s) {
  paused = Boolean(s.interrupted);
}
async function uploadFrame(capture = false) {
  // Serialize preview and shutter uploads so an older preview cannot replace a capture.
  if (frameUpload) await frameUpload;
  if (!camera || !$('video').videoWidth) return;
  const canvas = document.createElement('canvas');
  const video = $('video'), side = Math.min(video.videoWidth, video.videoHeight);
  canvas.width = canvas.height = Math.min(side, 1280);
  canvas.getContext('2d').drawImage(video, (video.videoWidth-side)/2, (video.videoHeight-side)/2,
    side, side, 0, 0, canvas.width, canvas.height);
  frameUpload = (async () => {
    let best = null, bestScore = -1;
    const ctx=canvas.getContext('2d', {willReadFrequently:true});
    for(let frame=0;frame<(capture?3:1);frame++) {
      if(frame) await new Promise(resolve=>setTimeout(resolve,120));
      ctx.drawImage(video,(video.videoWidth-side)/2,(video.videoHeight-side)/2,side,side,0,0,canvas.width,canvas.height);
      let score=0;
      if(capture) {
        const pixels=ctx.getImageData(0,0,canvas.width,canvas.height).data,w=canvas.width;
        for(let y=4;y<canvas.height-4;y+=4)for(let x=4;x<w-4;x+=4) {
          const i=(y*w+x)*4+1,dx=pixels[i]-pixels[i-16],dy=pixels[i]-pixels[i-16*w];score+=dx*dx+dy*dy;
        }
      }
      if(score>bestScore) {bestScore=score;best=await new Promise(resolve=>canvas.toBlob(resolve,'image/jpeg',.88));}
    }
    if(best)await api(capture?'upload?capture=1':'upload?preview=1',best,true);
  })();
  try { await frameUpload; } finally { frameUpload = null; }
}
$('camera').addEventListener('click', async () => {
  try {
    if (camera) { camera.getTracks().forEach(t=>t.stop()); camera=null; $('video').style.display='none'; $('camera').textContent='开启摄像头'; return; }
    camera = await navigator.mediaDevices.getUserMedia({video:{width:1280,height:720},audio:false});
    $('video').srcObject = camera; $('video').style.display='block'; $('camera').textContent='关闭摄像头'; $('source').textContent='摄像头已开启';
  } catch { notice('摄像头未获授权或不可用，也可以选择照片'); }
});
$('file').addEventListener('change', async e => {
  const f = e.target.files[0]; if (!f) return;
  try {
    if (camera) { camera.getTracks().forEach(t=>t.stop()); camera=null; $('video').style.display='none'; $('camera').textContent='开启摄像头'; }
    await api('upload', f, true); $('source').textContent='已选择：' + f.name; notice('照片已就绪，请进入入库或出库');
  } catch (e) { notice(e.message); }
});
for (const action of ['in','out']) $(action).addEventListener('click', async event => {
  const button = event.currentTarget; button.disabled = true;
  try { await command(action); reconcileEntry(state); } catch (e) { notice(e.message); }
  finally { button.disabled = false; }
});
$('finish').addEventListener('click', async () => {
  try { await command('finish'); stopSpeech(); stopMic(); } catch (e) { notice(e.message); }
});
$('resume').addEventListener('click', async () => { try { await command('resume'); paused=false; } catch(e) {notice(e.message);} });
$('correction').addEventListener('submit', async e => {
  e.preventDefault(); if (state?.page !== 1 || !state?.undo) return notice('请先拍照识别，再纠正本次记录');
  interrupt(); notice('正在理解纠正…');
  try { state = await api('correct',{session:state.session,text:$('text').value}); $('text').value=''; notice(state.message); paused=state.interrupted; reconcileCorrection(state); }
  catch(e) {notice(e.message);}
});
function stopMic() {
  const oldSocket=socket; socket=null; if (oldSocket) oldSocket.close();
  if (processor) processor.disconnect(); if (sourceNode) sourceNode.disconnect();
  if (mic) mic.getTracks().forEach(t=>t.stop()); if (context) context.close().catch(()=>{});
  mic=null;context=null;processor=null;sourceNode=null;audioSession=-1;
}
async function startMic(sid) {
  if (connecting) return; connecting=true;
  try {
    stopMic();
    const stream=await navigator.mediaDevices.getUserMedia({audio:{echoCancellation:true,noiseSuppression:true,autoGainControl:true},video:false});
    if (!voiceEnabled || state?.operation!==sid || state?.page===0) {stream.getTracks().forEach(t=>t.stop());return;}
    mic=stream; audioSession=sid;
    const ws=new WebSocket('ws://'+location.host+'/audio'); socket=ws;
    await new Promise((resolve,reject)=>{ws.onopen=resolve;ws.onerror=reject;});
    let ready=false;
    ws.onmessage=e=>{
      const msg=JSON.parse(e.data);
      if(msg.type==='ready'){ready=true;$('voiceStatus').textContent='正在聆听 · 可以随时说出纠正';}
      if(msg.type==='speech') interrupt();
      if(msg.type==='transcript') $('transcript').textContent='听到：'+msg.text;
      if(msg.type==='corrected'){state=msg.state;paused=state.interrupted;reconcileCorrection(state);notice(state.message);}
      if(msg.type==='error') {notice(msg.text);$('voiceStatus').textContent=msg.text;}
    };
    ws.onclose=()=>{if(socket===ws){$('voiceStatus').textContent='语音连接已断开，点击语音按钮重新启用';voiceEnabled=false;stopMic();}};
    context=new AudioContext({sampleRate:16000});await context.resume();
    sourceNode=context.createMediaStreamSource(mic);
    processor=context.createScriptProcessor(2048,1,1);
    processor.onaudioprocess=e=>{
      if(!ready || ws.readyState!==WebSocket.OPEN)return;
      const data=e.inputBuffer.getChannelData(0), ratio=context.sampleRate/16000;
      const samples=new Int16Array(Math.floor(data.length/ratio));let sum=0;
      for(let i=0;i<samples.length;i++){const v=data[Math.floor(i*ratio)];sum+=v*v;samples[i]=Math.max(-1,Math.min(1,v))*32767;}
      const rms=Math.sqrt(sum/samples.length);
      vadFrames=rms>.035?vadFrames+1:0;
      if(vadFrames>=2 && Date.now()-lastVad>1000){lastVad=Date.now();interrupt();}
      ws.send(samples.buffer);
    };
    sourceNode.connect(processor);processor.connect(context.destination);
  }catch{permissionFailed=true;voiceEnabled=false;stopMic();$('voice').textContent='启用麦克风';notice('麦克风未获授权或连接失败，可使用文字纠正');}
  finally{connecting=false;}
}
$('voice').addEventListener('click',async()=>{
  if(voiceEnabled){voiceEnabled=false;stopMic();$('voice').textContent='启用麦克风';$('voiceStatus').textContent='收音已关闭 · 识别结果仅在屏幕显示';return;}
  voiceEnabled=true;permissionFailed=false;
  $('voice').textContent='关闭麦克风';$('voiceStatus').textContent='进入入库或出库后开始聆听';
  if(state?.page>0)await startMic(state.operation);
});
async function poll(){
  if(polling)return;polling=true;
  try{
    state=await api('state');
    if (soundTest !== null && soundTest !== state.sound_test && state.page === 4) {
      stopSpeech();
      const preview = new SpeechSynthesisUtterance('买了些什么？');
      preview.lang = 'zh-CN'; preview.volume = (state.volume ?? 45) / 100;
      speechSynthesis.speak(preview);
    }
    soundTest = state.sound_test;
    if(state.photo_available && $('source').textContent==='尚未选择照片') $('source').textContent='已载入上次选择的照片，可重新选择';
    if(state.message)notice(state.message);else if(state.page===0)notice('已连接 · 库存会保存在本机');
    if(state.page===0||state.page>=4){if(mic)stopMic();if(voiceEnabled)$('voiceStatus').textContent='收音已暂停';}
    else if(voiceEnabled&&!permissionFailed&&audioSession!==state.operation&&!connecting)startMic(state.operation);
    reconcileEntry(state); reconcileCorrection(state);
    if(state.page===2){
      const token=state.session+':'+state.capture;
      if(captureToken!==token){captureToken=token;const captured={session:state.session,capture:state.capture};
        (async()=>{try{await uploadFrame(true);await api('recognize',captured);}catch(e){notice(e.message);}})();
      }
    }
  }catch(e){notice(e.message);}finally{polling=false;}
}
setInterval(poll,250);poll();
setInterval(()=>{if(camera&&(state?.page===1||state?.page===2)&&!frameUpload)uploadFrame().catch(()=>{});},250);
window.addEventListener('pagehide',()=>{stopMic();stopSpeech();if(camera)camera.getTracks().forEach(t=>t.stop());});
