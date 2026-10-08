@echo off
call "payload\vr-foveated\native\vcvars.bat" x64
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /MT /I tests\fake-reshade /Fetests\openxr-pose-test.exe /Fotests\openxr-pose-test.obj tests\openxr-pose-test.cpp
if errorlevel 1 exit /b 1
tests\openxr-pose-test.exe
