import QGroundControl
import QGroundControl.Controls

/// Ends the plan by landing where the pattern finishes, rather than flying home first.
///
/// QGC's own insert strip cannot say this on a multirotor: its landing button produces a return to
/// launch. That is the right ending for a pattern that starts and finishes in the same place, and
/// the wrong one for a pattern meant to finish at its far corner -- which is most of the patterns
/// this grid exists to fly.
///
/// Armed rather than inserted outright, because unlike a return it needs a point: the whole
/// difference between this and Return is that it lands somewhere the operator chooses.
ToolStripAction {
    id: _root

    objectName: "flyToolStrip_planLandButton"

    /// The local grid this arms
    property var gridView: null

    text:       qsTr("Land")
    iconSource: "/res/land.svg"
    // Multirotor only. On a fixed wing the Return button above already inserts a real landing
    // pattern, which flies its own altitudes and has nothing to do with RTL_ALT.
    visible:    (gridView !== null) && gridView.planEditMode
                    && (gridView.vehicle !== null) && gridView.vehicle.multiRotor
    enabled:    (gridView !== null) && !gridView.planNeedsTakeoffFirst && gridView.canInsertLandHere
    checkable:  true
    // checked is driven from FlyViewToolStrip rather than bound here: the strip's buttons write
    // checked back into their action, which would destroy a binding on it and leave this showing
    // armed after the grid has already put the tool down.

    onTriggered: {
        if (gridView) {
            gridView.toggleArmedTool("landHere")
        }
    }
}
