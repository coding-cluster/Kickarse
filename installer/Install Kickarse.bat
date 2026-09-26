@echo off
rem Double-click installer: runs Install-Kickarse.ps1 elevated (a normal install needs to write
rem to %CommonProgramFiles% and HKLM). For a no-admin test install, call the .ps1 directly with
rem -NoReg and your own -Vst3Dir/-Vst2Dir/-ClapDir instead of using this .bat.
setlocal EnableDelayedExpansion

set "PSSCRIPT=%~dp0Install-Kickarse.ps1"
if not exist "%PSSCRIPT%" (
    echo Could not find "%PSSCRIPT%"
    exit /b 1
)

set "ARGS="
:parseArgs
if "%~1"=="" goto runElevated
set "ARGS=!ARGS! %1"
shift
goto parseArgs

:runElevated
powershell.exe -NoProfile -ExecutionPolicy Bypass -Command ^
    "Start-Process -FilePath 'powershell.exe' -ArgumentList '-NoProfile -ExecutionPolicy Bypass -File \"%PSSCRIPT%\"!ARGS!' -Verb RunAs -Wait"

endlocal
