import QtQuick
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls
import QGroundControl.FlyView

/// Opens the same four inserts the local grid's own click panel offers -- waypoint, takeoff, return
/// or landing, and ROI -- without a click to hang a drop panel from. The strip has no map to click
/// on the way the Plan view's tool strip does, so this one carries its own panel instead of putting
/// the buttons in the strip directly.
///
/// Kept out of the strip's own row of buttons on purpose. The strip already carries Takeoff, Land
/// and RTL buttons that fly the vehicle right now (GuidedActionTakeoff and its neighbours); a second
/// set of buttons with the same three names two centimetres below them, meaning "add this to the
/// plan" instead of "do this now", is a mistake with the vehicle's altitude behind it. One drop
/// panel keeps the two meanings apart, at the cost of one extra tap to reach either.
ToolStripAction {
    id: _root

    objectName: "flyToolStrip_planButton"

    /// The local grid this arms and inserts into
    property var gridView: null

    readonly property var  _mc:        gridView ? gridView.missionController : null
    readonly property bool _canPlace:  gridView ? gridView.canPlaceWaypoints : false
    readonly property bool _isMultiRotor: (gridView && gridView.vehicle) ? gridView.vehicle.multiRotor : false
    readonly property bool _roiSupported: (gridView && gridView.vehicle) ? gridView.vehicle.supports.roiMode : false

    // The label is the armed-tool indicator, not the button's own checked state. checked cannot
    // carry this: the moment dropPanelComponent is set, ToolStripHoverButton takes checked over to
    // mean "is my drop panel open" and, on the first click, overwrites the declarative binding this
    // action's own checked would need with a plain value -- so a checked driven off armedTool would
    // go stale after one use. text stays a real binding throughout, so it is what says whether the
    // next tap on the grid places something.
    text: {
        if (!gridView) {
            return qsTr("Plan")
        }
        switch (gridView.armedTool) {
        case "waypoint": return qsTr("Tap: Waypoint")
        case "roi":      return qsTr("Tap: ROI")
        default:         return qsTr("Plan")
        }
    }
    iconSource: "/res/waypoint.svg"
    visible:    QGroundControl.settingsManager.flyViewSettings.showLocalGridView.rawValue
    enabled:    gridView !== null

    function _cannotAddTakeoffReason() {
        if (!_root._mc || (_root._mc.isInsertTakeoffValid === true)) {
            return ""
        }
        return _root.gridView.planHasTakeoff
                ? qsTr("This plan already begins with a takeoff.")
                : qsTr("A takeoff can only be the first item.")
    }

    dropPanelComponent: Component {
        ColumnLayout {
            spacing: ScreenTools.defaultFontPixelWidth * 0.5

            QGCLabel { text: qsTr("Add to plan") }

            QGCButton {
                objectName:         "localGrid_planWaypointButton"
                Layout.fillWidth:   true
                text:               qsTr("Waypoint")
                checkable:          true
                checked:            _root.gridView && (_root.gridView.armedTool === "waypoint")
                enabled:            _root._canPlace && _root._mc && (_root._mc.flyThroughCommandsAllowed === true)
                onClicked: {
                    _root.gridView.toggleArmedTool("waypoint")
                    dropPanel.hide()
                }
            }

            QGCButton {
                objectName:         "localGrid_planTakeoffButton"
                Layout.fillWidth:   true
                text:               qsTr("Takeoff at origin")
                enabled:            _root._canPlace && _root._mc && (_root._mc.isInsertTakeoffValid === true)
                onClicked: {
                    _root.gridView.insertTakeoffAtOrigin()
                    dropPanel.hide()
                }
            }

            QGCLabel {
                objectName:             "localGrid_planTakeoffRefusedReason"
                Layout.maximumWidth:    ScreenTools.defaultFontPixelWidth * 24
                visible:                _root._canPlace && _root._cannotAddTakeoffReason() !== ""
                wrapMode:               Text.WordWrap
                font.pointSize:         ScreenTools.smallFontPointSize
                color:                  qgcPal.colorGrey
                text:                   _root._cannotAddTakeoffReason()
            }

            QGCButton {
                objectName:         "localGrid_planReturnButton"
                Layout.fillWidth:   true
                text:               _root._isMultiRotor ? qsTr("Return") : qsTr("Land")
                enabled:            _root._canPlace && _root._mc && (_root._mc.isInsertLandValid === true)
                                        && !(_root.gridView && _root.gridView.returnAltitudeAboveCeiling)
                onClicked: {
                    _root.gridView.insertReturnOrLandItem()
                    dropPanel.hide()
                }
            }

            QGCLabel {
                objectName:             "localGrid_planReturnRefusedReason"
                Layout.maximumWidth:    ScreenTools.defaultFontPixelWidth * 24
                visible:                _root._canPlace && _root.gridView && _root.gridView.returnAltitudeAboveCeiling
                wrapMode:               Text.WordWrap
                font.pointSize:         ScreenTools.smallFontPointSize
                color:                  qgcPal.colorOrange
                text:                   qsTr("A return climbs above the rangefinder's range. Lower RTL_ALT, or use 'Land here' on the grid instead.")
            }

            QGCButton {
                objectName:         "localGrid_planRoiButton"
                Layout.fillWidth:   true
                visible:            _root._roiSupported
                text:               (_root._mc && _root._mc.isROIActive) ? qsTr("Cancel ROI") : qsTr("ROI")
                checkable:          !(_root._mc && _root._mc.isROIActive)
                checked:            _root.gridView && (_root.gridView.armedTool === "roi")
                enabled:            _root._canPlace && _root._mc && (_root._mc.isInsertROIValid === true)
                onClicked: {
                    if (_root._mc && _root._mc.isROIActive) {
                        _root.gridView.insertCancelROIItem()
                    } else {
                        _root.gridView.toggleArmedTool("roi")
                    }
                    dropPanel.hide()
                }
            }

            // Points the nose and holds it. A waypoint's own yaw parameter never reaches an
            // ArduCopter -- the 15-byte mission record has no room for it, and QGC's command tree
            // already removes it for these vehicles -- so heading has to be an item of its own.
            //
            // Inserted at the vehicle's current heading rather than at zero: the operator is almost
            // always fixing the nose where it already points, and a yaw item that silently means
            // "turn to north" is one that swings the airframe on the first run.
            QGCButton {
                objectName:         "localGrid_planYawButton"
                Layout.fillWidth:   true
                text:               qsTr("Hold heading")
                enabled:            _root._canPlace && _root._mc && (_root._mc.flyThroughCommandsAllowed === true)
                onClicked: {
                    const heading = _root.gridView.vehicleHeadingDegrees
                    _root.gridView.insertConditionYaw(isNaN(heading) ? 0 : heading)
                    dropPanel.hide()
                }
            }

            QGCLabel {
                objectName:             "localGrid_planCannotPlaceReason"
                Layout.maximumWidth:    ScreenTools.defaultFontPixelWidth * 24
                visible:                !_root._canPlace
                wrapMode:               Text.WordWrap
                font.pointSize:         ScreenTools.smallFontPointSize
                color:                  qgcPal.colorOrange
                text:                   (_root.gridView && _root.gridView.planSyncInProgress)
                                            ? qsTr("The plan is being transferred. Wait for it to finish.")
                                            : qsTr("No estimator origin yet, so there is nowhere to place this against.")
            }

            QGCPalette { id: qgcPal; colorGroupEnabled: true }
        }
    }
}
