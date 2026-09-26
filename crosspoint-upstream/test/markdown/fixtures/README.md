`math.bmath` is a legacy v1 reading copy generated from `math.md` using the
Python companion's original PNG renderer and the title `Equation`. Keep it as a
backward-compatibility fixture; the current renderer produces baseline grayscale
JPEG images to reduce decoding memory on the reader.

The firmware test opens this fixture through the normal Markdown document path
and checks its embedded PNG and EPUB contents. Companion tests check the current
JPEG output, its source fingerprint, and automatic regeneration of v1 copies.
The binary sidecar format is unchanged.

`tools.md` and `tools.bnotes` are a Python-generated navigation/card protocol fixture, validated by `NoteToolsTest`.
