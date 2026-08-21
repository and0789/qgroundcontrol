import QtQuick
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls

/// Shapes the whole pattern rather than adding to it: turn it, move it, and pin the nose.
///
/// The one plan control that keeps a drop panel. Waypoint and ROI gave theirs up because they are
/// used over and over while a pattern is drawn, and a panel reopened for each one puts a tap and a
/// moving target between the operator and every item. These three are the opposite: two of them need
/// a number typed before they mean anything, and turning a pattern square to a room is done by eye
/// and in more than one go -- a quarter turn, a look at the grid, then the rest. A panel that stays
/// open across those attempts is what that wants.
///
/// Outside the strip's exclusive group, like the Plan switch itself. Opening this is not picking up
/// a tool, so a waypoint already armed stays armed and its button stays lit.
ToolStripAction {
    id: _root

    objectName: "flyToolStrip_planShapeButton"

    /// The local grid whose pattern this shapes
    property var gridView: null

    readonly property var  _mc:       gridView ? gridView.missionController : null
    readonly property bool _canPlace: gridView ? gridView.canPlaceWaypoints : false

    readonly property bool   _canTransform:           gridView ? gridView.canTransformPlan : false
    readonly property string _transformBlockedReason: gridView ? gridView.transformBlockedReason : ""
    readonly property string _distanceUnits: (gridView && gridView.gridTransform)
                                                ? gridView.gridTransform.displayUnits : ""

    /// Nothing to shape until the plan has its takeoff, which is the same gate every other plan
    /// control on the strip is behind
    readonly property bool _needsTakeoff: gridView ? gridView.planNeedsTakeoffFirst : false

    text:       qsTr("Shape")
    iconSource: "/qmlimages/MapDrawShape.svg"
    visible:    (gridView !== null) && gridView.planEditMode
    enabled:    gridView !== null
    // Opening a panel is not arming a tool, so it neither clears the armed one nor is cleared by it
    nonExclusive: true

    /// What the last shaping did, for the panel to say back. The pattern turning on the grid is easy
    /// to miss on a bright screen, and nothing has reached the aircraft until the plan is uploaded.
    property string _shapedResult: ""

    /// Turns the whole pattern. No confirmation, unlike the move to the aircraft in the actions
    /// panel: the angle was typed and then applied, which is two deliberate acts already, and the way
    /// back is the same angle negated.
    function _rotatePattern(degreesCW) {
        if (!gridView || isNaN(degreesCW)) {
            return
        }
        const turned = gridView.rotatePlan(degreesCW)
        _shapedResult = turned > 0
                            ? qsTr("%1 item(s) turned. Upload the plan to fly it.").arg(turned)
                            : qsTr("Nothing turned.")
    }

    /// Moves the whole pattern, in the units on screen
    function _movePattern(northDisplay, eastDisplay) {
        if (!gridView || !gridView.gridTransform || isNaN(northDisplay) || isNaN(eastDisplay)) {
            return
        }
        const transform = gridView.gridTransform
        const moved = gridView.nudgePlan(transform.fromDisplay(northDisplay), transform.fromDisplay(eastDisplay))
        _shapedResult = moved > 0
                            ? qsTr("%1 item(s) moved. Upload the plan to fly it.").arg(moved)
                            : qsTr("Nothing moved.")
    }

    dropPanelComponent: Component {
        ColumnLayout {
            spacing: ScreenTools.defaultFontPixelWidth * 0.5

            // Cleared on every open. The panel is rebuilt each time it is shown, so what the last
            // shaping did is read in the same breath as the button that caused it and never comes
            // back later as a stale sentence about a plan that has since changed.
            Component.onCompleted: _root._shapedResult = ""

            // What a room asks for and a map never does: the pattern is square to the walls or it is
            // not, and the angle that makes it square is one number rather than a new position for
            // every waypoint. It turns about the point the pattern starts from; the takeoff stays
            // pinned where the aircraft stands, and a yaw item's heading turns with the pattern so
            // the nose keeps the direction it was given relative to it.
            QGCLabel { text: qsTr("Shape the pattern") }

            RowLayout {
                Layout.fillWidth:   true
                spacing:            ScreenTools.defaultFontPixelWidth / 2

                QGCLabel {
                    font.pointSize: ScreenTools.smallFontPointSize
                    text:           qsTr("Turn")
                }

                QGCTextField {
                    id:                     rotateField
                    objectName:             "localGrid_planRotateField"
                    Layout.preferredWidth:  ScreenTools.defaultFontPixelWidth * 7
                    font.pointSize:         ScreenTools.smallFontPointSize
                    text:                   "90"
                    unitsLabel:             qsTr("° CW")
                }

                QGCButton {
                    objectName: "localGrid_planRotateButton"
                    text:       qsTr("Turn")
                    enabled:    _root._canTransform
                    // The panel stays open. Squaring a pattern to a room is done by eye and in more
                    // than one go -- a quarter turn, then a look at the grid, then the rest.
                    onClicked:  _root._rotatePattern(parseFloat(rotateField.text))
                }
            }

            // The other half of squaring a pattern to a room: clear of the net, off the wall.
            // Deliberate, unlike the drift correction and the move to the aircraft, which are both
            // remedies for the frame having shifted under a pattern that was drawn correctly.
            RowLayout {
                Layout.fillWidth:   true
                spacing:            ScreenTools.defaultFontPixelWidth / 2

                QGCLabel {
                    font.pointSize: ScreenTools.smallFontPointSize
                    text:           qsTr("Move (%1)").arg(_root._distanceUnits)
                }

                QGCTextField {
                    id:                     moveNorthField
                    objectName:             "localGrid_planMoveNorthField"
                    Layout.preferredWidth:  ScreenTools.defaultFontPixelWidth * 7
                    font.pointSize:         ScreenTools.smallFontPointSize
                    text:                   "0"
                    unitsLabel:             qsTr("N")
                }

                QGCTextField {
                    id:                     moveEastField
                    objectName:             "localGrid_planMoveEastField"
                    Layout.preferredWidth:  ScreenTools.defaultFontPixelWidth * 7
                    font.pointSize:         ScreenTools.smallFontPointSize
                    text:                   "0"
                    unitsLabel:             qsTr("E")
                }

                QGCButton {
                    objectName: "localGrid_planMoveButton"
                    text:       qsTr("Move")
                    enabled:    _root._canTransform
                    onClicked:  _root._movePattern(parseFloat(moveNorthField.text),
                                                   parseFloat(moveEastField.text))
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
                Layout.topMargin:   ScreenTools.defaultFontPixelHeight / 3
                text:               qsTr("Hold heading")
                enabled:            _root._canPlace && !_root._needsTakeoff
                                        && _root._mc && (_root._mc.flyThroughCommandsAllowed === true)
                onClicked: {
                    const heading = _root.gridView.vehicleHeadingDegrees
                    _root.gridView.insertConditionYaw(isNaN(heading) ? 0 : heading)
                    dropPanel.hide()
                }
            }

            // Why the two above are shut, rather than leaving them dead and unexplained. Only shown
            // when there is a pattern to shape: an empty grid explains itself.
            QGCLabel {
                objectName:             "localGrid_planShapeBlockedReason"
                Layout.maximumWidth:    ScreenTools.defaultFontPixelWidth * 24
                visible:                _root._transformBlockedReason !== ""
                wrapMode:               Text.WordWrap
                font.pointSize:         ScreenTools.smallFontPointSize
                color:                  qgcPal.colorGrey
                text:                   _root._transformBlockedReason
            }

            QGCLabel {
                objectName:             "localGrid_planShapeResult"
                Layout.maximumWidth:    ScreenTools.defaultFontPixelWidth * 24
                visible:                _root._shapedResult !== ""
                wrapMode:               Text.WordWrap
                font.pointSize:         ScreenTools.smallFontPointSize
                color:                  qgcPal.colorGreen
                text:                   _root._shapedResult
            }

            QGCPalette { id: qgcPal; colorGroupEnabled: true }
        }
    }
}
