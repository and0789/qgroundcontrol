import QGroundControl
import QGroundControl.Controls

ToolStripAction {
    objectName: "flyToolStrip_opticalFlowCalibrationButton"

    // Spelled out now that the strip wraps a long label instead of clipping it. "Flow Cal" was an
    // abbreviation forced by a single line, and it reads as a truncation rather than a name.
    text:       qsTr("Flow Calibration")
    iconSource: "/InstrumentValueIcons/target.svg"
    // Calibration is a bench procedure with the propellers off, so it is pointless in the air
    enabled:    _activeVehicle && !_activeVehicle.armed

    property var _activeVehicle: QGroundControl.multiVehicleManager.activeVehicle
}
