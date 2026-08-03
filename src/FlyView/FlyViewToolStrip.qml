import QtQml.Models

import QGroundControl
import QGroundControl.Controls
import QGroundControl.FlyView

ToolStrip {
    id: _root

    signal displayPreFlightChecklist
    signal displayOpticalFlowCalibration

    FlyViewToolStripActionList {
        id: flyViewToolStripActionList

        onDisplayPreFlightChecklist:     _root.displayPreFlightChecklist()
        onDisplayOpticalFlowCalibration: _root.displayOpticalFlowCalibration()
    }

    model: flyViewToolStripActionList.model
}
