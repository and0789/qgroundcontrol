import QtQuick

import QGroundControl
import QGroundControl.FactControls

/// Judges whether the optical flow readings are usable, rather than only reporting them.
///
/// ArduPilot's EKF discards a flow reading whose magnitude exceeds EK3_MAX_FLOW, and a run of
/// discarded readings leaves the velocity estimate uncorrected, which is what makes a flow-only
/// LOITER drift away. The instantaneous magnitude does not show that, so this keeps a rolling
/// window instead.
///
/// The window is fed from rawValueChanged, which fires on every message, rather than from
/// valueChanged, which the fact group defers to its display update rate. A brief excursion above
/// the limit would otherwise fall between two display updates and never be counted.
QtObject {
    id: _root

    property var vehicle
    property int windowSecs: 10

    /// The vehicle parameter holding the flow magnitude the EKF will still accept
    property string limitParameterName: "EK3_MAX_FLOW"

    /// EK3_MAX_FLOW only exists on ArduPilot, so there is no verdict to give on other firmware
    readonly property bool  limitKnown:         _limitFact !== null
    readonly property real  flowLimit:          _limitFact ? _limitFact.rawValue : NaN

    readonly property int   sampleCount:        _sampleCount
    readonly property int   rejectedCount:      _rejectedCount
    readonly property bool  hasSamples:         _sampleCount > 0

    /// NaN rather than 0 when nothing arrived, so "no flow at all" cannot be misread as "all good"
    readonly property real  rejectedPercent:    _sampleCount > 0 ? ((100 * _rejectedCount) / _sampleCount) : NaN
    readonly property real  averageMagnitude:   _sampleCount > 0 ? (_sum / _sampleCount) : NaN
    readonly property real  peakMagnitude:      _peak

    /// True while the latest reading is one the EKF would throw away
    readonly property bool  rejectingNow:       limitKnown && _magnitudeFact !== null &&
                                                    !isNaN(_magnitudeFact.rawValue) &&
                                                    _magnitudeFact.rawValue > flowLimit

    property var    _opticalFlow:       vehicle ? vehicle.opticalFlow : null
    property var    _magnitudeFact:     _opticalFlow ? _opticalFlow.flowCompMagnitude : null
    property bool   _parametersReady:   vehicle ? vehicle.parameterManager.parametersReady : false
    property var    _limitFact:         (_parametersReady && _controller.parameterExists(-1, limitParameterName))
                                            ? _controller.getParameterFact(-1, limitParameterName)
                                            : null

    property var    _samples:           []
    property int    _sampleCount:       0
    property int    _rejectedCount:     0
    property real   _sum:               0
    property real   _peak:              NaN

    property FactPanelController _controller: FactPanelController { }

    /// Drops samples that aged out even while no new ones arrive, so a stopped flow decays to
    /// "no data" instead of freezing on the last verdict
    property Timer _ageOutTimer: Timer {
        interval:   1000
        running:    true
        repeat:     true
        onTriggered: _root._recalculate()
    }

    property Connections _magnitudeConnections: Connections {
        target: _root._magnitudeFact
        function onRawValueChanged(value) { _root._addSample(value) }
    }

    function reset() {
        _samples = []
        _recalculate()
    }

    function _addSample(magnitude) {
        const value = Number(magnitude)
        if (isNaN(value)) {
            return
        }

        const samples = _samples
        samples.push({ time: Date.now(), magnitude: value })
        _samples = samples
        _recalculate()
    }

    function _recalculate() {
        const cutoff = Date.now() - (windowSecs * 1000)
        const samples = _samples

        while (samples.length > 0 && samples[0].time < cutoff) {
            samples.shift()
        }
        _samples = samples

        var sum = 0
        var peak = NaN
        var rejected = 0
        for (var i = 0; i < samples.length; i++) {
            const value = samples[i].magnitude
            sum += value
            if (isNaN(peak) || (value > peak)) {
                peak = value
            }
            if (limitKnown && (value > flowLimit)) {
                rejected++
            }
        }

        _sum = sum
        _peak = peak
        _rejectedCount = rejected
        _sampleCount = samples.length
    }
}
