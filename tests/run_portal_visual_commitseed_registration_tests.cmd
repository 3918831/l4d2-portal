@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0PortalVisualCommitSeedCommandRegistrationTests.ps1"
exit /b %errorlevel%
