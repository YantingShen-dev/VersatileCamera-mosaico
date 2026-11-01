// Browser speech policy: operation greetings only, including after corrections/retries.
const fs = require('node:fs');
const vm = require('node:vm');
const assert = require('node:assert/strict');
const spoken = [];
const speechSynthesis = {cancel() {}, getVoices: () => [], speak: u => spoken.push(u.text)};
const sandbox = {
  document: {getElementById: () => ({addEventListener() {}, textContent: ''})},
  window: {speechSynthesis, addEventListener() {}}, speechSynthesis,
  SpeechSynthesisUtterance: function(text) { this.text = text; },
  setInterval() {}, fetch: async () => ({ok: true, json: async () => ({page: 0})}),
};
vm.createContext(sandbox);
vm.runInContext(fs.readFileSync(require('node:path').join(__dirname, '../tools/web/app.js'), 'utf8'), sandbox);
vm.runInContext(`
  const operation = {page:1, operation:1, direction:1, session:1, undo:false};
  reconcileEntry(operation);
  reconcileEntry({...operation, page:2});
  const result = {...operation, undo:true, batch:[{name:'乐事原味薯片',quantity:2}], revision:1};
  reconcileEntry(result); reconcileCorrection(result);
  reconcileCorrection({...result, interrupted:true});
  reconcileEntry({...result, revision:2}); reconcileCorrection({...result, revision:2});
  reconcileEntry({...result, session:2}); // consecutive capture, same operation
  reconcileEntry({...operation, operation:2, direction:2});
`, sandbox);
assert.deepEqual(spoken, ['买了些什么？', '想取出些什么？']);
console.log('PASS: greetings only; results, corrections and consecutive captures stay silent.');
