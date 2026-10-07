@echo off
setlocal
set "ROOT=%~dp0.."
for %%I in ("%ROOT%") do set "ROOT=%%~fI"
set "FWD=%ROOT:\=/%"

if "%DATALAKE_CMAKE%"=="" set "DATALAKE_CMAKE=C:\Users\AzK-v\AppData\Local\Python\pythoncore-3.14-64\Scripts\cmake.exe"
if "%DATALAKE_NINJA%"=="" set "DATALAKE_NINJA=C:\Users\AzK-v\AppData\Roaming\Python\Python314\Scripts\ninja.exe"

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
  echo vswhere not found. Install Visual Studio Build Tools 2022 with the "Desktop development with C++" workload.
  exit /b 1
)
set "VSROOT="
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -products * -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSROOT=%%i"
if "%VSROOT%"=="" (
  echo No MSVC toolchain found. Install Visual Studio Build Tools 2022 with the "Desktop development with C++" workload.
  exit /b 1
)
call "%VSROOT%\VC\Auxiliary\Build\vcvars64.bat" || exit /b 1

"%DATALAKE_CMAKE%" -G Ninja "-DCMAKE_MAKE_PROGRAM=%DATALAKE_NINJA%" -DCMAKE_BUILD_TYPE=Release -DEXTENSION_STATIC_BUILD=1 -DBUILD_UNITTESTS=FALSE "-DDUCKDB_EXTENSION_CONFIGS=%FWD%/extension_config.cmake" "-DUNITTEST_ROOT_DIRECTORY=%FWD%/" -DENABLE_UNITTEST_CPP_TESTS=FALSE -DENABLE_EXTENSION_AUTOLOADING=1 -DENABLE_EXTENSION_AUTOINSTALL=1 -S "%FWD%/duckdb" -B "%FWD%/build/msvc"
if errorlevel 1 exit /b 1

"%DATALAKE_CMAKE%" --build "%FWD%/build/msvc" --config Release -j 24
if errorlevel 1 exit /b 1

echo.
echo Built: %FWD%\build\msvc\extension\datalake\datalake.duckdb_extension
exit /b 0
