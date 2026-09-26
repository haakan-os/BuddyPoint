@echo off
cd /d "%~dp0"
py -3 buddy_notes_gui.py
if errorlevel 1 pause
