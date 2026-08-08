import QtQuick

import QGroundControl
import QGroundControl.Controls

/// Checks that the estimator has actually converged on a horizontal solution before a vehicle
/// navigating without GNSS is flown.
///
/// The sensors reading well is not the same as the EKF having accepted them. Until it reports a
/// horizontal velocity and relative position, and has stopped assuming the aircraft is stationary,
/// nothing holds position: a flow-only LOITER drifts off with no warning to the operator. When the
/// estimator says so itself there is no override, matching how the autopilot's own arming checks
/// treat it. Missing telemetry is a different matter and can be clicked past, since a link that
/// does not carry EKF_STATUS_REPORT says nothing about the aircraft.
PreFlightCheckButton {
    name:                           qsTr("EKF navigation")
    visible:                        _navigatingWithoutGNSS
    telemetryFailure:               _navigatingWithoutGNSS && (!_statusReceived || !_estimateGood)
    telemetryTextFailure:           !_statusReceived
                                        ? qsTr("Warning - no EKF status telemetry. The estimator's readiness cannot be confirmed.")
                                        : (_constPosMode
                                            ? qsTr("Estimator has fallen back to assuming the vehicle is stationary.")
                                            : qsTr("Waiting for a horizontal position and velocity estimate."))
    // Only the "cannot tell" case is the operator's to judge. An estimator that reports itself
    // unready is a hard stop.
    allowTelemetryFailureOverride:  !_statusReceived

    property var    _estimatorStatus:           globals.activeVehicle ? globals.activeVehicle.estimatorStatus : null
    property bool   _navigatingWithoutGNSS:     globals.activeVehicle ? globals.activeVehicle.navigatingWithoutGNSS : false
    // These flags default to false, so an unpopulated group looks exactly like a failing estimator.
    property bool   _statusReceived:            _estimatorStatus ? _estimatorStatus.telemetryAvailable : false
    property bool   _horizVelGood:              _estimatorStatus ? _estimatorStatus.goodHorizVelEstimate.rawValue : false
    property bool   _horizPosGood:              _estimatorStatus ? _estimatorStatus.goodHorizPosRelEstimate.rawValue : false
    /// Set means the estimator gave up on its aiding sources, so here the flag being set is failure
    property bool   _constPosMode:              _estimatorStatus ? _estimatorStatus.goodConstPosModeEstimate.rawValue : false
    property bool   _estimateGood:              _statusReceived && _horizVelGood && _horizPosGood && !_constPosMode
}
