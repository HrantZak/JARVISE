# Third-party sources, vendored as pinned git submodules.
#
# llama.cpp is added with LLAMA_STANDALONE off, so its tools, examples, server,
# unified app and tests are all skipped: JARVIS links libllama and nothing else.
#
# Note on warnings: the strict flags (/W4 /WX /permissive-) live on the
# JARVIS::CompileOptions interface target, not in CMAKE_CXX_FLAGS, so upstream
# code is never compiled under this project's warning policy. That is
# deliberate - vendored code must not be held to our style rules, and we must
# not be tempted to relax ours because of it.

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/llama.cpp/CMakeLists.txt")
    message(FATAL_ERROR
        "third_party/llama.cpp is empty. Fetch the submodule:\n"
        "    git submodule update --init --recursive")
endif()

# llama.cpp is C++17 code and must be compiled as such.
#
# JARVIS builds at C++23, and CMAKE_CXX_STANDARD is inherited by subdirectories.
# Under C++20 and later a u8"" literal is char8_t[] rather than char[], which
# breaks llama-chat.cpp outright: it passes u8 literals to const char* parameters
# and streams them into std::stringstream, where operator<< is deleted.
#
# CMAKE_CXX_STANDARD is directory-scoped, so lowering it here applies to the
# vendored targets only. JARVIS's own modules keep C++23.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Static libraries: everything ends up inside JARVIS.exe, so there are no
# ggml/llama DLLs to deploy alongside it.
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

set(LLAMA_BUILD_COMMON   OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_TOOLS    OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_SERVER   OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_APP      OFF CACHE BOOL "" FORCE)
set(LLAMA_CURL           OFF CACHE BOOL "" FORCE)

# GPU backend. Vulkan rather than CUDA: it needs only the display driver at run
# time, and it sidesteps nvcc's host-compiler version check against MSVC 14.50.
set(GGML_VULKAN ON  CACHE BOOL "" FORCE)
set(GGML_NATIVE ON  CACHE BOOL "" FORCE)
set(GGML_OPENMP OFF CACHE BOOL "" FORCE)

if(DEFINED ENV{VULKAN_SDK})
    set(_jarvis_vulkan_sdk "$ENV{VULKAN_SDK}")
    if(NOT DEFINED Vulkan_GLSLC_EXECUTABLE)
        set(Vulkan_GLSLC_EXECUTABLE "${_jarvis_vulkan_sdk}/Bin/glslc.exe" CACHE FILEPATH "")
    endif()
    message(STATUS "  Vulkan SDK    : ${_jarvis_vulkan_sdk}")
else()
    message(FATAL_ERROR
        "VULKAN_SDK is not set. The Vulkan backend needs the LunarG SDK for "
        "glslc and the Vulkan headers.")
endif()

add_subdirectory(llama.cpp EXCLUDE_FROM_ALL)

# ---------------------------------------------------------------------------
# whisper.cpp
# ---------------------------------------------------------------------------
#
# ORDER MATTERS. whisper.cpp's CMakeLists guards its bundled ggml with
# `if (NOT TARGET ggml)`, so adding it *after* llama.cpp makes it reuse the ggml
# that llama.cpp already built - one copy of ggml, one Vulkan backend, and
# Whisper inherits GPU support without a second shader compile.
#
# Reversing these two lines would produce two conflicting ggml targets.

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/whisper.cpp/CMakeLists.txt")
    message(FATAL_ERROR
        "third_party/whisper.cpp is empty. Fetch the submodule:\n"
        "    git submodule update --init --recursive")
endif()

set(WHISPER_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_SERVER   OFF CACHE BOOL "" FORCE)
set(WHISPER_CURL           OFF CACHE BOOL "" FORCE)
set(WHISPER_SDL2           OFF CACHE BOOL "" FORCE)

add_subdirectory(whisper.cpp EXCLUDE_FROM_ALL)

# Keep the vendored targets out of the way in IDE project trees.
foreach(target llama whisper ggml ggml-base ggml-cpu ggml-vulkan vulkan-shaders-gen)
    if(TARGET ${target})
        set_target_properties(${target} PROPERTIES FOLDER "third_party")
    endif()
endforeach()
