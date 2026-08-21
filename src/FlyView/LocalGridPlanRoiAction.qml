import QGroundControl
import QGroundControl.Controls

/// Arms ROI placement, or cancels an ROI already running in the plan.
///
/// Worth more here than on a map. Without GNSS the compass is the only absolute reference the
/// aircraft has, and the optical flow sensor measures in the airframe's own frame -- so which way
/// the nose points while a leg is flown is part of what is being measured, not a detail of how it
/// looks. A pattern flown nose-forward and the same pattern flown pointed at a fixed mark are two
/// different experiments.
ToolStripAction {
    id: _root

    objectName: "flyToolStrip_planRoiButton"

    /// The local grid this arms
    property var gridView: null

    readonly property var  _mc:  gridView ? gridView.missionController : null
    readonly property bool _roiRunning: (_mc !== null) && (_mc.isROIActive === true)

    text:       _roiRunning ? qsTr("Cancel ROI") : qsTr("ROI")
    iconSource: "/qmlimages/roi.svg"
    // Hidden outright on firmware that does not support it, rather than shown dead: a button that
    // can never work on this airframe is not a refusal the operator can act on
    visible:    (gridView !== null) && gridView.planEditMode
                    && (gridView.vehicle !== null) && gridView.vehicle.supports.roiMode
    enabled:    (gridView !== null) && gridView.canPlaceWaypoints && !gridView.planNeedsTakeoffFirst
                    && (_mc !== null) && (_mc.isInsertROIValid === true)
    // Cancelling carries no coordinate to place, so it is done outright rather than armed
    checkable:  !_roiRunning
    // checked is driven from FlyViewToolStrip rather than bound here: the strip's buttons write
    // checked back into their action, which would destroy a binding on it and leave this showing
    // armed after the grid has already put the tool down.

    onTriggered: {
        if (!gridView) {
            return
        }
        if (_roiRunning) {
            gridView.insertCancelROIItem()
        } else {
            gridView.toggleArmedTool("roi")
        }
    }
}
