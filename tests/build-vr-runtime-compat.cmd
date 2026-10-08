@echo off
call payload\vr-foveated\native\vcvars.bat x64
if errorlevel 1 exit /b 1
cl /nologo /O2 /MT /EHsc /std:c++20 /Ithird_party\Detours\include /Ipayload\vr-foveated\native\reshade /Ipayload\vr-foveated\native\reshade-internals\source /Ipayload\vr-foveated\native /Fe:tests\vr-runtime-compat-work\arbitrary-vr-game.exe /Fo:tests\vr-runtime-compat-work\test.obj tests\vr-runtime-compat.cpp third_party\Detours\lib.X64\detours.lib user32.lib

