@echo off
call "D:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars32.bat" >nul
if errorlevel 1 exit /b %errorlevel%

if not exist tests\obj\portal_visual_continuity_tracker mkdir tests\obj\portal_visual_continuity_tracker

cl.exe /nologo /EHsc /std:c++17 tests\PortalVisualContinuityTrackerTests.cpp src\Portal\PortalVisualContinuityTracker.cpp /Fo:tests\obj\portal_visual_continuity_tracker\ /Fe:tests\PortalVisualContinuityTrackerTests.exe
if errorlevel 1 exit /b %errorlevel%

tests\PortalVisualContinuityTrackerTests.exe
