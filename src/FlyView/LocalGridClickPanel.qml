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

    readonly property var  _missionController: gridView ? gridView.missionController : null
    readonly property bool _canPlace:      gridView ? gridView.canPlaceWaypoints : false
    // Compared against true rather than taken as-is: a controller that does not carry these answers
    // undefined, which is not a bool and would be refused with a warning on every rebuild
    readonly property bool _takeoffValid:  _missionController ? (_missionController.isInsertTakeoffValid === true) : false
    readonly property bool _landValid:     _missionController ? (_missionController.isInsertLandValid === true) : false
    readonly property bool _isMultiRotor:  (gridView && gridView.vehicle) ? gridView.vehicle.multiRotor : false

    readonly property bool _syncing: gridView ? gridView.planSyncInProgress : false

    function _add(kind) {
        if (gridView) {
            gridView.addMissionItemAt(kind, _north, _east)
        }
        visible = false
    }

    /// Why nothing can be placed right now. Left unsaid, the transfer case is the one that costs a
    /// flight: the buttons go dead for a second or two in the middle of building a plan, and an
    /// operator who reads that as a stuck click keeps working -- into a list the vehicle's reply is
    /// about to overwrite.
    function _cannotPlaceReason() {
        if (_syncing) {
            return qsTr("The plan is being transferred. Anything placed now would be overwritten by the vehicle's copy when it finishes.")
        }
        if (gridView && !gridView.originKnown) {
            return qsTr("The vehicle has no estimator origin, so this grid is not anchored to anything a mission can be stored against.")
        }
        return qsTr("No plan is loaded.")
    }

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

        // The same three the Plan view's insert strip offers. A plan needs more than waypoints to
        // fly itself, and having to leave the grid for the takeoff was the point at which building
        // a mission here stopped being possible.
        QGCButton {
            Layout.fillWidth:   true
            text:               qsTr("Add waypoint")
            enabled:            _root._canPlace
            onClicked:          _root._add("waypoint")
        }

        // Named for where it lands rather than for where it was clicked. A multirotor climbs in
        // place whatever coordinate is uploaded with NAV_TAKEOFF, so the takeoff goes on the origin
        // -- and the operator should read that off the button rather than discover it afterwards.
        QGCButton {
            Layout.fillWidth:   true
            text:               qsTr("Add takeoff at origin")
            enabled:            _root._canPlace && _root._takeoffValid
            onClicked:          _root._add("takeoff")
        }

        QGCButton {
            Layout.fillWidth:   true
            // A multirotor's landing item is a return to launch, which is what the Plan view
            // inserts here and what it calls it
            text:               _root._isMultiRotor ? qsTr("Add return") : qsTr("Add landing")
            enabled:            _root._canPlace && _root._landValid
            onClicked:          _root._add("land")
        }

        // Only where the button above does not already mean this. On a multirotor that button
        // returns the aircraft to launch, which is the wrong ending for a pattern meant to finish at
        // its far corner -- and QGC offers no other way to say it.
        QGCButton {
            Layout.fillWidth:   true
            visible:            _root._isMultiRotor
            text:               qsTr("Land here")
            enabled:            _root._canPlace
            onClicked:          _root._add("landHere")
        }

        // Shown rather than left as a dead button, and saying which of the three reasons it is.
        QGCLabel {
            Layout.maximumWidth:    ScreenTools.defaultFontPixelWidth * 24
            visible:                !_root._canPlace
            wrapMode:               Text.WordWrap
            font.pointSize:         ScreenTools.smallFontPointSize
            color:                  qgcPal.colorOrange
            text:                   _root._cannotPlaceReason()
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
