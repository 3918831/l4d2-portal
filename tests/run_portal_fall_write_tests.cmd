@echo off
call "D:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars32.bat" >nul
if errorlevel 1 exit /b %errorlevel%
if not exist tests\obj\portal_fall_write mkdir tests\obj\portal_fall_write
cl.exe /nologo /EHsc /std:c++17 tests\PortalFallWriteTests.cpp /Fo:tests\obj\portal_fall_write\ /Fe:tests\PortalFallWriteTests.exe
if errorlevel 1 exit /b %errorlevel%
tests\PortalFallWriteTests.exe
