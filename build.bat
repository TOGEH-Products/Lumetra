@echo off
setlocal

rem Always build from the repository root, even when launched from a shortcut.
cd /d "%~dp0"
if errorlevel 1 goto :fail

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"

rem Find the latest Visual Studio installation that includes the x64 C++ tools.
rem vswhere ships with the Visual Studio Installer; when it is missing (a
rem Build Tools only install, for example) the standard install locations are
rem probed directly instead.
set "VSINSTALL="
if exist "%VSWHERE%" (
    for /f "usebackq tokens=*" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSINSTALL=%%I"
)

if not defined VSINSTALL (
    for %%E in ("%ProgramFiles(x86)%\Microsoft Visual Studio" "%ProgramFiles%\Microsoft Visual Studio") do (
        if not defined VSINSTALL (
            for /d %%V in ("%%E\*") do (
                if exist "%%~fV\VC\Auxiliary\Build\vcvars64.bat" set "VSINSTALL=%%~fV"
            )
        )
    )
)

if not defined VSINSTALL (
    echo [ERROR] No Visual Studio installation with the x64 C++ toolchain was found.
    echo         Install the Visual Studio Build Tools with the "Desktop development with C++" workload.
    goto :fail
)

set "VCVARS=%VSINSTALL%\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VCVARS%" (
    echo [ERROR] The x64 compiler environment script is missing:
    echo         "%VCVARS%"
    goto :fail
)

echo [1/3] Loading Visual Studio C++ build tools...
call "%VCVARS%" >nul
if errorlevel 1 (
    echo [ERROR] Visual Studio could not initialize its x64 build environment.
    goto :fail
)

rem Visual Studio ships CMake and Ninja alongside its C++ toolchain.
set "CMAKE_DIR=%VSINSTALL%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"
set "NINJA_DIR=%VSINSTALL%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja"
if not exist "%CMAKE_DIR%\cmake.exe" (
    echo [ERROR] CMake was not found in the Visual Studio installation:
    echo         "%CMAKE_DIR%\cmake.exe"
    goto :fail
)
if not exist "%NINJA_DIR%\ninja.exe" (
    echo [ERROR] Ninja was not found in the Visual Studio installation:
    echo         "%NINJA_DIR%\ninja.exe"
    goto :fail
)
set "PATH=%CMAKE_DIR%;%NINJA_DIR%;%PATH%"

echo [2/4] Configuring Lumetra (Release, x64)...
cmake -S "%CD%" -B "%CD%\build" -G Ninja -DCMAKE_BUILD_TYPE=Release
if errorlevel 1 goto :fail

echo [3/4] Building and linking Lumetra.exe...
cmake --build "%CD%\build" --config Release --target Lumetra --parallel
if errorlevel 1 goto :fail

if not exist "%CD%\build\Lumetra.exe" (
    echo [ERROR] The build completed without producing build\Lumetra.exe.
    goto :fail
)

if not defined LOCALAPPDATA (
    echo [ERROR] LOCALAPPDATA is not defined; cannot update the current-user installation.
    goto :fail
)

echo [4/4] Updating the current-user Lumetra installation...
cmake --install "%CD%\build" --prefix "%LOCALAPPDATA%\Programs\Lumetra"
if errorlevel 1 goto :fail

echo.
echo [SUCCESS] Lumetra was built and installed successfully:
echo           "%CD%\build\Lumetra.exe"
echo           "%LOCALAPPDATA%\Programs\Lumetra\Lumetra.exe"
echo.
set "BUILD_RESULT=0"
goto :finish

:fail
set "BUILD_RESULT=%ERRORLEVEL%"
if "%BUILD_RESULT%"=="0" set "BUILD_RESULT=1"
echo.
echo [FAILED] Lumetra could not be built. Review the error above.
echo.

:finish
pause
exit /b %BUILD_RESULT%
