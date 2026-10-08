@echo off
call "payload\vr-foveated\native\vcvars.bat" x64
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /MT /I payload\vr-foveated\native\reshade /Fetests\vr-depth-test.exe /Fotests\vr-depth-test.obj tests\vr-depth-test.cpp /link d3d11.lib d3dcompiler.lib
if errorlevel 1 exit /b 1
tests\vr-depth-test.exe
