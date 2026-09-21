# Jarvis.Ui - the application shell.

qt_add_library(jarvis_ui STATIC)

qt_add_qml_module(jarvis_ui
    URI Jarvis.Ui
    VERSION 1.0
    QML_FILES
        ConfirmationOverlay.qml
        Main.qml
        SystemHud.qml
)

target_link_libraries(jarvis_ui PRIVATE
    Qt6::Quick
    Qt6::QuickControls2
    jarvis_themeplugin
    jarvis_componentsplugin
    jarvis_pagesplugin
)

set_target_properties(jarvis_ui PROPERTIES FOLDER "UI")
