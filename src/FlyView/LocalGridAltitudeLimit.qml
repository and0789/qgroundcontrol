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

    /// How far under the rangefinder's range a plan should be flown. A waypoint sitting exactly on
    /// the limit is one gust and one patch of soft ground away from being over it, and the
    /// rangefinder's last metre is where its readings get least trustworthy.
    property real marginMetres: 1

    /// The altitude to fall back to: the ceiling less that margin. On a rangefinder short enough
    /// that the margin would take the answer to nothing -- a two metre indoor sensor -- half the
    /// range is used instead, since an altitude of zero is not a flight.
    readonly property real safeDefaultMetres: !limitKnown
                                                ? NaN
                                                : Math.max(limitMetres - marginMetres, limitMetres / 2)

    /// Why the ceiling exists, so the warning can name the consequence rather than the parameter.
    /// Both reasons can hold at once; the altitude one is stated first because it is the one that
    /// runs away rather than merely degrading.
    readonly property string limitReason: !limitKnown
                                            ? ""
                                            : (rangefinderIsAltitudeSource
                                                ? qsTr("the estimator takes its height from it, and loses the reference above that")
                                                : qsTr("optical flow is scaled into a velocity using it, and has nothing to scale with above that"))

    /// How high the vehicle is right now, as the downward rangefinder itself reports it.
    ///
    /// The rangefinder's own reading rather than a barometric or relative altitude, because the
    /// ceiling being approached is the rangefinder's range -- so the honest quantity to compare
    /// against it is the one that sensor is actually returning, not a height derived from something
    /// else that happens to agree on level ground.
    readonly property var _downwardFact: (vehicle && vehicle.distanceSensors)
                                            ? vehicle.distanceSensors.rotationPitch270
                                            : null

    /// The same height as it arrives inside OPTICAL_FLOW, for a vehicle that does not stream
    /// DISTANCE_SENSOR.
    ///
    /// ArduPilot fills that message's ground_distance from the same downward rangefinder, and
    /// whether DISTANCE_SENSOR is streamed alongside it depends on how the link's message rates are
    /// set up. On a vehicle that sends only the flow message this is the one place the height
    /// appears at all -- and that is precisely the aircraft this ceiling was written for, so
    /// reading the rangefinder fact alone left the warning silent for it.
    readonly property var _flowHeightFact: (vehicle && vehicle.opticalFlow)
                                            ? vehicle.opticalFlow.groundDistance
                                            : null

    readonly property real currentHeightMetres: {
        // The dedicated message first: it states the orientation it was measured in, where the flow
        // message only promises a distance to the ground.
        const rangefinder = _downwardFact ? _downwardFact.rawValue : NaN
        if (!isNaN(rangefinder) && (rangefinder > 0)) {
            return rangefinder
        }
        // Zero is not a height. A rangefinder that is streaming while returning nothing -- out of
        // range, or a surface it cannot see -- reports zero, and taking that literally would put the
        // vehicle at ground level whatever it is really doing. Fall through to the flow message,
        // which may still carry a usable one.
        const flowHeight = _flowHeightFact ? _flowHeightFact.rawValue : NaN
        return (!isNaN(flowHeight) && (flowHeight > 0)) ? flowHeight : NaN
    }

    readonly property bool currentHeightKnown: limitApplies && !isNaN(currentHeightMetres)

    /// The vehicle is flying high enough that the height reference is about to go, or has gone.
    ///
    /// Warned about in flight and not only when the plan was drawn. A plan can be flown correctly
    /// and still end up here: the operator climbs by hand, the ground falls away beneath a pattern
    /// flown level, or the vehicle overshoots its target altitude. The plan check cannot see any of
    /// those, and the failure is the same one -- above the rangefinder's range there is no height
    /// source, and nothing on screen says so.
    readonly property bool nearCeiling: currentHeightKnown && limitKnown
                                            && (currentHeightMetres > safeDefaultMetres)
    readonly property bool aboveCeiling: currentHeightKnown && limitKnown
                                            && (currentHeightMetres > limitMetres)

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
