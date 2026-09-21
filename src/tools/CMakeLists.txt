# jarvis_tools - the finite, reviewed set of things JARVIS can do.
#
# This module is the security boundary between the language model and the
# machine. It links Qt6::Core for QJsonDocument only - a reviewed JSON parser
# rather than a hand-rolled one on hostile input - and nothing else from Qt.
# There is no process-spawning facility here beyond the one allowlisted shell
# verb in OpenApplicationTool, and no code path that takes a command string.

jarvis_add_module(jarvis_tools
    SOURCES
        src/AuditLog.cpp
        src/Confirmation.cpp
        src/PermissionManager.cpp
        src/SystemTools.cpp
        src/ShellTool.cpp
        src/CreateArtifactTool.cpp
        src/NetworkDiscoveryTool.cpp
        src/ScreenAnalysisTool.cpp
        src/ToolExecutor.cpp
        src/ToolRegistry.cpp
        src/ToolTypes.cpp
        src/ToolValidator.cpp
    HEADERS
        include/jarvis/tools/AuditLog.h
        include/jarvis/tools/Confirmation.h
        include/jarvis/tools/ITool.h
        include/jarvis/tools/PermissionManager.h
        include/jarvis/tools/SystemTools.h
        include/jarvis/tools/ShellTool.h
        include/jarvis/tools/CreateArtifactTool.h
        include/jarvis/tools/NetworkDiscoveryTool.h
        include/jarvis/tools/ScreenAnalysisTool.h
        include/jarvis/tools/ToolExecutor.h
        include/jarvis/tools/ToolRegistry.h
        include/jarvis/tools/ToolTypes.h
        include/jarvis/tools/ToolValidator.h
    PUBLIC
        JARVIS::jarvis_core
        JARVIS::jarvis_system
    PRIVATE
        Qt6::Core
        Qt6::Gui
        shell32
        psapi
        iphlpapi
        Ws2_32
        windowsapp
)

# C++/WinRT ships with the Windows SDK rather than Qt. Keep the include path
# explicit when Visual Studio exposes the SDK environment variables.
if(DEFINED ENV{WindowsSdkDir} AND DEFINED ENV{WindowsSDKVersion})
    string(REGEX REPLACE "[/\\\\]+$" "" JARVIS_WINDOWS_SDK_ROOT "$ENV{WindowsSdkDir}")
    string(REGEX REPLACE "[/\\\\]+$" "" JARVIS_WINDOWS_SDK_VERSION "$ENV{WindowsSDKVersion}")
    target_include_directories(jarvis_tools PRIVATE
        "${JARVIS_WINDOWS_SDK_ROOT}/Include/${JARVIS_WINDOWS_SDK_VERSION}/cppwinrt")
endif()
