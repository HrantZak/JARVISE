@echo off
:: Standalone build of third_party/llama.cpp with the Vulkan backend.
::
:: This is the verification build: it produces the llama CLI and llama-bench so
:: the engine can be exercised directly, independently of JARVIS. The JARVIS
:: build links libllama through add_subdirectory and does not use this output.
::
:: LLAMA_BUILD_SERVER is ON only because this revision folds the CLI into a
:: single llama.exe that links the server implementation unconditionally. JARVIS
:: itself never builds or ships the server.
::
::   scripts\build-llama.bat [--fresh]

setlocal
set "PATH=%PATH%;C:\Program Files (x86)\Microsoft Visual Studio\Installer"
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
if errorlevel 1 (echo VCVARS FAILED & exit /b 1)

set "VULKAN_SDK=C:\VulkanSDK\1.4.357.0"
set "CMAKE=C:\Program Files\CMake\bin\cmake.exe"
set "NINJA=C:\QtX\Tools\Ninja\ninja.exe"

cd /d "%~dp0.."
set "SRC=third_party\llama.cpp"
set "BLD=build\llama-vulkan"

if "%1"=="--fresh" if exist "%BLD%" rmdir /s /q "%BLD%"

echo ==== CONFIGURE (Vulkan) ====
"%CMAKE%" -S "%SRC%" -B "%BLD%" -G Ninja ^
  -DCMAKE_MAKE_PROGRAM="%NINJA%" ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DGGML_VULKAN=ON ^
  -DGGML_NATIVE=ON ^
  -DLLAMA_BUILD_TESTS=OFF ^
  -DLLAMA_BUILD_EXAMPLES=OFF ^
  -DLLAMA_BUILD_SERVER=ON ^
  -DLLAMA_BUILD_TOOLS=ON ^
  -DLLAMA_CURL=OFF ^
  -DVulkan_GLSLC_EXECUTABLE="%VULKAN_SDK%/Bin/glslc.exe" ^
  -DVulkan_INCLUDE_DIR="%VULKAN_SDK%/Include" ^
  -DVulkan_LIBRARY="%VULKAN_SDK%/Lib/vulkan-1.lib"
if errorlevel 1 (echo CONFIGURE FAILED & exit /b 1)

echo ==== BUILD ====
"%CMAKE%" --build "%BLD%" --parallel
if errorlevel 1 (echo BUILD FAILED & exit /b 1)

echo ==== BUILD OK ====
