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

    /// The point this panel is describing, in view pixels. Public so the view can mark it: the two
    /// numbers here mean nothing without something on the grid saying which point they are about.
    property real pointX: 0
    property real pointY: 0
    readonly property bool pointMarked: visible && !isNaN(_north) && !isNaN(_east)

    function showAt(pixelX, pixelY) {
        if (!_transform) {
            return
        }

        _north = _transform.northForPixelY(pixelY)
        _east = _transform.eastForPixelX(pixelX)
        pointX = pixelX
        pointY = pixelY
        visible = true
    }

    // Placement as bindings rather than as assignments made inside showAt().
    //
    // The two numbers at the top of this panel are set by showAt, and the width they give the panel
    // is not known until the layout has been through another pass. Working the position out on the
    // spot therefore worked it out from the size the panel had for the *previous* point clicked --
    // and from nothing at all the first time. Bound, it settles itself once the size it is being
    // placed by is real.
    x: _placeBeside(pointX, width,
                    gridView ? gridView.safeAreaLeft : 0,
                    parent ? parent.width - (gridView ? gridView.safeAreaRight : 0) : 0)
    y: _placeBeside(pointY, height,
                    gridView ? gridView.safeAreaTop : 0,
                    parent ? parent.height - (gridView ? gridView.safeAreaBottom : 0) : 0)

    /// Where to start an edge of length @a size so the panel sits beside @a point rather than under
    /// the finger that summoned it, and stays within [@a low, @a high].
    function _placeBeside(point, size, low, high) {
        const offset = ScreenTools.minTouchPixels

        // Past the point, which is where a panel opening from a tap is expected to appear
        if ((point + offset + size) <= high) {
            return point + offset
        }

        // Before it when there is no room past it. Sliding to the edge instead -- which is what this
        // did -- put the panel at the bottom of the view for any point in the lower half of it, and
        // a panel whose whole content is two numbers about one point is useless once it is nowhere
        // near the point. Flipping keeps it beside the point at every corner of the view.
        if ((point - offset - size) >= low) {
            return point - offset - size
        }

        // Neither side fits, which needs a view narrower than the panel. Clamped, and the marker the
        // view draws on the point is what still ties the two together.
        return Math.max(low, Math.min(point + offset, high - size))
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
