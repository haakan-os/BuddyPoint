#!/usr/bin/env python3
"""Package the BuddySync Lua sources for installation in KOReader/plugins."""
import sys
import zipfile
from pathlib import Path
root = Path(__file__).resolve().parents[1] / 'koreader-plugin'
output = Path(sys.argv[1]) if len(sys.argv) > 1 else root / 'buddysync-plugin.zip'
output.parent.mkdir(parents=True, exist_ok=True)
with zipfile.ZipFile(output, 'w', zipfile.ZIP_DEFLATED) as archive:
    for path in sorted((root / 'buddysync.koplugin').glob('*.lua')):
        archive.write(path, path.relative_to(root))
print(output)
