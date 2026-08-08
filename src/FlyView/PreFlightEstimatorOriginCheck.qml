import QtQuick

import QGroundControl
import QGroundControl.Controls

/// Checks that a vehicle navigating without GNSS has been given an estimator origin.
///
/// Without one the vehicle has no home, so an altitude relative to home cannot be resolved: an auto
/// takeoff climbs and then hangs on the mission's first item, reporting nothing, with the aircraft
/// hovering. There is no override because there is nothing to override -- the mission cannot run,
/// and setting an origin is one click on the fly view map.
PreFlightCheckButton {
    name:                   qsTr("Estimator origin")
    visible:                _navigatingWithoutGNSS
    telemetryFailure:       _navigatingWithoutGNSS && !_originValid
    telemetryTextFailure:   qsTr("Not set. Click the map and choose 'Set Estimator Origin'.")

    property bool _navigatingWithoutGNSS:   globals.activeVehicle ? globals.activeVehicle.navigatingWithoutGNSS : false
    property bool _originValid:             globals.activeVehicle ? globals.activeVehicle.estimatorOrigin.isValid : false
}
