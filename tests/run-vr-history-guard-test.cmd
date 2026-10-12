@echo off
node tests\vr-history-guard-test.cjs
if errorlevel 1 exit /b 1
call payload\vr-foveated\native\vcvars.bat x64
if errorlevel 1 exit /b 1
cl /nologo /EHsc /std:c++20 /MT tests\vr-runtime-compat-work\history-guard-test.cpp /Fetests\vr-runtime-compat-work\history-guard-test.exe /Fotests\vr-runtime-compat-work\history-guard-test.obj
if errorlevel 1 exit /b 1
tests\vr-runtime-compat-work\history-guard-test.exe
