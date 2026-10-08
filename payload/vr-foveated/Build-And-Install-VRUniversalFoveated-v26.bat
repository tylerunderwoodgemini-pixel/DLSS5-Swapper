@echo off
cd /d "%~dp0"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Build-And-Install-VRUniversalFoveated-v26.ps1" %*
pause
