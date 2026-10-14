@echo off
if not exist tests\vr-runtime-compat-work mkdir tests\vr-runtime-compat-work
"C:\Program Files\Python310\python.exe" tests\dfc-loader-stack.py
if errorlevel 1 exit /b 1
call payload\vr-foveated\native\vcvars.bat x64
if errorlevel 1 exit /b 1
cl /nologo /O2 /MT /EHsc /std:c++20 /Fetests\vr-runtime-compat-work\dfc-loader-stack-native.exe /Fotests\vr-runtime-compat-work\dfc-loader-stack-native.obj tests\dfc-loader-stack-native.cpp
if errorlevel 1 exit /b 1
tests\vr-runtime-compat-work\dfc-loader-stack-native.exe payload\deep-fried-chicken\deep-fried-chicken.addon64
if errorlevel 1 exit /b 1
tests\vr-runtime-compat-work\dfc-loader-stack-native.exe payload\deep-fried-chicken\deep-fried-chicken-nms.addon64
