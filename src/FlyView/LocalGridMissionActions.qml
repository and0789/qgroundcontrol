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

    /// The grid this belongs to, for the altitude ceiling it knows about
    property var gridView: null

    readonly property var  _itemsTooHigh:   gridView ? gridView.itemsAboveAltitudeLimit : []
    readonly property bool _anyItemTooHigh: _itemsTooHigh.length > 0

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

    readonly property string _limitText: (gridView && gridView.altitudeLimitKnown)
                                            ? (gridView.gridTransform.toDisplay(gridView.altitudeLimitMetres).toFixed(1)
                                               + " " + gridView.gridTransform.displayUnits)
                                            : qsTr("--")

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

        // Placed where the plan is committed, and holding Upload shut while it stands. A warning
        // beside the button that sends the plan is one the operator meets at the moment it matters;
        // one tucked into an item editor is met only by chance.
        QGCLabel {
            Layout.maximumWidth:    ScreenTools.defaultFontPixelWidth * 28
            visible:                _root._anyItemTooHigh
            wrapMode:               Text.WordWrap
            font.pointSize:         ScreenTools.smallFontPointSize
            color:                  qgcPal.colorOrange
            text:                   qsTr("Item %1 climbs past the rangefinder's %2 range — %3. Lower it before flying.")
                                        .arg(_root._itemsTooHigh.join(", "))
                                        .arg(_root._limitText)
                                        .arg(_root.gridView ? _root.gridView.altitudeLimitReason : "")
        }

        RowLayout {
            spacing: ScreenTools.defaultFontPixelWidth / 2

            QGCButton {
                objectName: "localGrid_uploadMissionButton"
                // Highlighted while the vehicle is holding something older than what is on screen,
                // since that difference is invisible otherwise
                primary:    _root._dirtyForUpload
                text:       qsTr("Upload")
                // Held shut rather than warned about twice. This is the failure that runs a vehicle
                // away rather than merely degrading it, and the remedy is one field.
                enabled:    !_root._offline && _root._hasItems && !_root._anyItemTooHigh
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
