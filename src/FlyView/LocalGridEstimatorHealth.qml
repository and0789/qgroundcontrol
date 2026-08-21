import QtQuick

import QGroundControl

/// The estimator's own verdict on whether it still knows where the vehicle is.
///
/// The grid draws a position with the same confidence whatever the EKF thinks of it. A marker on a
/// measured grid reads as a measurement, and there is nothing in the picture to say that the
/// solution behind it has degraded or stopped aiding altogether -- so the operator keeps flying a
/// pattern against numbers the autopilot has already stopped believing.
///
/// This is the estimator giving up, which is a different failure from the telemetry going quiet:
/// there the picture froze and the aircraft was fine, here the picture keeps updating and the
/// aircraft is lost. The grid has to be able to say which one it is.
QtObject {
    id: _root

    property var vehicle

    readonly property var _estimator: vehicle ? vehicle.estimatorStatus : null

    /// Nothing is claimed until the vehicle has actually reported estimator status. Treating an
    /// unreported flag as healthy is how a link carrying no EKF status at all reads as a flawless
    /// solution -- which is the same green picture as a working one.
    readonly property bool known: _estimator ? _estimator.telemetryAvailable : false

    /// The estimator has fallen back to assuming the vehicle is not moving.
    ///
    /// EKF_CONST_POS_MODE is not a degraded position, it is the absence of one: with no aiding
    /// source left the filter holds the last position and lets the vehicle drift away from it
    /// unobserved. Everything the grid draws after this is fiction that updates.
    readonly property bool aidingLost: known && _flag("goodConstPosModeEstimate", false)

    /// The estimator no longer considers its horizontal position good
    readonly property bool horizontalPositionUnhealthy: known && !_flag("goodHorizPosRelEstimate", true)

    /// Normalised innovation test ratio for horizontal position. Above 1 the filter is rejecting the
    /// measurements it is being given, which is the state just before it stops using them at all.
    readonly property real positionRatio: _factValue("horizPosRatio")
    readonly property real velocityRatio: _factValue("velRatio")

    /// Same thresholds the non-GPS status panel colours its ratios against, so a number that reads
    /// as trouble there does not read as fine here
    readonly property real warnRatio: 0.8
    readonly property real badRatio:  1.0

    readonly property bool ratioDegraded: _overRatio(warnRatio)
    readonly property bool ratioRejecting: _overRatio(badRatio)

    /// True when anything is wrong enough to be worth interrupting the operator over
    readonly property bool degraded: aidingLost || horizontalPositionUnhealthy || ratioDegraded

    /// The single worst thing that is true right now, or "" when the solution is healthy.
    ///
    /// One line rather than a list. This is read while flying, off a panel the size of a business
    /// card, by someone whose attention is on the aircraft -- and the remedy for every one of these
    /// is the same first move, so the operator needs the most severe one, not all of them.
    readonly property string warning: {
        if (!known) {
            return ""
        }
        if (aidingLost) {
            return qsTr("Estimator is not aiding — it has stopped tracking position. This grid is no longer measuring anything.")
        }
        if (horizontalPositionUnhealthy) {
            return qsTr("Estimator reports its horizontal position is not good. Treat the grid as approximate.")
        }
        if (ratioRejecting) {
            return qsTr("Estimator is rejecting its position measurements. Position is drifting away from what is drawn.")
        }
        if (ratioDegraded) {
            return qsTr("Estimator is having trouble with its position measurements.")
        }
        return ""
    }

    /// True when the worst of it is bad enough that the picture should not be trusted at all, rather
    /// than merely treated with suspicion
    readonly property bool severe: aidingLost || ratioRejecting

    function _flag(name, valueWhenMissing) {
        const fact = _estimator ? _estimator[name] : null
        return fact ? (fact.rawValue === true) : valueWhenMissing
    }

    /// @return the fact's value, or NaN where the vehicle has not reported it. These start as NaN
    /// rather than zero on purpose, so a missing ratio is not read as a perfect one.
    function _factValue(name) {
        const fact = _estimator ? _estimator[name] : null
        return fact ? fact.rawValue : NaN
    }

    function _overRatio(threshold) {
        if (!known) {
            return false
        }
        // Either ratio counts. Horizontal position and velocity are the two aiding paths this way of
        // flying depends on, and losing either one takes the grid's numbers with it.
        return (!isNaN(positionRatio) && (positionRatio > threshold))
                || (!isNaN(velocityRatio) && (velocityRatio > threshold))
    }
}
