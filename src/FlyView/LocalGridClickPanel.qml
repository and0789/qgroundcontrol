import QtQuick
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls

/// What a click on the local grid offers: the point clicked, stated in metres north and east of the
/// origin, and the option to make a waypoint of it.
///
/// The numbers are the point. A waypoint placed this way is briefed, flown and measured as an offset
/// in metres, so the operator should see the offset before committing to it rather than after.
Rectangle {
    id: _root

    property var gridView: null

    /// Raised when the operator asks to set an origin from here, so the view that owns this panel
    /// decides how the dialog is shown rather than this panel reaching out to build one
    signal setOriginRequested()

    visible:        false
    width:          layout.implicitWidth + (_margins * 2)
    height:         layout.implicitHeight + (_margins * 2)
    color:          qgcPal.window
    radius:         ScreenTools.defaultFontPixelHeight / 4
    border.color:   qgcPal.text
    border.width:   1

    property real _margins: ScreenTools.defaultFontPixelHeight / 3

    property real _north: NaN
    property real _east:  NaN

    QGCPalette { id: qgcPal; colorGroupEnabled: enabled }

    readonly property var _transform: gridView ? gridView.gridTransform : null

    function showAt(pixelX, pixelY) {
        if (!_transform) {
            return
        }

        _north = _transform.northForPixelY(pixelY)
        _east = _transform.eastForPixelX(pixelX)

        // Kept inside the view, so a click near an edge does not put the panel half off screen
        x = Math.max(0, Math.min(pixelX, parent.width - width))
        y = Math.max(0, Math.min(pixelY, parent.height - height))
        visible = true
    }

    function _distanceText(metres) {
        if (!_transform || isNaN(metres)) {
            return qsTr("--")
        }
        return _transform.toDisplay(metres).toFixed(1) + " " + _transform.displayUnits
    }

    ColumnLayout {
        id:                 layout
        anchors.margins:    _root._margins
        anchors.left:       parent.left
        anchors.top:        parent.top
        spacing:            ScreenTools.defaultFontPixelHeight / 6

        GridLayout {
            columns:        2
            columnSpacing:  ScreenTools.defaultFontPixelWidth
            rowSpacing:     0

            QGCLabel { font.pointSize: ScreenTools.smallFontPointSize; text: qsTr("North") }
            QGCLabel {
                font.pointSize:         ScreenTools.smallFontPointSize
                horizontalAlignment:    Text.AlignRight
                Layout.fillWidth:       true
                text:                   _root._distanceText(_root._north)
            }

            QGCLabel { font.pointSize: ScreenTools.smallFontPointSize; text: qsTr("East") }
            QGCLabel {
                font.pointSize:         ScreenTools.smallFontPointSize
                horizontalAlignment:    Text.AlignRight
                Layout.fillWidth:       true
                text:                   _root._distanceText(_root._east)
            }
        }

        QGCButton {
            Layout.fillWidth:   true
            text:               qsTr("Add waypoint")
            enabled:            _root.gridView ? _root.gridView.canPlaceWaypoints : false
            onClicked: {
                _root.gridView.addWaypointAt(_root._north, _root._east)
                _root.visible = false
            }
        }

        // Shown rather than left as a dead button. Without an origin there is no mapping between
        // this frame and the coordinates a mission is stored in, so a waypoint placed here would
        // upload cleanly and be flown somewhere else entirely.
        QGCLabel {
            Layout.maximumWidth:    ScreenTools.defaultFontPixelWidth * 24
            visible:                _root.gridView ? !_root.gridView.canPlaceWaypoints : false
            wrapMode:               Text.WordWrap
            font.pointSize:         ScreenTools.smallFontPointSize
            color:                  qgcPal.colorOrange
            text:                   (_root.gridView && !_root.gridView.originKnown)
                                        ? qsTr("The vehicle has no estimator origin, so this grid is not anchored to anything a mission can be stored against.")
                                        : qsTr("No plan is loaded.")
        }

        // The way out of that message. Without it the operator has to turn the grid off, find the
        // spot on a map and turn the grid back on -- and a map is the one thing that may not be
        // available where this is being flown.
        QGCButton {
            Layout.fillWidth:   true
            visible:            _root.gridView ? !_root.gridView.originKnown : false
            text:               qsTr("Set Estimator Origin…")
            onClicked: {
                _root.visible = false
                _root.setOriginRequested()
            }
        }

        QGCButton {
            Layout.fillWidth:   true
            text:               qsTr("Close")
            onClicked:          _root.visible = false
        }
    }
}
