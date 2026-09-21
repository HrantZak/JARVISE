import QtQuick
import QtQuick.Shapes
import Jarvis.Theme

/// History graph drawn with QtQuick.Shapes.
///
/// Deliberately not a chart library: a PathPolyline is one scene-graph node,
/// renders on the GPU, and lets the fill, stroke and baseline follow the theme
/// exactly. Qt Charts would have brought QtWidgets with it and would have
/// looked like a business dashboard.
Item {
    id: graph

    /// Values in 0..1, oldest first.
    property var values: []

    /// Number of slots the graph is scaled to, so a partly-filled history
    /// grows in from the right instead of stretching.
    property int capacity: 60

    property color lineColor: Theme.accent
    property real lineWidth: 1.5
    property bool filled: true
    property bool showBaseline: true

    implicitHeight: 44

    readonly property int count: values ? values.length : 0

    function _points() {
        const result = []
        if (count < 2 || width <= 0 || height <= 0) {
            return result
        }

        const step = width / Math.max(1, capacity - 1)
        const firstIndex = capacity - count
        const usable = height - lineWidth

        for (let i = 0; i < count; ++i) {
            const value = Math.max(0, Math.min(1, values[i]))
            result.push(Qt.point((firstIndex + i) * step,
                                 height - lineWidth / 2 - value * usable))
        }
        return result
    }

    // Baseline and mid rule: the graph reads as an instrument, not a drawing.
    Rectangle {
        visible: graph.showBaseline
        anchors.bottom: parent.bottom
        width: parent.width
        height: 1
        color: Theme.hairline
    }

    Rectangle {
        visible: graph.showBaseline
        anchors.verticalCenter: parent.verticalCenter
        width: parent.width
        height: 1
        color: Theme.alpha(graph.lineColor, 0.07)
    }

    Shape {
        id: shape
        anchors.fill: parent
        preferredRendererType: Shape.CurveRenderer
        antialiasing: true
        visible: graph.count >= 2

        // Filled area under the trace.
        ShapePath {
            strokeColor: "transparent"
            fillGradient: LinearGradient {
                x1: 0; y1: 0
                x2: 0; y2: graph.height
                GradientStop { position: 0.0; color: Theme.alpha(graph.lineColor, 0.22) }
                GradientStop { position: 1.0; color: "transparent" }
            }

            PathPolyline {
                id: area
                path: {
                    const points = graph._points()
                    if (points.length < 2 || !graph.filled) {
                        return []
                    }
                    // Close the polygon along the bottom edge.
                    return points
                        .concat([Qt.point(points[points.length - 1].x, graph.height),
                                 Qt.point(points[0].x, graph.height)])
                }
            }
        }

        // The trace itself.
        ShapePath {
            strokeColor: graph.lineColor
            strokeWidth: graph.lineWidth
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin

            PathPolyline {
                path: graph._points()
            }
        }
    }

    // Head marker: where the newest reading sits.
    Rectangle {
        visible: graph.count >= 2
        width: 3
        height: 3
        radius: 1.5
        color: graph.lineColor

        readonly property var head: {
            const points = graph._points()
            return points.length > 0 ? points[points.length - 1] : null
        }

        x: head ? head.x - width / 2 : 0
        y: head ? head.y - height / 2 : 0
    }

    Text {
        visible: graph.count < 2
        anchors.centerIn: parent
        text: qsTr("COLLECTING")
        color: Theme.textDim
        font.family: Theme.monoFamily
        font.pixelSize: Theme.fontNano
        font.letterSpacing: Theme.trackingLabel
    }
}
