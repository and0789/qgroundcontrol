import QtQuick

import QGroundControl

/// How far the estimator's idea of where the vehicle is has slid away from the ground it is standing
/// on.
///
/// Measured as movement rather than as distance from the origin, and that distinction is the whole
/// of it. A vehicle parked twenty metres from the origin is not drifting, it is parked; a warning
/// that fired on distance would go off every flight and be learned away. What cannot be explained
/// any other way is a *disarmed* aircraft whose reported position keeps moving: nothing is flying it,
/// so whatever the number is doing, the ground is not doing it.
///
/// The consequence is invisible until it is expensive. A mission's waypoints are stored as
/// latitude and longitude and flown as offsets from the estimator origin, so once the frame has slid
/// a metre the whole pattern is flown a metre out -- and the plan uploads cleanly, the flight looks
/// normal, and the aircraft simply arrives somewhere else.
QtObject {
    id: _root

    property var vehicle

    /// How far the reported position may wander before it is worth saying out loud. Below this is
    /// the ordinary noise of a flow sensor watching a static scene.
    property real warnMetres: 1.0

    /// Watching: there is a position to watch and nothing is flying the aircraft
    readonly property bool observing: (_localPosition !== null) && _positionValid && !_armed

    /// How far the reported position has moved since this stretch of being disarmed began, or NaN
    /// before there is anything to compare against
    readonly property real driftMetres: (_referenceNorth === null) || !_positionValid
                                            ? NaN
                                            : Math.sqrt(Math.pow(_north - _referenceNorth, 2)
                                                        + Math.pow(_east - _referenceEast, 2))

    /// How long it has been watching, in seconds. Drift is only worth reading against the time it
    /// took: half a metre in ten seconds is a different aircraft from half a metre in an hour.
    readonly property real observedSeconds: (_referenceMSecs > 0)
                                                ? ((_clock - _referenceMSecs) / 1000)
                                                : NaN

    readonly property bool drifting: observing && !isNaN(driftMetres) && (driftMetres > warnMetres)

    /// Empty unless there is something to say, so this is never background noise.
    ///
    /// States the measurement and leaves the conclusion to the operator, who is the only one who
    /// knows whether the aircraft was carried. QGC cannot tell being picked up from drifting: both
    /// are the reported position moving while disarmed.
    readonly property string warning: {
        if (!drifting) {
            return ""
        }
        return qsTr("Reported position has moved %1 m in %2 while disarmed. If the aircraft has not been moved, the estimator has slid that far from the ground and a mission will fly that far out.")
                    .arg(driftMetres.toFixed(1))
                    .arg(_elapsedText)
    }

    /// Starts the measurement again from wherever the vehicle is now. For after the aircraft has
    /// genuinely been carried somewhere, which is not drift and should not be counted as it.
    function reset() {
        _referenceNorth = null
        _referenceEast = null
        _referenceMSecs = 0
        _takeReference()
    }

    property var  _localPosition:  vehicle ? vehicle.localPosition : null
    property real _north:          _localPosition ? _localPosition.x.rawValue : NaN
    property real _east:           _localPosition ? _localPosition.y.rawValue : NaN
    property bool _armed:          vehicle ? vehicle.armed : false

    readonly property bool _positionValid: (_localPosition !== null)
                                            && _localPosition.telemetryAvailable
                                            && !isNaN(_north) && !isNaN(_east)

    /// Where the reported position was when this stretch of being disarmed began. Null rather than
    /// zero for "not taken yet": zero is a perfectly ordinary place for the vehicle to be.
    property var  _referenceNorth: null
    property var  _referenceEast:  null
    property real _referenceMSecs: 0

    /// Read on a tick rather than derived, because nothing signals the passing of time and an age
    /// bound to the position would freeze the moment the position stopped changing -- which is
    /// exactly the case this is measuring
    property real _clock: 0

    readonly property string _elapsedText: {
        if (isNaN(observedSeconds)) {
            return qsTr("--")
        }
        const minutes = Math.floor(observedSeconds / 60)
        return (minutes >= 1)
                    ? qsTr("%1 min").arg(minutes)
                    : qsTr("%1 s").arg(Math.round(observedSeconds))
    }

    function _takeReference() {
        if (!_positionValid || _armed) {
            return
        }
        if (_referenceNorth !== null) {
            return
        }
        _referenceNorth = _north
        _referenceEast = _east
        _referenceMSecs = Date.now()
        _clock = _referenceMSecs
    }

    // Armed, the aircraft is moving on purpose and every metre of it would be counted as drift. The
    // reference goes with it, so each stretch on the ground is measured from where that stretch
    // started rather than from before the last flight.
    on_ArmedChanged: {
        if (_armed) {
            _referenceNorth = null
            _referenceEast = null
            _referenceMSecs = 0
        } else {
            _takeReference()
        }
    }

    // Fires on every LOCAL_POSITION_NED rather than on a changed fact, so a reference can be taken
    // from a vehicle reporting the same position over and over
    property Connections _positionConnections: Connections {
        target:  _root._localPosition
        enabled: _root._localPosition !== null

        function onUpdated() {
            _root._takeReference()
            if (_root._referenceMSecs > 0) {
                _root._clock = Date.now()
            }
        }
    }

    /// Keeps the elapsed time moving while the position does not. A vehicle whose estimate has
    /// frozen still needs its clock to run, or the drift figure would be quoted against an age that
    /// stopped when the drift did.
    property Timer _ageTicker: Timer {
        interval:       1000
        running:        _root.observing && (_root._referenceMSecs > 0)
        repeat:         true
        onTriggered:    _root._clock = Date.now()
    }
}
