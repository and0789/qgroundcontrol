import QtQuick
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls

/// The plan's Upload, Download, Save and Clear, in the toolbar corner the flying indicators
/// usually hold.
///
/// The same four buttons live in LocalGridMissionActions, bottom-left on the grid -- but that panel
/// folds itself away on a screen too small to carry every panel open at once, and folded it is a
/// title with nothing under it. Reaching the buttons costs a tap to unfold the panel and then a
/// scroll to their row, on a screen already too small for both -- for the four controls a plan
/// cannot be sent, fetched, filed or wiped without. This row is those same four buttons -- driven
/// straight through the panel's own pre-checks and confirmation dialogs, not a second copy of
/// either -- put where FlyViewToolBar's MainStatusIndicator and FlightModeIndicator stand while the
/// plan is being edited, disarmed, on a screen too small for both: the aircraft is not flying, and
/// neither indicator has anything urgent to say that LocalGridReadout is not already saying.
RowLayout {
    id: _root

    /// The grid this drives -- its missionActions child is where Upload/Download/Save/Clear
    /// actually live
    property var gridView: null

    readonly property var _actions: gridView ? gridView.missionActions : null

    spacing: ScreenTools.defaultFontPixelWidth / 2

    QGCButton {
        objectName: "toolbar_localGridUploadButton"
        Layout.fillHeight: true
        text:       (_root._actions !== null) && _root._actions.syncing ? qsTr("Sending…") : qsTr("Upload")
        iconSource: "/res/UploadToVehicle.svg"
        enabled:    (_root._actions !== null) && _root._actions.canUpload
        primary:    (_root._actions !== null) && _root._actions.uploadHighlighted
        onClicked:  _root._actions.requestUpload()
    }

    QGCButton {
        objectName: "toolbar_localGridDownloadButton"
        Layout.fillHeight: true
        text:       qsTr("Download")
        iconSource: "/res/Download.svg"
        enabled:    (_root._actions !== null) && _root._actions.canDownload
        onClicked:  _root._actions.requestDownload()
    }

    QGCButton {
        objectName: "toolbar_localGridSaveButton"
        Layout.fillHeight: true
        text:       qsTr("Save")
        iconSource: "/res/SaveToDisk.svg"
        enabled:    (_root._actions !== null) && _root._actions.canSave
        onClicked:  _root._actions.requestSave()
    }

    QGCButton {
        objectName: "toolbar_localGridClearButton"
        Layout.fillHeight: true
        text:       qsTr("Clear")
        iconSource: "/res/TrashCan.svg"
        enabled:    (_root._actions !== null) && _root._actions.canClear
        onClicked:  _root._actions.requestClear()
    }
}
