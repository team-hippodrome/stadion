@echo off
rem Smooth motion trace for bug reports: put this file next to stadion.exe and double-click it.
rem Play about 20 s in the main menu and 20 s in a 3D scene, close the game, and send smooth_motion_log.txt
rem (written next to the game by the game itself, PORT_LOG_FILE: no shell redirect needed).
cd /d "%~dp0"
set PORT_LOG_FILE=smooth_motion_log.txt
set PORT_INTERP_LOG=1
set PORT_TRACE_PACING=1
start "" /wait stadion.exe
if exist smooth_motion_log.txt (echo Log written: %~dp0smooth_motion_log.txt) else (echo No log was written. Is this file next to stadion.exe?)
pause
