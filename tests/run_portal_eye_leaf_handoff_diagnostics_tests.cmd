@echo off
call "D:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars32.bat" >nul
if errorlevel 1 exit /b %errorlevel%

if not exist tests\obj\portal_eye_leaf_handoff_diagnostics mkdir tests\obj\portal_eye_leaf_handoff_diagnostics

cl.exe /nologo /EHsc /std:c++17 tests\PortalEyeLeafHandoffDiagnosticsTests.cpp src\Portal\PortalEyeLeafHandoffDiagnostics.cpp /Fo:tests\obj\portal_eye_leaf_handoff_diagnostics\ /Fe:tests\PortalEyeLeafHandoffDiagnosticsTests.exe
if errorlevel 1 exit /b %errorlevel%

tests\PortalEyeLeafHandoffDiagnosticsTests.exe
