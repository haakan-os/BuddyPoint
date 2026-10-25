(function () {
  'use strict';
  let token = '';
  let timer = null;
  let submitting = false;
  let pendingCancel = false;
  let waitingForReader = false;
  const el = id => document.getElementById(id);
  const message = text => { el('message').textContent = text; };

  async function request(path, body) {
    const headers = {};
    if (token) headers.Authorization = 'Bearer ' + token;
    const options = { method: body === undefined ? 'GET' : 'POST', headers, cache: 'no-store', credentials: 'omit' };
    if (body !== undefined) { headers['Content-Type'] = 'application/json'; options.body = JSON.stringify(body); }
    let response;
    try { response = await fetch(path, options); }
    catch (error) { error.retryable = true; throw error; }
    let data;
    try { data = await response.json(); } catch (_) { throw new Error('The reader returned an unexpected response.'); }
    if (!response.ok) {
      if (response.status === 401) {
        token = ''; pendingCancel = false; clearTimeout(timer);
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
    try {
      if (pendingCancel) {
        await request('/api/action', { action: 'cancel' });
        pendingCancel = false;
      }
      render(await request('/api/status'));
      if (waitingForReader) { message(''); waitingForReader = false; }
    } catch (error) {
      if (error.retryable) {
        waitingForReader = true;
        message(pendingCancel ? 'Cancellation queued in this tab. Keep it open; it will be sent between AO3 requests. Press Back on the reader to stop sooner.' :
          'Waiting for the reader. Status refreshes between AO3 requests; keep this tab open.');
      } else message(error.message);
    }
    if (token) timer = setTimeout(poll, 2000);
  }
  async function submit(path, body) {
    if (submitting) return;
    submitting = true; message('');
    try { await request(path, body); message('Request accepted. Follow progress below.'); }
    catch (error) {
      if (error.retryable && body.action === 'cancel') pendingCancel = true;
      else message(error.message);
    }
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
