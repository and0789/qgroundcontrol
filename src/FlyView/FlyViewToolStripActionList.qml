import QtQml.Models

import QGroundControl
import QGroundControl.Controls
import QGroundControl.Viewer3D

ToolStripActionList {
    id: _root

    signal displayPreFlightChecklist
    signal displayOpticalFlowCalibration

    /// Exposed so FlyViewToolStrip can drive their checked state through Binding elements, which is
    /// the only form of it that survives the write-back the strip's own buttons perform
    property alias planAction:          planButton
    property alias planWaypointAction:  planWaypointButton
    property alias planRoiAction:       planRoiButton
    property alias planLandAction:      planLandButton
    /// Same, for the after-flight entry -- which is lit for emphasis rather than to show a mode
    property alias afterFlightAction:   afterFlightButton

    readonly property var  _gridView:     globals.localGridViewFlyView
    readonly property bool _planEditMode: _gridView ? _gridView.planEditMode : false

    /// The strip is two strips in one place. While the local grid is building a plan, the buttons
    /// that command the aircraft step aside for the mission items of the same kind, so only one
    /// meaning is ever on screen -- which is what makes it safe to reuse the positions at all.
    ///
    /// Each plan button is declared beside the flying button whose place it takes, so the two
    /// columns can be read down this list. The pairing is deliberately not name-for-name: putting
    /// the mission takeoff where the guided one sits would leave two near-identical labels in one
    /// position with opposite consequences -- one adds a line to a plan, the other lifts the
    /// aircraft off the ground -- which is the worst mode confusion the strip could offer. Moved to
    /// the head of the plan group instead, it pairs with Non-GPS, and the group then reads top to
    /// bottom in the order a mission is flown: take off, fly the pattern, point the nose, come home.
    ///
    /// Pause keeps its place in both. It is the control that stops what is already happening, and a
    /// plan edited while the aircraft is flying one is exactly when that is wanted.
    model: [
        Viewer3DShowAction { },
        PreFlightCheckListShowAction { onTriggered: displayPreFlightChecklist() },
        LocalGridShowAction { },
        LocalGridPlanAction { id: planButton; gridView: _root._gridView },

        NonGpsStatusShowAction { visible: !_root._planEditMode },
        LocalGridPlanTakeoffAction { gridView: _root._gridView },

        OpticalFlowCalibrationShowAction {
            visible:        !_root._planEditMode
            onTriggered:    displayOpticalFlowCalibration()
        },
        LocalGridPlanWaypointAction { id: planWaypointButton; gridView: _root._gridView },

        // Standing on its own between the two groups rather than paired, because it belongs to
        // neither: it is not a way of building a pattern and not a way of commanding the aircraft,
        // it is what is done to the estimate and the plan between two flights. It is also the only
        // entry here that comes and goes with the state of the aircraft rather than with the mode,
        // so there is nothing for it to stand in for and nothing to stand in for it.
        LocalGridAfterFlightAction { id: afterFlightButton; gridView: _root._gridView },

        GuidedActionTakeoff { },
        LocalGridPlanRoiAction { id: planRoiButton; gridView: _root._gridView },

        GuidedActionLand { },
        LocalGridPlanReturnAction { gridView: _root._gridView },

        GuidedActionRTL { },
        LocalGridPlanLandAction { id: planLandButton; gridView: _root._gridView },

        // Below the group that builds the pattern, because it acts on the whole of one rather than
        // adding to it -- and so it has no flying button to stand in for, which is why the strip is
        // one row longer in plan mode than out of it.
        LocalGridPlanShapeAction { gridView: _root._gridView },

        GuidedActionPause { },
        FlyViewAdditionalActionsButton { },
        FlyViewGripperButton { }
    ]
}
