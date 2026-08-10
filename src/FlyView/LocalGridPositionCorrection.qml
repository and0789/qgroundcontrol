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
                                        && _accuracyValid && !_awaitingReply

    function _distanceText(metres) {
        if (isNaN(metres)) {
            return qsTr("--")
        }
        return (metres * _displayPerMetre).toFixed(2) + " " + _displayUnits
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

        QGCLabel {
            Layout.preferredWidth:  _fieldWidth
            wrapMode:               Text.WordWrap
            font.pointSize:         ScreenTools.smallFontPointSize
            text:                   qsTr("This does not move the origin, and it does not touch height. Only the horizontal position the estimator is holding is reset — the rangefinder or barometer keeps the vertical.")
        }
    }
}
