#!/bin/sh
cd "$(dirname "$0")" || exit 1
exec python3 buddy_notes_gui.py
