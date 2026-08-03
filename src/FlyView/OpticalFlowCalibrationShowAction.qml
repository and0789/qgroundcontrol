import QGroundControl
import QGroundControl.Controls

ToolStripAction {
    objectName: "flyToolStrip_opticalFlowCalibrationButton"

    text:       qsTr("Flow Cal")
    iconSource: "/InstrumentValueIcons/target.svg"
    // Calibration is a bench procedure with the propellers off, so it is pointless in the air
    enabled:    _activeVehicle && !_activeVehicle.armed

    property var _activeVehicle: QGroundControl.multiVehicleManager.activeVehicle
}
