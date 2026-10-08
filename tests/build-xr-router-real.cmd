@echo off
call payload\vr-foveated\native\vcvars.bat x64
if errorlevel 1 exit /b 1
cl /nologo /O2 /MT /EHsc /std:c++20 /Fe:tests\xr-router-real-work\generic-launch.exe /Fo:tests\xr-router-real-work\test.obj tests\xr-router-real.cpp
