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
/// For a mission flown entirely in relative terms the absolute value does not matter -- the
/// autopilot only needs some origin so it has a home and can resolve an altitude relative to it. So
/// the operator is offered a coordinate they can accept as it stands, not made to find a real one.
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

    QGCPalette { id: qgcPal; colorGroupEnabled: enabled }

    Component.onCompleted: _prefillFields()

    /// Fills the entry fields with the best coordinate available, in the order the operator is most
    /// likely to want: where they flew last, then where the ground station says it is. Both are only
    /// a suggestion -- nothing is sent until a button is pressed, so whatever the vehicle receives is
    /// a value the operator has seen.
    function _prefillFields() {
        if (_lastOriginValid) {
            latitudeField.text = _lastLatitude.toFixed(7)
            longitudeField.text = _lastLongitude.toFixed(7)
        } else if (_gcsPositionValid) {
            latitudeField.text = _gcsPosition.latitude.toFixed(7)
            longitudeField.text = _gcsPosition.longitude.toFixed(7)
        } else {
            // A placeholder rather than a discovered position. Shown, not hidden, so an origin that
            // means nothing geographically cannot be mistaken later for a surveyed one.
            latitudeField.text = "1.0000000"
            longitudeField.text = "1.0000000"
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
                                        : qsTr("The ground station has no position of its own. Enter a coordinate below instead.")
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
            enabled:            _root._activeVehicle
            onClicked:          _root._applyTypedCoordinate()
        }

        // The point most operators need to hear, and the one that makes this usable indoors. Said
        // plainly rather than left to be inferred, and paired with its limit so a made-up origin
        // never ends up quoted as a measurement.
        QGCLabel {
            Layout.preferredWidth:  _fieldWidth
            wrapMode:               Text.WordWrap
            font.pointSize:         ScreenTools.smallFontPointSize
            text:                   qsTr("For a mission flown entirely as offsets in metres, any coordinate works — the autopilot only needs an origin so it has a home. Use a surveyed one when the flight has to be compared against anything outside itself.")
        }
    }
}
