@echo off
call payload\vr-foveated\native\vcvars.bat x64
if errorlevel 1 exit /b 1
cl /nologo /LD /MT /DTEST_RESULT=111 /Fe:tests\xr-router-work\ExistingReShade.dll /Fo:tests\xr-router-work\existing.obj tests\xr-router-stub.cpp
if errorlevel 1 exit /b 1
cl /nologo /LD /MT /DTEST_RESULT=222 /Fe:tests\xr-router-work\ReShade64.dll /Fo:tests\xr-router-work\fallback.obj tests\xr-router-stub.cpp
if errorlevel 1 exit /b 1
cl /nologo /MT /Fe:tests\xr-router-test.exe /Fo:tests\xr-router-work\test.obj tests\xr-router-test.cpp
if errorlevel 1 exit /b 1
tests\xr-router-test.exe existing tests\xr-router-work
if errorlevel 1 exit /b 1
tests\xr-router-test.exe fallback tests\xr-router-work
if errorlevel 1 exit /b 1
tests\xr-router-test.exe missing tests\xr-router-work\missing

if errorlevel 1 exit /b 1
cl /nologo /LD /MT /DTEST_RESULT=333 /Fe:tests\xr-router-work\adapter\dlss5-vr-compat.addon64 /Fo:tests\xr-router-work\adapter.obj tests\xr-router-stub.cpp
if errorlevel 1 exit /b 1
tests\xr-router-test.exe adapter tests\xr-router-work\adapter
