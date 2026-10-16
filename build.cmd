@echo off
rem Builds build\yan.exe (Release). Usage: build.cmd [Debug]
setlocal
set CONFIG=%1
if "%CONFIG%"=="" set CONFIG=Release

if not defined VCINSTALLDIR (
  for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set VSINSTALL=%%i
)
if not defined VCINSTALLDIR (
  if not defined VSINSTALL (
    echo Visual Studio C++ build tools not found.
    exit /b 1
  )
  call "%VSINSTALL%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
)

set BUILD_DIR=%~dp0build\%CONFIG%
if not exist "%BUILD_DIR%\build.ninja" (
  cmake -S "%~dp0." -B "%BUILD_DIR%" -G Ninja -DCMAKE_BUILD_TYPE=%CONFIG% || exit /b 1
)
cmake --build "%BUILD_DIR%" || exit /b 1
echo.
echo Built %BUILD_DIR%\yan.exe
