import QGroundControl
import QGroundControl.Controls

/// Ends the plan by sending the aircraft home.
///
/// Refused, not merely warned about, when a return would climb out of the rangefinder's range. A
/// return goes to RTL_ALT first, and that altitude is a vehicle parameter rather than part of the
/// plan -- so it is the one item an operator can add that the grid's altitude ceiling cannot reach
/// and cannot clamp. Above that range the estimator loses its height source and optical flow loses
/// the height it scales velocity by, both at once and out of reach.
ToolStripAction {
    id: _root

    objectName: "flyToolStrip_planReturnButton"

    /// The local grid this inserts into
    property var gridView: null

    readonly property var _mc: gridView ? gridView.missionController : null

    text:       qsTr("Return")
    iconSource: "/res/rtl.svg"
    visible:    (gridView !== null) && gridView.planEditMode
    enabled:    (gridView !== null) && gridView.canPlaceWaypoints && !gridView.planNeedsTakeoffFirst
                    && (_mc !== null) && (_mc.isInsertLandValid === true)
                    && !gridView.returnAltitudeAboveCeiling

    onTriggered: {
        if (gridView) {
            gridView.insertReturnOrLandItem()
        }
    }
}
