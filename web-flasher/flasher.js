import { loadFirmware, writeFirmware } from './flash-core.js';

const $ = id => document.getElementById(id);
const connectBtn = $('connectBtn');
const flashBtn = $('flashBtn');
const disconnectBtn = $('disconnectBtn');
const firmwareFile = $('firmwareFile');
const useCustom = $('useCustom');
let transport = null;
let loader = null;
let library = null;
let busy = false;
const supported = window.isSecureContext && 'serial' in navigator;

function log(message) {
  $('logConsole').textContent = ($('logConsole').textContent + `${message}\n`).slice(-24000);
  $('logConsole').scrollTop = $('logConsole').scrollHeight;
}
function status(message, error = false) {
  $('status').textContent = message;
  $('status').classList.toggle('error', error);
}
function controls() {
  connectBtn.disabled = !supported || busy || !!loader;
  disconnectBtn.disabled = busy || !loader;
  useCustom.disabled = busy;
  firmwareFile.disabled = busy || !useCustom.checked;
  flashBtn.disabled = busy || !loader || (useCustom.checked && !firmwareFile.files.length);
  connectBtn.textContent = loader ? 'Device connected' : 'Connect device';
}
function progress(percent) {
  $('progressBar').style.width = `${percent}%`;
  $('progress').setAttribute('aria-valuenow', percent);
  $('progressText').textContent = percent === 100 ? '100% — written and verified' : `${percent}%`;
}
async function disconnect() {
  const previous = transport;
  loader = null;
  transport = null;
  if (previous) {
    try { await previous.disconnect(); } catch (error) { log(`Closing connection: ${error.message}`); }
  }
  controls();
}

connectBtn.addEventListener('click', async () => {
  busy = true;
  controls();
  progress(0);
  try {
    // Open the picker while the browser still has the button's user gesture.
    const port = await navigator.serial.requestPort();
    status('Connecting to the bootloader…');
    library ||= await import('./vendor/flashing.js');
    transport = new library.Transport(port);
    loader = new library.ESPLoader({
      transport, baudrate: 115200,
      terminal: { clean() {}, write: log, writeLine: log },
    });
    await loader.main();
    if (loader.chip.CHIP_NAME !== 'ESP32-C3' || loader.secureDownloadMode) {
      throw new Error('This device is not a supported ESP32-C3 X3/X4.');
    }
    if (await loader.detectFlashSize() !== '16MB') throw new Error('This device does not have the expected 16 MB flash.');
    status('Connected. Ready to install firmware.');
  } catch (error) {
    await disconnect();
    if (error.name === 'NotFoundError') status('No device selected. Connect when you are ready.');
    else { status(`Connection failed: ${error.message}`, true); log(error.message); }
  } finally {
    busy = false;
    controls();
  }
});

flashBtn.addEventListener('click', async () => {
  busy = true;
  controls();
  progress(0);
  try {
    status('Checking firmware…');
    const firmware = await loadFirmware(path => fetch(path, { cache: 'no-store' }), useCustom.checked ? firmwareFile.files[0] : null);
    log(`Installing ${firmware.name}`);
    status('Writing and verifying firmware. Keep the USB cable connected.');
    await writeFirmware(loader, firmware.files, bytes => library.SparkMD5.ArrayBuffer.hash(bytes), progress);
    status('Firmware installed and verified. Restarting your reader…');
    try {
      await loader.after('hard_reset');
      status('Firmware installed and verified. Your reader is restarting.');
    } catch (error) {
      log(`Automatic restart: ${error.message}`);
      status('Firmware installed and verified. Unplug and restart your reader.');
    }
  } catch (error) {
    status(`Installation failed: ${error.message} Reconnect to try again.`, true);
    log(error.message);
  } finally {
    await disconnect();
    busy = false;
    controls();
  }
});

disconnectBtn.addEventListener('click', async () => {
  busy = true;
  controls();
  try { await loader.after('hard_reset'); } catch (error) { log(error.message); }
  await disconnect();
  busy = false;
  status('Disconnected.');
  controls();
});
useCustom.addEventListener('change', controls);
firmwareFile.addEventListener('change', controls);
if (supported) {
  navigator.serial.addEventListener('disconnect', event => {
    if (event.target === transport?.device && !busy) {
      void disconnect();
      status('Device disconnected. Connect again to continue.');
    }
  });
} else {
  status(window.isSecureContext
    ? 'Use desktop Chrome or Edge to connect over USB. This browser does not support Web Serial.'
    : 'Open this page over HTTPS or at localhost to connect over USB.', true);
}
window.addEventListener('beforeunload', event => {
  if (busy) { event.preventDefault(); event.returnValue = ''; }
});
controls();
