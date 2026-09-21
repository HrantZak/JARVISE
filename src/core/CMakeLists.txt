# jarvis_core - foundational vocabulary types.
#
# Deliberately free of any Qt dependency: this is the deepest layer and is
# linked by future modules that wrap plain C/C++ libraries (llama.cpp,
# whisper.cpp), where dragging QtCore in would be wrong.

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/BuildInfo.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/jarvis/core/BuildInfo.h"
    @ONLY
)

jarvis_add_module(jarvis_core
    SOURCES
        src/Error.cpp
        src/Version.cpp
        src/ThreadPool.cpp
    HEADERS
        include/jarvis/core/Error.h
        include/jarvis/core/Result.h
        include/jarvis/core/ThreadPool.h
        include/jarvis/core/Version.h
        "${CMAKE_CURRENT_BINARY_DIR}/generated/jarvis/core/BuildInfo.h"
)

target_include_directories(jarvis_core
    PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated"
)

# $<CONFIG> resolves at build time, so this is correct for both the Ninja
# single-config and the Visual Studio multi-config generators.
target_compile_definitions(jarvis_core
    PRIVATE JARVIS_BUILD_TYPE="$<CONFIG>"
)
