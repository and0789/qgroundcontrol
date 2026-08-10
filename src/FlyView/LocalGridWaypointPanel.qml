import QtQuick
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls
import QGroundControl.FlyView

/// The frame around one waypoint's fields: which item it is, and the two things that can be done to
/// it from here.
///
/// The fields themselves live in LocalGridWaypointEditor, which knows nothing about being a floating
/// panel. Everything here is what makes it one -- a title naming the item, a border to lift it off
/// the grid, and a way to dismiss it.
Rectangle {
    id: _root

    property var gridView: null

    /// Index into the mission's visual items, or -1 when nothing is selected
    property int  visualItemIndex: -1
    property int  sequenceNumber:  0
    property real north:           NaN
    property real east:            NaN

    signal deleteRequested()
    signal closeRequested()

    visible:        visualItemIndex >= 0
    implicitWidth:  layout.implicitWidth + (_margins * 2)
    implicitHeight: layout.implicitHeight + (_margins * 2)
    color:          qgcPal.window
    radius:         ScreenTools.defaultFontPixelHeight / 4
    border.color:   qgcPal.text
    border.width:   1

    property real _margins: ScreenTools.defaultFontPixelHeight / 2

    QGCPalette { id: qgcPal; colorGroupEnabled: enabled }

    ColumnLayout {
        id:                 layout
        anchors.margins:    _root._margins
        anchors.left:       parent.left
        anchors.top:        parent.top
        spacing:            ScreenTools.defaultFontPixelHeight / 5

        QGCLabel {
            font.bold:  true
            text:       qsTr("Item %1").arg(_root.sequenceNumber)
        }

        LocalGridWaypointEditor {
            id:                 editor
            Layout.fillWidth:   true
            gridView:           _root.gridView
            visualItemIndex:    _root.visualItemIndex
            north:              _root.north
            east:               _root.east
        }

        RowLayout {
            Layout.fillWidth:   true
            Layout.topMargin:   ScreenTools.defaultFontPixelHeight / 4
            spacing:            ScreenTools.defaultFontPixelWidth

            QGCButton {
                objectName:         "localGrid_deleteWaypointButton"
                Layout.fillWidth:   true
                text:               qsTr("Delete")
                onClicked:          _root.deleteRequested()
            }

            QGCButton {
                Layout.fillWidth:   true
                text:               qsTr("Close")
                onClicked:          _root.closeRequested()
            }
        }
    }
}
