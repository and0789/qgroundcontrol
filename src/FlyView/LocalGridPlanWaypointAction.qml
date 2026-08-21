import QGroundControl
import QGroundControl.Controls

/// Arms waypoint placement: tap this, then tap the grid, and keep tapping to lay out a pattern.
///
/// Stays armed between placements on purpose. Building a box means four taps in a row, and a control
/// that disarmed itself after each one would put a second tap between every corner.
///
/// Dead until the plan has a takeoff. A mission is flown from its first item and that item has to be
/// the takeoff, so a plan that begins with a waypoint does not climb -- the aircraft sits there.
///
/// The grid itself still refuses to build such a plan: addMissionItemAt puts a takeoff on the origin
/// underneath a first waypoint rather than let one exist. That backstop stays, but nothing on screen
/// reaches it any more -- this button and the press-and-hold gesture are both shut until the takeoff
/// is placed, and the grid says so in a line of its own. The rule is asked for rather than arranged
/// silently, because an operator who is shown it can see a plan that breaks it and one who is
/// protected from it cannot.
ToolStripAction {
    id: _root

    objectName: "flyToolStrip_planWaypointButton"

    /// The local grid this arms
    property var gridView: null

    readonly property var _mc: gridView ? gridView.missionController : null

    text:       qsTr("Waypoint")
    iconSource: "/res/waypoint.svg"
    visible:    (gridView !== null) && gridView.planEditMode
    enabled:    (gridView !== null) && gridView.canPlaceWaypoints && !gridView.planNeedsTakeoffFirst
                    && (_mc !== null) && (_mc.flyThroughCommandsAllowed === true)
    checkable:  true
    // checked is driven from FlyViewToolStrip rather than bound here: the strip's buttons write
    // checked back into their action, which would destroy a binding on it and leave this showing
    // armed after the grid has already put the tool down.

    onTriggered: {
        if (gridView) {
            gridView.toggleArmedTool("waypoint")
        }
    }
}
