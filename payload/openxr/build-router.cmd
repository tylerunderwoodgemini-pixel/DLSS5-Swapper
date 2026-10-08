@echo off
call payload\vr-foveated\native\vcvars.bat x64
if errorlevel 1 exit /b 1
cl /nologo /LD /O2 /MT /EHsc /Fe:payload\openxr\DLSS5OpenXRRouter.dll /Fo:payload\openxr\dlss5-openxr-router.obj payload\openxr\dlss5-openxr-router.cpp version.lib
