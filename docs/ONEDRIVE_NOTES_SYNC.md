# OneDrive notes sync from a computer

The Python companion syncs a folder of Markdown notes between your computer and BuddyPoint over Wi-Fi. Your computer's OneDrive app handles Microsoft sign-in and cloud sync. There is no Microsoft app registration, password, token, or extra Python package to configure.

This works with the current BuddyPoint firmware (1.6.7); no firmware update is needed. It also works with an ordinary local folder or a folder managed by another cloud-storage app.

## First sync

1. On the computer, create a dedicated folder in OneDrive, for example **BuddyPoint Notes**. Put your `.md` or `.markdown` notes directly inside it. Mark the folder **Always keep on this device** and let OneDrive finish downloading it.
2. Put the computer and X3 on the same trusted Wi-Fi network. Open **BuddySync** on the reader and start its connection. Keep that screen open during sync.
3. Install Python 3.10 or newer if needed. Download [buddy_notes_sync.py](../companion/notes-sync/buddy_notes_sync.py), or use the copy in this repository.
4. Run a preview, replacing the example folder with your actual folder:

   ```sh
   python3 companion/notes-sync/buddy_notes_sync.py --folder "/path/to/OneDrive/BuddyPoint Notes" --dry-run
   ```

   On Windows, use `py` instead of `python3` and a path such as `"C:\Users\YourName\OneDrive\BuddyPoint Notes"`.

5. Run the same command without `--dry-run` to sync. If `haakanpoint.local` cannot be found, add `--device 192.168.1.42`, using the address shown on the reader. Continue using the same address/hostname for later runs.
6. When sync finishes, leave BuddySync and open **Apps → Markdown viewer → OneDriveNotes**. Open a note and use its **Checklist** menu to tick tasks. Return to BuddySync and run the tool again to copy those edits back to the computer; OneDrive will then upload them to the cloud.

This syncs Markdown files, not OneNote notebooks, Word files or images. The first version processes files directly inside the chosen folder; subfolders and hidden files are ignored. Each note can be up to 8 MiB. The reader directory defaults to `/OneDriveNotes`, separate from `/BuddyNotes` used by KOReader exports.

## Leave the tool running

```sh
python3 companion/notes-sync/buddy_notes_sync.py --folder "/path/to/OneDrive/BuddyPoint Notes" --watch
```

It checks every 60 seconds and retries if the reader is asleep or disconnected. It can transfer only while BuddySync is open. Use `--interval 120` to change the interval and **Ctrl+C** to stop. The computer and OneDrive app need to stay running; the reader cannot reach OneDrive independently.

## What happens to edits

- A new note on either side is copied to the other side.
- Changes on just one side replace the unchanged copy on the other side.
- If both copies changed, the computer version keeps the original filename. The reader version is saved on **both sides** as a separate `name.reader-conflict-<fingerprint>.md` note. This also applies to different files with the same name on the first sync.
- Deletions are **not propagated**. If a note exists on one side only, it is copied back to the other. To remove a note permanently, stop the sync tool and remove it from both sides.
- Renaming a note is treated as a new filename. The old copy can reappear because deletion is not propagated.
- Save and close active edits before syncing, and allow OneDrive to settle. The reader API has no transaction lock shared with other upload clients. Avoid concurrent transfers from another tool.

The tool compares SHA-256 file contents rather than timestamps. It refuses case/Unicode filename collisions, symbolic links, unsupported filenames, and invalid sync history. No reading positions, firmware, or settings are synced.

## Interrupted transfers and backups

Before replacing a reader note, the tool uploads to a unique temporary file and downloads it again to verify the bytes. It then renames the original to a backup, promotes the verified upload, verifies the final copy, and removes the temporary backup. A local journal records the transaction so the next run can recover after a disconnect. If an upload is interrupted, reconnect the reader and run the same command again. Do not remove `buddysync-*.part` or `buddysync-*.backup` files manually while recovery is pending.

Local writes use a temporary file and an atomic replacement. Content snapshots and sync history are stored outside OneDrive, under `~/.buddypoint-sync/<folder-id>/`; the tool prints the exact path. `backups/<SHA-256>.md` files hold the original bytes and can be opened or copied to recover notes. These backups are local and are not encrypted or automatically pruned. Back up the state directory if you want to preserve that recovery history.

History is tied to the chosen computer folder, device address and reader directory. If you change the device address, first finish any pending recovery using the old address if possible. With the tool stopped and no `pending.json` present, move `state.json` aside to start a fresh comparison; retain `backups/`. The first sync will preserve differing copies as conflicts. Do not reuse one computer's history on another computer.

The reader's transfer server uses local HTTP without authentication. Run it on a trusted local network; do not expose its port to the internet.

## Validation

```sh
python3 -m unittest discover -s companion/notes-sync -v
```

Automated tests cover bidirectional edits, first-sync conflicts, empty/Unicode notes, deletion behavior, preview mode, interrupted uploads, changes during transfer, recovery, local backups, unsafe paths, duplicate names, process locking, and real HTTP request formatting against a local server.

Physical-device verification is still needed: upload a small note, open it on the X3, tick a checklist item, sync back, and confirm the changed file reaches OneDrive. Disconnect Wi-Fi during a test upload and rerun to check recovery on the real SD card.
