# jarvis_system - live machine telemetry.
#
# Qt-free: this reads the machine, and the Qt-side adapter that polls it lives
# in src/app. iphlpapi is a Windows system import library, not a third-party
# dependency, and NVML is loaded at run time from the installed display driver.

jarvis_add_module(jarvis_system
    SOURCES
        src/NvmlGpuMonitor.cpp
        src/WindowsSystemMetricsProvider.cpp
    HEADERS
        include/jarvis/system/ISystemMetricsProvider.h
        include/jarvis/system/NvmlGpuMonitor.h
        include/jarvis/system/SystemMetrics.h
        include/jarvis/system/WindowsSystemMetricsProvider.h
    PUBLIC
        JARVIS::jarvis_core
    PRIVATE
        iphlpapi
)
