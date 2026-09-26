# BuddyPoint notes sync

Sync a local Markdown folder (including subfolders) with BuddyPoint over Wi-Fi.
The folder can be inside OneDrive; the OneDrive desktop app handles cloud syncing.
Open **BuddySync** on the reader before running the script.

```sh
python buddy_notes_sync.py --folder "/path/to/OneDrive/Notes" --device haakanpoint.local
```

Use `--reader-folder "/MyNotes"` to choose the destination and `--watch` to repeat.
Ordinary syncing needs only Python 3.10+, with no extra packages.

## Equations

With BuddyPoint **v1.6.9 or newer**, install the optional Python packages and add
`--math`. Keep `buddy_math.py` beside `buddy_notes_sync.py`:

```sh
python -m pip install -r requirements-math.txt
python buddy_notes_sync.py --folder "/path/to/OneDrive/Notes" --device haakanpoint.local --math
```

Write equations on their own lines using `$$ ... $$`. Try copying
[examples/math.md](examples/math.md) into your notes folder. The tool renders
common LaTeX maths with Matplotlib MathText, embeds images in a separate reading
copy, and preserves the original Markdown for editing/checklists. It uses no
JavaScript or system TeX installation. Inline maths and full LaTeX environments
are not supported; unrecognised equations remain text.

See the [complete guide](../../docs/ONEDRIVE_NOTES_SYNC.md) for setup, limits,
conflict handling, and recovery.
