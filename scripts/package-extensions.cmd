@echo off
setlocal
set "ROOT=%~dp0.."
for %%I in ("%ROOT%") do set "ROOT=%%~fI"
set "REL=%ROOT%\release"

set "N=0"

call :put "windows_amd64"        "%ROOT%\build\msvc\extension\datalake\datalake.duckdb_extension"
call :put "windows_amd64_mingw"  "%ROOT%\build\release\extension\datalake\datalake.duckdb_extension"
call :put "linux_amd64"          "%ROOT%\build\linux\extension\datalake\datalake.duckdb_extension"

echo.
echo Packaged %N% build(s) into %REL%
exit /b 0

:put
if not exist "%~2" (
  echo   skip  %~1\datalake.duckdb_extension  ^(not built yet^)
  exit /b 0
)
if not exist "%REL%\%~1" mkdir "%REL%\%~1"
copy /y "%~2" "%REL%\%~1\datalake.duckdb_extension" >nul || exit /b 1
echo   ok    release\%~1\datalake.duckdb_extension
set /a N+=1
exit /b 0
