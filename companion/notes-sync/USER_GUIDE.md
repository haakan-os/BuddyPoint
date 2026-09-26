# BuddyPoint: notes, sync and study guide

This guide covers the desktop sync app and the new reading features in **BuddyPoint v1.7.0**. Your original Markdown notes stay editable in Obsidian, OneDrive, or any text editor.

## 1. Update and open the sync app

1. On the reader, use the firmware update option in **Settings** to install **v1.7.0 or newer**.
2. On your computer, download the complete notes-sync companion or update this repository. Keep all files in the `notes-sync` folder together, including `gui` and the Python modules.
3. Install **Python 3.9 or newer** if it is not already available.
4. **Mac:** double-click **Start BuddyPoint Sync.command**. **Windows:** double-click **Start BuddyPoint Sync.bat**. **Linux:** run `python3 buddy_notes_gui.py` from the notes-sync folder. The same Python command also works on Mac.
5. A browser window opens with **BuddyPoint Sync**. It runs on this computer; there is no account to create.

The Mac launcher may open a Terminal window as well. Leave it open while syncing. If macOS blocks a downloaded launcher, open Terminal in the notes-sync folder and run `python3 buddy_notes_gui.py`. Do not disable macOS security settings.

The browser interface itself needs no extra Python packages. For equations and images, run this once **from the notes-sync folder**, using the same Python that starts the app:

```sh
python3 -m pip install -r requirements-math.txt
```

On Windows, use `py -3 -m pip install -r requirements-math.txt`.

This desktop UI does not require another firmware update beyond v1.7.0.

## 2. Choose your folders and connect

1. Put the reader and computer on the same Wi-Fi network.
2. Open **BuddySync** on the reader and start its connection. Keep it open during transfers.
3. In the desktop app, click **Browse** and select your notes folder or Obsidian vault. You can also paste its full folder path.
4. Set **Reader address** to the hostname or IP shown in BuddySync. The usual hostname is `haakanpoint.local`.
5. Set **Folder on reader** to a name such as `Obsidian`. This creates a dedicated top-level folder on the SD card.
6. Enable **Render maths & images** if you have installed the optional packages.
7. Click **Preview changes first**, or go straight to **Sync now**.
8. Wait for **Sync complete**, close BuddySync on the reader, and open **Library → Markdown** to find your notes.

**Already using the command line?** Keep exactly the same notes folder, reader address and destination folder. The UI shares its history and backups with the command-line tool. For example, if you used `--device haakanpoint.local --reader-folder Obsidian`, use those values in the UI. Switching from hostname to IP changes the saved sync identity. If the app reports an identity mismatch, restore the original values; do not delete your history to bypass it.

The reader's Markdown tab refreshes the library index when first opened. Very deeply nested notes may need to be opened through the file browser: the library indexes only a limited folder depth.

## 3. Everyday syncing

| Control | What it does |
| --- | --- |
| **Sync now** | Runs one sync, then stops. |
| **Start auto-sync** | Repeats while the app runs. The interval is at least 10 seconds. It retries when the reader is unavailable. |
| **Stop** | Stops automatic checks and finishes the current operation safely before stopping. A network request can take up to 60 seconds to finish. |
| **Preview changes first** | Shows planned note transfers without writing notes or generating reading copies. It is not a full equation/image preview. |
| **Quit** | Requests a safe stop and closes the local app after the current operation. You can then close the browser tab and launcher window. |

Settings are saved when you start a sync or preview. Reopening the app restores the settings but **does not automatically start syncing**.

Closing the browser tab alone **does not stop auto-sync**. Use Stop or Quit. The computer must stay awake and BuddySync must be open on the reader for transfers to work. Stop an existing command-line watch process before starting the UI; both use the same lock so they cannot sync the same folder simultaneously.

### What the summary means

- **Uploaded:** notes sent from computer to reader.
- **Downloaded:** notes brought back from reader to computer, including checklist edits.
- **Unchanged:** original note text already matched. Its images may still have changed.
- **Conflicts:** both copies changed; the reader copy was preserved separately.
- **Reading copies:** updated equation/image versions uploaded to the reader.
- **Links & cards:** updated navigation and flashcard helper files.
- **Skipped:** rendering operations or helper generation that could not complete. Read the activity log for the reason.

A preview's counts are planned actions, not completed transfers. During auto-sync, the summary shows the last completed pass; the status shows whether it is waiting, syncing or stopping. The activity panel keeps the latest 500 messages.

## 4. What is synced and how edits are protected

The tool syncs `.md` and `.markdown` files in both directions, including subfolders. Hidden files, hidden folders, symbolic links, empty folders, EPUBs and other documents are not copied. Required destination folders are created automatically.

OneDrive syncing works by choosing a folder that the OneDrive desktop app already manages. BuddyPoint does not log into Microsoft itself. Make cloud files available locally before syncing.

Images referenced by notes can be embedded in their reader viewing copies when **Render maths & images** is enabled. They are not copied back from the reader as editable image files.

If both copies of a note changed, the computer version stays at the original name. The reader version is preserved on both devices as a file with `.reader-conflict-…` in its name. Compare and merge these copies yourself.

Deletions are **not propagated**: a note missing from one side is copied back from the other. To remove a note permanently, stop sync and remove it from both locations. A rename behaves like a new path, so an old copy may reappear.

Backups and sync history live in `~/.buddypoint-sync` on your computer. Interrupted transfers are recovered on the next run. Do not delete this folder to fix a connection problem.

## 5. Read Markdown and use checklists

Open a note from **Library → Markdown**, the file browser, or **Apps → Markdown viewer**. Headings, lists, emphasis, code and basic tables are supported. The reader remembers your reading position; a changed note may need to be repaginated.

Create tasks like this:

```markdown
# Before class
- [ ] Read chapter 3
- [x] Download the lecture notes
- [ ] Revise the practice questions
```

On the reader, open the note's menu → **Checklist**. Choose a task and press Confirm to toggle it. Back reopens the note. Sync again to bring the changes to your computer. The checklist supports up to 128 tasks per note; tasks inside code blocks are ignored.

## 6. Equations and maths size

Enable **Render maths & images** before syncing.

Use single dollar signs for inline maths:

```markdown
The point is $A=(2.75,3.75)$.
```

Use double dollar signs for a display equation:

```markdown
$$x=\frac{-b\pm\sqrt{b^2-4ac}}{2a}$$
```

Matrices, cases, binomial coefficients, cancellation, fractions, roots, limits, sums and integrals are supported. Long equality chains can wrap at `=`. Try the supplied `examples/math.md` and `examples/advanced-math.md`.

Choose **Maths size** in the desktop app to make equations and paragraphs containing inline maths larger or smaller. Standard is 26; Large is 32. The command line accepts any size from 18 to 40. Sync again after changing the size; unchanged notes are regenerated automatically. Very wide equations may still shrink to fit.

Ordinary text uses the reader's own font settings. Paragraphs containing inline maths are rendered as images and use the companion's maths size. Maths inside tables stays as LaTeX text. Unsupported expressions remain readable as source text and produce a message in the log; this is not a complete LaTeX document engine.

On the reader, set **Settings → Reader → Images → Display**. Close BuddySync and reopen the note after syncing.

## 7. Diagrams and images

Standard Markdown:

```markdown
![Number line](numberline.png)
![Lecture diagram](../Attachments/diagram.png)
```

Obsidian attachment syntax:

```markdown
![[diagram.png]]
```

The companion first looks beside the note, then relative to the vault root. A bare filename can be found in an attachment subfolder if it is unique. If two files have the same name, use a folder path.

PNG, JPEG, GIF (first frame), WebP and BMP are supported. They are converted to grayscale JPEGs up to 420 × 500 pixels; transparent backgrounds become white. SVG, internet images and images outside the chosen notes folder are not loaded. Images inside tables show descriptions.

Editing an image triggers an updated reading copy even when the Markdown text hasn't changed. Keep **Render maths & images** enabled, sync, then reopen the note.

## 8. Links between notes

Create a standard Markdown link:

```markdown
[My revision notes](Revision.md)
[Next topic](../Week2/Calculus.md)
```

Or an Obsidian wikilink:

```markdown
[[Revision]]
[[Week2/Calculus|Next topic]]
```

Run a sync. On the reader, open the note menu → **Note links**, select a destination, and press Confirm. Links are chosen from this menu rather than directly from the text on the page.

Only existing Markdown notes within the synced folder are offered. Duplicate bare filenames need a folder path. External websites are not opened. Heading fragments currently open the destination note at its saved position; they do not jump directly to that heading.

Links need normal sync, but do not require the maths packages.

## 9. Flashcards

Put each question and answer on one line with **spaces around `::`**:

```markdown
What is the derivative of x²? :: 2x
How many arrangements of 3 colours from 8? :: 8 × 7 × 6 = 336
```

1. Sync the note.
2. Open its reader menu → **Flashcards**.
3. Select a question. The question is shown first.
4. Press **Reveal** to see the answer; **Question** flips it back.
5. Page buttons scroll longer text. Back returns to the question list; Back again returns to the note.

This first version uses plain text. It does not render equations or images inside cards, track scores, or schedule spaced repetition. Cards inside fenced or indented code blocks are ignored.

Questions/link labels are limited to 180 UTF-8 bytes; answers to 2,048 bytes. A note supports up to 64 links and cards combined. Oversized entries are reported in the log. Cards need normal sync, without optional maths packages.

## 10. Favourites

While reading an EPUB or Markdown note, open the reader menu → **Toggle favourite**. A message confirms whether it was added or removed.

Find saved items in **Library → Favourites**. These are shortcuts to whole books or notes, separate from page bookmarks. Favourites stay on this reader and are not synced to the computer. If you rename or move a file, add it again at its new location.

## 11. Other BuddyPoint features

- **Sudoku:** open it from Apps. Games are saved so you can return later.
- **KOReader reading notes:** the separate BuddySync KOReader plugin can send highlights and notes to `/BuddyNotes`. Open them through **Apps → Reading notes**. These exports are snapshots from KOReader; sending again replaces previous exports. Keep your own two-way notes in a separate folder such as `/Obsidian`.
- **OTA updates:** use the firmware update option in Settings. BuddyPoint checks stable releases in the BuddyPoint GitHub repository. Updating the desktop companion alone does not update the reader, and installing firmware does not update the computer's scripts.

## 12. Troubleshooting

| Symptom | What to try |
| --- | --- |
| Cannot connect / waiting for reader | Open BuddySync, start its connection, and check both devices are on the same Wi-Fi. Keep your existing saved reader address. |
| Sync history belongs to another folder/device | Restore the original folder, reader address and destination from your previous command. Do not erase history. |
| Another sync is running | Stop your old command-line watch process or another UI instance. |
| Missing optional packages | Run the installation command in section 1 with the same Python used to launch the app, then retry. |
| Empty equation/image boxes | Check Images is set to Display, update all companion files, sync with maths enabled, close BuddySync and reopen the note. Read the log for missing images or unsupported expressions. |
| Empty Links or Flashcards menu | Install v1.7.0+, check syntax, sync that note again, then reopen it. |
| An image is missing | Use a local supported image within the selected notes folder. If names are duplicated, specify the relative path. Make OneDrive files available offline. |
| Browse does nothing | The folder picker may be behind the browser. You can paste a full path instead. Linux needs Zenity or KDialog for Browse. |
| Browser says local app unavailable | Reopen the launcher and use its new browser window/link. Old tabs stop working after the app exits. |
| Stop is taking a while | Let the current transfer or network timeout finish. Stop preserves transaction recovery. |

For a first test, copy the entire supplied `examples` folder into your notes folder. It contains study cards, linked notes, an image and equation examples. Sync it, then try the menus on the reader.
