import QtQuick
import Jarvis.Theme
import Jarvis.Components
import Jarvis.App

/// Settings that exist.
///
/// Only options the running build actually honours are shown. Sections for AI,
/// voice and permissions appear in the phase that implements them, so this page
/// never offers a control that does nothing.
Item {
    id: page

    PageScaffold {
        anchors.fill: parent
        title: qsTr("SETTINGS")
        subtitle: qsTr("Every option here is written to config.json and takes effect as described.")
        statusText: qsTr("ACTIVE")
        statusLevel: StatusChip.Level.Ready

        // --- Language --------------------------------------------------------

        GlassPanel {
            width: parent.width
            height: languageColumn.implicitHeight + Theme.spacingLg * 2
            title: qsTr("LANGUAGE")

            Column {
                id: languageColumn
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: Theme.spacingLg
                spacing: Theme.spacingMd

                OptionSelector {
                    width: parent.width
                    label: qsTr("Interface and reply language")
                    description: qsTr("Applied immediately, without restarting. The assistant will also answer in this language by default, while still understanding the others.")
                    options: App.languageOptions
                    current: App.language
                    uppercasePlainOptions: false
                    onSelected: function(code) { App.setLanguage(code) }
                }

                Text {
                    width: parent.width
                    text: qsTr("Application names, file names, paths and commands are never translated.")
                    color: Theme.textDim
                    wrapMode: Text.WordWrap
                    font.family: Theme.bodyFamily
                    font.pixelSize: Theme.fontMicro
                }
            }
        }

        // --- Diagnostics ---------------------------------------------------

        GlassPanel {
            width: parent.width
            height: loggingColumn.implicitHeight + Theme.spacingLg * 2
            title: qsTr("LOGGING")

            Column {
                id: loggingColumn
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: Theme.spacingLg
                spacing: Theme.spacingLg

                OptionSelector {
                    width: parent.width
                    label: qsTr("Log level")
                    description: qsTr("Applied to the running logger immediately and saved to config.json.")
                    options: App.logLevelOptions
                    current: App.logLevel
                    onSelected: function(value) { App.setLogLevel(value) }
                }

                ToggleSwitch {
                    width: parent.width
                    label: qsTr("Write log to console")
                    description: qsTr("Takes effect on the next start: sinks are attached during bootstrap.")
                    checked: App.logToConsole
                    onToggled: function(value) { App.setLogToConsole(value) }
                }

                InfoRow {
                    width: parent.width
                    label: qsTr("RETENTION")
                    value: qsTr("%1 days").arg(App.logRetentionDays)
                }

                InfoRow {
                    width: parent.width
                    label: qsTr("LOG FILE")
                    value: App.logFileReady ? App.logFilePath
                                            : qsTr("unavailable - file logging could not start")
                    valueColor: App.logFileReady ? Theme.textPrimary : Theme.warning
                }
            }
        }

        // --- Window --------------------------------------------------------

        GlassPanel {
            width: parent.width
            height: windowColumn.implicitHeight + Theme.spacingLg * 2
            title: qsTr("WINDOW")

            Column {
                id: windowColumn
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: Theme.spacingLg
                spacing: Theme.spacingLg

                ToggleSwitch {
                    width: parent.width
                    label: qsTr("Remember window size and position")
                    description: qsTr("Saved when the window closes, restored on the next start.")
                    checked: App.rememberGeometry
                    onToggled: function(value) { App.setRememberGeometry(value) }
                }
            }
        }

        // --- Core state preview --------------------------------------------

        GlassPanel {
            width: parent.width
            height: previewColumn.implicitHeight + Theme.spacingLg * 2
            title: qsTr("AI CORE - VISUAL PREVIEW")
            trailingLabel: Core.previewing ? qsTr("PREVIEW ACTIVE") : qsTr("OFF")
            trailingColor: Core.previewing ? Theme.warning : Theme.textDim
            accentColor: Core.previewing ? Theme.warning : Theme.accent
            highlighted: Core.previewing

            Column {
                id: previewColumn
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: Theme.spacingLg
                spacing: Theme.spacingMd

                Text {
                    width: parent.width
                    text: qsTr("A development tool. It forces the Core's visual state so the interface can be inspected before the engines exist. It does not start, stop or simulate any engine, and the Core shows a PREVIEW badge the whole time it is active.")
                    color: Theme.textDim
                    wrapMode: Text.WordWrap
                    font.family: Theme.bodyFamily
                    font.pixelSize: Theme.fontSmall
                }

                Flow {
                    width: parent.width
                    spacing: Theme.spacingSm

                    Repeater {
                        // `key` is the untranslated state identifier: it drives
                        // both the comparison and the preview, so there is no
                        // numeric index here to fall out of step with the enum.
                        model: [
                            { key: "IDLE",       label: qsTr("IDLE") },
                            { key: "LISTENING",  label: qsTr("LISTENING") },
                            { key: "THINKING",   label: qsTr("THINKING") },
                            { key: "PLANNING",   label: qsTr("PLANNING") },
                            { key: "EXECUTING",  label: qsTr("EXECUTING") },
                            { key: "CONFIRMING", label: qsTr("CONFIRMING") },
                            { key: "RECOVERING", label: qsTr("RECOVERING") },
                            { key: "SPEAKING",   label: qsTr("SPEAKING") },
                            { key: "WARNING",    label: qsTr("WARNING") },
                            { key: "ERROR",      label: qsTr("ERROR") },
                            { key: "OFFLINE",    label: qsTr("OFFLINE") }
                        ]

                        delegate: HudButton {
                            id: stateButton
                            required property var modelData

                            text: modelData.label
                            primary: Core.previewing && Core.stateKey === modelData.key
                            implicitWidth: 132
                            onClicked: Core.previewStateKey(stateButton.modelData.key)
                        }
                    }
                }

                Row {
                    spacing: Theme.spacingMd

                    HudButton {
                        text: qsTr("STOP PREVIEW")
                        enabled: Core.previewing
                        opacity: Core.previewing ? 1.0 : 0.4
                        onClicked: Core.clearPreview()
                    }

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: Core.previewing
                              ? qsTr("The engine state underneath is still OFFLINE.")
                              : qsTr("Showing the real engine state.")
                        color: Core.previewing ? Theme.warning : Theme.textDim
                        font.family: Theme.bodyFamily
                        font.pixelSize: Theme.fontMicro
                    }
                }
            }
        }

        // --- Build and storage ---------------------------------------------

        GlassPanel {
            width: parent.width
            height: buildColumn.implicitHeight + Theme.spacingLg * 2
            title: qsTr("BUILD AND STORAGE")

            Column {
                id: buildColumn
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: Theme.spacingLg
                spacing: Theme.spacingSm

                InfoRow { width: parent.width; label: qsTr("VERSION")
                          value: App.version + "  (" + App.buildType + ")" }
                InfoRow { width: parent.width; label: qsTr("COMPILER"); value: App.compiler }
                InfoRow { width: parent.width; label: qsTr("STANDARD"); value: App.cxxStandard }
                InfoRow { width: parent.width; label: qsTr("QT"); value: App.qtVersion }
                InfoRow { width: parent.width; label: qsTr("BUILT"); value: App.buildStamp }
                InfoRow { width: parent.width; label: qsTr("ROOT"); value: App.rootPath }
                InfoRow { width: parent.width; label: qsTr("CONFIG"); value: App.configFilePath }

                Item { width: 1; height: Theme.spacingSm }

                Row {
                    spacing: Theme.spacingMd

                    HudButton {
                        id: logsButton
                        text: qsTr("OPEN LOG FOLDER")
                        onClicked: failed = !App.openLogDirectory()
                    }

                    HudButton {
                        id: configButton
                        text: qsTr("OPEN CONFIG")
                        onClicked: failed = !App.openConfigFile()
                    }

                    HudButton {
                        id: copyButton
                        text: copied ? qsTr("COPIED") : qsTr("COPY DIAGNOSTICS")
                        property bool copied: false
                        onClicked: App.copyDiagnostics()

                        Connections {
                            target: App
                            function onDiagnosticsCopied() {
                                copyButton.copied = true
                                copyResetTimer.restart()
                            }
                        }

                        Timer {
                            id: copyResetTimer
                            interval: 1600
                            onTriggered: copyButton.copied = false
                        }
                    }
                }
            }
        }
    }
}
