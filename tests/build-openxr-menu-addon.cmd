@echo off
call payload\vr-foveated\native\vcvars.bat x64
if errorlevel 1 exit /b 1
cl /nologo /LD /O2 /MT /EHsc /std:c++20 /Ithird_party\Detours\include /Ipayload\vr-foveated\native\reshade /Ipayload\vr-foveated\native\reshade-internals\source /Ipayload\vr-foveated\native /Ipayload\vr-foveated\native\vulkan-headers\include /Fepayload\vr-foveated\dlss5-vr-compat.addon64 /Fopayload\vr-foveated\native\vr-runtime-compat.obj payload\vr-foveated\native\vr-runtime-compat.cpp third_party\Detours\lib.X64\detours.lib user32.lib gdi32.lib opengl32.lib
