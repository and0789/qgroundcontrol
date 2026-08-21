import QGroundControl
import QGroundControl.Controls

/// Switches the tool strip between flying the aircraft and building a plan for it.
///
/// A mode, not a menu. While it is on, the buttons that command the aircraft -- the guided takeoff,
/// land and return -- step aside along with Non-GPS and flow calibration, and the local grid's plan
/// inserts take their places. Only one meaning is ever on screen at a time, which is what makes
/// reusing the positions safe at all.
///
/// The substitution is deliberately not name-for-name. Putting the mission takeoff where the guided
/// takeoff sits would pair two near-identical labels in one position with opposite consequences --
/// one adds a line to a plan, the other lifts the aircraft off the ground -- which is the worst mode
/// confusion the strip could offer. Moved to the head of the group instead it pairs with Non-GPS,
/// impossible to mistake, and the group then reads top to bottom in the order a mission is flown.
ToolStripAction {
    id: _root

    objectName: "flyToolStrip_planButton"

    /// The local grid whose mode this switches
    property var gridView: null

    text:       qsTr("Plan")
    iconSource: "/res/waypoint.svg"
    visible:    QGroundControl.settingsManager.flyViewSettings.showLocalGridView.rawValue
    // Shut while the aircraft is armed. The grid drops the mode itself the moment that happens, and a
    // button that could switch it straight back on would be offering a strip of plan inserts over an
    // aircraft in the air -- with the controls that command it standing aside to make room.
    enabled:    (gridView !== null) && !gridView.vehicleArmed
    checkable:  true
    // Not one of the armed tools, so it does not take part in their exclusive group: arming Waypoint
    // must not switch the mode off underneath it. checked itself is driven from FlyViewToolStrip,
    // where a Binding can survive the write-back the strip's buttons perform -- see the note there.
    nonExclusive: true

    onTriggered: {
        if (gridView) {
            gridView.planEditMode = !gridView.planEditMode
        }
    }
}
