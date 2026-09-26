#!/usr/bin/env python3
"""Package the standalone notes companion, UI, launchers and user guide."""
import argparse
from pathlib import Path
import zipfile

ROOT = Path(__file__).resolve().parents[1] / 'companion' / 'notes-sync'


def package(output):
    paths = list(ROOT.glob('buddy_*.py'))
    paths += [ROOT / name for name in ('README.md', 'USER_GUIDE.md', 'requirements-math.txt',
                                     'Start BuddyPoint Sync.command', 'Start BuddyPoint Sync.bat')]
    paths += [path for directory in ('gui', 'examples') for path in (ROOT / directory).rglob('*') if path.is_file()]
    output = Path(output)
    output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(output, 'w', compression=zipfile.ZIP_DEFLATED) as archive:
        for path in sorted(paths):
            entry = zipfile.ZipInfo('notes-sync/' + path.relative_to(ROOT).as_posix())
            entry.compress_type = zipfile.ZIP_DEFLATED
            entry.create_system = 3
            entry.external_attr = (0o100755 if path.suffix == '.command' else 0o100644) << 16
            archive.writestr(entry, path.read_bytes())
    print(output)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=Path)
    package(parser.parse_args().output)
