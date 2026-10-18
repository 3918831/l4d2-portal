@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0PortalEyeCrossLeafHandoffCommandRegistrationTests.ps1"
exit /b %errorlevel%
