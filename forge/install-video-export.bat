@echo off
rem Optional: adds the video (stinger) export feature. Not needed for the OBS plugin transitions.
cd /d "%~dp0"
if exist "%~dp0node\node.exe" set "PATH=%~dp0node;%PATH%"
echo Installing the video renderer (Playwright + Chromium). This downloads about 150 MB.
call npm install playwright
call npx playwright install chromium
echo.
echo Video export also needs ffmpeg on your PATH (winget install Gyan.FFmpeg).
pause
