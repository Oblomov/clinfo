REM call as fetch-opencl-vcpkg-win x86|x64
@echo off
SETLOCAL

set VCPKG_VERSION="2024.10.21"

if not "%~1" == "" (
  set VCPKG_ARCH=%1
) else if not defined PLATFORM (
    set VCPKG_ARCH=%VSCMD_ARG_TGT_ARCH%
) else (
    set VCPKG_ARCH=%PLATFORM%
)

if NOT "%VCPKG_ARCH%" == "x64" (
  if NOT "%VCPKG_ARCH%" == "x86" (
    echo Bad argument: %VCPKG_ARCH%
    echo %0 [x86^|x64]
    exit /B 1
  )
)

if not exist ".\vcpkg" (
  git clone -b %VCPKG_VERSION%  --depth 1 https://github.com/microsoft/vcpkg .\vcpkg
)

if not exist ".\vcpkg\vcpkg.exe" (
  cmd.exe /C .\vcpkg\bootstrap-vcpkg.bat -disableMetrics
)

echo Install OpenCL SDK
.\vcpkg\vcpkg.exe install opencl --triplet="%VCPKG_ARCH%-windows"

ENDLOCAL
set VCPKG_ROOT=.\vcpkg

