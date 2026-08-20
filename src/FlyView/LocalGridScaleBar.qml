import QtQuick
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls

/// States how long a piece of the grid is on the ground.
///
/// Without it the grid is a picture of squares with no size: zoomed in far enough, a 2 m wobble and
/// a 200 m transit look identical.
Item {
    id: _root

    objectName: "localGrid_scaleBar"

    property var gridTransform: null

    /// Longest the bar is allowed to get before the length it represents is rounded down
    property real maximumBarWidth: ScreenTools.defaultFontPixelWidth * 20

    implicitWidth:  layout.implicitWidth
    implicitHeight: layout.implicitHeight

    QGCPalette { id: qgcPal; colorGroupEnabled: enabled }

    /// The bar shows a round number of ground units and takes whatever width that needs, rather than
    /// a round number of pixels representing an unreadable distance.
    readonly property real _barMetres: gridTransform ? gridTransform.gridStepMetres(maximumBarWidth) : 0
    readonly property real _barWidth:  (gridTransform && (gridTransform.metresPerPixel > 0))
                                            ? (_barMetres / gridTransform.metresPerPixel)
                                            : 0

    ColumnLayout {
        id:         layout
        spacing:    ScreenTools.defaultFontPixelHeight / 6

        QGCLabel {
            Layout.alignment:   Qt.AlignHCenter
            font.pointSize:     ScreenTools.smallFontPointSize
            text:               _root.gridTransform
                                    ? _root.gridTransform.toDisplay(_root._barMetres).toFixed(0) + " " + _root.gridTransform.displayUnits
                                    : ""
        }

        Rectangle {
            Layout.preferredWidth:  Math.max(_root._barWidth, 1)
            Layout.preferredHeight: ScreenTools.defaultFontPixelHeight / 4
            color:                  "transparent"
            border.color:           qgcPal.text
            border.width:           1

            // Ticked into halves, so a distance can be stepped off the bar rather than only compared
            // against its whole length
            Rectangle {
                anchors.left:   parent.left
                anchors.top:    parent.top
                anchors.bottom: parent.bottom
                width:          parent.width / 2
                color:          qgcPal.text
                opacity:        0.5
            }
        }
    }
}
