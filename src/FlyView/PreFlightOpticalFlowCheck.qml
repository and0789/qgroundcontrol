import QtQuick

import QGroundControl
import QGroundControl.Controls

/// Checks that the optical flow sensor is producing usable readings before a vehicle that navigates
/// by them leaves the ground.
///
/// Flow quality depends on surface texture and lighting, both of which change with where the
/// aircraft is standing, so this is a judgement rather than a hard fact and the operator can click
/// past it. What it must not do is stay silent: a launch site with too little texture reads fine on
/// the ground and only shows itself as drift once the aircraft is airborne.
PreFlightCheckButton {
    name:                           qsTr("Optical flow")
    visible:                        _navigatingWithoutGNSS
    telemetryFailure:               _navigatingWithoutGNSS && (!_flowReceived || _qualityFailure)
    telemetryTextFailure:           !_flowReceived
                                        ? qsTr("No optical flow telemetry. Is the sensor connected and streaming?")
                                        : qsTr("Warning - quality %1, below %2.").arg(_quality).arg(minQuality)
    allowTelemetryFailureOverride:  true

    /// The quality the project's own flow calibration treats as the floor for a usable reading
    property int    minQuality:     50

    property var    _opticalFlow:               globals.activeVehicle ? globals.activeVehicle.opticalFlow : null
    property bool   _navigatingWithoutGNSS:     globals.activeVehicle ? globals.activeVehicle.navigatingWithoutGNSS : false
    // The facts default to zero, so a group that has never received a message is indistinguishable
    // from a sensor reporting a dead reading. Say which one it is.
    property bool   _flowReceived:              _opticalFlow ? _opticalFlow.telemetryAvailable : false
    property int    _quality:                   _opticalFlow ? _opticalFlow.quality.rawValue : 0
    property bool   _qualityFailure:            _flowReceived && (_quality < minQuality)
}
