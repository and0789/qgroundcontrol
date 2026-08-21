import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

import QtPositioning

import QGroundControl
import QGroundControl.Controls

/// Sets the estimator origin without needing a map.
///
/// The origin is the bridge between the frame the estimator flies in and the coordinates a mission
/// is stored against, so nothing else about flying without GNSS works until it is set. Until now the
/// only way to set one was to click the fly view map -- which is the worst possible dependency in
/// the situation this exists for: map tiles need a network, and finding your own position on a map
/// is exactly the knowledge GNSS was providing. Indoors it is meaningless.
///
/// The value cannot be invented, though it does not have to be surveyed. Before it will arm,
/// ArduPilot compares what the compass reads against the world magnetic model at the vehicle's
/// position -- which, with no GNSS, comes from this origin. An origin on the wrong continent makes
/// that comparison fail and the vehicle refuses to arm with "Check mag field", so the coordinate has
/// to be roughly right even when the mission itself is flown purely as offsets in metres.
QGCPopupDialog {
    id:         _root
    title:      qsTr("Set Estimator Origin")
    buttons:    Dialog.Close

    property var    _activeVehicle:     QGroundControl.multiVehicleManager.activeVehicle
    property var    _gcsPosition:       QGroundControl.qgcPositionManger.gcsPosition
    property bool   _gcsPositionValid:  _gcsPosition && _gcsPosition.isValid
    property var    _currentOrigin:     _activeVehicle ? _activeVehicle.estimatorOrigin : null
    property bool   _originIsSet:       _currentOrigin ? _currentOrigin.isValid : false

    property var    _flyViewSettings:   QGroundControl.settingsManager.flyViewSettings
    property real   _lastLatitude:      _flyViewSettings.lastEstimatorOriginLatitude.rawValue
    property real   _lastLongitude:     _flyViewSettings.lastEstimatorOriginLongitude.rawValue
    /// Zero for both is what the setting holds before an origin has ever been set. It is also a real
    /// place in the Gulf of Guinea, and Vehicle reads an origin of exactly zero as "no origin", so
    /// it can never be offered as one.
    property bool   _lastOriginValid:   (_lastLatitude !== 0) || (_lastLongitude !== 0)

    property real   _fieldWidth:        ScreenTools.defaultFontPixelWidth * 46

    /// Recomputed from the field text so the apply button cannot send a half-typed coordinate.
    /// Latitude and longitude are range checked because a transposed pair -- 47 typed into the
    /// longitude box of a place at longitude 8 -- is the kind of slip that produces a valid
    /// coordinate a thousand kilometres away, and a compass check failure rather than an error.
    property bool   _typedCoordinateValid: _isLatitude(latitudeField.text) && _isLongitude(longitudeField.text)

    function _isLatitude(text) {
        const value = parseFloat(text)
        return !isNaN(value) && (value >= -90) && (value <= 90)
    }

    function _isLongitude(text) {
        const value = parseFloat(text)
        return !isNaN(value) && (value >= -180) && (value <= 180)
    }

    QGCPalette { id: qgcPal; colorGroupEnabled: enabled }

    Component.onCompleted: _prefillFields()

    /// Fills the entry fields with the best coordinate available, in the order the operator is most
    /// likely to want: where they flew last, then where the ground station says it is. Both are only
    /// a suggestion -- nothing is sent until a button is pressed, so whatever the vehicle receives is
    /// a value the operator has seen.
    ///
    /// When neither is known the fields are left empty rather than seeded with a made-up coordinate.
    /// A convenient default that puts the origin on the wrong continent is not convenient: it arms
    /// nothing, because the compass check below fails against it.
    function _prefillFields() {
        if (_lastOriginValid) {
            latitudeField.text = _lastLatitude.toFixed(7)
            longitudeField.text = _lastLongitude.toFixed(7)
        } else if (_gcsPositionValid) {
            latitudeField.text = _gcsPosition.latitude.toFixed(7)
            longitudeField.text = _gcsPosition.longitude.toFixed(7)
        }
        altitudeField.text = "0"
    }

    function _apply(coordinate) {
        if (!_activeVehicle || !coordinate.isValid) {
            return
        }

        _activeVehicle.setEstimatorOrigin(coordinate)

        // Remembered so the next flight from the same place is one button. Stored only after it has
        // been sent, so a coordinate the operator abandoned is never offered back to them.
        _flyViewSettings.lastEstimatorOriginLatitude.rawValue = coordinate.latitude
        _flyViewSettings.lastEstimatorOriginLongitude.rawValue = coordinate.longitude

        _root.close()
    }

    function _applyTypedCoordinate() {
        const latitude = parseFloat(latitudeField.text)
        const longitude = parseFloat(longitudeField.text)
        if (isNaN(latitude) || isNaN(longitude)) {
            return
        }

        var altitude = parseFloat(altitudeField.text)
        if (isNaN(altitude)) {
            altitude = 0
        }

        _apply(QtPositioning.coordinate(latitude, longitude, altitude))
    }

    ColumnLayout {
        spacing: ScreenTools.defaultFontPixelHeight / 2

        QGCLabel {
            Layout.preferredWidth:  _fieldWidth
            wrapMode:               Text.WordWrap
            visible:                !_root._activeVehicle
            color:                  qgcPal.warningText
            text:                   qsTr("No vehicle connected.")
        }

        QGCLabel {
            Layout.preferredWidth:  _fieldWidth
            wrapMode:               Text.WordWrap
            visible:                _root._originIsSet
            font.pointSize:         ScreenTools.smallFontPointSize
            text:                   qsTr("This vehicle already has an origin at %1, %2. Setting a new one moves the frame every local position and waypoint is measured in.")
                                        .arg(_root._originIsSet ? _root._currentOrigin.latitude.toFixed(7) : "")
                                        .arg(_root._originIsSet ? _root._currentOrigin.longitude.toFixed(7) : "")
        }

        // ---------------- Ground station position ----------------

        QGCButton {
            objectName:         "setOrigin_useGcsButton"
            Layout.fillWidth:   true
            text:               qsTr("Use ground station position")
            enabled:            _root._activeVehicle && _root._gcsPositionValid
            onClicked:          _root._apply(QtPositioning.coordinate(_root._gcsPosition.latitude,
                                                                     _root._gcsPosition.longitude,
                                                                     0))
        }

        QGCLabel {
            Layout.preferredWidth:  _fieldWidth
            wrapMode:               Text.WordWrap
            font.pointSize:         ScreenTools.smallFontPointSize
            color:                  _root._gcsPositionValid ? qgcPal.text : qgcPal.colorOrange
            text:                   _root._gcsPositionValid
                                        ? qsTr("%1, %2 — the ground station is usually at the launch point, which is where the origin belongs.")
                                            .arg(_root._gcsPosition.latitude.toFixed(7))
                                            .arg(_root._gcsPosition.longitude.toFixed(7))
                                        : qsTr("The ground station has no position of its own, so there is nothing to prefill. Enter roughly where the vehicle is below.")
        }

        // ---------------- Typed coordinate ----------------

        QGCLabel {
            Layout.topMargin:   ScreenTools.defaultFontPixelHeight / 2
            font.bold:          true
            text:               qsTr("Enter a coordinate")
        }

        GridLayout {
            Layout.fillWidth:   true
            columns:            2
            columnSpacing:      ScreenTools.defaultFontPixelWidth
            rowSpacing:         ScreenTools.defaultFontPixelHeight / 3

            QGCLabel { text: qsTr("Latitude") }
            QGCTextField {
                id:                 latitudeField
                objectName:         "setOrigin_latitudeField"
                Layout.fillWidth:   true
            }

            QGCLabel { text: qsTr("Longitude") }
            QGCTextField {
                id:                 longitudeField
                objectName:         "setOrigin_longitudeField"
                Layout.fillWidth:   true
            }

            QGCLabel { text: qsTr("Altitude (AMSL)") }
            QGCTextField {
                id:                 altitudeField
                objectName:         "setOrigin_altitudeField"
                Layout.fillWidth:   true
                unitsLabel:         QGroundControl.unitsConversion.appSettingsVerticalDistanceUnitsString
            }
        }

        QGCButton {
            objectName:         "setOrigin_applyTypedButton"
            Layout.fillWidth:   true
            primary:            true
            text:               qsTr("Set origin to this coordinate")
            enabled:            _root._activeVehicle && _root._typedCoordinateValid
            onClicked:          _root._applyTypedCoordinate()
        }

        // The constraint that actually bites, stated where it is needed rather than discovered at
        // the flight line. An earlier version of this dialog said any coordinate would do; it will
        // not, and the failure it produces names the compass rather than the origin.
        QGCLabel {
            Layout.preferredWidth:  _fieldWidth
            wrapMode:               Text.WordWrap
            font.pointSize:         ScreenTools.smallFontPointSize
            text:                   qsTr("It does not have to be surveyed, but it does have to be roughly where the vehicle really is. Before arming, the autopilot compares the compass against the world magnetic model at this position — an origin in the wrong region fails pre-arm with \"Check mag field\", which names the compass rather than the origin that caused it.")
        }
    }
}
