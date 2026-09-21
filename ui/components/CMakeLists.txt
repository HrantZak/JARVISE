# Jarvis.Components - the reusable building blocks of the JARVIS visual system.
#
# Everything here is generic: components take values through properties and know
# nothing about the application singletons. Anything that has to read App, Core
# or Sys lives in Jarvis.Ui instead, which keeps this module reusable and
# testable in isolation.

qt_add_library(jarvis_components STATIC)

qt_add_qml_module(jarvis_components
    URI Jarvis.Components
    VERSION 1.0
    QML_FILES
        AiCore.qml
        CoreArc.qml
        GlassPanel.qml
        HudButton.qml
        HudTextField.qml
        HudMetric.qml
        InfoRow.qml
        MetricReadout.qml
        NavRail.qml
        OptionSelector.qml
        PageScaffold.qml
        ParticleField.qml
        Sparkline.qml
        StatusChip.qml
        TickRing.qml
        TickScale.qml
        TitleBar.qml
        ToggleSwitch.qml
        TopTabs.qml
        VoiceWave.qml
)

target_link_libraries(jarvis_components PRIVATE
    Qt6::Quick
    Qt6::QuickControls2
)

set_target_properties(jarvis_components PROPERTIES FOLDER "UI")
