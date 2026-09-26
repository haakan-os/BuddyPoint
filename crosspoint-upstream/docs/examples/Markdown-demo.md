# HaakanPoint Markdown

This file demonstrates the Markdown viewer. Open **Apps → Markdown viewer**, or select this file from **Browse files**.

## Text formatting

This is **bold**, this is *italic*, and this is ***both***. Inline code such as `return true;` stays literal.

You can write escaped punctuation: \*this is not italic\*.

## Lists

- Read a chapter
- Make a note
- Solve a Sudoku puzzle

1. Copy a Markdown file to the SD card.
2. Open it on your Xteink.
3. Use the normal reader controls to turn pages.

Task markers remain readable:

- [x] Add the Apps menu
- [x] Add Sudoku
- [x] Add a Markdown viewer
- [ ] Configure HaakanPoint OTA releases

## A quote

> Small notes fit a pocket reader well.
> Keep the useful details close at hand.

## Code

```cpp
if (reader.isReady()) {
    showDocument("notes.md");
}
```

Code keeps its line breaks and spaces, using the selected reader font. Long lines wrap to fit the screen.

## A table

| Feature | Available |
| --- | --- |
| Headings and contents | Yes |
| Bold and italic | Yes |
| Lists and quotes | Yes |
| Saved reading position | Yes |

## Links and images

[Project notes](https://example.com/notes) displays the link label. No webpage is opened.

![A small e-reader on a desk](reader.png)

Images display their alternative text. No image is downloaded.

## Different languages

Café, déjà vu, smörgåsbord — UTF-8 text uses the reader's installed fonts. Glyph coverage depends on your chosen font.

---

## Heading navigation

Open the reader menu and select **Contents** or **Select chapter** to jump to a heading in this document.

## Resume reading

Leave the viewer and reopen the same file to resume. Editing the source document rebuilds its formatted copy and resets its saved page position.
