# OneDrive notes sync from a computer

The Python companion syncs a folder of Markdown notes between your computer and BuddyPoint over Wi-Fi. Your computer's OneDrive app handles Microsoft sign-in and cloud sync. There is no Microsoft app registration, password, token, or extra Python package to configure.

This works with the current BuddyPoint firmware (1.6.7); no firmware update is needed. It also works with an ordinary local folder or a folder managed by another cloud-storage app.

## First sync

1. On the computer, create a dedicated folder in OneDrive, for example **BuddyPoint Notes**. Put your `.md` or `.markdown` notes inside it, including subfolders if desired. Mark the folder **Always keep on this device** and let OneDrive finish downloading it.
2. Put the computer and X3 on the same trusted Wi-Fi network. Open **BuddySync** on the reader and start its connection. Keep that screen open during sync.
3. Install Python 3.9 or newer if needed. Download [buddy_notes_sync.py](../companion/notes-sync/buddy_notes_sync.py), or use the copy in this repository.
4. Run a preview, replacing the example folder with your actual folder:

   ```sh
   python3 companion/notes-sync/buddy_notes_sync.py --folder "/path/to/OneDrive/BuddyPoint Notes" --dry-run
   ```

   On Windows, use `py` instead of `python3` and a path such as `"C:\Users\YourName\OneDrive\BuddyPoint Notes"`.

5. Run the same command without `--dry-run` to sync. If `haakanpoint.local` cannot be found, add `--device 192.168.1.42`, using the address shown on the reader. Continue using the same address/hostname for later runs.
6. When sync finishes, leave BuddySync and open **Library → Markdown** on firmware **1.6.8 or newer**. That tab refreshes the index when first opened and shows notes separately from books, including notes in custom sync folders. On older firmware, use **Apps → Markdown viewer → OneDriveNotes**. Open a note and use its **Checklist** menu to tick tasks. Return to BuddySync and run the tool again to copy those edits back to the computer; OneDrive will then upload them to the cloud.

This syncs Markdown files recursively, not OneNote notebooks, Word files or images. Subfolder paths are preserved in both directions: `Work/Tasks.md` becomes `/OneDriveNotes/Work/Tasks.md` on the reader. Required folders are created automatically; empty folders and hidden files/folders are not copied. Each note can be up to 8 MiB. Scans stop with an error beyond 32 path components or 10,000 entries rather than silently skipping deeper content. The reader directory defaults to `/OneDriveNotes`, separate from `/BuddyNotes` used by KOReader exports. Use `--reader-folder "/MyNotes"` to choose another destination.

The firmware's Library index scans up to five folder levels below the SD root. Notes nested more deeply can still sync, but may require the Markdown file browser to locate them.

If upgrading from the original top-level-only script, replace the script and rerun your existing command. Recursion is automatic, and existing top-level sync history and interrupted-transfer recovery remain compatible.

## Leave the tool running

```sh
python3 companion/notes-sync/buddy_notes_sync.py --folder "/path/to/OneDrive/BuddyPoint Notes" --watch
```

It checks every 60 seconds and retries if the reader is asleep or disconnected. It can transfer only while BuddySync is open. Use `--interval 120` to change the interval and **Ctrl+C** to stop. The computer and OneDrive app need to stay running; the reader cannot reach OneDrive independently.

## What happens to edits

- A new note on either side is copied to the other side.
- Changes on just one side replace the unchanged copy on the other side.
- If both copies changed, the computer version keeps the original filename. The reader version is saved on **both sides**, in the same subfolder, as a separate `name.reader-conflict-<fingerprint>.md` note. This also applies to different files at the same relative path on the first sync. Identical filenames in different subfolders are independent notes.
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

Automated tests cover recursive bidirectional edits, first-sync and nested conflicts, empty/Unicode notes, deletion behavior, preview mode, interrupted uploads, changes during transfer, nested recovery, local backups, unsafe paths and parent directory links, duplicate names, process locking, and real HTTP request formatting against a local server.

Physical-device verification is still needed: upload a small note, open it on the X3, tick a checklist item, sync back, and confirm the changed file reaches OneDrive. Disconnect Wi-Fi during a test upload and rerun to check recovery on the real SD card.

## Optional equation rendering (Python)

The `--math` option prepares an offline reading copy of notes containing inline
`$...$` and display `$$...$$` maths. It requires BuddyPoint v1.6.9 or newer.
Older firmware still reads the original Markdown but shows the math source.

Install the optional packages into the same Python environment used for syncing.
From the repository root:

```sh
python3 -m pip install -r companion/notes-sync/requirements-math.txt
python3 companion/notes-sync/buddy_notes_sync.py --folder "/path/to/OneDrive/Notes" --device haakanpoint.local --math
```

If you are already inside `companion/notes-sync`, use `requirements-math.txt`
and `buddy_notes_sync.py` without the directory prefix. Keep `buddy_math.py` and
`buddy_math_layout.py` beside the sync script. Python 3.9 is supported, including
the macOS system Python; newer Python versions use newer compatible packages.

Keep any existing `--reader-folder`, `--watch` and other options. For example:

```markdown
Point $A$ is at $(2.75, 3.75)$.

$$\begin{pmatrix}n\\r\end{pmatrix}\times r!=\frac{n!}{(n-r)!}$$

$$
g(x)=\begin{cases}
1-(x-1)^2 & \text{for x < 0} \\
e^{x^2} & \text{for x = 0} \\
0 & \text{for x > 0}
\end{cases}
$$

$$\frac{(x+1)\cancel{(x-4)}}{(x-2)\cancel{(x-4)}}$$
```

This uses **Ziamath and latex2mathml** for layout and **resvg** for rasterization,
all through Python. No Node.js, browser, network rendering service, or system TeX
installation is needed. Supported notation includes matrices, cases, binomials,
cancellation, fractions, roots, Greek letters, sums, integrals, limits,
superscripts and subscripts. This does not execute LaTeX documents or load TeX
packages/macros. Unsupported expressions are retained as text and reported.
Inline delimiters must touch the expression (`$x^2$`) and stay on one line.
Dollar signs inside fenced/indented code and inline code stay literal; ordinary
prices such as `$5 and $10` are not treated as maths.

Equations may span lines with the opening delimiter beside the expression.
Adjacent expressions (`$$a=b$$$$b=c$$`) and prose beside a display equation work.
Wide equality chains break at outer `=` signs; other oversized expressions shrink
to fit. Inline formulas and surrounding prose are composed into wrapped line
images because the reader itself places images on separate lines. These lines
keep text and maths together, with page breaks between lines. They use a fixed
font size and do not respond to reader font-size changes. Ordinary paragraphs
continue to use the reader's font settings. Maths in Markdown tables remains
LaTeX text because the reader does not render images within table cells.

Original `.md`/`.markdown` files are never rewritten by the renderer. Each rendered
note gets a `buddy-math-<filename-hash>.bmath` file beside it **on the reader only**;
this contains a source fingerprint and an EPUB copy with baseline grayscale JPEG
images. JPEG avoids the PNG decoder's larger memory requirement. These files do
not appear as books or notes in the library. The reader verifies the source and
reading copy before use. Editing a checklist or note invalidates the copy:
reopen the note to use native Markdown until the next sync with `--math`. Sync
again and reopen to see updated maths. Reading position/pagination resets when
the reading copy changes.

Notes without maths keep native Markdown rendering. Math-enabled notes use
CommonMark plus tables; ordinary image references still show their descriptions,
links show labels, and raw HTML is displayed as text. Expressions are limited to
4,096 characters, 512 equations per note, 2,048 unique image assets, and 8 MiB per
reading copy (up to 7 MiB of image data). Invalid expressions remain text; a note
that exceeds the total reading-copy limit still syncs as original Markdown.

Generated copies are cached outside OneDrive in the sync history's `math` folder.
Renderer upgrades automatically regenerate outdated copies, even when the note
has not changed. **After upgrading, reinstall requirements, stop and restart the
watch command, and sync again.** Deleting the local math cache also forces
regeneration. Old reader sidecars are not automatically deleted (the sync tool
does not propagate deletions); they are ignored if their source changes or
disappears and may be removed manually.

To verify on hardware, copy [the advanced example](../companion/notes-sync/examples/advanced-math.md)
into your notes folder, sync with `--math`, close BuddySync and open the note.
Check the matrix, cases, crossed-out factors, wrapped inline text and long equality
chain. Also confirm checklists still work. Host tests cannot verify the physical
e-ink screen.
