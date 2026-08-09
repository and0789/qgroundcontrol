import QtQuick

import QGroundControl
import QGroundControl.FactControls

/// How high a waypoint may be before the estimator loses its height reference.
///
/// A vehicle flying without GNSS commonly takes its altitude from a rangefinder -- EK3_SRC1_POSZ set
/// to 2. Above that rangefinder's usable range the EKF has no height source at all, and the way this
/// fails is the reason it is worth checking on the ground: the plan uploads cleanly, nothing is
/// refused, and the aircraft climbs out of range mid-flight.
///
/// QGC cannot know this on its own. The relationship between an altitude source parameter, a
/// rangefinder's range and a waypoint's height only means anything on an aircraft flown this way.
QtObject {
    id: _root

    property var vehicle

    /// Which source EKF3 takes vertical position from, and how far the first rangefinder can see
    property string altitudeSourceParameterName: "EK3_SRC1_POSZ"
    property string rangefinderMaxParameterName: "RNGFND1_MAX"

    /// EK3_SRC1_POSZ value meaning the rangefinder, from AP_NavEKF_Source::SourceZ
    readonly property int rangefinderSourceValue: 2

    /// True only when the rangefinder really is the height source. On a vehicle using the barometer
    /// the rangefinder's range says nothing about how high it may fly, and warning about it would be
    /// noise the operator learns to ignore.
    readonly property bool rangefinderIsAltitudeSource: (_sourceFact !== null)
                                                            && (_sourceFact.rawValue === rangefinderSourceValue)

    readonly property bool limitKnown:  rangefinderIsAltitudeSource && (_maxFact !== null)
    readonly property real limitMetres: limitKnown ? _maxFact.rawValue : NaN

    /// @return true when this altitude would take the vehicle past the rangefinder's range. False
    /// whenever the answer is not known, so an unrecognised setup warns about nothing rather than
    /// warning about everything.
    function exceeds(metres) {
        if (!limitKnown || isNaN(metres)) {
            return false
        }
        return metres > limitMetres
    }

    property bool _parametersReady: vehicle ? vehicle.parameterManager.parametersReady : false

    property var _sourceFact: _factOrNull(altitudeSourceParameterName)
    property var _maxFact:    _factOrNull(rangefinderMaxParameterName)

    function _factOrNull(parameterName) {
        return (_controller && _parametersReady && _controller.parameterExists(-1, parameterName))
                    ? _controller.getParameterFact(-1, parameterName)
                    : null
    }

    // FactPanelController binds its vehicle in its constructor and never rebinds. This object is
    // built with the fly view, before any vehicle connects, so a single instance would hold the
    // offline editing vehicle forever and never find a parameter. Rebuild it when the vehicle changes.
    property Component _controllerComponent: Component { FactPanelController { } }
    property var _controller: null

    onVehicleChanged:       _rebuildController()
    Component.onCompleted:  _rebuildController()

    function _rebuildController() {
        if (_controller) {
            _controller.destroy()
            _controller = null
        }
        if (vehicle) {
            _controller = _controllerComponent.createObject(_root)
        }
    }
}
