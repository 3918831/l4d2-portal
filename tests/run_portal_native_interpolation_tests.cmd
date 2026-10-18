@echo off
call "D:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars32.bat" >nul
if errorlevel 1 exit /b %errorlevel%
if not exist tests\obj\portal_native_interpolation mkdir tests\obj\portal_native_interpolation
cl.exe /nologo /EHsc /std:c++17 tests\PortalNativeInterpolationTests.cpp /Fo:tests\obj\portal_native_interpolation\ /Fe:tests\PortalNativeInterpolationTests.exe
if errorlevel 1 exit /b %errorlevel%
tests\PortalNativeInterpolationTests.exe
