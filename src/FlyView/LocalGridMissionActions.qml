import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls

/// What can be done with the plan as a whole, from the grid it was built on.
///
/// Building a mission here and then having to leave for the Plan view to send it is the same break
/// this whole view exists to remove: the operator loses sight of the aircraft and the pattern at the
/// moment they are committing to it.
///
/// The destructive ones ask first. Clearing a plan and overwriting one with the vehicle's copy both
/// throw away work that took a flight line to build, and neither can be undone.
Rectangle {
    id: _root

    property var planMasterController: null

    readonly property var _missionController: planMasterController ? planMasterController.missionController : null
    readonly property bool _hasController:    planMasterController !== null
    readonly property bool _offline:          _hasController ? planMasterController.offline : true
    readonly property bool _dirtyForUpload:   _hasController ? planMasterController.dirtyForUpload : false
    readonly property bool _hasItems:         _missionController ? (_missionController.visualItems.count > 1) : false

    implicitWidth:  layout.implicitWidth + (_margins * 2)
    implicitHeight: layout.implicitHeight + (_margins * 2)
    color:          qgcPal.window
    opacity:        0.9
    radius:         ScreenTools.defaultFontPixelHeight / 4
    visible:        _hasController

    property real _margins: ScreenTools.defaultFontPixelHeight / 3

    QGCPalette { id: qgcPal; colorGroupEnabled: enabled }

    function _confirm(title, message, action) {
        QGroundControl.showMessageDialog(_root, title, message, Dialog.Yes | Dialog.Cancel, action)
    }

    function _clear() {
        // Removing from the vehicle as well when one is connected, matching what the Plan view does:
        // clearing only the editor would leave the aircraft holding the plan that was just discarded
        if (_offline) {
            _confirm(qsTr("Clear"),
                     qsTr("Remove every item from the plan?"),
                     function() { planMasterController.removeAll() })
        } else {
            _confirm(qsTr("Clear"),
                     qsTr("Remove the plan from the vehicle and from here?"),
                     function() { planMasterController.removeAllFromVehicle() })
        }
    }

    function _download() {
        _confirm(qsTr("Download"),
                 qsTr("Replace the plan here with the one on the vehicle? Anything not sent is lost."),
                 function() { planMasterController.loadFromVehicle() })
    }

    function _load() {
        fileDialog.title =       qsTr("Select Plan File")
        fileDialog.nameFilters = planMasterController.loadNameFilters
        fileDialog.openForLoad()
    }

    function _save() {
        fileDialog.title =       qsTr("Save Plan")
        fileDialog.nameFilters = planMasterController.saveNameFilters
        fileDialog.openForSave()
    }

    QGCFileDialog {
        id:     fileDialog
        folder: QGroundControl.settingsManager.appSettings.missionSavePath

        onAcceptedForSave: (file) => {
            if (_root.planMasterController.saveToFile(file)) {
                close()
            }
        }

        onAcceptedForLoad: (file) => {
            _root.planMasterController.loadFromFile(file)
            close()
        }
    }

    ColumnLayout {
        id:                 layout
        anchors.margins:    _root._margins
        anchors.left:       parent.left
        anchors.top:        parent.top
        spacing:            ScreenTools.defaultFontPixelHeight / 6

        QGCLabel {
            font.pointSize: ScreenTools.smallFontPointSize
            font.bold:      true
            text:           qsTr("Mission")
        }

        RowLayout {
            spacing: ScreenTools.defaultFontPixelWidth / 2

            QGCButton {
                objectName: "localGrid_uploadMissionButton"
                // Highlighted while the vehicle is holding something older than what is on screen,
                // since that difference is invisible otherwise
                primary:    _root._dirtyForUpload
                text:       qsTr("Upload")
                enabled:    !_root._offline && _root._hasItems
                onClicked:  _root.planMasterController.sendToVehicle()
            }

            QGCButton {
                objectName: "localGrid_downloadMissionButton"
                text:       qsTr("Download")
                enabled:    !_root._offline
                onClicked:  _root._download()
            }
        }

        RowLayout {
            spacing: ScreenTools.defaultFontPixelWidth / 2

            QGCButton {
                text:       qsTr("Save")
                enabled:    _root._hasItems
                onClicked:  _root._save()
            }

            QGCButton {
                text:       qsTr("Load")
                onClicked:  _root._load()
            }

            QGCButton {
                objectName: "localGrid_clearMissionButton"
                text:       qsTr("Clear")
                enabled:    _root._hasItems
                onClicked:  _root._clear()
            }
        }
    }
}
