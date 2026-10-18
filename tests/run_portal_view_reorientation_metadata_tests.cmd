@echo off
setlocal
cd /d "%~dp0\.."
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tests\PortalViewReorientationMetadataTests.ps1
exit /b %ERRORLEVEL%
