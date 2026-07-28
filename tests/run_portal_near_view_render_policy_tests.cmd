@echo off
setlocal
where cl.exe >nul 2>nul
if errorlevel 1 call "D:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x86 -host_arch=x64
if errorlevel 1 exit /b %errorlevel%
if not exist tests\obj\portal_near_view_render_policy mkdir tests\obj\portal_near_view_render_policy
cl.exe /nologo /EHsc /std:c++17 tests\PortalNearViewRenderPolicyTests.cpp src\Portal\PortalNearViewRenderPolicy.cpp /Fo:tests\obj\portal_near_view_render_policy\ /Fe:tests\PortalNearViewRenderPolicyTests.exe
if errorlevel 1 exit /b %errorlevel%
tests\PortalNearViewRenderPolicyTests.exe
