@echo off
setlocal
if not exist "%NGX_SDK_INCLUDE%\nvsdk_ngx_params.h" (
  echo Set NGX_SDK_INCLUDE to the include directory of the NVIDIA DLSS SDK.
  exit /b 1
)
call "%~dp0vcvars.bat" x64
if errorlevel 1 exit /b 1
if not defined DETOURS_ROOT (
  if exist "%~dp0..\..\..\third_party\Detours\include\detours.h" (
    set "DETOURS_ROOT=%~dp0..\..\..\third_party\Detours"
  ) else (
    set "DETOURS_ROOT=%~dp0..\..\..\..\fork-source\third_party\Detours"
  )
)
cl /nologo /LD /O2 /MT /EHsc /std:c++20 /I"%NGX_SDK_INCLUDE%" /I"%DETOURS_ROOT%\include" /I"%~dp0reshade" /Fe:"%~dp0..\native-vr-foveation-nvngx.dll.addon64" /Fo:"%~dp0native-vr-foveation.obj" "%~dp0native-vr-foveation.cpp" "%DETOURS_ROOT%\lib.X64\detours.lib" d3d12.lib d3dcompiler.lib user32.lib
exit /b %errorlevel%
