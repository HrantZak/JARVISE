import QtQuick
import QtQuick.Window
import QtQuick.Controls.Basic

import Jarvis.App
import Jarvis.Theme
import Jarvis.Components
import Jarvis.Pages
import Jarvis.Scene3D

/// The JARVIS shell: frameless window, custom chrome, status strip, top
/// navigation and the page stack.
ApplicationWindow {
    id: root

    // Geometry comes from config.json, already validated in C++.
    width: App.storedWindowWidth()
    height: App.storedWindowHeight()
    minimumWidth: 1024
    minimumHeight: 680

    visible: true
    title: qsTr("JARVIS")

    flags: Qt.Window | Qt.FramelessWindowHint

    color: Theme.backgroundBase

    // --- Navigation model -------------------------------------------------
    //
    // `pending` marks destinations whose backend does not exist yet. The top
    // navigation can render that state before a page is even opened.

    // `key` is the untranslated identifier that goes into the log; `name` is
    // what the top navigation shows and is localised.
    readonly property var destinations: [
        { key: "HOME",          name: qsTr("HOME"),          source: "qrc:/qt/qml/Jarvis/Pages/HomePage.qml",          pending: false },
        { key: "MEMORY",        name: qsTr("MEMORY"),        source: "qrc:/qt/qml/Jarvis/Pages/MemoryPage.qml",        pending: false },
        { key: "SYSTEM",        name: qsTr("SYSTEM"),        source: "qrc:/qt/qml/Jarvis/Pages/SystemPage.qml",        pending: false },
        { key: "APPS",          name: qsTr("APPS"),          source: "qrc:/qt/qml/Jarvis/Pages/AppsPage.qml",          pending: false },
        { key: "FILES",         name: qsTr("FILES"),         source: "qrc:/qt/qml/Jarvis/Pages/FilesPage.qml",         pending: false },
        { key: "AUTOMATION",    name: App.language === "ru" ? "СЦЕНАРИИ" : "ROUTINES",    source: "qrc:/qt/qml/Jarvis/Pages/AutomationPage.qml",    pending: false },
        { key: "CONVERSATIONS", name: qsTr("CONVERSATIONS"), source: "qrc:/qt/qml/Jarvis/Pages/ConversationsPage.qml", pending: false },
        { key: "MODELS",        name: "DEEPSEEK API",        source: "qrc:/qt/qml/Jarvis/Pages/ModelsPage.qml",        pending: false },
        { key: "VOICE",         name: qsTr("VOICE"),         source: "qrc:/qt/qml/Jarvis/Pages/VoicePage.qml",         pending: false },
        { key: "AGENT",         name: qsTr("AGENT"),         source: "qrc:/qt/qml/Jarvis/Pages/AgentPage.qml",         pending: false },
        { key: "TOOLS",         name: qsTr("TOOLS"),         source: "qrc:/qt/qml/Jarvis/Pages/ToolsPage.qml",         pending: false },
        { key: "SECURITY",      name: qsTr("SECURITY"),      source: "qrc:/qt/qml/Jarvis/Pages/SecurityPage.qml",      pending: false },
        { key: "SETTINGS",      name: qsTr("SETTINGS"),      source: "qrc:/qt/qml/Jarvis/Pages/SettingsPage.qml",      pending: false },
        { key: "SCENE3D",       name: "3D",                 source: "qrc:/qt/qml/Jarvis/Pages/ScenePage.qml",         pending: false }
    ]

    property int currentDestination: 0
    Connections {
        target: Scene3D
        function onShowRequested() { root.navigate(root.destinations.length-1) }
        function onFullscreenRequested() {
            if(root.visibility === Window.FullScreen) root.showNormal()
            else root.showFullScreen()
        }
    }

    function navigate(index) {
        if (index === currentDestination || index < 0 || index >= destinations.length) {
            return
        }
        currentDestination = index
        pageStack.replace(null, destinations[index].source)
        App.logInfo("ui", "navigated to " + destinations[index].key)
    }

    Component.onCompleted: {
        const storedX = App.storedWindowX()
        const storedY = App.storedWindowY()
        if (storedX >= 0 && storedY >= 0) {
            root.x = storedX
            root.y = storedY
        } else {
            root.x = Math.round((Screen.width - root.width) / 2)
            root.y = Math.round((Screen.height - root.height) / 2)
        }

        pageStack.replace(null, destinations[0].source)
        App.logInfo("ui", "shell ready at " + root.width + "x" + root.height)
    }

    onClosing: {
        if (App.remembersGeometry() && root.visibility === Window.Windowed) {
            App.saveWindowGeometry(root.width, root.height, root.x, root.y)
        }
    }

    // A minimised window has nothing to draw and nobody to inform: stop
    // sampling telemetry entirely rather than polling into the void.
    onVisibilityChanged: Sys.setActive(root.visibility !== Window.Minimized &&
                                       root.visibility !== Window.Hidden)

    // --- Background layers -------------------------------------------------

    Rectangle {
        anchors.fill: parent
        color: Theme.backgroundDeep
    }

    // Technical grid, painted once into a texture rather than assembled from
    // hundreds of Rectangles.
    Canvas {
        id: grid
        anchors.fill: parent
        visible: false

        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            ctx.strokeStyle = Qt.rgba(0.36, 0.64, 0.80, 0.035)
            ctx.lineWidth = 1

            const step = 48
            ctx.beginPath()
            for (let x = 0; x < width; x += step) {
                ctx.moveTo(x + 0.5, 0)
                ctx.lineTo(x + 0.5, height)
            }
            for (let y = 0; y < height; y += step) {
                ctx.moveTo(0, y + 0.5)
                ctx.lineTo(width, y + 0.5)
            }
            ctx.stroke()
        }

        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
    }

    // One restrained scan sweep gives the HUD a live instrument feel. It is a
    // single rectangle, paused while the window is inactive, so it costs almost
    // nothing compared with a per-cell animation.
    Rectangle {
        id: scanSweep
        width: parent.width
        height: 1
        y: -2
        color: Theme.alpha(Theme.accent, 0.10)
        visible: false
        opacity: 0.55
        SequentialAnimation on y {
            running: scanSweep.visible
            loops: Animation.Infinite
            NumberAnimation { from: -2; to: root.height + 2; duration: 9000; easing.type: Easing.Linear }
            PauseAnimation { duration: 1800 }
        }
    }

    // --- Chrome ------------------------------------------------------------

    header: Column {
        spacing: 0

        TitleBar {
            width: parent.width
            window: root
            subtitle: App.version + "  ·  " + App.buildType
            menuModel: root.destinations
            currentIndex: root.currentDestination
            onNavigated: function(index) { root.navigate(index) }
        }
    }

    // --- Body ---------------------------------------------------------------

    Item {
        anchors.fill: parent

        StackView {
            id: pageStack
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.margins: Theme.spacingLg
            clip: true

            // Pages arrive by rising slightly and fading in. Slow enough to
            // register, short enough never to be in the way.
            replaceEnter: Transition {
                ParallelAnimation {
                    NumberAnimation {
                        property: "opacity"; from: 0.0; to: 1.0
                        duration: Theme.durationSlow; easing.type: Theme.easeStandard
                    }
                    NumberAnimation {
                        property: "y"; from: 18; to: 0
                        duration: Theme.durationSlow; easing.type: Theme.easeEmphasis
                    }
                }
            }

            replaceExit: Transition {
                NumberAnimation {
                    property: "opacity"; from: 1.0; to: 0.0
                    duration: Theme.durationFast; easing.type: Easing.InQuad
                }
            }
        }
    }

    // --- Frameless window resize handles -----------------------------------
    //
    // startSystemResize() delegates to Windows so aero-snap, cursor feedback
    // and per-monitor DPI keep working.

    Repeater {
        model: [
            { edge: Qt.LeftEdge,                  cursor: Qt.SizeHorCursor },
            { edge: Qt.RightEdge,                 cursor: Qt.SizeHorCursor },
            { edge: Qt.TopEdge,                   cursor: Qt.SizeVerCursor },
            { edge: Qt.BottomEdge,                cursor: Qt.SizeVerCursor },
            { edge: Qt.LeftEdge | Qt.TopEdge,     cursor: Qt.SizeFDiagCursor },
            { edge: Qt.RightEdge | Qt.BottomEdge, cursor: Qt.SizeFDiagCursor },
            { edge: Qt.RightEdge | Qt.TopEdge,    cursor: Qt.SizeBDiagCursor },
            { edge: Qt.LeftEdge | Qt.BottomEdge,  cursor: Qt.SizeBDiagCursor }
        ]

        delegate: Item {
            id: handle
            required property var modelData

            // Not named left/right/top/bottom: QQuickItem already declares
            // those as FINAL anchor-line properties and QML refuses to shadow
            // them.
            readonly property int thickness: 6
            readonly property bool atLeft:   (modelData.edge & Qt.LeftEdge) !== 0
            readonly property bool atRight:  (modelData.edge & Qt.RightEdge) !== 0
            readonly property bool atTop:    (modelData.edge & Qt.TopEdge) !== 0
            readonly property bool atBottom: (modelData.edge & Qt.BottomEdge) !== 0
            readonly property bool corner: (atLeft || atRight) && (atTop || atBottom)

            width: corner ? thickness * 2 : (atLeft || atRight ? thickness : root.width)
            height: corner ? thickness * 2 : (atTop || atBottom ? thickness : root.height)

            x: atLeft ? 0 : (atRight ? root.width - width : 0)
            y: atTop ? 0 : (atBottom ? root.height - height : 0)

            z: corner ? 2 : 1

            visible: root.visibility === Window.Windowed

            HoverHandler {
                cursorShape: handle.modelData.cursor
            }

            TapHandler {
                gesturePolicy: TapHandler.DragThreshold
                onPressedChanged: {
                    if (pressed) {
                        root.startSystemResize(handle.modelData.edge)
                    }
                }
            }
        }
    }

    // Above everything, including the resize handles: a question about an action
    // on this machine must not be dismissible by accident.
    ConfirmationOverlay {}
}
