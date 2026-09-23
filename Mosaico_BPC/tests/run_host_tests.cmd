@echo off
call "%~dp0setup_msvc.cmd"
if errorlevel 1 exit /b 1
python "%~dp0test_protocol.py"
