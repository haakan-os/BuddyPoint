# BuddySync: KOReader EPUB editing

Install the entire `koreader-plugin/buddysync.koplugin` folder and restart KOReader. The optimizer now includes `epub_editor.lua` and `epub_xml.lua`; replacing only `epub_optimizer.lua` is not sufficient when upgrading from the previous version.

When upgrading from HaakanPoint, close KOReader and remove the old `haakanpoint.koplugin` directory before installing `buddysync.koplugin`. Keep `settings/haakanpoint.lua`; BuddySync intentionally uses that settings file. The X3 address remains `haakanpoint.local`, and the upload API is unchanged.

## Runtime dependencies

The plugin uses KOReader's bundled `ffi/archiver`, `libs/libkoreader-lfs`, `ui/renderimage`, and `ffi/blitbuffer` modules. It does not execute `zip`, `unzip`, BusyBox, ImageMagick, `find`, or other shell commands. Missing native modules produce an error and stop the transfer.

Extraction uses a unique folder beside the source book. The book's filesystem must have enough room for expanded contents and the output EPUB. Normal failures and Lua exceptions close archive handles and clean up the work folder. A power loss or forced process termination can leave a `.haakanpoint-work-*` folder; remove it only after KOReader has stopped processing that book.

## Editing API

`epub_editor` provides a focused EPUB container editor:

- `Editor.open(input_path)` extracts the ZIP through KOReader, rejects unsafe or duplicate paths and links, and validates the container, manifest resources, and spine references.
- `editor:list()`, `read(path)`, and `write(path, contents)` access resources using archive-relative paths. `write` updates existing resources or files in existing extracted directories; it does not automatically add manifest entries.
- `editor:fontResources()` identifies embedded font files by extension.
- `editor:imagePath(path)` selects a PNG name without overwriting another resource.
- `editor:replace(path, target, temporary_file)` installs a converted resource at its target path without overwriting a different resource, then removes the old copy from the work folder.
- `editor:updateResources(renames, removed)` updates links after resource replacement. Each rename maps the original archive path to `{path = new_path, media_type = "image/png"}`; removed paths map to `true`.
- `editor:save(output_path)` creates a separate EPUB, stores `mimetype` first and uncompressed, compresses the remaining entries, checks the final ZIP record, and refuses to overwrite existing files. With no output path it creates a uniquely named temporary EPUB beside the book, which the plugin removes after upload.
- `editor:close()` removes the extraction folder. Call it after success or failure; caller owns the final EPUB returned by `save`.

Operations raise descriptive Lua errors. The optimizer catches them and both transfer actions stop on failure. This is a targeted resource editor, not a general EPUB conformance validator or DRM editor.

## Resource consistency

The preserving XML editor changes attribute spans and removes complete selected elements while retaining untouched markup, namespace prefixes, comments, and CDATA. Image references are resolved against the containing file's directory rather than matched by basename. It handles manifest media types, XHTML/SVG links, ordinary `srcset` lists, CSS URLs, inline styles, and embedded stylesheets. Removed fonts are also removed from manifests, matching `@font-face` rules, and their encryption records. Unsupported constructs such as `xml:base`, escaped CSS URLs, and malformed XML stop optimization instead of producing a guessed rewrite.

Images are bounded to 480×800 while preserving aspect ratio and converted to grayscale PNG through KOReader. Small images below the existing 2 KB threshold are preserved. Resizing and re-encoding do not guarantee a smaller file for every source image.

PNG, JPEG, GIF, and WebP entries use ZIP's stored method because these formats already compress their pixels. This avoids the X3 allocating a second decompression window and buffers just to extract an image. XHTML, CSS, and other resources still use deflate compression.

KOReader's older `Blitbuffer:writePNG` returns no values, including on success; newer versions return a success flag and error. The optimizer supports both, honors reported errors, and checks the saved PNG signature, dimensions, and final IEND chunk before replacing an image. The integration tests cover both return conventions and missing, empty, truncated, and malformed output.

## Tests

Run `./scripts/test_sync.sh` for host checks, transfer failure handling, and preserving XML edits.

The native integration suite requires Python with Pillow and a KOReader-compatible LuaJIT environment exposing `ffi/archiver` and `libs/libkoreader-lfs`:

```sh
python3 scripts/test_epub_native.py --lua /path/to/luajit --bootstrap /path/to/koreader-bootstrap.lua
```

The optional bootstrap initializes KOReader's native module search paths. It is unnecessary when the runtime already exposes these modules. The test uses real archive and filesystem libraries but stubs image decoding/encoding. It disables `os.execute` and `io.popen`, exercises complete EPUB transformations, and independently checks ZIP contents, CRCs, XML, resource references, and cleanup using Python. It also injects decoder, encoder, extraction, write, and finalization failures.

Set `HAAKANPOINT_NATIVE_LUA` and optionally `HAAKANPOINT_NATIVE_BOOTSTRAP` to include this suite in `test_sync.sh`. Device testing is still needed for the real image renderer and memory behavior on large books.
