@echo off
if not exist tests\vr-runtime-compat-work mkdir tests\vr-runtime-compat-work
call payload\vr-foveated\native\vcvars.bat x64
if errorlevel 1 exit /b 1
cl /nologo /O2 /MT /EHsc /std:c++20 /Ithird_party\Detours\include /Fetests\vr-runtime-compat-work\reshade-settings-sync.exe /Fotests\vr-runtime-compat-work\reshade-settings-sync.obj tests\reshade-settings-sync.cpp third_party\Detours\lib.X64\detours.lib
if errorlevel 1 exit /b 1
tests\vr-runtime-compat-work\reshade-settings-sync.exe payload\openxr\ReShade64.dll
