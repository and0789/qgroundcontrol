import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

import QtPositioning

import QGroundControl
import QGroundControl.Controls

/// Tells the vehicle where it actually is, for an estimator whose frame has slid away from the ground
/// beneath it.
///
/// This is not a second origin. The origin cannot be changed once ArduPilot has one, and nothing here
/// tries to: what is sent resets the filter's own position inside the frame the origin already
/// anchors. That is the repair an aircraft navigating on optical flow needs after its estimate has
/// crept, and without it the only remedy is a power cycle -- which loses the origin, the plan, and the
/// flight.
///
/// The point stated is a point on the ground, not a correction in metres. The operator puts the
/// aircraft somewhere they can identify -- the origin it took off from, a marked corner of the grid --
/// and says that is where it is. Everything shown here is that claim written out both ways, in grid
/// metres and as the coordinate that goes on the wire, because the whole failure being repaired is a
/// position that reads plausibly and is wrong.
QGCPopupDialog {
    id:         _root
    title:      qsTr("Correct Position")
    buttons:    Dialog.Close

    property var  vehicle:    null

    /// The grid this was opened from, which holds the reported position the claim below is measured
    /// against and the plan the fallback moves
    property var  gridView:   null

    /// Where the vehicle is being told it is, on the grid the operator pointed at
    property real north:      0
    property real east:       0

    /// The same point as a global coordinate, worked out by the view that owns the grid so this
    /// dialog never has to guess at the origin the offsets were measured from
    property var  coordinate: QtPositioning.coordinate()

    /// Named for the place rather than the numbers, when there is a name for it. "the origin" is what
    /// the operator carried the aircraft back to; "3.2 m north, 1.4 m east" is not.
    property string placeName: ""

    QGCPalette { id: qgcPal; colorGroupEnabled: enabled }

    property real _fieldWidth: ScreenTools.defaultFontPixelWidth * 46

    readonly property bool _armed:          _root.vehicle ? _root.vehicle.armed : false
    readonly property bool _coordinateValid: _root.coordinate ? _root.coordinate.isValid : false

    readonly property real   _displayPerMetre: QGroundControl.unitsConversion.metersToAppSettingsHorizontalDistanceUnits(1)
    readonly property string _displayUnits:    QGroundControl.unitsConversion.appSettingsHorizontalDistanceUnitsString

    /// True from pressing send until the vehicle answers. The command is acknowledged, so there is a
    /// real gap here rather than an instant one, and a second press during it is refused by the
    /// vehicle as a duplicate.
    property bool _awaitingReply: false

    /// What came back. Empty before anything has been sent, so a dialog that has just opened does not
    /// wear the answer to somebody else's question.
    property bool   _replied:  false
    property bool   _accepted: false
    property string _reason:   ""

    /// How well the operator knows where the aircraft is, as one standard deviation.
    ///
    /// It is not a formality: the estimator weighs the correction against this, so a figure invented
    /// large enough to be safe is a figure that barely moves the position. A metre is a marked spot
    /// stepped up to by eye, which is what this is usually being done from.
    ///
    /// Read off the field rather than held in a property of its own, so the field's own text is the
    /// only copy and there is no binding writing back into what it is bound to.
    readonly property real _accuracyMetres: parseFloat(accuracyField.text)
    /// No NaN path out of this dialog even though the underlying call takes one. What firmware does
    /// with an unknown accuracy is firmware's business and varies; a number the operator can see is
    /// something they can reason about afterwards.
    readonly property bool _accuracyValid: !isNaN(_accuracyMetres) && (_accuracyMetres > 0)

    readonly property bool _canSend: (_root.vehicle ? true : false) && _coordinateValid && !_armed
                                        && _accuracyValid && !_awaitingReply && !_shifted

    // ---------------- The fallback: move the plan instead of the aircraft ----------------

    /// Where the estimator says the vehicle is, against which the claim above is a measurement of how
    /// far the frame has slid
    readonly property real _reportedNorth: _root.gridView ? _root.gridView.vehicleNorth : NaN
    readonly property real _reportedEast:  _root.gridView ? _root.gridView.vehicleEast : NaN

    /// A frozen readout is indistinguishable from a steady hover, and an offset worked out from one
    /// would move the whole plan by a number that stopped being true minutes ago
    readonly property bool _reportedUsable: _root.gridView
                                                ? (_root.gridView.positionValid && !_root.gridView.positionStale)
                                                : false

    /// How far the frame has slid: what the estimator reports, less where the vehicle actually is.
    ///
    /// The plan moves by this and not by its negative. The aircraft flies until its *reported*
    /// position reaches a waypoint, so it arrives short by exactly this offset -- adding it to every
    /// waypoint is what puts the ground track back where it was drawn.
    readonly property real _shiftNorth: _reportedNorth - _root.north
    readonly property real _shiftEast:  _reportedEast - _root.east

    readonly property bool _canShift: _reportedUsable && !isNaN(_shiftNorth) && !isNaN(_shiftEast)
                                        && (_root.gridView ? _root.gridView.canPlaceWaypoints : false)
                                        && !_accepted && !_shifted

    /// True once the plan has been moved. The two remedies are for the same drift, so doing both
    /// counts it twice and flies the pattern out by double -- each one shuts the other off.
    property bool _shifted:      false
    property int  _shiftedCount: 0

    /// Set by the button whether or not anything moved. A plan holding nothing but a takeoff is a
    /// perfectly ordinary thing to have open, and without this the button would swallow the press and
    /// say nothing at all.
    property bool _shiftAttempted: false

    function _shiftPlan() {
        if (!_canShift) {
            return
        }

        _shiftedCount = _root.gridView.offsetMission(_shiftNorth, _shiftEast)
        _shifted = _shiftedCount > 0
        _shiftAttempted = true
    }

    function _distanceText(metres) {
        if (isNaN(metres)) {
            return qsTr("--")
        }
        return (metres * _displayPerMetre).toFixed(2) + " " + _displayUnits
    }

    /// A pair of offsets as one line, each named for the direction it actually points. "2.0 m south"
    /// is a thing an operator can check against the field in front of them; "-2.0 m north" is a thing
    /// they have to decode, and a sign decoded wrong here flies the pattern out by twice the drift.
    function _offsetText(northMetres, eastMetres) {
        if (isNaN(northMetres) || isNaN(eastMetres)) {
            return qsTr("--")
        }

        const northText = qsTr("%1 %2 %3").arg(Math.abs(northMetres * _displayPerMetre).toFixed(2))
                                          .arg(_displayUnits)
                                          .arg(northMetres < 0 ? qsTr("south") : qsTr("north"))
        const eastText = qsTr("%1 %2 %3").arg(Math.abs(eastMetres * _displayPerMetre).toFixed(2))
                                         .arg(_displayUnits)
                                         .arg(eastMetres < 0 ? qsTr("west") : qsTr("east"))
        return northText + ", " + eastText
    }

    /// Why the plan cannot be moved right now, in the operator's terms rather than as a dead button
    function _cannotShiftReason() {
        if (_accepted) {
            return qsTr("The vehicle took the correction, so the plan is already in the right frame. Moving it as well would put the pattern out by twice the drift.")
        }
        if (!_root.gridView) {
            return qsTr("No grid to read a plan from.")
        }
        if (!_root.gridView.positionValid) {
            return qsTr("No local position telemetry, so there is nothing to measure the drift against.")
        }
        if (_root.gridView.positionStale) {
            return qsTr("The reported position is not current. An offset worked out from a frozen readout would move the plan by a number that stopped being true.")
        }
        if (!_root.gridView.canPlaceWaypoints) {
            return qsTr("No plan is loaded, or it is being transferred to the vehicle. A plan moved mid-transfer is overwritten by the vehicle's copy when it lands.")
        }
        return qsTr("The plan cannot be moved right now.")
    }

    function _send() {
        if (!_canSend) {
            return
        }

        _replied = false
        _reason = ""
        _awaitingReply = true
        _root.vehicle.sendExternalPositionEstimate(_root.coordinate, _accuracyMetres)
    }

    // The answer arrives on the vehicle rather than from the call, because the vehicle is what
    // decides. Shown here rather than raised as one of QGC's generic command failures: the refusals
    // carry the diagnosis -- firmware built without the feature, an estimator that has stopped aiding
    // -- and a banner saying a command failed throws all of that away.
    Connections {
        target:  _root.vehicle
        enabled: _root.vehicle ? true : false

        function onExternalPositionEstimateResult(accepted, reason) {
            if (!_root._awaitingReply) {
                return
            }
            _root._awaitingReply = false
            _root._replied = true
            _root._accepted = accepted
            _root._reason = reason
        }
    }

    ColumnLayout {
        spacing: ScreenTools.defaultFontPixelHeight / 2

        QGCLabel {
            Layout.preferredWidth:  _fieldWidth
            wrapMode:               Text.WordWrap
            text:                   _root.placeName !== ""
                                        ? qsTr("Tell the vehicle it is standing on %1.").arg(_root.placeName)
                                        : qsTr("Tell the vehicle it is standing on this point.")
        }

        // ---------------- What is being claimed ----------------

        GridLayout {
            Layout.fillWidth:   true
            columns:            2
            columnSpacing:      ScreenTools.defaultFontPixelWidth
            rowSpacing:         0

            QGCLabel { text: qsTr("North of origin") }
            QGCLabel {
                objectName:             "correctPosition_northLabel"
                Layout.fillWidth:       true
                horizontalAlignment:    Text.AlignRight
                text:                   _root._distanceText(_root.north)
            }

            QGCLabel { text: qsTr("East of origin") }
            QGCLabel {
                objectName:             "correctPosition_eastLabel"
                Layout.fillWidth:       true
                horizontalAlignment:    Text.AlignRight
                text:                   _root._distanceText(_root.east)
            }

            // The coordinate as well as the offsets. This is what goes on the wire, and an origin
            // that was set to the wrong place shows up here as a latitude in the wrong country --
            // which is worth catching before telling the aircraft it is there.
            QGCLabel { text: qsTr("Coordinate") }
            QGCLabel {
                objectName:             "correctPosition_coordinateLabel"
                Layout.fillWidth:       true
                horizontalAlignment:    Text.AlignRight
                text:                   _root._coordinateValid
                                            ? (_root.coordinate.latitude.toFixed(7) + ", " + _root.coordinate.longitude.toFixed(7))
                                            : qsTr("--")
            }
        }

        RowLayout {
            Layout.fillWidth:   true
            spacing:            ScreenTools.defaultFontPixelWidth

            QGCLabel { text: qsTr("Known to within") }

            QGCTextField {
                id:                 accuracyField
                objectName:         "correctPosition_accuracyField"
                Layout.fillWidth:   true
                text:               "1.0"
                unitsLabel:         qsTr("m")
            }
        }

        // ---------------- Sending ----------------

        QGCButton {
            objectName:         "correctPosition_sendButton"
            Layout.fillWidth:   true
            primary:            true
            text:               _root._awaitingReply ? qsTr("Sending…") : qsTr("Correct position to here")
            enabled:            _root._canSend
            onClicked:          _root._send()
        }

        // Locked in flight, and said rather than left as a dead button. A correction is a step change
        // in where the aircraft believes it is: in any mode holding position the controller reads that
        // step as having been blown off course and flies the whole distance to close it, immediately
        // and at whatever speed the mode allows.
        QGCLabel {
            objectName:             "correctPosition_armedWarning"
            Layout.preferredWidth:  _fieldWidth
            visible:                _root._armed
            wrapMode:               Text.WordWrap
            font.pointSize:         ScreenTools.smallFontPointSize
            color:                  qgcPal.colorRed
            text:                   qsTr("The vehicle is armed. Correcting the position now moves where the aircraft believes it is, and a mode holding position will fly the whole correction to get back to its target. Land and disarm first.")
        }

        QGCLabel {
            Layout.preferredWidth:  _fieldWidth
            visible:                !_root._coordinateValid
            wrapMode:               Text.WordWrap
            font.pointSize:         ScreenTools.smallFontPointSize
            color:                  qgcPal.colorOrange
            text:                   qsTr("The vehicle has no estimator origin, so there is no frame to correct a position inside of.")
        }

        // The whole reason the command is acknowledged rather than fired and forgotten. Both answers
        // are shown: an operator who is not told it worked will send it again, and one who is not told
        // why it did not will keep flying a position that never moved.
        QGCLabel {
            objectName:             "correctPosition_result"
            Layout.preferredWidth:  _fieldWidth
            visible:                _root._replied
            wrapMode:               Text.WordWrap
            font.bold:              !_root._accepted
            color:                  _root._accepted ? qgcPal.colorGreen : qgcPal.colorRed
            text:                   _root._accepted
                                        ? qsTr("Applied. The reported position should now read close to the point above.")
                                        : _root._reason
        }

        // ---------------- The fallback ----------------

        // Offered whether or not the command has been tried, because on a board that cannot do this
        // at all there is nothing to try: the feature is compiled out below 1 MB of flash, and the
        // operator finds that out from the refusal above. Moving the plan needs no firmware support --
        // it is arithmetic on coordinates QGC already holds.
        QGCLabel {
            Layout.topMargin:   ScreenTools.defaultFontPixelHeight / 2
            font.bold:          true
            text:               qsTr("Or move the plan instead")
        }

        QGCLabel {
            Layout.preferredWidth:  _fieldWidth
            wrapMode:               Text.WordWrap
            font.pointSize:         ScreenTools.smallFontPointSize
            text:                   qsTr("Leaves the aircraft's estimate alone and shifts every waypoint by the same amount the frame has slid, so the pattern is flown over the ground it was drawn on. Works on firmware that cannot reset a position at all.")
        }

        GridLayout {
            Layout.fillWidth:   true
            visible:            _root._canShift || _root._shifted
            columns:            2
            columnSpacing:      ScreenTools.defaultFontPixelWidth
            rowSpacing:         0

            // The measurement the shift is worked out from, shown next to it. Reported minus claimed
            // is the whole of the arithmetic, and an operator who can see both numbers can catch a
            // sign that went the wrong way before the aircraft flies it.
            QGCLabel { text: qsTr("Estimator reports") }
            QGCLabel {
                objectName:             "correctPosition_reportedLabel"
                Layout.fillWidth:       true
                horizontalAlignment:    Text.AlignRight
                text:                   _root._offsetText(_root._reportedNorth, _root._reportedEast)
            }

            QGCLabel { text: qsTr("Shift the plan by") }
            QGCLabel {
                objectName:             "correctPosition_shiftLabel"
                Layout.fillWidth:       true
                horizontalAlignment:    Text.AlignRight
                text:                   _root._offsetText(_root._shiftNorth, _root._shiftEast)
            }
        }

        QGCButton {
            objectName:         "correctPosition_shiftPlanButton"
            Layout.fillWidth:   true
            text:               qsTr("Move the plan by this instead")
            enabled:            _root._canShift
            onClicked:          _root._shiftPlan()
        }

        // Why the button above is dead, in the operator's terms. A plan cannot be moved by an offset
        // nobody can measure, and the frozen-readout case is the one worth naming: the numbers still
        // look like a measurement.
        QGCLabel {
            objectName:             "correctPosition_cannotShiftReason"
            Layout.preferredWidth:  _fieldWidth
            visible:                !_root._canShift && !_root._shiftAttempted
            wrapMode:               Text.WordWrap
            font.pointSize:         ScreenTools.smallFontPointSize
            color:                  qgcPal.colorOrange
            text:                   _root._cannotShiftReason()
        }

        // Said plainly, because a shifted plan that was never sent is a plan the vehicle is still
        // flying in its old place -- and everything on the grid will already be drawn in the new one.
        QGCLabel {
            objectName:             "correctPosition_shiftResult"
            Layout.preferredWidth:  _fieldWidth
            visible:                _root._shiftAttempted
            wrapMode:               Text.WordWrap
            color:                  _root._shifted ? qgcPal.colorGreen : qgcPal.colorOrange
            text:                   _root._shifted
                                        ? qsTr("Moved %1 waypoint(s). Send the plan to the vehicle for this to take effect — nothing has changed on board yet.")
                                            .arg(_root._shiftedCount)
                                        : qsTr("Nothing moved. The plan holds no waypoint that can be moved — a takeoff is pinned to the origin and stays there.")
        }

        QGCLabel {
            Layout.preferredWidth:  _fieldWidth
            wrapMode:               Text.WordWrap
            font.pointSize:         ScreenTools.smallFontPointSize
            text:                   qsTr("This does not move the origin, and it does not touch height. Only the horizontal position the estimator is holding is reset — the rangefinder or barometer keeps the vertical.")
        }

        // ---------------- The third remedy, when the frame itself is wrong ----------------

        // Kept here rather than in the readout beside the live numbers. An origin in the wrong region
        // is not a cosmetic mistake -- the autopilot checks the compass against the magnetic model at
        // that position and refuses to arm with "Check mag field" -- so changing one has to stay
        // reachable without reconnecting the vehicle. But it is done once a flight at most, and it
        // moves the frame every position and waypoint on this grid is measured in, so it belongs one
        // click in rather than under the operator's thumb.
        QGCButton {
            objectName:         "correctPosition_changeOriginButton"
            Layout.fillWidth:   true
            Layout.topMargin:   ScreenTools.defaultFontPixelHeight / 2
            visible:            _root.gridView ? true : false
            text:               qsTr("Change Estimator Origin…")
            onClicked: {
                // Closed first: both are modal, and a dialog opened over this one leaves the operator
                // to dismiss two things to get back to the grid
                _root.close()
                _root.gridView.showSetOriginDialog()
            }
        }

        QGCLabel {
            Layout.preferredWidth:  _fieldWidth
            wrapMode:               Text.WordWrap
            font.pointSize:         ScreenTools.smallFontPointSize
            color:                  qgcPal.colorOrange
            text:                   qsTr("A last resort. Moving the origin moves the frame this whole grid is drawn in, so every waypoint already placed means a different point on the ground afterwards.")
        }
    }
}
