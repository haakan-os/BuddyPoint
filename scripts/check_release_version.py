#!/usr/bin/env python3
"""Reject release tags that would not match the compiled OTA version."""
import configparser
import re
import sys
from pathlib import Path


def check(tag):
    if not re.fullmatch(r'v?(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)', tag):
        raise ValueError('Use a stable tag such as v1.6.6, without a prerelease suffix.')
    config = configparser.ConfigParser()
    config.read(Path(__file__).resolve().parents[1] / 'crosspoint-upstream/platformio.ini')
    version = tag.removeprefix('v')
    if version != config['crosspoint']['version']:
        raise ValueError('The release tag must match [crosspoint] version in platformio.ini.')
    return version


if __name__ == '__main__':
    if len(sys.argv) != 2:
        raise SystemExit('Usage: check_release_version.py vMAJOR.MINOR.PATCH')
    try:
        print(check(sys.argv[1]))
    except ValueError as error:
        raise SystemExit(str(error))
