@echo off
call "D:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars32.bat" >nul
if errorlevel 1 exit /b %errorlevel%

if not exist tests\obj\portal_view_reorientation mkdir tests\obj\portal_view_reorientation

cl.exe /nologo /EHsc /std:c++17 tests\PortalViewReorientationTests.cpp src\Portal\PortalViewReorientation.cpp /Fo:tests\obj\portal_view_reorientation\ /Fe:tests\PortalViewReorientationTests.exe
if errorlevel 1 exit /b %errorlevel%

tests\PortalViewReorientationTests.exe
