@echo off
if not exist tests\vr-runtime-compat-work mkdir tests\vr-runtime-compat-work
call payload\vr-foveated\native\vcvars.bat x64
if errorlevel 1 exit /b 1
cl /nologo /O2 /MT /EHsc /std:c++20 /Fetests\vr-runtime-compat-work\openxr-focus-reentry.exe /Fotests\vr-runtime-compat-work\openxr-focus-reentry.obj tests\openxr-focus-reentry.cpp
if errorlevel 1 exit /b 1
tests\vr-runtime-compat-work\openxr-focus-reentry.exe payload\vr-foveated\dlss5-vr-compat.addon64
