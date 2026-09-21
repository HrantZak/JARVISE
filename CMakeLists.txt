cmake_minimum_required(VERSION 3.25)

project(JARVIS
    VERSION 0.1.0
    DESCRIPTION "JARVIS - private, local-first AI desktop assistant for Windows"
    HOMEPAGE_URL "https://github.com/local/jarvis"
    LANGUAGES CXX
)

# ---------------------------------------------------------------------------
# Global settings
# ---------------------------------------------------------------------------

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

if(NOT CMAKE_RUNTIME_OUTPUT_DIRECTORY)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
endif()

# This project targets Windows + MSVC only (see docs/BUILD.md).
# MinGW is explicitly out of scope: the CUDA path and several Win32 engines
# planned for later phases require the MSVC toolchain.
if(NOT WIN32)
    message(FATAL_ERROR "JARVIS targets Windows only.")
endif()

if(NOT MSVC)
    message(FATAL_ERROR
        "JARVIS requires the MSVC toolchain. Detected: '${CMAKE_CXX_COMPILER_ID}'. "
        "MinGW/Clang builds are not supported.")
endif()

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------

option(JARVIS_BUILD_TESTS "Build unit tests" ON)
option(JARVIS_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)

# ---------------------------------------------------------------------------
# Shared compile settings, consumed by every JARVIS target
# ---------------------------------------------------------------------------

add_library(jarvis_compile_options INTERFACE)
add_library(JARVIS::CompileOptions ALIAS jarvis_compile_options)

target_compile_options(jarvis_compile_options INTERFACE
    /W4                 # high warning level
    /permissive-        # strict standard conformance
    /utf-8              # source and execution charset
    /Zc:__cplusplus     # report the real __cplusplus value
    /Zc:preprocessor    # conforming preprocessor
    /EHsc               # standard C++ exception model
    /MP                 # parallel compilation (no-op under Ninja)
    /wd4251             # DLL-interface warning: not relevant, we build statics
)

if(JARVIS_WARNINGS_AS_ERRORS)
    target_compile_options(jarvis_compile_options INTERFACE /WX)
endif()

target_compile_definitions(jarvis_compile_options INTERFACE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    NOGDI
)

# ---------------------------------------------------------------------------
# Qt
# ---------------------------------------------------------------------------
#
# Deliberately NOT requested, and enforced by jarvis_assert_no_forbidden_qt_modules():
#   Qt6::Widgets - JARVIS is a pure Qt Quick application.
#   Qt6::Charts  - requires QApplication (QtWidgets); crashes under QGuiApplication.
#                  HUD graphs are drawn with QtQuick.Shapes instead.
#
# LinguistTools brings lupdate/lrelease and qt_add_translations(); JARVIS is a
# Russian-first application, so translation support is not optional here.
# Multimedia arrives in Phase 4: it owns the microphone and the speaker.
# It is a media framework, not a widget toolkit - the no-QtWidgets rule is
# unaffected and still enforced below.
find_package(Qt6 6.11 REQUIRED
    COMPONENTS Core Gui Network Qml Quick QuickControls2 Multimedia LinguistTools Quick3D Concurrent)

if(JARVIS_BUILD_TESTS)
    find_package(Qt6 6.11 REQUIRED COMPONENTS Test)
endif()

qt_standard_project_setup(REQUIRES 6.8)

# Every QML module must be laid out under a single root as <root>/Jarvis/<Name>,
# matching its URI. Without this each module lands next to its own CMakeLists
# and qmllint / Qt Creator cannot resolve the imports.
set(QT_QML_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/qml")

set(CMAKE_AUTOMOC ON)

include(cmake/JarvisHelpers.cmake)

# ---------------------------------------------------------------------------
# Modules
# ---------------------------------------------------------------------------

add_subdirectory(third_party)

add_subdirectory(src/core)
add_subdirectory(src/logging)
add_subdirectory(src/i18n)
add_subdirectory(src/config)
add_subdirectory(src/system)
add_subdirectory(src/llm)
add_subdirectory(src/voice)
add_subdirectory(src/tools)
add_subdirectory(src/agent)
add_subdirectory(src/scene3d)
add_subdirectory(ui)
add_subdirectory(src/app)

if(JARVIS_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ---------------------------------------------------------------------------
# Translations
# ---------------------------------------------------------------------------
#
# Declared here rather than in src/app because the catalogues have to be
# embedded in the test binary too - tst_i18n loads them from :/i18n, which is
# the same path the application uses, so the test exercises the real mechanism
# rather than a copy on disk.
#
# SOURCE_TARGETS makes lupdate walk the QML modules and the C++ sources.
# Refresh the .ts files after adding or changing user-visible strings:
#
#     cmake --build build/msvc-debug --target update_translations
#     python scripts/fill_translations.py

set(JARVIS_TRANSLATION_TARGETS JARVIS)
if(JARVIS_BUILD_TESTS AND TARGET tst_i18n)
    list(APPEND JARVIS_TRANSLATION_TARGETS tst_i18n)
endif()

qt_add_translations(
    TARGETS ${JARVIS_TRANSLATION_TARGETS}
    SOURCE_TARGETS
        JARVIS               # the C++ tr() strings in AiCoreModel and SystemMonitor
        jarvis_theme
        jarvis_components
        jarvis_pages
        jarvis_ui
    TS_FILES
        "${CMAKE_SOURCE_DIR}/i18n/jarvis_ru.ts"
        "${CMAKE_SOURCE_DIR}/i18n/jarvis_en.ts"
    RESOURCE_PREFIX "/i18n"
    LUPDATE_OPTIONS -no-obsolete -locations none
)

# ---------------------------------------------------------------------------
# Constraint enforcement (runs after every target is defined)
# ---------------------------------------------------------------------------

jarvis_assert_no_forbidden_qt_modules()

# ---------------------------------------------------------------------------
# Configure summary
# ---------------------------------------------------------------------------

message(STATUS "")
message(STATUS "=== JARVIS ${PROJECT_VERSION} ===")
message(STATUS "  Compiler      : ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "  C++ standard  : ${CMAKE_CXX_STANDARD}")
message(STATUS "  Qt            : ${Qt6_VERSION} (${QT6_INSTALL_PREFIX})")
message(STATUS "  Generator     : ${CMAKE_GENERATOR}")
message(STATUS "  Build type    : ${CMAKE_BUILD_TYPE}")
message(STATUS "  Tests         : ${JARVIS_BUILD_TESTS}")
message(STATUS "  Warnings=err  : ${JARVIS_WARNINGS_AS_ERRORS}")
message(STATUS "")
