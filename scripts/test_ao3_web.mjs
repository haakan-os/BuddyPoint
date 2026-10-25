import test from 'node:test';
import assert from 'node:assert/strict';
import vm from 'node:vm';
import { readFileSync } from 'node:fs';
const script = readFileSync(new URL('../crosspoint-upstream/src/network/html/js/ao3.js', import.meta.url), 'utf8');
function browser() {
  const elements = new Map(), requests = [], responses = [], timers = new Map();
  const el = id => {
    if (!elements.has(id)) elements.set(id, { value: '', hidden: false, disabled: false, textContent: '', listeners: {},
      addEventListener(type, cb) { this.listeners[type] = cb; }, removeAttribute(name) { delete this[name]; } });
    return elements.get(id);
  };
  vm.runInNewContext(script, { document: { getElementById: el },
    fetch: async (path, options) => { requests.push({ path, ...options }); assert.ok(responses.length, 'unexpected fetch');
      const next = responses.shift(); if (next instanceof Error) throw next;
      const [status, data] = next; return { ok: status < 400, status, json: async () => data }; },
    setTimeout: cb => { const id = timers.size + 1; timers.set(id, cb); return id; }, clearTimeout: id => timers.delete(id) });
  return { el, requests, responses, timers, event: (id, type = 'click') => el(id).listeners[type]({ preventDefault() {} }) };
}
const status = { signedIn: true, account: 'Reader', busy: false, phase: 'Ready', checked: 0, saved: 0, unchanged: 0, failed: 0, total: 0 };
async function pair(b) {
  b.el('pairCode').value = '00112233';
  b.responses.push([200, { token: 'test-token' }], [200, status]);
  await b.event('pairForm', 'submit');
}
test('pairing sends code once and keeps authorization out of URLs', async () => {
  const b = browser(); await pair(b);
  assert.equal(JSON.parse(b.requests[0].body).code, '00112233');
  assert.equal(b.requests[0].headers.Authorization, undefined);
  assert.equal(b.requests[1].headers.Authorization, 'Bearer test-token');
  assert.equal(b.el('pairCode').value, ''); assert.equal(b.el('pairCard').hidden, true);
  assert.equal(b.el('accountCard').hidden, false);
  for (const r of b.requests) { assert.equal(r.credentials, 'omit'); assert.equal(r.cache, 'no-store'); assert.ok(!r.path.includes('token')); }
});
test('login clears password and renders untrusted account text literally', async () => {
  const b = browser(); await pair(b);
  b.el('username').value = ' Reader '; b.el('password').value = 'private-password';
  b.responses.push([202, { accepted: true }], [200, { ...status, account: '<img src=x onerror=alert(1)>', busy: true }]);
  const submitting = b.event('loginForm', 'submit');
  assert.equal(b.el('password').value, ''); await submitting;
  assert.deepEqual(JSON.parse(b.requests[2].body), { username: 'Reader', password: 'private-password' });
  assert.equal(b.el('password').value, ''); assert.equal(b.el('loginButton').disabled, true);
  assert.equal(b.el('accountStatus').textContent, 'Saved AO3 account: <img src=x onerror=alert(1)>');
  assert.equal(b.el('accountStatus').innerHTML, undefined);
});
test('all account actions use authenticated POST and cancellation remains available', async () => {
  const b = browser(); await pair(b);
  for (const [id, action] of [['bookmarksButton','bookmarks'], ['updatesButton','updates'], ['forgetButton','forget'], ['cancelButton','cancel']]) {
    b.responses.push([202, { accepted: true }], [200, { ...status, busy: true }]);
    await b.event(id);
    const r = b.requests.at(-2); assert.equal(r.path, '/api/action'); assert.equal(r.method, 'POST');
    assert.equal(r.headers.Authorization, 'Bearer test-token'); assert.equal(JSON.parse(r.body).action, action);
    assert.equal(b.el('cancelButton').disabled, false); assert.equal(b.el('updatesButton').disabled, true);
  }
});
test('expired pairing hides account controls and stops polling', async () => {
  const b = browser(); await pair(b);
  b.responses.push([401, { error: 'Pair again' }]);
  await [...b.timers.values()][0]();
  assert.equal(b.el('pairCard').hidden, false); assert.equal(b.el('accountCard').hidden, true);
  assert.equal(b.timers.size, 0); assert.match(b.el('message').textContent, /Pair again/);
});
test('busy errors are displayed and password stays cleared', async () => {
  const b = browser(); await pair(b);
  b.el('username').value = 'Reader'; b.el('password').value = 'secret';
  b.responses.push([409, { error: 'Another job is running' }], [200, { ...status, busy: true }]);
  await b.event('loginForm', 'submit');
  assert.equal(b.el('password').value, ''); assert.match(b.el('message').textContent, /Another job/);
});

test('polling reconnects after the listener pauses without losing pairing', async () => {
  const b = browser(); await pair(b);
  b.responses.push(new Error('Network unavailable'));
  await [...b.timers.values()][0]();
  assert.equal(b.el('pairCard').hidden, true);
  assert.match(b.el('message').textContent, /Status refreshes between AO3 requests/);
  b.responses.push([200, { ...status, phase: 'Finished' }]);
  await [...b.timers.values()][0]();
  assert.equal(b.requests.at(-1).headers.Authorization, 'Bearer test-token');
  assert.equal(b.el('message').textContent, '');
});
test('cancellation retries after a paused listener and then resumes status polling', async () => {
  const b = browser(); await pair(b);
  b.responses.push(new Error('Network unavailable'), new Error('Network unavailable'));
  await b.event('cancelButton');
  assert.match(b.el('message').textContent, /Cancellation queued/);
  b.responses.push([202, { accepted: true }], [200, { ...status, phase: 'Cancelled' }]);
  await [...b.timers.values()][0]();
  assert.equal(b.requests.at(-2).path, '/api/action');
  assert.equal(JSON.parse(b.requests.at(-2).body).action, 'cancel');
  assert.equal(b.requests.at(-1).path, '/api/status');
  assert.match(b.el('jobStatus').textContent, /Cancelled/);
});
