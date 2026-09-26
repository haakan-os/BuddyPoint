# BuddyPoint notes sync

Sync a local Markdown folder (including subfolders) with BuddyPoint over Wi-Fi.
The folder can be inside OneDrive; the OneDrive desktop app handles cloud syncing.
Open **BuddySync** on the reader before running the script.

```sh
python buddy_notes_sync.py --folder "/path/to/OneDrive/Notes" --device haakanpoint.local
```

Use `--reader-folder "/MyNotes"` to choose the destination and `--watch` to repeat.
Ordinary syncing needs only Python 3.9+, with no extra packages.

## Equations

With BuddyPoint **v1.6.9 or newer**, install the optional Python packages and add
`--math`. Update the whole `companion/notes-sync` directory, keeping all `buddy_*.py` modules beside the sync script.
From this directory:

```sh
python3 -m pip install -r requirements-math.txt
python3 buddy_notes_sync.py --folder "/path/to/OneDrive/Notes" --device haakanpoint.local --math
```

The renderer supports inline `$...$` and display `$$...$$` maths, including
matrices, binomial coefficients, cases, cancellation, fractions, roots, limits,
sums and integrals. Display delimiters can sit beside the expression, span lines,
share a line with prose, or appear back-to-back. Long equality chains wrap at `=`.
Try [examples/advanced-math.md](examples/advanced-math.md).

The Python renderer uses [Ziamath](https://ziamath.readthedocs.io/), latex2mathml
and resvg. It needs no Node.js, system TeX installation, or online rendering
service. The resulting reading copy contains baseline grayscale JPEG images,
which use the reader's smaller image decoder. Original notes remain unchanged.

Inline formulas and their surrounding paragraph are composed into wrapped line
images so they stay together on the reader. These paragraphs have a fixed font
size; ordinary paragraphs still use your reader font settings. Maths inside
Markdown tables stays as LaTeX text, images inside tables show descriptions,
and unsupported expressions are retained as text with a message in the terminal.
This is a maths renderer, not a complete LaTeX document engine or package system.

After upgrading the companion, install the requirements again, stop and restart
any running watch command, and sync with `--math`. Old reading copies regenerate
automatically even if the notes have not changed. Then close BuddySync and reopen
the note. **Images and maths work on v1.6.9; Links, Flashcards and Favourites need v1.7.0.**

See the [complete guide](../../docs/ONEDRIVE_NOTES_SYNC.md) for setup, limits,
conflict handling, and recovery.

## Images and maths size

`--math` also embeds local Markdown images, even in notes with no equations.
Both `![description](assets/diagram.png)` and Obsidian `![[diagram.png]]` work.
Paths resolve relative to the note, then the vault root. A bare attachment name
can be found in a subfolder when unique; ambiguous names need a relative path.
PNG, JPEG, GIF (first frame), WebP and BMP are converted to grayscale JPEGs
up to 420 × 500 pixels. Transparent backgrounds become white. Internet images,
SVG and linked files outside the vault are not loaded. Source images are untouched;
the reader receives an embedded viewing copy, not separately editable images.
Image-only changes are detected on every sync.

Add `--math-size 32` for larger equations and paragraphs containing inline maths.
The range is 18–40; default 26. Changing it regenerates reading copies without
editing your notes. Long equations may still shrink to fit the screen.

## Note links, flashcards and favourites (v1.7.0)

Normal sync prepares the **Note links** and **Flashcards** reader menus;
these do not need `--math` or extra Python packages.

- Links: `[title](../Other/note.md)` and `[[note|title]]`. Select **Note links**
  from the reader menu, then choose a destination. Only existing synced Markdown
  notes are offered. Ambiguous bare names need folder paths. Heading fragments
  open the destination note at its saved position; they do not jump to a heading.
- Flashcards: put `Question :: Answer` on a line, with spaces around `::`.
  Open **Flashcards**, select a question, then **Reveal** to see the answer.
  **Question** flips back; page buttons scroll long text; Back returns to the list.
  This first version uses plain text, without rendered equations, scheduling or scores.
  Each question/label is limited to 180 UTF-8 bytes and each answer to 2,048 bytes;
  up to 64 links/cards combined are included per note. Oversized entries are reported.
- Favourites: choose **Toggle favourite** from an EPUB or Markdown reader menu.
  Find saved items in **Library → Favourites**. Favourites stay on this reader,
  are separate from bookmarks, and use file paths (renaming a file requires re-adding it).

Try [examples/study-notes.md](examples/study-notes.md) with the other example files.
The companion creates checked `.bnotes` helper files beside notes on the reader;
missing or stale helpers prompt you to sync again. They are not library entries.

Each completed sync reports uploaded, downloaded, conflicting and unchanged notes,
updated rendered copies, and skipped rendering operations. Dry runs preview note
transfers without generating images or helper files.
