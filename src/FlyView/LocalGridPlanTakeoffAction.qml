import QGroundControl
import QGroundControl.Controls

/// Puts the plan's takeoff on the origin, which is where the aircraft is standing.
///
/// First of the plan group and the only one enabled on an empty plan, so the strip itself teaches
/// the order a mission has to be in. That is the whole reason it is not a slot-for-slot replacement
/// for the guided takeoff below it: those two would sit in one position under nearly the same word,
/// one adding a line to a plan and the other lifting the aircraft off the ground.
ToolStripAction {
    id: _root

    objectName: "flyToolStrip_planTakeoffButton"

    /// The local grid this inserts into
    property var gridView: null

    readonly property var _mc: gridView ? gridView.missionController : null

    text:       qsTr("Take off")
    iconSource: "/res/takeoff.svg"
    visible:    (gridView !== null) && gridView.planEditMode
    // Refused once the plan already begins with one, which MissionController works out against the
    // point the grid inserts at rather than from the item list alone
    enabled:    (gridView !== null) && gridView.canPlaceWaypoints
                    && (_mc !== null) && (_mc.isInsertTakeoffValid === true)

    onTriggered: {
        if (gridView) {
            gridView.insertTakeoffAtOrigin()
        }
    }
}
