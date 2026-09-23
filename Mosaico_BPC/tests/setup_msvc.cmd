@echo off
rem Use the current Developer Command Prompt, or discover Visual Studio.
where cl >nul 2>nul
if not errorlevel 1 exit /b 0
set "BPC_VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%BPC_VSWHERE%" (
    echo Open an x64 MSVC Developer Command Prompt first.
    exit /b 1
)
set "BPC_VS_ROOT="
for /f "usebackq delims=" %%I in (`"%BPC_VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "BPC_VS_ROOT=%%I"
if not defined BPC_VS_ROOT (
    echo MSVC x64 build tools were not found.
    exit /b 1
)
call "%BPC_VS_ROOT%\VC\Auxiliary\Build\vcvars64.bat" >nul
exit /b %ERRORLEVEL%

