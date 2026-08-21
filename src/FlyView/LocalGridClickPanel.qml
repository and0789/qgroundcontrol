import QtQuick
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls

/// Where a point on the local grid is, and whether the aircraft is standing on it.
///
/// A position panel, not a mission one. Placing items used to live here too, which meant a plain tap
/// on the grid opened a menu of mission choices -- and it was that path, not the plan tool strip,
/// that an operator met when the order of a plan came out wrong. Items are placed from the tool
/// strip's Plan mode now: arm Waypoint or ROI there and tap the grid.
///
/// What is left is the thing nothing else offers. The origin marker can say "the aircraft is here"
/// about the origin, and the readout can set an origin, but only this panel can say it about an
/// arbitrary point -- which is the whole drift-correction workflow: mark a spot on the ground, stand
/// the aircraft on it, tap that spot, read back the same two numbers, and only then commit.
Rectangle {
    id: _root

    objectName: "localGrid_clickPanel"

    property var gridView: null

    /// Raised when the operator asks to set an origin from here, so the view that owns this panel
    /// decides how the dialog is shown rather than this panel reaching out to build one
    signal setOriginRequested()

    /// Raised when the operator says the vehicle is standing on the point clicked, in metres north
    /// and east of the origin
    signal correctPositionRequested(real north, real east)

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

        // Kept inside the view, and clear of whatever chrome is anchored to its edges -- the tool
        // strip in the top-left corner, the flight controls that share the bottom on a phone -- so a
        // click near a corner does not open a panel under a button it then covers, or that cannot be
        // reached past to dismiss it.
        const left   = gridView ? gridView.safeAreaLeft   : 0
        const top    = gridView ? gridView.safeAreaTop    : 0
        const right  = parent.width  - (gridView ? gridView.safeAreaRight  : 0)
        const bottom = parent.height - (gridView ? gridView.safeAreaBottom : 0)

        // Set clear of the point that was touched rather than starting at it. The panel used to open
        // with its top-left corner exactly under the finger that summoned it, which on a phone means
        // it opens underneath the hand still resting there -- and the two numbers at the top of it,
        // the whole reason this panel exists, are the part the fingertip covers. Offset by a touch
        // target down and to the right, so the point stays visible beside the panel describing it.
        //
        // The clamps below still win at the edges: pushed past the right or bottom margin the panel
        // comes back inside, which puts it above or left of the touch instead. Either way it is not
        // under the finger.
        const offset = ScreenTools.minTouchPixels

        x = Math.max(left, Math.min(pixelX + offset, right - width))
        y = Math.max(top, Math.min(pixelY + offset, bottom - height))
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

        // Not a waypoint but a statement about where the aircraft already is, which is why it sits
        // apart from the three above. The offsets shown at the top of this panel are exactly what
        // makes it usable: the operator marks a spot, stands the aircraft on it, clicks that spot on
        // the grid and reads back the same two numbers before committing to them.
        QGCButton {
            objectName:         "localGrid_correctPositionButton"
            Layout.fillWidth:   true
            Layout.topMargin:   ScreenTools.defaultFontPixelHeight / 4
            visible:            _root.gridView ? _root.gridView.originKnown : false
            text:               qsTr("Vehicle is here…")
            onClicked: {
                _root.visible = false
                _root.correctPositionRequested(_root._north, _root._east)
            }
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
