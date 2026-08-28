import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

import QtPositioning

import QGroundControl
import QGroundControl.Controls
import QGroundControl.FactControls

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

    // ---------------- Remembering the origin across a reboot ----------------
    //
    // ArduPilot can save the origin to parameters as soon as one is set, and put it back on the
    // next boot when the estimator is not taking position from GNSS. Without it the origin dies
    // with every power cycle, and the aircraft comes back with no home, no local position on the
    // wire at all, and a mission that raises an internal error on its first takeoff.
    //
    // Named as properties so the parameters can be pointed elsewhere without editing the logic, the
    // way the local grid's other parameter readers are written.
    property string rememberOriginOptionsParameterName: "AHRS_OPTIONS"
    /// Read only to tell firmware that has this facility from firmware that does not. AHRS_OPTIONS
    /// itself is far older than the two bits below, so its presence says nothing.
    property string rememberOriginStorageParameterName: "AHRS_ORIGIN_LAT"

    /// AHRS_OPTIONS bit 3 saves the origin when it becomes valid, bit 4 restores it at boot on a
    /// vehicle whose estimator does not take horizontal position from GNSS. One is no use without
    /// the other, so they are offered as a single choice.
    readonly property int _rememberOriginBits: 24

    readonly property var  _rememberOriginFact: _factOrNull(rememberOriginOptionsParameterName)
    readonly property bool _canRememberOrigin:  (_rememberOriginFact !== null)
                                                    && (_factOrNull(rememberOriginStorageParameterName) !== null)
    readonly property bool _remembersOrigin:    _canRememberOrigin
                                                    && ((_rememberOriginFact.rawValue & _rememberOriginBits) === _rememberOriginBits)

    /// Sets or clears both bits, leaving the rest of AHRS_OPTIONS alone -- the lower bits carry
    /// unrelated DCM fallback and airspeed behaviour, and writing the mask whole would silently
    /// change how the aircraft flies.
    function _setRemembersOrigin(remember) {
        if (!_canRememberOrigin) {
            return
        }
        const current = _rememberOriginFact.rawValue
        _rememberOriginFact.rawValue = remember ? (current | _rememberOriginBits)
                                                : (current & ~_rememberOriginBits)
    }

    property bool _parametersReady: _activeVehicle ? _activeVehicle.parameterManager.parametersReady : false

    function _factOrNull(parameterName) {
        return (_controller && _parametersReady && _controller.parameterExists(-1, parameterName))
                    ? _controller.getParameterFact(-1, parameterName)
                    : null
    }

    // FactPanelController binds its vehicle in its constructor and never rebinds. This dialog is
    // built fresh each time it is opened, so one built here is bound to the vehicle it was opened
    // for and lives no longer than the dialog does.
    property var _controller: null
    Component { id: _controllerComponent; FactPanelController { } }

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

    Component.onCompleted: {
        if (_activeVehicle) {
            _controller = _controllerComponent.createObject(_root)
        }
        _prefillFields()
    }

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

        // ---------------- Remembering it across a reboot ----------------

        QGCCheckBox {
            id:                 rememberOriginCheckBox
            objectName:         "setOrigin_rememberCheckBox"
            Layout.topMargin:   ScreenTools.defaultFontPixelHeight / 2
            visible:            _root._canRememberOrigin
            text:               qsTr("Remember this origin on the vehicle")
            checked:            _root._remembersOrigin
            onClicked: {
                _root._setRemembersOrigin(checked)
                // Clicking a check box writes its own checked property, which breaks the binding
                // above. Without this the box would stop following the parameter the moment it is
                // touched -- and a refused or corrected write would leave it showing a state the
                // vehicle is not in.
                checked = Qt.binding(function() { return _root._remembersOrigin })
            }
        }

        QGCLabel {
            Layout.preferredWidth:  _fieldWidth
            visible:                _root._canRememberOrigin
            wrapMode:               Text.WordWrap
            font.pointSize:         ScreenTools.smallFontPointSize
            text:                   qsTr("Saves the origin to %1 and puts it back at the next boot, so a vehicle restarted between flights comes back with the origin it had. Takes effect immediately — it is a vehicle parameter, not part of the origin set above. Move to a different site and the vehicle will boot believing it is still at the saved one, so clear it or set a new origin there.")
                                        .arg(_root.rememberOriginOptionsParameterName)
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
