# Web flasher

The static site is deployed from `web-flasher/` to GitHub Pages. The Pages workflow runs the flasher tests before publishing; changes on `main` to the flasher or workflow trigger deployment. GitHub Pages must be configured with **GitHub Actions** as its source.

## What is written

| File | Offset |
| --- | --- |
| `bootloader.bin` | `0x0000` |
| `partitions.bin` | `0x8000` |
| `boot_app0.bin` | `0xe000` |
| `firmware.bin` | `0x10000` |

The support images come from the same completed PlatformIO `default` build. The packager verifies the partition layout and checks that the factory image matches the separate bootloader, partition table, and application. It extracts boot selection data from that factory image.

The flasher does not erase all flash. It writes the four regions above and selects the newly installed application even if an earlier OTA update selected the other slot. NVS at `0x9000` and SD files are not targeted.

A custom application replaces only `firmware.bin`; it must use the same partition layout. The flasher rejects factory images, wrong chip IDs, missing or incompatible board tags, oversized/truncated images, and invalid application checksums. Packaged files have SHA-256 download checks. Espressif's writer compares device MD5 hashes against each written file, and the UI only reports 100% after those checks pass.

## Update the package

```sh
./scripts/build.sh
```

Or, after building `crosspoint-upstream` directly:

```sh
python3 scripts/package_web_firmware.py
```

Review and commit `web-flasher/firmware/` together with the corresponding source changes. The manifest records the application's build time and each file's size, offset, and SHA-256.

## JavaScript dependencies

Dependencies are pinned in `web-flasher/package-lock.json`. The committed `vendor/flashing.js` bundle makes the deployed page independent of a third-party CDN. After changing dependencies:

```sh
cd web-flasher
npm ci
npm run build
npm test
```

Update `vendor/NOTICE.txt` if licenses change.

## Validation

`npm test` validates the actual bundled firmware and exercises the real esptool-js writer with a mocked device transport, including MD5 mismatch and USB-disconnection failures. These tests cannot establish physical-device compatibility.

For a device check, use desktop Chrome/Edge with a data cable, connect the X3, install the included build, wait for the verified completion message, and confirm the reader restarts and opens a book. If reset fails after verification, restart the reader manually. Keep the cable connected while writing. A failed install requires reconnecting before retrying.

This work changes USB installation only. The firmware's existing OTA update source has not been switched to BuddyPoint releases.
