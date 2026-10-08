@echo off
call "%~dp0native\vcvars.bat" x64
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /O2 /MT /LD /Fe"%~dp0DLSS5OpenXRPose.dll" /Fo"%~dp0native\openxr-pose-layer.obj" "%~dp0native\openxr-pose-layer.cpp"
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /O2 /MT /LD /I"%~dp0native\reshade" /Fe"%~dp0vr-pose-bridge-v1.addon64" /Fo"%~dp0native\vr-pose-bridge.obj" "%~dp0native\vr-pose-bridge.cpp"
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /O2 /MT /LD /I"%~dp0native\reshade" /Fe"%~dp0vr-depth-bridge.addon64" /Fo"%~dp0native\vr-depth-bridge.obj" "%~dp0native\vr-depth-bridge.cpp" /link d3d11.lib d3dcompiler.lib
exit /b %errorlevel%
