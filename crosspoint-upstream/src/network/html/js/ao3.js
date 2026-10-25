(function () {
  'use strict';
  let token = '';
  let timer = null;
  let submitting = false;
  const el = id => document.getElementById(id);
  const message = text => { el('message').textContent = text; };

  async function request(path, body) {
    const headers = {};
    if (token) headers.Authorization = 'Bearer ' + token;
    const options = { method: body === undefined ? 'GET' : 'POST', headers, cache: 'no-store', credentials: 'omit' };
    if (body !== undefined) { headers['Content-Type'] = 'application/json'; options.body = JSON.stringify(body); }
    const response = await fetch(path, options);
    let data;
    try { data = await response.json(); } catch (_) { throw new Error('The reader returned an unexpected response.'); }
    if (!response.ok) {
      if (response.status === 401) {
        token = ''; clearTimeout(timer);
        el('pairCard').hidden = false; el('accountCard').hidden = true; el('jobCard').hidden = true;
      }
      throw new Error(data.error || 'The reader could not complete this request.');
    }
    return data;
  }
  function render(status) {
    el('accountStatus').textContent = status.signedIn ? 'Saved AO3 account: ' + status.account : 'Not signed in';
    el('loginButton').disabled = status.busy || submitting;
    el('username').disabled = status.busy; el('password').disabled = status.busy;
    el('bookmarksButton').disabled = status.busy || !status.signedIn || submitting;
    el('updatesButton').disabled = status.busy || submitting;
    el('forgetButton').disabled = status.busy || !status.signedIn || submitting;
    el('cancelButton').disabled = !status.busy || submitting;
    el('jobStatus').textContent = status.phase + (status.title ? '\n' + status.title : '') +
      '\n' + status.checked + ' checked · ' + status.saved + ' saved · ' + status.unchanged + ' up to date · ' + status.failed + ' failed' +
      (status.error ? '\n' + status.error : '');
    const progress = el('progress'); progress.hidden = !status.busy;
    if (status.total > 0) progress.value = Math.min(100, Math.round(status.bytes * 100 / status.total));
    else progress.removeAttribute('value');
  }
  async function poll() {
    clearTimeout(timer);
    if (!token) return;
    try { render(await request('/api/status')); }
    catch (error) { message(error.message + ' If disconnected, keep the reader on and reopen its AO3 page.'); }
    if (token) timer = setTimeout(poll, 2000);
  }
  async function submit(path, body) {
    if (submitting) return;
    submitting = true; message('');
    try { await request(path, body); message('Request accepted. Follow progress below.'); }
    catch (error) { message(error.message); }
    finally { submitting = false; await poll(); }
  }
  el('pairForm').addEventListener('submit', async event => {
    event.preventDefault(); el('pairButton').disabled = true; message('');
    try {
      const data = await request('/api/pair', { code: el('pairCode').value });
      token = data.token; el('pairCode').value = '';
      el('pairCard').hidden = true; el('accountCard').hidden = false; el('jobCard').hidden = false;
      await poll();
    } catch (error) { message(error.message); }
    finally { el('pairButton').disabled = false; }
  });
  el('loginForm').addEventListener('submit', async event => {
    event.preventDefault();
    const credentials = { username: el('username').value.trim(), password: el('password').value };
    el('password').value = '';
    try { await submit('/api/login', credentials); }
    finally { credentials.password = ''; el('password').value = ''; }
  });
  for (const [id, action] of [['bookmarksButton', 'bookmarks'], ['updatesButton', 'updates'], ['forgetButton', 'forget'], ['cancelButton', 'cancel']]) {
    el(id).addEventListener('click', () => submit('/api/action', { action }));
  }
})();
