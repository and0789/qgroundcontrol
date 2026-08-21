import QGroundControl
import QGroundControl.Controls

ToolStripAction {
    id: _root

    objectName: "flyToolStrip_nonGpsStatusButton"

    text:       _showPanel ? qsTr("Hide Non-GPS") : qsTr("Non-GPS")
    iconSource: "/InstrumentValueIcons/chart.svg"

    property var  _setting:     QGroundControl.settingsManager.flyViewSettings.showNonGpsStatusPanel
    property bool _showPanel:   _setting.rawValue

    onTriggered: _setting.rawValue = !_showPanel
}
