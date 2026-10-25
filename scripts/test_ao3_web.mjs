import test from 'node:test';
import assert from 'node:assert/strict';
import vm from 'node:vm';
import { readFileSync } from 'node:fs';
const script = readFileSync(new URL('../crosspoint-upstream/src/network/html/js/ao3.js', import.meta.url), 'utf8');
function browser() {
  const elements = new Map(), requests = [], responses = [], timers = new Map();
  let nextTimer = 0;
  const el = id => {
    if (!elements.has(id)) elements.set(id, { value: '', hidden: false, disabled: false, textContent: '', listeners: {},
      addEventListener(type, cb) { this.listeners[type] = cb; } });
    return elements.get(id);
  };
  vm.runInNewContext(script, { document: { getElementById: el }, AbortController, TypeError,
    fetch: async (path, options) => {
      requests.push({ path, ...options }); assert.ok(responses.length, 'unexpected fetch');
      const next = responses.shift();
      if (next instanceof Error) throw next;
      if (typeof next === 'function') return next(options);
      const [status, data] = next; return { ok: status < 400, status, json: async () => data };
    },
    setTimeout: (cb, ms) => { const id = ++nextTimer; timers.set(id, { cb, ms }); return id; },
    clearTimeout: id => timers.delete(id) });
  return { el, requests, responses, timers,
    event: (id, type = 'submit') => el(id).listeners[type]({ preventDefault() {} }),
    expire: ms => {
      const found = [...timers].find(([, value]) => value.ms === ms);
      assert.ok(found, `missing ${ms} ms timeout`); timers.delete(found[0]); found[1].cb();
    } };
}
function untilAbort(signal) {
  return new Promise((_, reject) => signal.addEventListener('abort', () => {
    const error = new Error('Aborted'); error.name = 'AbortError'; reject(error);
  }, { once: true }));
}
async function pair(b) {
  b.el('pairCode').value = '00112233'; b.responses.push([200, { token: 'test-token' }]);
  await b.event('pairForm');
}
function credentials(b) { b.el('username').value = ' Reader '; b.el('password').value = 'private-password'; }
const accepted = [202, { accepted: true, continueOnReader: true }];

test('pairing sends code once and keeps authorization out of URLs', async () => {
  const b = browser(); await pair(b);
  assert.equal(b.requests.length, 1);
  assert.equal(JSON.parse(b.requests[0].body).code, '00112233');
  assert.equal(b.requests[0].headers.Authorization, undefined);
  assert.equal(b.requests[0].credentials, 'omit'); assert.equal(b.requests[0].cache, 'no-store');
  assert.equal(b.el('pairCode').value, ''); assert.equal(b.el('pairCard').hidden, true);
  assert.equal(b.el('accountCard').hidden, false); assert.equal(b.timers.size, 0);
});
test('login hands off once, clears password, and never polls the departing reader', async () => {
  const b = browser(); await pair(b); credentials(b); b.responses.push(accepted);
  const sending = b.event('loginForm'); assert.equal(b.el('password').value, ''); await sending;
  const r = b.requests[1]; assert.equal(r.path, '/api/login');
  assert.equal(r.method, 'POST'); assert.equal(r.headers.Authorization, 'Bearer test-token');
  assert.deepEqual(JSON.parse(r.body), { username: 'Reader', password: 'private-password' });
  assert.equal(b.el('password').value, ''); assert.equal(b.el('loginButton').disabled, true);
  assert.match(b.el('message').textContent, /Continue on your reader/);
  assert.equal(b.timers.size, 0);
  await b.event('loginForm'); await b.event('pairForm'); assert.equal(b.requests.length, 2);
});
test('expired pairing hides the credential form', async () => {
  const b = browser(); await pair(b); credentials(b); b.responses.push([401, { error: 'Pair again' }]);
  await b.event('loginForm');
  assert.equal(b.el('pairCard').hidden, false); assert.equal(b.el('accountCard').hidden, true);
  assert.match(b.el('message').textContent, /Pair again/); assert.equal(b.el('password').value, '');
  assert.equal(b.timers.size, 0);
});
test('rejected credentials remain editable and response text cannot become HTML', async () => {
  const b = browser(); await pair(b); credentials(b);
  b.responses.push([409, { error: '<img src=x onerror=alert(1)>' }]); await b.event('loginForm');
  assert.equal(b.el('loginButton').disabled, false); assert.equal(b.el('password').value, '');
  assert.equal(b.el('message').textContent, '<img src=x onerror=alert(1)>');
  assert.equal(b.el('message').innerHTML, undefined); assert.equal(b.timers.size, 0);
});
test('a stalled pairing connection times out and can be retried', async () => {
  const b = browser(); b.responses.push(options => untilAbort(options.signal));
  const pending = b.event('pairForm'); b.expire(10000); await pending;
  assert.equal(b.el('pairButton').disabled, false); assert.match(b.el('message').textContent, /did not respond/);
  assert.equal(b.timers.size, 0); await pair(b); assert.equal(b.el('accountCard').hidden, false);
});
test('a stalled login response body times out without resending the password', async () => {
  const b = browser(); await pair(b); credentials(b);
  let readingBody = false;
  b.responses.push(options => ({ ok: true, status: 202, json: () => {
    readingBody = true; return untilAbort(options.signal);
  } }));
  const pending = b.event('loginForm');
  for (let i = 0; i < 5 && !readingBody; i++) await Promise.resolve();
  assert.ok(readingBody); b.expire(10000); await pending;
  assert.match(b.el('message').textContent, /Check the reader/);
  assert.equal(b.el('password').value, ''); assert.equal(b.el('loginButton').disabled, false);
  assert.equal(b.requests.length, 2); assert.equal(b.timers.size, 0);
});
test('double submission does not send a second login request', async () => {
  const b = browser(); await pair(b); credentials(b);
  b.responses.push(options => untilAbort(options.signal));
  const pending = b.event('loginForm'); await b.event('loginForm');
  assert.equal(b.requests.length, 2); b.expire(10000); await pending;
});
test('lost login acknowledgement tells the user to check the device', async () => {
  const b = browser(); await pair(b); credentials(b); b.responses.push(new TypeError('Failed to fetch'));
  await b.event('loginForm'); assert.match(b.el('message').textContent, /Check the reader/);
  assert.equal(b.el('password').value, ''); assert.equal(b.requests.length, 2); assert.equal(b.timers.size, 0);
});
test('malformed acknowledgements do not claim a successful handoff', async () => {
  const b = browser(); await pair(b); credentials(b); b.responses.push([200, { accepted: true }]);
  await b.event('loginForm'); assert.match(b.el('message').textContent, /did not confirm/);
  assert.equal(b.el('loginButton').disabled, false); assert.equal(b.timers.size, 0);
});
