import QtQuick

import QGroundControl
import QGroundControl.Controls

/// Checks that the operator has said where the aircraft is standing, since the last time it flew.
///
/// An aircraft navigating on optical flow carries its position forward by dead reckoning, and
/// landing does not undo the creep in that estimate: the frame the next mission is flown in is the
/// frame the last one drifted into. Because the plan is drawn against the origin, a frame that has
/// slid puts every waypoint out by the same distance in the same direction -- the pattern is right
/// and the ground track is not. Nothing on screen says so either, because the aircraft reports
/// itself exactly where the plan says it should be. It is found at the flight line, by watching the
/// aircraft fly the shape somewhere other than where it was drawn.
///
/// The remedy costs one click and it is the click nobody makes unprompted, because the display gives
/// no reason to. Asking for it once per flight is what turns it from a thing an operator remembers
/// into a thing the checklist holds them to.
///
/// Only asked of a vehicle that already has an origin: without one there is nothing to state a
/// position inside, and PreFlightEstimatorOriginCheck is already saying so. Two rows failing over
/// the same missing origin would teach the operator to click past both.
///
/// No override. It is not a judgement call about a marginal sensor -- it is a statement only the
/// operator can make, and clicking past it is the same as not having made it.
PreFlightCheckButton {
    name:                   qsTr("Position confirmed")
    visible:                _navigatingWithoutGNSS && _originValid
    telemetryFailure:       _navigatingWithoutGNSS && _originValid && !_confirmed
    // Names the one-press route first. It is the case an operator is usually in between two
    // flights -- the aircraft carried back to the mark it took off from -- and it asks nothing of
    // them but the press. The click on the grid is for the other case, where the aircraft is
    // standing somewhere they have to point at.
    telemetryTextFailure:   qsTr("Not stated since the last flight. If the aircraft is back on the origin, press 'Vehicle is on the origin'. Otherwise click where it is standing and choose 'Vehicle is here…'.")

    property bool _navigatingWithoutGNSS:   globals.activeVehicle ? globals.activeVehicle.navigatingWithoutGNSS : false
    property bool _originValid:             globals.activeVehicle ? globals.activeVehicle.estimatorOrigin.isValid : false
    property bool _confirmed:               globals.activeVehicle ? globals.activeVehicle.positionConfirmedSinceLastFlight : false
}
