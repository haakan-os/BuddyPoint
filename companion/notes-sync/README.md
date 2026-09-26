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
`--math`. Keep `buddy_math.py` and `buddy_math_layout.py` beside the sync script.
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
Markdown tables stays as LaTeX text, ordinary images show their descriptions,
and unsupported expressions are retained as text with a message in the terminal.
This is a maths renderer, not a complete LaTeX document engine or package system.

After upgrading the companion, install the requirements again, stop and restart
any running watch command, and sync with `--math`. Old reading copies regenerate
automatically even if the notes have not changed. Then close BuddySync and reopen
the note. **No firmware update beyond v1.6.9 is required.**

See the [complete guide](../../docs/ONEDRIVE_NOTES_SYNC.md) for setup, limits,
conflict handling, and recovery.
