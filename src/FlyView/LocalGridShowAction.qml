import QGroundControl
import QGroundControl.Controls

ToolStripAction {
    id: _root

    objectName: "flyToolStrip_localGridButton"

    text:       _showGrid ? qsTr("Hide Grid") : qsTr("Local Grid")
    iconSource: "/InstrumentValueIcons/target.svg"

    property var  _setting:  QGroundControl.settingsManager.flyViewSettings.showLocalGridView
    property bool _showGrid: _setting.rawValue

    onTriggered: _setting.rawValue = !_showGrid
}
