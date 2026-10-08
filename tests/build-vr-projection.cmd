@echo off
call "payload\vr-foveated\native\vcvars.bat" x64
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /O2 /MT /Ipayload\vr-foveated\native\reshade tests\vr-projection-test.cpp /Fe:tests\vr-projection-test.exe d3d11.lib
if errorlevel 1 exit /b 1
tests\vr-projection-test.exe
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /O2 /MT /LD /Ipayload\vr-foveated\native\reshade payload\vr-foveated\native\vr-projection-bridge.cpp /Fe:payload\vr-foveated\vr-projection-bridge.addon64 d3d11.lib
