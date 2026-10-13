@echo off
setlocal
rem Prefer PowerShell 7; fall back to Windows PowerShell.
where pwsh.exe >nul 2>nul
if errorlevel 1 goto windows_powershell
pwsh.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0build.ps1" %*
exit /b %errorlevel%

:windows_powershell
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0build.ps1" %*
exit /b %errorlevel%
