@echo off
call payload\vr-foveated\native\vcvars.bat x64
if errorlevel 1 exit /b 1
if not exist tests\vr-runtime-compat-work\menu-layer-test mkdir tests\vr-runtime-compat-work\menu-layer-test
cl /nologo /LD /O2 /MT /EHsc /std:c++20 /Fetests\vr-runtime-compat-work\menu-layer-test\dlss5-vr-compat.addon64 /Fotests\vr-runtime-compat-work\menu-layer-test\probe.obj tests\openxr-menu-probe.cpp
if errorlevel 1 exit /b 1
cl /nologo /O2 /MT /EHsc /std:c++20 /Fetests\vr-runtime-compat-work\menu-layer-test\menu-layer.exe /Fotests\vr-runtime-compat-work\menu-layer-test\test.obj tests\openxr-menu-layer-test.cpp
if errorlevel 1 exit /b 1
copy /y payload\vr-foveated\DLSS5OpenXRPose.dll tests\vr-runtime-compat-work\menu-layer-test\DLSS5OpenXRPose.dll >nul
pushd tests\vr-runtime-compat-work\menu-layer-test
menu-layer.exe
set test_exit=%errorlevel%
popd
exit /b %test_exit%
