# BuddySync Pairing Guide: KOReader & Xteink X3

This guide explains how to pair your **KOReader device** (Kindle, Kobo, Android, Boox, iPad, or desktop) with your **Xteink X3** running the **HaakanPoint** firmware.

---

## 1. Installing the KOReader Plugin

1. Locate your KOReader directory on your main device:
   - **Kobo / Kindle:** `/mnt/onboard/.koreader/plugins/` or `extensions/koreader/plugins/`
   - **Android:** `/sdcard/koreader/plugins/`
   - **Linux / macOS:** `~/.config/koreader/plugins/`
2. Copy the `koreader-plugin/buddysync.koplugin` folder into the `plugins/` directory:
   ```bash
   cp -r koreader-plugin/buddysync.koplugin <your_koreader_path>/plugins/
   ```
   If upgrading from HaakanPoint, close KOReader first and remove the old `haakanpoint.koplugin` folder to prevent duplicate transfers. Keep `settings/haakanpoint.lua`; your saved settings will be reused.
3. Restart KOReader. You will now see **BuddySync** under the top Tools/Sync menu.

---

## 2. Configuring the Xteink X3

1. Format a MicroSD card with **FAT32**.
2. Create the following folder structure on the SD card:
   ```
   /books/
   /cache/
   /fonts/
   ```
3. In `/cache/config.json`, configure your Wi-Fi credentials and KOSync settings:
   ```json
   {
     "sync": {
       "ssid": "YOUR_WIFI_SSID",
       "password": "YOUR_WIFI_PASSWORD",
       "server": "https://sync.koreader.rocks",
       "user": "your_username",
       "pass_md5": "your_md5_hashed_password",
       "auto_open": true,
       "auto_close": true,
       "auto_sleep": true
     },
     "reading": {
       "font_size": 16,
       "line_spacing": 4,
       "bionic": false,
       "dark_mode": false
     },
     "auto_sleep_sec": 300
   }
   ```
4. Insert the SD card into the Xteink X3 and power it on.

---

## 3. Wi-Fi Pairing & Book Transfer

When both your KOReader device and Xteink X3 are connected to the same Wi-Fi (or your phone's mobile hotspot):

Open **BuddySync** on the X3 and select **Sync Now** to make it available for transfers. Its default address remains `haakanpoint.local`.

### Automatic Book Push
1. Open any EPUB book on your KOReader device.
2. Tap the menu -> **BuddySync** -> **Send Current Book to Xteink X3**.
3. The plugin will optimize the EPUB and wirelessly stream it directly to your X3.

### Bi-Directional Reading Progress
* Whenever you finish reading a session on KOReader, closing the book or sleeping the device automatically saves your position to KOSync.
* When you pick up the Xteink X3 and open the book, it pulls the latest position and jumps straight to your page!
* Reading on the Xteink X3 updates the position on your main KOReader device automatically.
