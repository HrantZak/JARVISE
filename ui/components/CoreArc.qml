import QtQuick
import QtQuick.Shapes
import Jarvis.Theme

/// A single stroked arc of the AI Core, as a Shape.
///
/// Shapes render through the scene graph, so arcs cost a draw call rather than
/// a repaint. Every ring in the Core is one of these.
Shape {
    id: arc

    property real centerX: width / 2
    property real centerY: height / 2
    property real arcRadius: Math.min(width, height) / 2
    property real startAngle: 0
    property real sweepAngle: 360
    property color strokeColor: Theme.accent
    property real strokeWidth: 2
    property real strokeOpacity: 1.0
    property int capStyle: ShapePath.FlatCap

    preferredRendererType: Shape.CurveRenderer
    antialiasing: true
    opacity: strokeOpacity

    ShapePath {
        strokeColor: arc.strokeColor
        strokeWidth: arc.strokeWidth
        fillColor: "transparent"
        capStyle: arc.capStyle

        PathAngleArc {
            centerX: arc.centerX
            centerY: arc.centerY
            radiusX: arc.arcRadius
            radiusY: arc.arcRadius
            startAngle: arc.startAngle
            sweepAngle: arc.sweepAngle
        }
    }
}
