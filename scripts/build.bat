@echo off
rem ---------------------------------------------------------------------------
rem JARVIS build helper.
rem
rem   scripts\build.bat [debug|release] [--test] [--fresh]
rem
rem Sets up the MSVC environment (CMake presets cannot do that themselves when
rem using the Ninja generator), then configures, builds and optionally tests.
rem ---------------------------------------------------------------------------
setlocal EnableDelayedExpansion

rem Capture the project root before the argument loop: `shift` also shifts %0,
rem after which %~dp0 no longer points at this script.
set "PROJECT_ROOT=%~dp0.."

set "VS_ROOT=C:\Program Files\Microsoft Visual Studio\18\Community"
set "CMAKE=C:\Program Files\CMake\bin\cmake.exe"
set "CTEST=C:\Program Files\CMake\bin\ctest.exe"

rem The Vulkan backend of llama.cpp needs the LunarG SDK for glslc and headers.
rem Set it here rather than relying on the machine environment: a shell opened
rem before the SDK was installed will not have inherited the variable.
if not defined VULKAN_SDK set "VULKAN_SDK=C:\VulkanSDK\1.4.357.0"
if not exist "%VULKAN_SDK%\Bin\glslc.exe" (
    echo Vulkan SDK not found at "%VULKAN_SDK%".
    echo Set VULKAN_SDK to the LunarG SDK root before building.
    exit /b 1
)

set "CONFIG=debug"
set "RUN_TESTS=0"
set "FRESH="

:parse
if "%~1"=="" goto parsed
if /i "%~1"=="debug"    set "CONFIG=debug"    & shift & goto parse
if /i "%~1"=="release"  set "CONFIG=release"  & shift & goto parse
if /i "%~1"=="--test"   set "RUN_TESTS=1"     & shift & goto parse
if /i "%~1"=="--fresh"  set "FRESH=--fresh"   & shift & goto parse
echo Unknown argument: %~1
exit /b 2
:parsed

set "PRESET=msvc-%CONFIG%"

if not exist "%VS_ROOT%\VC\Auxiliary\Build\vcvars64.bat" (
    echo ERROR: Visual Studio 2026 not found at "%VS_ROOT%"
    exit /b 1
)
if not exist "%CMAKE%" (
    echo ERROR: CMake not found at "%CMAKE%"
    exit /b 1
)

rem vcvars64.bat needs vswhere on PATH.
set "PATH=%PATH%;C:\Program Files (x86)\Microsoft Visual Studio\Installer"
call "%VS_ROOT%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 (
    echo ERROR: could not initialise the MSVC environment
    exit /b 1
)

cd /d "%PROJECT_ROOT%"

echo ==== CONFIGURE [%PRESET%] ====
"%CMAKE%" --preset %PRESET% %FRESH%
if errorlevel 1 exit /b 1

echo.
echo ==== BUILD [%PRESET%] ====
"%CMAKE%" --build --preset %PRESET%
if errorlevel 1 exit /b 1

if "%RUN_TESTS%"=="1" (
    echo.
    echo ==== TEST [%PRESET%] ====
    "%CTEST%" --preset %PRESET%
    if errorlevel 1 exit /b 1
)

echo.
echo ==== DONE ====
echo Executable: build\%PRESET%\bin\JARVIS.exe
