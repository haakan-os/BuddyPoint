# BuddyPoint

A CrossPoint-based firmware for the **Xteink X3**, with enhanced KOReader sync, custom themes, Sudoku, and Markdown support. The local project was originally called HaakanPoint; some companion tools and device settings retain that name.

[**Open the web flasher**](https://haakan-os.github.io/BuddyPoint/)

## Features

- **BuddySync:** pair with the included KOReader plugin to transfer books and keep reading progress in sync.
- **Custom themes:** Retro Mac, NeXTSTEP, and Windows 95 styles alongside the CrossPoint themes.
- **Apps:** Sudoku with saved games, a Markdown viewer, and synced KOReader reading notes.
- **Notes and checklists:** send highlights and notes with BuddySync, and toggle Markdown tasks with the reader buttons. See the [setup guide](docs/NOTES_AND_CHECKLISTS.md).
- **OneDrive notes:** a Python companion syncs a computer's OneDrive Markdown folder and subfolders with the reader, including checklist edits and conflict copies. See the [desktop sync guide](docs/ONEDRIVE_NOTES_SYNC.md).
- **Markdown reading:** open `.md` and `.markdown` files from the SD card, with headings, lists, code blocks, and reading progress. See the [viewer guide](crosspoint-upstream/docs/markdown.md).
- **Markdown maths:** optional Python `--math` sync renders inline maths, display equations, matrices, cases and cancellation for offline reading, keeping original notes intact. See the [math setup](docs/ONEDRIVE_NOTES_SYNC.md#optional-equation-rendering-python).
- **Study tools (v1.7.0):** local diagrams and Obsidian images, note-link navigation, question/answer flashcards, adjustable desktop maths size, sync summaries, and a Favourites library shelf. See the [companion guide](companion/notes-sync/README.md).
- **Markdown library tab:** find notes separately from books, with search and alphabetical sorting. Opening the tab refreshes the index to pick up newly synced notes.
- **OTA updates:** download stable releases from this BuddyPoint repository. See the [OTA guide](docs/OTA_UPDATES.md) for the one-time migration and release process.
- **USB web flasher:** install the included firmware or a compatible application build, with device checks, real progress, and verification of the written data.

## Install

Open the [web flasher](https://haakan-os.github.io/BuddyPoint/) in desktop Chrome or Edge, connect the reader with a USB-C data cable, select **Connect device**, then **Install firmware**. Close other serial monitors first.

The flasher installs the bootloader, the 16 MB partition table, the boot selection data, and the application. It does not access the SD card. It accepts application `firmware.bin` files built for this project's X3/X4 layout; merged factory images are not accepted in the custom-file picker.

The included firmware passed compilation and automated tests. The new browser flashing flow still needs a physical-device test.

## Build locally

Clone with the SDK submodules:

```sh
git clone --recurse-submodules https://github.com/haakan-os/BuddyPoint.git
cd BuddyPoint
```

Install PlatformIO and the firmware's [build prerequisites](crosspoint-upstream/README.md), then:

```sh
./scripts/build.sh
```

The active firmware is in **`crosspoint-upstream/`**, built with the `default` environment for ESP32-C3 X3/X4 devices. The build script also refreshes the firmware packaged with the browser flasher. `firmware/` is an earlier prototype and is not used by these scripts.

To serve the flasher locally:

```sh
python3 scripts/serve_web_flasher.py
```

Open [localhost:8000](http://localhost:8000) in desktop Chrome or Edge. Opening `index.html` directly is not supported. The committed flasher includes its JavaScript dependencies and firmware, so Node.js is only needed when updating or testing the flasher itself:

```sh
cd web-flasher
npm ci
npm run build
npm test
```

To refresh just the firmware package after a direct PlatformIO build, run `python3 scripts/package_web_firmware.py` from the project root. For command-line flashing on macOS, `./scripts/flash.sh` uses PlatformIO.

## KOReader companion

Copy `koreader-plugin/buddysync.koplugin/` into your KOReader `plugins/` directory and restart KOReader. The plugin appears as **BuddySync**. See the [pairing guide](docs/PAIRING_GUIDE.md).

When upgrading, close KOReader and remove the old `haakanpoint.koplugin` folder so both plugins cannot run at once. Keep `settings/haakanpoint.lua`: BuddySync reuses it to preserve your preferences.

## Project layout

| Directory | Contents |
| --- | --- |
| `crosspoint-upstream/` | Active, customized CrossPoint firmware, tests, and feature guides |
| `crosspoint-upstream/freeink-sdk/` | Pinned FreeInk SDK submodule |
| `koreader-plugin/` | BuddySync companion source |
| `web-flasher/` | Static USB flasher and packaged default firmware |
| `scripts/` | Build, packaging, local preview, flashing, and test helpers |
| `companion/` | Python EPUB tools and mock server |
| `docs/` | Pairing and companion development guides |
| `firmware/` | Earlier firmware prototype, retained for reference |

## Credits

BuddyPoint incorporates [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader) (MIT) and [FreeInk SDK](https://github.com/Free-Ink/freeink-sdk). CrossPoint source was imported at `4b17a7bb9f78babc5d96d58e066b365145d5088b`, with the local BuddyPoint modifications included. Original licenses and attribution remain in their respective directories.

The flasher uses Espressif's esptool-js. Bundled library licenses are in [NOTICE.txt](web-flasher/vendor/NOTICE.txt). The [web flasher guide](docs/WEB_FLASHER.md) covers deployment and validation.
