@echo off
rem Project-specific wrapper to run the audio-io backup script
setlocal
set SCRIPT_DIR=%~dp0
set PYLAUNCHER=
where py >nul 2>&1
if %errorlevel%==0 (
  set PYLAUNCHER=py
) else (
  where python >nul 2>&1
  if %errorlevel%==0 set PYLAUNCHER=python
)
if "%PYLAUNCHER%"=="" (
  echo ERROR: Python not found. Install Python 3 and make sure it's on PATH.
  endlocal
  exit /b 1
)
"%PYLAUNCHER%" "%SCRIPT_DIR%python-scripts\audio-io_backup.py" %*
endlocal
