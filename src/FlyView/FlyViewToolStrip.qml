import QtQml.Models

import QGroundControl
import QGroundControl.Controls
import QGroundControl.FlyView

ToolStrip {
    id: _root

    signal displayPreFlightChecklist
    signal displayOpticalFlowCalibration

    readonly property var _gridView: globals.localGridViewFlyView

    FlyViewToolStripActionList {
        id: flyViewToolStripActionList

        onDisplayPreFlightChecklist:     _root.displayPreFlightChecklist()
        onDisplayOpticalFlowCalibration: _root.displayOpticalFlowCalibration()
    }

    // The local grid owns which plan tool is in hand; these four buttons only show it. Driven
    // through Binding elements rather than bound inside the actions themselves, which is the same
    // shape PlanView uses for its own Waypoint and ROI buttons and for the same reason: the strip's
    // buttons write checked back into their action, and an ordinary binding is destroyed the first
    // time that happens. A Binding re-asserts itself whenever the value changes, so a tool the grid
    // puts down on its own -- leaving plan mode drops whatever was armed -- still releases its
    // button instead of sitting there lit with nothing armed behind it.
    Binding {
        target:     flyViewToolStripActionList.planAction
        property:   "checked"
        value:      _root._gridView ? _root._gridView.planEditMode : false
    }

    Binding {
        target:     flyViewToolStripActionList.planWaypointAction
        property:   "checked"
        value:      _root._gridView ? (_root._gridView.armedTool === "waypoint") : false
    }

    Binding {
        target:     flyViewToolStripActionList.planRoiAction
        property:   "checked"
        value:      _root._gridView ? (_root._gridView.armedTool === "roi") : false
    }

    Binding {
        target:     flyViewToolStripActionList.planLandAction
        property:   "checked"
        value:      _root._gridView ? (_root._gridView.armedTool === "landHere") : false
    }

    model: flyViewToolStripActionList.model
}
