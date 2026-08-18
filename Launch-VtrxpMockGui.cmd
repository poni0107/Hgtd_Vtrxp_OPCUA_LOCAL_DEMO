@echo off
setlocal
set "DEMO_EXE=%~dp0vtrxp_mock_gui.exe"
if not exist "%DEMO_EXE%" (
  echo The packaged executable was not found:
  echo %DEMO_EXE%
  echo.
  echo Run BuildFromSource.cmd if the executable needs to be rebuilt.
  pause
  exit /b 1
)
"%DEMO_EXE%" %*
exit /b %ERRORLEVEL%
