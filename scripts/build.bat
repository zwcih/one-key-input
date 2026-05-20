@echo off
:: Build script — runs from any cmd, sets up VS2022 x64 env, configures + builds.
setlocal

set "VS_DIR=C:\Program Files\Microsoft Visual Studio\2022\Community"
set "CMAKE_BIN=%VS_DIR%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"
set "NINJA_BIN=%VS_DIR%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja"
set "PATH=%CMAKE_BIN%;%NINJA_BIN%;%PATH%"

:: vcpkg location. CMakePresets.json reads $env{VCPKG_ROOT} — fall back to the
:: legacy hard-coded path if the env var isn't set so existing local setups
:: keep working.
if "%VCPKG_ROOT%"=="" set "VCPKG_ROOT=C:\dev\vcpkg"

call "%VS_DIR%\VC\Auxiliary\Build\vcvars64.bat" || goto :err

pushd "%~dp0\.."

:: NuGet restore (Azure Speech SDK). Idempotent — skips if already on disk.
if not exist "nuget\packages" mkdir "nuget\packages"
"%~dp0\tools\nuget.exe" restore "nuget\packages.config" -PackagesDirectory "nuget\packages" -NonInteractive || goto :err

if "%1"=="release" (
    cmake --preset=release || goto :err
    cmake --build build\release || goto :err
) else (
    cmake --preset=default || goto :err
    cmake --build build\default || goto :err
)
popd
echo.
echo === build OK ===
exit /b 0

:err
echo.
echo === build FAILED ===
exit /b 1
