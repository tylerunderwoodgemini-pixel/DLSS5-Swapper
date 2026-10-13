@echo off
call payload\vr-foveated\native\vcvars.bat x64
if errorlevel 1 exit /b 1
cl /nologo /O2 /MT /EHsc /std:c++20 /Ipayload\vr-foveated\native\vulkan-headers\include /Fetests\vr-runtime-compat-work\openxr-menu-transport.exe /Fotests\vr-runtime-compat-work\menu-transport.obj tests\openxr-menu-transport.cpp user32.lib gdi32.lib opengl32.lib
if errorlevel 1 exit /b 1
tests\vr-runtime-compat-work\openxr-menu-transport.exe
