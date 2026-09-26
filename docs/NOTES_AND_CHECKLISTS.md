# Reading notes and Markdown checklists

Requires BuddyPoint **1.6.6** or newer and the matching BuddySync KOReader plugin.

## Sync KOReader highlights and notes

1. Update the reader through Settings → firmware update, or use the web flasher.
2. Replace `buddysync.koplugin` in KOReader's `plugins` folder with the updated plugin and restart KOReader. Existing BuddySync settings are retained.
3. Open **BuddySync** on the X3 and start its connection. Keep both devices on the same Wi-Fi network.
4. In KOReader, open a book and choose **BuddySync → Send Current Notes to X3**. Sending a book or the active shelf also sends notes when **Send Notes with Books** is enabled (the default).
5. On the X3, open **Apps → Reading notes**, then select the book's Markdown file.

Exports include text highlights, attached notes, chapter labels and page references when KOReader provides them. Current in-memory annotations are used for the open book, including unsaved edits. Closed shelf books use saved annotations. Older highlight/bookmark data is supported too. Image-only highlights and ordinary navigation bookmarks are not exported.

Each book has a stable filename under `/BuddyNotes/`, including a hash of the original KOReader path to distinguish books with identical filenames. Moving a book in KOReader creates a new notes filename. Old copies are not automatically deleted.

This is one-way snapshot sync: KOReader is the source of truth. Sending again replaces that book's notes, including deletions; an empty snapshot clears previously exported highlights. Edits made to the exported copy are replaced by the next sync. Checklist documents that you maintain yourself should live outside `/BuddyNotes/`.

Note uploads are written to a temporary file and closed before replacing the previous copy. Failed transfers leave the old document intact. Publication keeps a `.buddy-backup` until replacement succeeds; an interrupted rename is recovered on the next sync to the same document.

## Toggle Markdown tasks

Open a Markdown document, open the reader menu, and select **Checklist**. Use the normal Up/Down controls to select a task and **Confirm** to toggle it. Each successful toggle is saved immediately. Press **Back** to reopen the formatted document with its updated tasks.

```markdown
# Shopping
- [ ] Milk
- [x] Coffee
- [ ] Bread
```

`-`, `+`, `*`, and numbered list markers are supported, with `[ ]`, `[x]` or `[X]`. Tasks may be indented by up to three spaces. Fenced code, indented code and blockquotes are excluded. This remains a bounded Markdown subset, not a complete CommonMark editor.

The list shows the first 128 tasks. Long labels are shortened in the list, while the full original text remains intact. Other content, Unicode, line endings, and the final newline are preserved. Each save copies the document through a small buffer, checks that the original content still matches, and replaces it only after a complete write. Leave space on the SD card for a temporary copy. If the source changed or saving failed, go Back and reopen the checklist before retrying.

The reader's page buffers are released while the checklist is open. A changed document is repaginated when you return, which resets its reading position as with other Markdown edits.

## Verification

Automated checks cover the KOReader exporter and transfer flow, task selection and persistence, stale documents, Unicode/line endings, failed writes, and backup recovery. Hardware checks remain: send notes from an actual KOReader device; toggle tasks and reopen after sleep; check all four orientations and the installed themes; monitor free heap and repeated-open stability.
