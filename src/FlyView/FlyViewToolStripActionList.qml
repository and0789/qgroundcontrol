import QtQml.Models

import QGroundControl
import QGroundControl.Controls
import QGroundControl.Viewer3D

ToolStripActionList {
    id: _root

    signal displayPreFlightChecklist
    signal displayOpticalFlowCalibration

    model: [
        Viewer3DShowAction { },
        PreFlightCheckListShowAction { onTriggered: displayPreFlightChecklist() },
        LocalGridShowAction { },
        NonGpsStatusShowAction { },
        OpticalFlowCalibrationShowAction { onTriggered: displayOpticalFlowCalibration() },
        GuidedActionTakeoff { },
        GuidedActionLand { },
        GuidedActionRTL { },
        GuidedActionPause { },
        FlyViewAdditionalActionsButton { },
        FlyViewGripperButton { }
    ]
}
