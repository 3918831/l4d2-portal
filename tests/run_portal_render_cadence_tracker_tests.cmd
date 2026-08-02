@echo off
call "D:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars32.bat" >nul
if errorlevel 1 exit /b %errorlevel%

if not exist tests\obj\portal_render_cadence_tracker mkdir tests\obj\portal_render_cadence_tracker

cl.exe /nologo /EHsc /std:c++17 tests\PortalRenderCadenceTrackerTests.cpp src\Portal\PortalRenderCadenceTracker.cpp /Fo:tests\obj\portal_render_cadence_tracker\ /Fe:tests\PortalRenderCadenceTrackerTests.exe
if errorlevel 1 exit /b %errorlevel%

tests\PortalRenderCadenceTrackerTests.exe
