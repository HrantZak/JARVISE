# JARVIS.exe - composition root.
#
# This is the only place that knows about every module at once: it constructs
# them, wires them together and owns their lifetime. The modules themselves
# stay unaware of each other beyond their declared dependencies.

qt_add_executable(JARVIS
    main.cpp
    DeepSeekBackend.cpp
    DeepSeekBackend.h
    DeepSeekConnection.cpp
    DeepSeekConnection.h
    MemoryController.cpp
    MemoryController.h
    AgentLoop.cpp
    AgentLoop.h
    AiCoreModel.cpp
    AiCoreModel.h
    AudioCapture.cpp
    AudioCapture.h
    AudioPlayer.cpp
    AudioPlayer.h
    AppController.cpp
    AppController.h
    Bootstrap.cpp
    Bootstrap.h
    ConfirmationManager.cpp
    ConfirmationManager.h
    LlmController.cpp
    LlmController.h
    SystemMonitor.cpp
    SystemMonitor.h
    ToolCoordinator.cpp
    ToolCoordinator.h
    TranslationManager.cpp
    TranslationManager.h
    VoiceController.cpp
    VoiceController.h
)

target_link_libraries(JARVIS PRIVATE
    JARVIS::CompileOptions
    JARVIS::jarvis_agent
    JARVIS::jarvis_core
    JARVIS::jarvis_logging
    JARVIS::jarvis_config
    JARVIS::jarvis_i18n
    JARVIS::jarvis_llm
    JARVIS::jarvis_system
    JARVIS::jarvis_tools
    JARVIS::jarvis_voice

    Qt6::Gui
    Qt6::Network
    Advapi32
    uiautomationcore
    Qt6::Multimedia
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
    jarvis_scene3d

    # QML modules are static libraries; their generated plugin targets must be
    # linked too so the type registrations reach the executable.
    jarvis_theme
    jarvis_themeplugin
    jarvis_components
    jarvis_componentsplugin
    jarvis_pages
    jarvis_pagesplugin
    jarvis_ui
    jarvis_uiplugin
)

set_target_properties(JARVIS PROPERTIES
    WIN32_EXECUTABLE TRUE          # no console window
    OUTPUT_NAME "JARVIS"
    FOLDER "Application"
)

# Translations are wired up at the top level, after every target exists - see
# the root CMakeLists.txt.

# ---------------------------------------------------------------------------
# Deployment
# ---------------------------------------------------------------------------

# Copy the Qt runtime next to the freshly linked executable so the build output
# can be launched directly, without Qt on PATH. windeployqt only re-runs when
# the executable is actually relinked.
if(EXISTS "${QT6_INSTALL_PREFIX}/bin/windeployqt.exe")
    add_custom_command(TARGET JARVIS POST_BUILD
        COMMAND "${QT6_INSTALL_PREFIX}/bin/windeployqt.exe"
                --dir "$<TARGET_FILE_DIR:JARVIS>"
                --qmldir "${CMAKE_SOURCE_DIR}/ui"
                --no-translations
                --no-system-d3d-compiler
                --no-opengl-sw
                "$<TARGET_FILE:JARVIS>"
        COMMENT "Deploying the Qt runtime next to JARVIS.exe"
        VERBATIM
    )
else()
    message(WARNING
        "windeployqt.exe was not found under '${QT6_INSTALL_PREFIX}/bin'. "
        "JARVIS.exe will only run from a shell that has Qt on PATH.")
endif()

install(TARGETS JARVIS RUNTIME DESTINATION ".")

qt_generate_deploy_qml_app_script(
    TARGET JARVIS
    OUTPUT_SCRIPT deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR
)
install(SCRIPT "${deploy_script}")
