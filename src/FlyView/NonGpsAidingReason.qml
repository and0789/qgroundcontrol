import QtQuick

import QGroundControl

/// Why the estimator stopped aiding, named from the telemetry the panel is already receiving.
///
/// "Aiding: NO" says the estimator has fallen back to assuming the vehicle is stationary. It does
/// not say which of the things that state depends on went away, and the state itself is not
/// actionable -- the cause is. Every input needed to name the cause is already on the panel, spread
/// across the flow, rangefinder and EKF sections, but reading it off six rows and drawing the
/// conclusion takes longer than the failure gives the operator.
///
/// Nothing here diagnoses the autopilot. It reports which of flow's own preconditions is not being
/// met, which is the answer often enough to be worth saying, and says so plainly when none of them
/// explains it.
QtObject {
    id: _root

    property var vehicle

    /// Flow quality the aircraft needs, and the innovation ratio above which the EKF is rejecting
    /// what it is given. Kept as properties so the panel can hold this object to the same numbers it
    /// colours its own rows against, rather than the two drifting apart.
    property int  minFlowQuality: 50
    property real badRatio:       1.0

    readonly property var _opticalFlow:     vehicle ? vehicle.opticalFlow : null
    readonly property var _distanceSensors: vehicle ? vehicle.distanceSensors : null
    readonly property var _estimatorStatus: vehicle ? vehicle.estimatorStatus : null

    /// The estimator has reported EKF_CONST_POS_MODE: no aiding source left, holding the last
    /// position while the vehicle drifts away from it unobserved.
    ///
    /// Gated on the estimator having actually reported, because these flags default to false and an
    /// unset flag would otherwise read as an estimator confirming it is healthy.
    readonly property bool aidingLost: _estimatorReported
                                        && (_estimatorStatus.goodConstPosModeEstimate.rawValue === true)

    /// One line, and empty whenever aiding is not lost. The panel has room for a sentence, not a
    /// list, and the first thing in this order that is true is also the first thing to fix.
    readonly property string reason: {
        if (!aidingLost) {
            return ""
        }
        if (!_flowReported) {
            return qsTr("No optical flow telemetry is arriving, so the estimator has no horizontal aiding source at all.")
        }
        if (!isNaN(_flowQuality) && (_flowQuality <= minFlowQuality)) {
            return qsTr("Optical flow quality is %1, at or below the %2 this aircraft needs. Fly over ground with more texture, and check the sensor's focus and lighting.")
                        .arg(Math.round(_flowQuality)).arg(minFlowQuality)
        }
        if (!_heightKnown) {
            return qsTr("No height above the ground is being reported. Flow measures an angular rate, so without a height there is nothing to scale it into a velocity with.")
        }
        if (!isNaN(_worstRatio) && (_worstRatio > badRatio)) {
            return qsTr("Flow and height are arriving, but the estimator is rejecting them — its innovation ratio is %1. Slow down and fly more gently.")
                        .arg(_worstRatio.toFixed(2))
        }
        // Everything flow needs is present and the estimator dropped it anyway, which is no longer a
        // question about the sensors. Says so rather than picking the least unlikely sensor to blame.
        return qsTr("Flow, height and the estimator's own ratios all look healthy, so the aiding was lost for another reason. Check EK3_SRC1_VELXY is set to optical flow.")
    }

    readonly property bool _estimatorReported: _estimatorStatus ? _estimatorStatus.telemetryAvailable : false
    readonly property bool _flowReported:      _opticalFlow ? _opticalFlow.telemetryAvailable : false

    readonly property real _flowQuality: _opticalFlow ? _opticalFlow.quality.rawValue : NaN

    /// A height from either message that carries one, on the same terms the altitude ceiling reads
    /// them: the downward rangefinder first, and OPTICAL_FLOW's own ground distance for a vehicle
    /// that streams only that. Zero is not a height either way -- it is a sensor returning nothing.
    readonly property bool _heightKnown: {
        const rangefinder = _distanceSensors ? _distanceSensors.rotationPitch270.rawValue : NaN
        if (!isNaN(rangefinder) && (rangefinder > 0)) {
            return true
        }
        const flowHeight = _opticalFlow ? _opticalFlow.groundDistance.rawValue : NaN
        return !isNaN(flowHeight) && (flowHeight > 0)
    }

    /// The higher of the two ratios that matter here. Velocity is the one flow actually feeds, but
    /// position goes with it, and reporting the worse of the pair keeps the sentence short.
    readonly property real _worstRatio: {
        const velocity = _estimatorReported ? _estimatorStatus.velRatio.rawValue : NaN
        const position = _estimatorReported ? _estimatorStatus.horizPosRatio.rawValue : NaN
        if (isNaN(velocity)) {
            return position
        }
        if (isNaN(position)) {
            return velocity
        }
        return Math.max(velocity, position)
    }
}
