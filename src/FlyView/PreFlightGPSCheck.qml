import QtQuick

import QGroundControl
import QGroundControl.Controls

PreFlightCheckButton {
    name:                           _navigatingWithoutGNSS ? qsTr("GPS (not navigating)") : qsTr("GPS")
    telemetryFailure:               !_navigatingWithoutGNSS && (_3dLockFailure || _satCountFailure)
    telemetryTextFailure:           _3dLockFailure ?
                                        qsTr("Waiting for 3D lock.") :
                                        (_satCountFailure ? _satCountFailureText : "")
    allowTelemetryFailureOverride:  !_3dLockFailure && _satCountFailure && allowOverrideSatCount
    // Waiting for a lock the estimator will never use would leave this item failing forever, and a
    // checklist with a permanently red row stops being read. Asking the operator to confirm the
    // aircraft really is meant to fly GNSS-denied keeps the row meaningful instead of quietly
    // passing it.
    manualText:                     _navigatingWithoutGNSS ?
                                        qsTr("Estimator is not using GNSS. Any GPS fitted is for ground truth only - is that intended?") :
                                        ""

    property bool   allowOverrideSatCount:  false   ///< true: sat count above failureSatCount reguired to pass, false: user can click past satCount <= failureSetCount
    property int    failureSatCount:        -1      ///< -1 indicates no sat count check

    property bool   _navigatingWithoutGNSS: globals.activeVehicle ? globals.activeVehicle.navigatingWithoutGNSS : false
    property bool   _3dLock:                globals.activeVehicle ? globals.activeVehicle.gps.lock.rawValue >= 3 : false
    property int    _satCount:              globals.activeVehicle ? globals.activeVehicle.gps.count.rawValue : 0
    property bool   _3dLockFailure:         !_3dLock
    property bool   _satCountFailure:       failureSatCount !== -1 && _satCount <= failureSatCount
    property string _satCountFailureText:   allowOverrideSatCount ? qsTr("Warning - Sat count below %1.").arg(failureSatCount + 1) : qsTr("Waiting for sat count above %1.").arg(failureSatCount)
}
