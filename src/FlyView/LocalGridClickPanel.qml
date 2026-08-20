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

    readonly property var  _missionController: gridView ? gridView.missionController : null
    readonly property bool _canPlace:      gridView ? gridView.canPlaceWaypoints : false
    // Compared against true rather than taken as-is: a controller that does not carry these answers
    // undefined, which is not a bool and would be refused with a warning on every rebuild
    readonly property bool _takeoffValid:  _missionController ? (_missionController.isInsertTakeoffValid === true) : false
    readonly property bool _isMultiRotor:  (gridView && gridView.vehicle) ? gridView.vehicle.multiRotor : false

    readonly property bool _planIsEmpty:    gridView ? gridView.planIsEmpty : false
    readonly property bool _planHasTakeoff: gridView ? gridView.planHasTakeoff : false

    /// MissionController answers this against the end of the plan, which is where the grid inserts.
    /// On an empty plan it says no, because a mission has to begin with a takeoff -- but the grid
    /// puts one on the front of an empty plan itself, so the landing asked for here is not the
    /// mission-without-a-takeoff that is being refused.
    readonly property bool _landValid: _missionController
                                        ? ((_missionController.isInsertLandValid === true) || _planIsEmpty)
                                        : false

    readonly property bool _syncing: gridView ? gridView.planSyncInProgress : false

    /// On a multirotor the button below inserts a return to launch, and a return climbs to RTL_ALT
    /// before it starts home. That altitude is a parameter, not part of the plan, so the ceiling the
    /// grid holds every waypoint under cannot reach it: this is the one item an operator can add
    /// that leaves the rangefinder's range with nothing able to clamp it. Refused rather than
    /// warned about, because above that range the estimator has no height source and optical flow
    /// has no height to scale a velocity with -- the aircraft loses both at once, out of reach.
    ///
    /// Multirotor only. On a fixed wing the same button inserts a real landing pattern, which flies
    /// its own altitudes and has nothing to do with RTL_ALT.
    readonly property bool _returnAboveCeiling: _isMultiRotor && gridView
                                                    ? (gridView.returnAltitudeAboveCeiling === true)
                                                    : false

    /// Why a takeoff cannot be added, or empty while one can be.
    ///
    /// The button went dead with nothing said, and an operator cannot tell a refusal from a stuck
    /// click. That is the state that had them closing QGC to get the option back, when the plan
    /// mirrored from the vehicle was simply still carrying the takeoff of the flight before.
    function _cannotAddTakeoffReason() {
        if (_takeoffValid) {
            return ""
        }
        if (_planHasTakeoff) {
            return qsTr("This plan already begins with a takeoff.")
        }
        return qsTr("A takeoff can only be the first item, and this plan already holds others. Clear the plan to start a new pattern.")
    }

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

        // Which of the two reasons it is, rather than a grey button and no way to find out
        QGCLabel {
            objectName:             "localGrid_takeoffRefusedReason"
            Layout.maximumWidth:    ScreenTools.defaultFontPixelWidth * 24
            visible:                _root._canPlace && !_root._takeoffValid
            wrapMode:               Text.WordWrap
            font.pointSize:         ScreenTools.smallFontPointSize
            color:                  qgcPal.colorGrey
            text:                   _root._cannotAddTakeoffReason()
        }

        QGCButton {
            Layout.fillWidth:   true
            // A multirotor's landing item is a return to launch, which is what the Plan view
            // inserts here and what it calls it
            text:               _root._isMultiRotor ? qsTr("Add return") : qsTr("Add landing")
            enabled:            _root._canPlace && _root._landValid && !_root._returnAboveCeiling
            onClicked:          _root._add("land")
        }

        // Says which two numbers disagree and which one to change, because the operator cannot see
        // either of them from here and the button would otherwise just be dead
        QGCLabel {
            objectName:             "localGrid_returnRefusedReason"
            Layout.maximumWidth:    ScreenTools.defaultFontPixelWidth * 24
            visible:                _root._canPlace && _root._returnAboveCeiling
            wrapMode:               Text.WordWrap
            font.pointSize:         ScreenTools.smallFontPointSize
            color:                  qgcPal.colorOrange
            text:                   qsTr("A return climbs to %1 first, and the rangefinder only reaches %2. Lower RTL_ALT below that, or finish the plan with 'Land here'.")
                                        .arg(_root._distanceText(_root.gridView ? _root.gridView.returnAltitudeMetres : NaN))
                                        .arg(_root._distanceText(_root.gridView ? _root.gridView.altitudeLimitMetres : NaN))
        }

        // Only where the button above does not already mean this. On a multirotor that button
        // returns the aircraft to launch, which is the wrong ending for a pattern meant to finish at
        // its far corner -- and QGC offers no other way to say it.
        //
        // Gated on canInsertLandHere rather than _canPlace alone: this button builds its landing by
        // hand rather than through insertLandItem, so it never picks up isInsertLandValid's own
        // refusal on its own -- and without one, it would happily insert a landing in the middle of
        // a pattern with legs after it that the aircraft would never fly.
        QGCButton {
            Layout.fillWidth:   true
            visible:            _root._isMultiRotor
            text:               qsTr("Land here")
            enabled:            _root.gridView ? _root.gridView.canInsertLandHere : false
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
