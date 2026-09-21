# Jarvis.Theme - design tokens, exposed to QML as the `Theme` singleton.

qt_add_library(jarvis_theme STATIC)

# Must be set before qt_add_qml_module(), which reads it when generating qmldir.
set_source_files_properties(Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(jarvis_theme
    URI Jarvis.Theme
    VERSION 1.0
    QML_FILES
        Theme.qml
)

target_link_libraries(jarvis_theme PRIVATE Qt6::Quick)

set_target_properties(jarvis_theme PROPERTIES FOLDER "UI")
