# Markdown viewer

Open **Apps → Markdown viewer** to browse Markdown documents. Files with `.md` or `.markdown` extensions (including uppercase variants) also open from the regular file browser and Library. Rebuild the library index if a newly added document is not yet listed.

Use the normal reader controls for page turns, text settings, orientation, bookmarks and saved position. The reader's Contents/Select chapter menu lists document headings. The original Markdown file remains unchanged.

## Supported formatting

- ATX headings (`#` through `######`) and underlined headings.
- Bold, italic, combined emphasis and inline code.
- Unordered and ordered lists. Task markers remain visible as `[ ]` and `[x]`.
- Blockquotes and horizontal rules.
- Fenced and indented code with explicit line breaks and preserved spaces.
- Simple pipe tables, laid out by the existing adaptive table renderer.
- Inline links display their labels; inline images display their alternative text.
- UTF-8, optional UTF-8 BOM, LF and CRLF line endings.

This is a bounded Markdown subset, not a browser or a full CommonMark implementation. Nested lists are flattened, code uses the chosen reader font without syntax highlighting, and reference links, footnotes, embedded HTML, math and diagrams are not interpreted. HTML is shown as literal text. Very long physical lines remain readable, but formatting that crosses the 2 KB processing boundary may remain literal. Web links and image files are not fetched. KOReader progress syncing is not offered for Markdown documents.

## Storage and updates

The viewer streams the source into a generated EPUB in `/.crosspoint/md_<path-hash>/`, then uses the existing book renderer. Conversion uses fixed buffers instead of holding the full document in RAM. On reopening, a content fingerprint detects edits even when the file size is unchanged. An unchanged file reuses its formatted copy and reading progress; a changed file rebuilds pagination and resets the page position. The first open requires space on the SD card for temporary files and the formatted copy.

Conversion checks writes before publishing the archive. Failed conversion leaves the previous archive in place and retries on the next open. The normal cache-clear operation removes generated Markdown files, which are recreated from the original source when needed. Renaming through the file browser moves the formatted copy, reading cache and bookmarks alongside the document.

## Try it on the device

Copy [Markdown-demo.md](examples/Markdown-demo.md) to the SD card. After flashing the new firmware:

1. Open Apps → Markdown viewer; check that only folders and Markdown documents are shown.
2. Open the sample. Check emphasis, list markers, code indentation, table readability and heading navigation.
3. Turn a few pages, leave, and reopen to check resume. Repeat after sleep/wake.
4. Check portrait, inverted and both landscape orientations with your preferred font and theme.
5. Edit a word in the source without changing its length, then reopen and confirm the updated text appears.
6. Confirm Sudoku and ordinary EPUB/TXT reading still open normally.

Firmware builds and host tests cannot confirm physical screen appearance. For developer device testing, check that free heap stays above 50 KB during conversion and that repeated opens do not leak memory.
