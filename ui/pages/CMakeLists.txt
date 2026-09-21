# Jarvis.Pages - the eleven destinations.
#
# Pages may read the application singletons (App, Core, Sys); reusable
# components in Jarvis.Components may not.

qt_add_library(jarvis_pages STATIC)

qt_add_qml_module(jarvis_pages
    URI Jarvis.Pages
    VERSION 1.0
    QML_FILES
        AgentPage.qml
        AppsPage.qml
        AutomationPage.qml
        ConversationsPage.qml
        FilesPage.qml
        HomePage.qml
        MemoryPage.qml
        ModelsPage.qml
        PlaceholderPage.qml
        SecurityPage.qml
        SettingsPage.qml
        SystemPage.qml
        ToolsPage.qml
        VoicePage.qml
        ScenePage.qml
        DrawingView.qml
)

target_link_libraries(jarvis_pages PRIVATE
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Quick3D
    jarvis_themeplugin
    jarvis_componentsplugin
)

set_target_properties(jarvis_pages PROPERTIES FOLDER "UI")
