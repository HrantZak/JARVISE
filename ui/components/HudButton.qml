import QtQuick
import QtQuick.Controls.Basic
import Jarvis.Theme

/// Flat, bracketed action button.
///
/// Every instance in JARVIS is wired to a real action; this application has no
/// decorative buttons.
Button {
    id: control

    /// Set when the action failed, to flash the frame red.
    property bool failed: false

    /// Emphasised variant for the primary action on a page.
    property bool primary: false

    implicitWidth: Math.max(126, label.implicitWidth + Theme.spacingLg * 2)
    implicitHeight: 40
    Accessible.role: Accessible.Button
    Accessible.name: control.text
    opacity: enabled ? 1.0 : 0.42
    Behavior on opacity { NumberAnimation { duration: Theme.durationFast } }

    hoverEnabled: true

    background: Rectangle {
        radius: Theme.radiusSm
        color: control.down
               ? Theme.alpha(Theme.accent, 0.16)
               : (!control.enabled ? Theme.alpha(Theme.glassStroke, 0.03)
               : (control.hovered ? Theme.alpha(Theme.accent, 0.10)
                                  : (control.primary ? Theme.alpha(Theme.accent, 0.05)
                                                     : "transparent")))
        border.width: Theme.borderWidth
        border.color: control.failed
                      ? Theme.danger
                      : (!control.enabled ? Theme.glassStroke
                      : (control.hovered || control.primary
                         ? Theme.alpha(Theme.accent, control.hovered ? 0.85 : 0.45)
                         : Theme.glassStroke))

        Behavior on border.color { ColorAnimation { duration: Theme.durationFast } }
        Behavior on color { ColorAnimation { duration: Theme.durationFast } }

        // Left-edge marker: the same bracket motif as the panels.
        Rectangle {
            anchors.left: parent.left
            anchors.leftMargin: 5
            anchors.verticalCenter: parent.verticalCenter
            width: 1
            height: control.hovered ? 14 : 8
            color: control.failed ? Theme.danger : (control.enabled ? Theme.accent : Theme.textDim)
            opacity: control.hovered || control.primary ? 0.9 : 0.35

            Behavior on height { NumberAnimation { duration: Theme.durationFast } }
            Behavior on opacity { NumberAnimation { duration: Theme.durationFast } }
        }
    }

    contentItem: Text {
        id: label
        text: control.text
        color: control.failed
               ? Theme.danger
               : (!control.enabled ? Theme.textDim
               : (control.hovered ? Theme.accentBright
                                  : (control.primary ? Theme.accent : Theme.textSecondary)))
        font.family: Theme.displayFamily
        font.pixelSize: Theme.fontMicro
        font.letterSpacing: Theme.trackingLabel
        font.weight: Font.DemiBold
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter

        Behavior on color { ColorAnimation { duration: Theme.durationFast } }
    }

    // Clear the failure flash a moment after it is set.
    Timer {
        id: resetTimer
        interval: 1800
        onTriggered: control.failed = false
    }

    onFailedChanged: if (failed) resetTimer.restart()
}
