@echo off
call "payload\vr-foveated\native\vcvars.bat" x64
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /O2 /MT tests\vr-6dof-test.cpp /Fe:tests\vr-6dof-test.exe d3d11.lib d3dcompiler.lib
if errorlevel 1 exit /b 1
tests\vr-6dof-test.exe
