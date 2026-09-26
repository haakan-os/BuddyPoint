# Sudoku

Open **Apps → Sudoku** from the home menu. The latest saved puzzle resumes automatically.

- Use the front Left/Right buttons to move across a row and the side Up/Down buttons to move between rows. Directional controls follow the existing button and orientation settings.
- Press **Edit** on an editable cell. Use the arrows to cycle through blank (`-`) and 1–9, then **Done** to keep the entry. **Cancel** leaves the previous entry unchanged.
- Bold numbers are fixed clues. Underlined numbers conflict with another number in their row, column or 3×3 box. Conflicting entries are allowed so you can correct them yourself.
- Press **Menu** for **Resume**, **New puzzle**, or **Save and exit**. Starting another puzzle asks before replacing the current one.

Confirmed entries save after two seconds without another edit, and when leaving the game or entering sleep. An unconfirmed number is a draft and is not saved. If saving fails, the screen reports it and retries; **Save and exit** stays in the game until saving succeeds.

The game alternates two small, versioned saves under `/.crosspoint/sudoku-0.bin` and `sudoku-1.bin`. It resumes the newest valid save; an interrupted write can fall back to the preceding save. This is one puzzle with a recovery copy, not two separate game slots.

Puzzles are variations of three fixed puzzles, shuffled by digit relabelling, row and column permutations within bands/stacks, band/stack permutations and transposition. These transformations preserve uniqueness without running a puzzle generator on the reader. Apps are compiled into the firmware; there is no installable-app loader.

## Device verification

After installing the firmware, open Apps → Sudoku, enter a number, wait two seconds, exit and reopen the app. Confirm the entry resumes. Repeat after sleep and wake. Check that Cancel restores a draft, clues cannot be edited, conflicts are underlined, and New puzzle requires confirmation. Check controls and the board in each supported orientation and home-menu scrolling in your chosen theme. For developer testing, verify free heap remains above 50 KB and does not decline after repeated app entry/exit.
