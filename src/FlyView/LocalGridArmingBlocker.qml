import QtQuick

import QGroundControl

/// Whether the autopilot is refusing to arm, and the reason it gave for refusing.
///
/// Two separate things arrive from the vehicle and neither answers the question on its own.
/// SYS_STATUS carries a pre-arm check bit on every frame, so it says *that* the vehicle will not arm
/// for as long as that stays true -- but it never says why. The why comes as a "PreArm: ..." status
/// text, which is a message rather than a state: the firmware repeats it every thirty seconds or so
/// and QGC drops it again after thirty-five, so it is on screen most of the time and missing some of
/// it.
///
/// Taken together they answer what the operator is actually asking. Taken apart, the bit is a refusal
/// with no reason and the text is a reason that keeps disappearing -- and the fly view's existing
/// banner carries only the text, which is why an operator who looks up a moment too late is left with
/// an aircraft that will not arm and nothing on screen saying why.
QtObject {
    id: _root

    property var vehicle

    /// The vehicle will not arm right now.
    ///
    /// Either signal is enough. The check bit is the steady one and is preferred, but a reason having
    /// arrived is itself proof the autopilot refused -- and a vehicle that does not publish the bit
    /// at all would otherwise be silent here while sending the reason in plain words.
    readonly property bool blocked: !_armed && !_reportsAsEvents && (_checksFailing || (reason !== ""))

    /// The autopilot's own words for it, or "" when it has not sent them -- or sent them long enough
    /// ago that QGC has since let them go
    readonly property string reason: vehicle ? vehicle.prearmError : ""

    /// One line for the panel, empty while there is nothing to say.
    ///
    /// The refusal is worth stating even before its reason arrives, because "will not arm" and "you
    /// have not pressed arm yet" look identical on a grid, and only one of them is a problem the
    /// operator can go and fix.
    readonly property string warning: {
        if (!blocked) {
            return ""
        }
        if (reason !== "") {
            return qsTr("Will not arm — %1").arg(reason)
        }
        return qsTr("Will not arm. Asking the autopilot which check is failing…")
    }

    /// Asks the autopilot what is failing, whenever it is refusing to arm and has not said why.
    ///
    /// Left alone, ArduPilot volunteers the reason once every thirty seconds, and ARMING_OPTIONS bit
    /// 0 turns even that off -- so the answer to the question the operator is asking right now can be
    /// half a minute away, or never coming. Asking runs the checks with reporting forced on, and the
    /// answer arrives as the same status text a volunteered reason would have.
    ///
    /// Stops the moment there is a reason to show, and starts again if that reason goes stale and is
    /// dropped, which is what keeps the line on the panel rather than blinking out every half minute.
    /// Repeated rather than asked once, because the question is a single packet and the answer to a
    /// lost one is to ask again.
    property Timer _reasonRequestTimer: Timer {
        interval:           10000
        repeat:             true
        triggeredOnStart:   true
        running:            _root.blocked && (_root.reason === "")
        onTriggered: {
            if (_root.vehicle) {
                _root.vehicle.requestPrearmCheckReport()
            }
        }
    }

    readonly property bool _armed: vehicle ? vehicle.armed : false

    /// The pre-arm check bit is reported and failing.
    ///
    /// Gated on the vehicle publishing the bit at all, because nothing can be read from its absence:
    /// a vehicle that never sends it is not a vehicle reporting itself healthy.
    readonly property bool _checksFailing: vehicle
                                            ? (vehicle.readyToFlyAvailable && !vehicle.readyToFly)
                                            : false

    /// Firmwares that report their checks as structured events never set prearmError -- QGC drops
    /// those status texts, because the same content arrives in the arming report the toolbar already
    /// draws in full. Repeating one line of it here in different words would put two verdicts on one
    /// state, so this stays quiet for them and leaves that panel to say it.
    readonly property bool _reportsAsEvents: vehicle ? vehicle.healthAndArmingCheckReport.supported : false
}
