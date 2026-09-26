import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { createHash } from 'node:crypto';
import { ESPLoader, SparkMD5 } from '../vendor/flashing.js';
import { loadFirmware, validateApplication, writeFirmware } from '../flash-core.js';

const base = new URL('../', import.meta.url);
const diskFetch = async path => {
  try { const bytes = await readFile(new URL(path, base)); return new Response(bytes); }
  catch { return new Response('', { status: 404 }); }
};
const app = new Uint8Array(await readFile(new URL('firmware/firmware.bin', base)));

test('bundled firmware has matching hashes and the correct four flash offsets', async () => {
  const { files } = await loadFirmware(diskFetch);
  assert.deepEqual(files.map(f => f.address), [0, 0x8000, 0xe000, 0x10000]);
  assert.deepEqual(files[3].data, app);
});
test('accepts the current compiled application as a custom file', async () => {
  const { files, name } = await loadFirmware(diskFetch, new File([app], 'custom.bin'));
  assert.equal(name, 'custom.bin');
  assert.deepEqual(files[3].data, app);
});
test('rejects missing package and corrupt download before flashing', async () => {
  await assert.rejects(loadFirmware(async () => new Response('', { status: 404 })), /missing/);
  await assert.rejects(loadFirmware(path => path.endsWith('bootloader.bin') ? Promise.resolve(new Response('bad')) : diskFetch(path)), /integrity/);
});
test('rejects a corrupt application, wrong chip, wrong board, and factory image', async () => {
  const corrupt = app.slice(); corrupt[1000] ^= 1;
  await assert.rejects(validateApplication(corrupt), /checksum|SHA-256/);
  const wrongChip = app.slice(); wrongChip[12] = 9;
  await assert.rejects(validateApplication(wrongChip), /ESP32-C3/);
  const wrongBoard = app.slice();
  const tag = Buffer.from(wrongBoard).indexOf('CROSSPOINT-BOARD-V1:x4;'); wrongBoard[tag + 19] = 53;
  await assert.rejects(validateApplication(wrongBoard), /board tag/);
  const bootloader = new Uint8Array(await readFile(new URL('firmware/bootloader.bin', base)));
  await assert.rejects(validateApplication(bootloader), /factory image/);
});
test('rejects truncated and oversized files', async () => {
  await assert.rejects(validateApplication(app.slice(0, 2000)));
  await assert.rejects(loadFirmware(diskFetch, { size: 0x640001 }), /too large/);
});
test('refuses a different chip, secure mode, or flash size without writing', async () => {
  for (const properties of [{ chip: { CHIP_NAME: 'ESP32-S3' } }, { secureDownloadMode: true }, { detectFlashSize: async () => '4MB' }]) {
    const loader = { chip: { CHIP_NAME: 'ESP32-C3' }, detectFlashSize: async () => '16MB', writeFlash: () => assert.fail('must not write'), ...properties };
    await assert.rejects(writeFirmware(loader, [], () => '', () => {}));
  }
});

for (const outcome of ['verified', 'hash mismatch', 'disconnected']) {
  test(`real esptool writer: ${outcome}`, async () => {
    const loader = new ESPLoader({ transport: { getInfo: () => 'test', trace() {} }, baudrate: 115200, terminal: { clean() {}, write() {}, writeLine() {} } });
    loader.chip = { CHIP_NAME: 'ESP32-C3', BOOTLOADER_FLASH_OFFSET: 0 };
    loader.IS_STUB = true;
    loader.detectFlashSize = async () => '16MB';
    const data = new Uint8Array([1, 2, 3, 4, 5, 6, 7, 8]);
    let blocks = 0, checks = 0;
    loader.flashDeflBegin = async () => 1;
    loader.flashDeflBlock = async () => { blocks++; if (outcome === 'disconnected') throw new Error('Device disconnected'); };
    loader.flashDeflFinish = async () => {};
    loader.flashMd5sum = async () => { checks++; return outcome === 'hash mismatch' ? 'bad' : createHash('md5').update(data).digest('hex'); };
    const progress = [];
    const operation = writeFirmware(loader, [{ data, address: 0x10000 }], bytes => SparkMD5.ArrayBuffer.hash(bytes), value => progress.push(value));
    if (outcome === 'verified') { await operation; assert.equal(progress.at(-1), 100); assert.equal(checks, 1); }
    else { await assert.rejects(operation, outcome === 'hash mismatch' ? /MD5/ : /disconnected/); assert.ok(!progress.includes(100)); }
    assert.ok(blocks > 0, 'must call the actual flash protocol writer');
  });
}
