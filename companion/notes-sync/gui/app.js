'use strict';
const $ = id => document.getElementById(id);
const token = location.hash.slice(1);
let loaded = false, pending = false, quitting = false, lastServerError = '';
const keys = ['folder', 'device', 'reader_folder', 'math', 'math_size', 'interval'];
async function api(path, body) {
  const response = await fetch(path, {method: body === undefined ? 'GET' : 'POST', headers: {
    Authorization: `Bearer ${token}`, ...(body === undefined ? {} : {'Content-Type': 'application/json'})
  }, ...(body === undefined ? {} : {body: JSON.stringify(body)})});
  const data = await response.json();
  if (!response.ok) throw new Error(data.error || 'Could not contact the local sync app.');
  return data;
}
function error(message) { $('error').textContent = message; $('error').hidden = !message; }
function settings() {
  return Object.fromEntries(keys.map(key => [key, key === 'math' ? $(key).checked :
    ['math_size', 'interval'].includes(key) ? Number($(key).value) : $(key).value]));
}
function show(state) {
  if (!loaded) {
    for (const key of keys) {
      if (key === 'math') $(key).checked = state.settings[key];
      else {
        if (key === 'math_size' && ![...$(key).options].some(o => o.value === String(state.settings[key]))) {
          $(key).add(new Option(`Custom · ${state.settings[key]}`, state.settings[key]));
        }
        $(key).value = state.settings[key];
      }
    }
    loaded = true;
  }
  $('status').textContent = state.status;
  $('badge').textContent = state.running ? (state.stopping ? 'Stopping' : 'Active') : (state.error ? 'Check setup' : 'Ready');
  $('settings').disabled = state.running || pending;
  for (const id of ['once', 'watch', 'preview']) $(id).disabled = state.running || pending;
  $('stop').disabled = !state.running || state.stopping;
  $('math_size').disabled = !state.settings || ! $('math').checked;
  if (state.error !== lastServerError) { error(state.error); lastServerError = state.error; }
  const labels = {uploaded:'Uploaded',downloaded:'Downloaded',unchanged:'Unchanged',conflicts:'Conflicts',rendered:'Reading copies',helpers:'Links & cards',skipped:'Skipped'};
  if (Object.keys(state.summary).length) {
    $('summary').replaceChildren(...Object.entries(state.summary).map(([key,value]) => {
      const el = document.createElement('div'); el.className = 'metric';
      const number = document.createElement('b'); number.textContent = value;
      const label = document.createElement('span'); label.textContent = labels[key] || key;
      el.append(number,label); return el;
    }));
  } else {
    const placeholder = document.createElement('p'); placeholder.className = 'muted';
    placeholder.textContent = 'Your last sync summary will appear here.'; $('summary').replaceChildren(placeholder);
  }
  const log = $('log'), nearBottom = log.scrollHeight-log.scrollTop-log.clientHeight < 40;
  const text = state.logs.map(line => line.text).join('\n') || 'Ready when you are.';
  if (log.textContent !== text) { log.textContent = text; if (nearBottom) log.scrollTop=log.scrollHeight; }
}
async function start(mode) {
  pending = true; error('');
  for (const id of ['once','watch','preview']) $(id).disabled=true;
  try { await api('/api/start',{mode,settings:settings()}); }
  catch (e) { error(e.message); }
  finally { pending=false; }
  await poll(false);
}
async function poll(repeat=true) {
  if (quitting) return;
  try { show(await api('/api/state')); }
  catch (e) { error(`Local app unavailable. Reopen the launcher. ${e.message}`); }
  if (repeat) setTimeout(poll,1000);
}
for (const mode of ['once','watch','preview']) $(mode).onclick=()=>start(mode);
$('math').onchange=()=>{ $('math_size').disabled=!$('math').checked; };
$('stop').onclick=async()=>{ try { await api('/api/stop',{}); } catch(e) { error(e.message); } };
$('browse').onclick=async()=>{
  $('browse').disabled=true; error('');
  try { const result=await api('/api/folder',{}); if(result.folder) $('folder').value=result.folder; }
  catch(e) { error(e.message); }
  finally { $('browse').disabled=false; }
};
$('quit').onclick=async()=>{
  try { await api('/api/quit',{}); quitting=true; $('settings').disabled=true;
    for(const id of ['once','watch','preview','stop','quit']) $(id).disabled=true;
    $('status').textContent='Finishing any current operation, then closing. You can close this tab.';
    $('badge').textContent='Closing';
  } catch(e) { error(e.message); }
};
poll();
