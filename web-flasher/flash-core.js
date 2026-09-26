export const APP_OFFSET = 0x10000;
export const APP_SIZE = 0x640000;
const PARTS = [['bootloader.bin', 0], ['partitions.bin', 0x8000], ['boot_app0.bin', 0xe000], ['firmware.bin', APP_OFFSET]];

export async function sha256(bytes) {
  return Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256', bytes)), b => b.toString(16).padStart(2, '0')).join('');
}

export async function validateApplication(bytes) {
  if (!(bytes instanceof Uint8Array) || bytes.length < 288 || bytes.length > APP_SIZE) {
    throw new Error('The application is empty, incomplete, or too large for the X3.');
  }
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  if (bytes[0] !== 0xe9 || view.getUint16(12, true) !== 5) {
    throw new Error('Choose an ESP32-C3 firmware.bin built for the X3/X4.');
  }
  if (view.getUint32(32, true) !== 0xabcd5432) {
    throw new Error('Choose the application firmware.bin, not a factory image or bootloader.');
  }
  const tags = [...new TextDecoder('latin1').decode(bytes).matchAll(/CROSSPOINT-BOARD-V1:([^;]+);/g)];
  if (!tags.length || tags.some(tag => tag[1] !== 'x4')) {
    throw new Error('This image does not contain the compatible CrossPoint X3/X4 board tag.');
  }
  let end = 24;
  let checksum = 0xef;
  if (bytes[1] < 1 || bytes[1] > 16) throw new Error('Invalid firmware segment count.');
  for (let segment = 0; segment < bytes[1]; segment++) {
    if (end + 8 > bytes.length) throw new Error('Incomplete firmware segment.');
    const length = view.getUint32(end + 4, true);
    end += 8;
    if (end + length > bytes.length) throw new Error('Incomplete firmware data.');
    for (let i = end; i < end + length; i++) checksum ^= bytes[i];
    end += length;
  }
  const checksumOffset = Math.floor(end / 16) * 16 + 15;
  if (bytes[checksumOffset] !== checksum) throw new Error('Firmware checksum failed. Rebuild or download the file again.');
  if (bytes[23] === 1) {
    const hashEnd = checksumOffset + 1;
    const expected = Array.from(bytes.subarray(hashEnd, hashEnd + 32), b => b.toString(16).padStart(2, '0')).join('');
    if (await sha256(bytes.subarray(0, hashEnd)) !== expected) throw new Error('Firmware SHA-256 check failed.');
  }
}

export async function loadFirmware(fetchFile, customFile = null) {
  const response = await fetchFile('firmware/manifest.json');
  if (!response.ok) throw new Error('The bundled firmware is missing. Run scripts/package_web_firmware.py after building.');
  const manifest = await response.json();
  if (manifest.chip !== 'ESP32-C3' || manifest.flashSize !== '16MB' || manifest.parts?.length !== PARTS.length) {
    throw new Error('Unsupported firmware package.');
  }
  const files = [];
  for (let i = 0; i < PARTS.length; i++) {
    const [name, address] = PARTS[i];
    const part = manifest.parts[i];
    if (part.path !== name || part.offset !== address) throw new Error('Invalid firmware layout.');
    let data;
    if (customFile && address === APP_OFFSET) {
      if (customFile.size > APP_SIZE) throw new Error('The selected application is too large.');
      data = new Uint8Array(await customFile.arrayBuffer());
    } else {
      const download = await fetchFile(`firmware/${name}`);
      if (!download.ok) throw new Error(`Could not download ${name}.`);
      data = new Uint8Array(await download.arrayBuffer());
      if (data.length !== part.size || await sha256(data) !== part.sha256) throw new Error(`${name} failed its download integrity check.`);
    }
    if (address === APP_OFFSET) await validateApplication(data);
    files.push({ data, address });
  }
  return { files, name: customFile?.name || manifest.name, builtAt: manifest.builtAt };
}

export async function writeFirmware(loader, files, md5, onProgress) {
  if (loader.chip?.CHIP_NAME !== 'ESP32-C3' || loader.secureDownloadMode) {
    throw new Error('This flasher requires an X3/X4 with an ESP32-C3 in normal download mode.');
  }
  if (await loader.detectFlashSize() !== '16MB') throw new Error('Expected 16 MB of flash. No firmware was written.');
  const totalSize = files.reduce((sum, file) => sum + file.data.length, 0);
  await loader.writeFlash({
    fileArray: files, flashMode: 'keep', flashFreq: 'keep', flashSize: '16MB',
    eraseAll: false, compress: true,
    calculateMD5Hash: md5,
    reportProgress(index, written, total) {
      const before = files.slice(0, index).reduce((sum, file) => sum + file.data.length, 0);
      const fraction = total > 0 ? Math.min(1, written / total) : 0;
      // 100% is reserved for completion of the library's device MD5 checks.
      onProgress(Math.min(99, Math.floor(100 * (before + files[index].data.length * fraction) / totalSize)));
    },
  });
  onProgress(100);
}
