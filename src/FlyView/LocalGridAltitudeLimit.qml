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

    /// Which sources EKF3 takes vertical position and horizontal velocity from, and how far the
    /// first rangefinder can see
    property string altitudeSourceParameterName: "EK3_SRC1_POSZ"
    property string velocitySourceParameterName: "EK3_SRC1_VELXY"
    property string rangefinderMaxParameterName: "RNGFND1_MAX"

    /// EK3_SRC1_POSZ value meaning the rangefinder, from AP_NavEKF_Source::SourceZ
    readonly property int rangefinderSourceValue: 2
    /// EK3_SRC1_VELXY value meaning optical flow, from AP_NavEKF_Source::SourceXY
    readonly property int opticalFlowSourceValue: 5

    /// The rangefinder is the only thing holding the vehicle's height up
    readonly property bool rangefinderIsAltitudeSource: (_sourceFact !== null)
                                                            && (_sourceFact.rawValue === rangefinderSourceValue)

    /// Horizontal velocity comes from optical flow, which needs a height to be scaled into a
    /// velocity at all. Above the rangefinder's range there is no height to scale it with, so the
    /// vehicle loses its horizontal aiding even when the barometer is holding its altitude.
    readonly property bool opticalFlowIsVelocitySource: (_velocityFact !== null)
                                                            && (_velocityFact.rawValue === opticalFlowSourceValue)

    /// The ceiling applies for either reason. Where neither holds, the rangefinder's range says
    /// nothing about how high the vehicle may fly, and warning about it would be noise the operator
    /// learns to ignore.
    readonly property bool limitApplies: rangefinderIsAltitudeSource || opticalFlowIsVelocitySource
    readonly property bool limitKnown:   limitApplies && (_maxFact !== null)
    readonly property real limitMetres:  limitKnown ? _maxFact.rawValue : NaN

    /// Why the ceiling exists, so the warning can name the consequence rather than the parameter.
    /// Both reasons can hold at once; the altitude one is stated first because it is the one that
    /// runs away rather than merely degrading.
    readonly property string limitReason: !limitKnown
                                            ? ""
                                            : (rangefinderIsAltitudeSource
                                                ? qsTr("the estimator takes its height from it, and loses the reference above that")
                                                : qsTr("optical flow is scaled into a velocity using it, and has nothing to scale with above that"))

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

    property var _sourceFact:   _factOrNull(altitudeSourceParameterName)
    property var _velocityFact: _factOrNull(velocitySourceParameterName)
    property var _maxFact:      _factOrNull(rangefinderMaxParameterName)

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
