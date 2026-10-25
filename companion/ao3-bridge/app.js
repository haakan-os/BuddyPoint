'use strict';
const el = id => document.getElementById(id);
let busy = false;
async function api(path, body) {
  const response = await fetch(path.replace(/^\//, ''), {
    method: body ? 'POST' : 'GET', cache: 'no-store',
    headers: {'Authorization': 'Bearer ' + el('token').value, 'Content-Type': 'application/json'},
    body: body ? JSON.stringify(body) : undefined, signal: AbortSignal.timeout(15000)
  });
  const data = await response.json();
  if (!response.ok) throw new Error(data.message || data.error || 'Bridge request failed');
  return data;
}
async function run(action) {
  if (busy) return;
  busy = true;
  document.querySelectorAll('button').forEach(b => b.disabled = true);
  try { await action(); }
  catch (error) { el('status').textContent = error.message || 'Connection interrupted. Check status before retrying.'; }
  finally { busy = false; document.querySelectorAll('button').forEach(b => b.disabled = false); }
}
async function status() {
  const state = await api('/api/v1/status');
  el('status').textContent = state.signed_in ? 'Signed in as ' + state.username + '. Ready for your reader.' : 'Bridge connected. Sign in to access private bookmarks and account-only works.';
}
async function job(body) {
  const accepted = await api('/api/v1/jobs', body);
  body.password = '';
  const deadline = Date.now() + 600000;
  while (Date.now() < deadline) {
    const state = await api('/api/v1/jobs/' + accepted.job);
    if (state.state === 'error') throw new Error(state.message);
    if (state.state === 'done') return status();
    el('status').textContent = state.state === 'queued' ? 'Waiting for the bridge…' : 'Contacting AO3…';
    await new Promise(resolve => setTimeout(resolve, 1000));
  }
  throw new Error('Still waiting. Check connection status before trying again.');
}
el('connect').onclick = () => run(status);
el('login').onsubmit = event => {
  event.preventDefault();
  if (busy) return;
  const body = {operation: 'login', username: el('username').value, password: el('password').value};
  el('password').value = '';
  run(async () => { try { await job(body); } finally { body.password = ''; } });
};
el('logout').onclick = () => run(() => job({operation: 'logout'}));
