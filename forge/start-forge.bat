@echo off
cd /d "%~dp0"
if exist "%~dp0node\node.exe" set "PATH=%~dp0node;%PATH%"
echo Starting Transition Forge...
node forge-server.mjs
echo.
echo The helper stopped. Press any key to close.
pause >nul
