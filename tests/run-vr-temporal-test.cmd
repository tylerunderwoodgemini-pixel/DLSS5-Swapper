@echo off
call "payload\vr-foveated\native\vcvars.bat" x64
if errorlevel 1 exit /b 1
cl /nologo /EHsc /std:c++20 /Fetests\vr-temporal-test.exe /Fotests\vr-temporal-test.obj tests\vr-temporal-test.cpp /link d3d11.lib d3dcompiler.lib
if errorlevel 1 exit /b 1
tests\vr-temporal-test.exe
