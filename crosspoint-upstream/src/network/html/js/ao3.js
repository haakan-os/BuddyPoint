(function () {
  'use strict';
  let token = '';
  let submitting = false;
  let handedOff = false;
  const el = id => document.getElementById(id);
  const message = text => { el('message').textContent = text; };

  async function request(path, body) {
    const controller = new AbortController();
    const deadline = setTimeout(() => controller.abort(), 10000);
    const headers = { 'Content-Type': 'application/json' };
    if (token) headers.Authorization = 'Bearer ' + token;
    try {
      const response = await fetch(path, { method: 'POST', headers, body: JSON.stringify(body),
        cache: 'no-store', credentials: 'omit', signal: controller.signal });
      let data;
      try { data = await response.json(); }
      catch (error) {
        if (controller.signal.aborted) throw error;
        throw new Error('The reader returned an unexpected response.');
      }
      if (!response.ok) {
        if (response.status === 401) {
          token = '';
          el('pairCard').hidden = false; el('accountCard').hidden = true;
        }
        throw new Error(data.error || 'The reader could not complete this request.');
      }
      return data;
    } finally { clearTimeout(deadline); }
  }
  el('pairForm').addEventListener('submit', async event => {
    event.preventDefault();
    if (submitting || handedOff) return;
    submitting = true; el('pairButton').disabled = true; message('');
    try {
      const data = await request('/api/pair', { code: el('pairCode').value });
      if (!data.token) throw new Error('The reader returned an unexpected response.');
      token = data.token; el('pairCode').value = '';
      el('pairCard').hidden = true; el('accountCard').hidden = false;
    } catch (error) { message(error.name === 'AbortError' ? 'The reader did not respond. Check its AO3 screen and try connecting again.' : error.message); }
    finally { submitting = false; el('pairButton').disabled = false; }
  });
  el('loginForm').addEventListener('submit', async event => {
    event.preventDefault();
    const credentials = { username: el('username').value.trim(), password: el('password').value };
    el('password').value = '';
    if (submitting || handedOff || !token) { credentials.password = ''; return; }
    submitting = true; el('loginButton').disabled = true;
    message('Sending details to your reader…');
    try {
      const data = await request('/api/login', credentials);
      if (!data.accepted || !data.continueOnReader) throw new Error('The reader did not confirm the handoff.');
      handedOff = true; token = '';
      el('username').disabled = true; el('password').disabled = true;
      message('Details received. Continue on your reader for sign-in progress, downloads and updates. You can close this tab.');
    } catch (error) {
      message(error.name === 'AbortError' || error instanceof TypeError ?
        'No confirmation received. Check the reader for sign-in progress before trying again.' : error.message);
    } finally {
      credentials.password = ''; el('password').value = '';
      submitting = false; el('loginButton').disabled = handedOff;
    }
  });
})();
