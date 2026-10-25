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
      if (data.bridge) {
        el('bridgeEnabled').checked = data.bridge.enabled;
        el('bridgeDefault').value = data.bridge.default_url || '';
        el('bridgeSsid').value = data.bridge.ssid || '';
        const networks = data.bridge.networks || [];
        el('bridgeNetworks').textContent = networks.map(n => n.ssid + ': ' + n.url).join('\n');
        el('bridgeUrl').value = (networks.find(n => n.ssid === data.bridge.ssid) || {}).url || '';
      }
      el('pairCard').hidden = true; el('accountCard').hidden = false;
    } catch (error) { message(error.name === 'AbortError' ? 'The reader did not respond. Check its AO3 screen and try connecting again.' : error.message); }
    finally { submitting = false; el('pairButton').disabled = false; }
  });
  el('bridgeForm').addEventListener('submit', async event => {
    event.preventDefault();
    if (submitting || handedOff || !token) return;
    submitting = true; el('bridgeButton').disabled = true;
    const body = { enabled: !!el('bridgeEnabled').checked, default_url: el('bridgeDefault').value.trim(),
      ssid: el('bridgeSsid').value, url: el('bridgeUrl').value.trim() };
    if (el('bridgeToken').value) body.token = el('bridgeToken').value;
    el('bridgeToken').value = '';
    try {
      const result = await request('/api/bridge', body);
      if (!result.saved) throw new Error('The reader did not confirm saving the settings.');
      message('Bridge settings saved. Leave File Transfer and reopen AO3 on your reader.');
    } catch (error) { message(error.name === 'AbortError' ? 'No confirmation received. Reopen the panel to check saved settings.' : error.message); }
    finally { body.token = ''; submitting = false; el('bridgeButton').disabled = false; }
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
