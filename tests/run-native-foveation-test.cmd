@echo off
setlocal
if not exist "%NGX_SDK_INCLUDE%\nvsdk_ngx_params.h" exit /b 1
call payload\vr-foveated\native\vcvars.bat x64
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /MT /I"%NGX_SDK_INCLUDE%" /Ithird_party\Detours\include /Ipayload\vr-foveated\native\reshade /Fetests\native-foveation-test.exe /Fotests\native-foveation-test.obj tests\native-foveation-test.cpp /link third_party\Detours\lib.X64\detours.lib d3d12.lib dxgi.lib d3dcompiler.lib user32.lib
if errorlevel 1 exit /b 1
tests\native-foveation-test.exe
