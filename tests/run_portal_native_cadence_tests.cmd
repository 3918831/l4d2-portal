@echo off
call "D:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars32.bat" >nul
if errorlevel 1 exit /b %errorlevel%
if not exist tests\obj\portal_native_cadence mkdir tests\obj\portal_native_cadence
cl.exe /nologo /EHsc /std:c++17 tests\PortalNativeCadenceTests.cpp /Fo:tests\obj\portal_native_cadence\ /Fe:tests\PortalNativeCadenceTests.exe
if errorlevel 1 exit /b %errorlevel%
tests\PortalNativeCadenceTests.exe
