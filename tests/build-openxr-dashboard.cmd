@echo off
call payload\vr-foveated\native\vcvars.bat x64
if errorlevel 1 exit /b 1
cl /nologo /O2 /MT /EHsc /std:c++20 /Ipayload\vr-foveated\native\vulkan-headers\include /Ithird_party\Detours\include /Fe:tests\vr-runtime-compat-work\openxr-dashboard-test.exe /Fo:tests\vr-runtime-compat-work\dashboard-test.obj tests\openxr-dashboard.cpp third_party\Detours\lib.X64\detours.lib user32.lib gdi32.lib opengl32.lib d3d12.lib d3d11.lib dxgi.lib
exit /b %errorlevel%
