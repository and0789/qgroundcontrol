import QtQuick

import QGroundControl
import QGroundControl.Controls

/// Checks that the downward rangefinder is reporting, which optical flow navigation depends on.
///
/// Flow measures angular rate, not speed. Turning it into a velocity needs the height above the
/// surface, so a rangefinder that reports nothing does not merely lose an altitude source -- it
/// leaves the horizontal velocity estimate without a scale. Overridable because a bench or a very
/// dark or reflective surface can legitimately leave it silent while the aircraft is on the ground.
PreFlightCheckButton {
    name:                           qsTr("Rangefinder")
    visible:                        _navigatingWithoutGNSS
    telemetryFailure:               _navigatingWithoutGNSS && !_distanceValid
    telemetryTextFailure:           !_sensorReceived
                                        ? qsTr("No rangefinder telemetry. Optical flow cannot be scaled to a velocity without it.")
                                        : qsTr("Warning - no valid downward reading.")
    allowTelemetryFailureOverride:  true

    property var    _distanceSensors:           globals.activeVehicle ? globals.activeVehicle.distanceSensors : null
    property bool   _navigatingWithoutGNSS:     globals.activeVehicle ? globals.activeVehicle.navigatingWithoutGNSS : false
    property bool   _sensorReceived:            _distanceSensors ? _distanceSensors.telemetryAvailable : false
    property real   _distance:                  _distanceSensors ? _distanceSensors.rotationPitch270.rawValue : NaN
    // Unpopulated reads as NaN, and a rangefinder reporting zero is reporting a fault rather than a
    // height, so neither counts as a reading.
    property bool   _distanceValid:             _sensorReceived && !isNaN(_distance) && (_distance > 0)
}
