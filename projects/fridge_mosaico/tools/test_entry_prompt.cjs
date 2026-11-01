// Run with node. Speech is stubbed; no microphone, cloud calls or inventory writes.
const {readFileSync} = require('node:fs');
const {join} = require('node:path');
const vm = require('node:vm');
const assert = require('node:assert/strict');
const spoken = [], elements = new Map();
let cancels = 0;
const speechSynthesis = {cancel(){cancels++;}, getVoices(){return [];}, speak(u){spoken.push(u);}};
const sandbox = {
  document:{getElementById(id){if(!elements.has(id))elements.set(id,{addEventListener(){},textContent:''});return elements.get(id);}},
  window:{speechSynthesis,addEventListener(){}}, speechSynthesis,
  SpeechSynthesisUtterance:class{constructor(text){this.text=text;}},
  setInterval(){}, setTimeout(){}, fetch:()=>new Promise(()=>{})
};
vm.createContext(sandbox);
vm.runInContext(readFileSync(join(__dirname,'web/app.js'),'utf8'),sandbox);
const run = code => vm.runInContext(code,sandbox);
run("reconcileEntry({page:1,operation:1,direction:1})");
assert.equal(spoken[0].text,'买了些什么？');
run("reconcileEntry({page:1,operation:1,direction:1}); reconcileCorrection({page:1})");
assert.equal(spoken.length,1);
assert.equal(run('entrySpeaking'),true,'polling must not cancel the entry prompt');
const beforeCapture=cancels;
run("reconcileEntry({page:2,operation:1,direction:1})");
assert.equal(cancels,beforeCapture+1,'shutter cancels an unfinished prompt');
run("reconcileEntry({page:1,operation:1,direction:1})");
assert.equal(spoken.length,1,'recognition retry must not repeat the prompt');
run("reconcileEntry({page:0,operation:1,direction:1}); reconcileEntry({page:1,operation:2,direction:2})");
assert.equal(spoken[1].text,'想取出些什么？');
spoken[0].onend();
assert.equal(run('entrySpeaking'),true,'late callbacks must not finish a newer prompt');
spoken[1].onend();
assert.equal(run('entrySpeaking'),false);
run("reconcileEntry({page:0,operation:2,direction:2}); reconcileEntry({page:1,operation:3,direction:1})");
assert.equal(spoken[2].text,'买了些什么？');
run("state={page:1,operation:3,feedback:1,feedback_id:1,batch:[{name:'西红柿',quantity:2}],revision:1}; reconcileEntry(state); reconcileCorrection(state)");
assert.equal(spoken.length,3,'recognition results remain silent');
console.log('PASS: entry prompts, shutter cancellation, retries, late callbacks, silent recognition results');
