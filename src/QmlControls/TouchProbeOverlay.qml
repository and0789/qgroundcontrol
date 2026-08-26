import QtQuick

import QGroundControl
import QGroundControl.Controls

/// DIAGNOSTIC ONLY -- not part of the shipping UI.
///
/// Reads out what Qt actually receives when the screen is touched, so a device whose taps go
/// nowhere can be looked at rather than reasoned about. Every guess about this failure has been
/// wrong so far because the evidence stopped at "it does not work"; this puts the events on screen.
///
/// The panel is deliberately dumb: it takes no input of its own, so it cannot be the thing that
/// swallows the touch it is there to observe.
Item {
    id: root

    anchors.fill: parent
    z: 100000

    // Nothing here may accept a pointer event, or the probe becomes the bug
    enabled: false

    Component.onCompleted: ScreenToolsController.startPointerProbe()

    Rectangle {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: 4
        width: Math.min(parent.width - 8, ScreenTools.defaultFontPixelWidth * 46)
        height: column.height + 8
        color: "#d0000000"
        border.color: "#ff4444"
        border.width: 1
        radius: 3

        Column {
            id: column
            x: 4
            y: 4
            width: parent.width - 8
            spacing: 2

            Text {
                width: parent.width
                text: "TOUCH PROBE — devices Qt can see:"
                color: "#ff8888"
                font.family: ScreenTools.fixedFontFamily
                font.pointSize: ScreenTools.smallFontPointSize
                wrapMode: Text.Wrap
            }

            Text {
                width: parent.width
                text: ScreenToolsController.inputDeviceSummary()
                color: "#ffff88"
                font.family: ScreenTools.fixedFontFamily
                font.pointSize: ScreenTools.smallFontPointSize
                wrapMode: Text.Wrap
            }

            Text {
                width: parent.width
                text: "isMobile=" + ScreenTools.isMobile + "  hasTouch=" + ScreenToolsController.hasTouch
                color: "#ff8888"
                font.family: ScreenTools.fixedFontFamily
                font.pointSize: ScreenTools.smallFontPointSize
            }

            Text {
                width: parent.width
                text: "events (newest first):"
                color: "#ff8888"
                font.family: ScreenTools.fixedFontFamily
                font.pointSize: ScreenTools.smallFontPointSize
            }

            Text {
                objectName: "touchProbe_log"
                width: parent.width
                text: ScreenToolsController.probeLog === "" ? "(nothing yet)" : ScreenToolsController.probeLog
                color: "#88ff88"
                font.family: ScreenTools.fixedFontFamily
                font.pointSize: ScreenTools.smallFontPointSize
                wrapMode: Text.Wrap
            }
        }
    }
}
