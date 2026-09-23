@echo off
call "%~dp0setup_msvc.cmd"
if errorlevel 1 exit /b 1
set "BPC_CMAKE=cmake"
"%BPC_CMAKE%" -S "%~dp0ui_host" -B "%~dp0..\build\ui-host" -G Ninja
if errorlevel 1 exit /b 1
"%BPC_CMAKE%" --build "%~dp0..\build\ui-host" -j 8
if errorlevel 1 exit /b 1
pushd "%~dp0..\build\ui-host"
ui_smoke.exe
set "BPC_TEST_RESULT=%ERRORLEVEL%"
popd
exit /b %BPC_TEST_RESULT%
