`math.bmath` is a reference reading copy generated from `math.md` using the Python
companion's `buddy_math.render_note(source_bytes, "Equation")`. The firmware test
opens it through the normal Markdown document path and checks its embedded PNG
and EPUB contents. This catches drift between the Python and C++ file formats.

To regenerate from the repository root after an intentional format change:

```sh
PYTHONPATH=companion/notes-sync python -c 'from pathlib import Path; from buddy_math import render_note; p = Path("crosspoint-upstream/test/markdown/fixtures"); (p / "math.bmath").write_bytes(render_note((p / "math.md").read_bytes(), "Equation"))'
```

The optional `requirements-math.txt` packages must be installed in that Python
environment. Image bytes may vary between font/rendering versions; the tests
check the container, source fingerprint and content rather than exact pixels.
