import QtQuick
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls
import QGroundControl.FlyView

/// One mission item as a row in a list of them: what it is when read past, and every field it has
/// when it is the one being worked on.
///
/// Shaped after the Plan view's MissionItemEditor, because an operator who has planned a mission
/// there should not have to learn a second way of reading the same list. Collapsed it is a name and
/// a number; the item being edited is the only one that opens, so the list stays readable however
/// many legs the pattern has.
///
/// The fields are LocalGridWaypointEditor, loaded only while this row is the current one. Building
/// every editor up front would mean one set of live bindings on the transform, the altitude limit
/// and the altitude fact per item in the plan, all recomputing on every pan of the grid.
Rectangle {
    id:         _root
    objectName: "localGrid_missionItemRow"

    property var gridView: null

    /// Index into the mission's visual items, which is what every gridView call takes
    property int  visualItemIndex: -1

    /// Where this item comes in the plan, counting from one, and the number on its marker's face.
    ///
    /// Not the mission sequence number. Those count the home position and the DO_CHANGE_SPEED items
    /// QGC folds into a waypoint's speed, so a plan of takeoff-waypoint-land numbered its rows 2 and
    /// 4 -- with the takeoff, and the speed item nobody placed, silently taking 1 and 3.
    property int  itemNumber:      0

    /// NaN for an item the grid has nowhere to put, which is listed all the same
    property real north:           NaN
    property real east:            NaN

    /// Open, and drawn as the one being edited
    property bool isCurrentItem: false

    /// The item the vehicle is flying to right now, which is not the one being edited and is not
    /// the operator's to choose. MissionController fills it from the vehicle's own mission index
    /// while the fly view is up.
    property bool isVehicleTarget: false

    /// Raised by a click anywhere on the row. Which item is current is not this row's to decide --
    /// only the list holding all of them can answer that.
    signal clicked()
    signal removeRequested()

    implicitHeight: contentColumn.implicitHeight
    color:          qgcPal.windowShade
    radius:         ScreenTools.defaultFontPixelHeight / 4
    // Dimmed rather than greyed, the way the Plan view does it, so the open row reads as the one in
    // hand without the others looking disabled
    opacity:        isCurrentItem ? 1.0 : 0.7

    readonly property real _margins:    ScreenTools.defaultFontPixelHeight / 4
    readonly property real _iconSize:   ScreenTools.defaultFontPixelHeight

    /// Which row is open is said by the header strip alone, not by colouring the whole row.
    ///
    /// The editor is seven fields tall, so highlighting the row flooded a third of the panel with
    /// saturated colour -- and left every label in it standing on a background none of them were
    /// coloured for. On a light theme that is dark text on blue; here it survived only because the
    /// ordinary text colour happens to be light too.
    readonly property color _headerColor: isCurrentItem ? qgcPal.buttonHighlight : "transparent"
    readonly property color _textColor:   isCurrentItem ? qgcPal.buttonHighlightText : qgcPal.text

    readonly property string _commandName: (gridView && (visualItemIndex >= 0))
                                            ? gridView.waypointCommandName(visualItemIndex)
                                            : ""

    /// The item's current command, or -1 for one whose type cannot be changed
    readonly property int  _command:      (gridView && (visualItemIndex >= 0))
                                            ? gridView.waypointCommand(visualItemIndex)
                                            : -1
    readonly property bool _commandKnown: _command >= 0

    /// Position in the type list, so the box shows what the item already is rather than always
    /// reading "Waypoint" and inviting a change nobody asked for
    function _typeIndexFor(command) {
        if (!gridView) {
            return 0
        }
        switch (command) {
        case gridView.commandTakeoff:   return 1
        case gridView.commandLand:      return 2
        default:                        return 0
        }
    }

    function _commandForTypeIndex(index) {
        if (!gridView) {
            return -1
        }
        switch (index) {
        case 1:     return gridView.commandTakeoff
        case 2:     return gridView.commandLand
        default:    return gridView.commandWaypoint
        }
    }

    // Changing the type in place rather than deleting and re-adding, so the position already
    // dragged or typed into place survives the change. Turning the last waypoint of a pattern into
    // a landing is the common edit, and rebuilding it from scratch to do that loses the offsets
    // that were the point of placing it.
    function _applyType(index) {
        const command = _commandForTypeIndex(index)
        if ((command >= 0) && gridView) {
            gridView.setWaypointCommand(visualItemIndex, command)
        }
    }

    on_CommandChanged: typeCombo.currentIndex = _typeIndexFor(_command)

    QGCPalette { id: qgcPal; colorGroupEnabled: enabled }

    MouseArea {
        anchors.fill:   parent
        onClicked:      _root.clicked()
    }

    ColumnLayout {
        id:                 contentColumn
        anchors.left:       parent.left
        anchors.right:      parent.right
        anchors.top:        parent.top
        spacing:            0

        // Runs the full width of the row and carries the highlight, with only its top corners
        // rounded so it sits flush against the editor below rather than showing a notch of row
        // background at each shoulder
        Rectangle {
            Layout.fillWidth:   true
            implicitHeight:     headerRow.implicitHeight + (_root._margins * 2)
            color:              _root._headerColor
            topLeftRadius:      _root.radius
            topRightRadius:     _root.radius

            RowLayout {
                id:                     headerRow
                anchors.left:           parent.left
                anchors.right:          parent.right
                anchors.margins:        _root._margins
                anchors.verticalCenter: parent.verticalCenter
                spacing:                ScreenTools.defaultFontPixelWidth / 2

                // Carries the same green disc the marker on the grid wears, so the row and the
                // marker for the waypoint the vehicle is flying to are recognisably the same thing.
                // Being edited is shown by the row opening; this says nothing about that.
                Rectangle {
                    Layout.preferredWidth:  _root._iconSize
                    Layout.preferredHeight: _root._iconSize
                    Layout.alignment:       Qt.AlignVCenter
                    radius:                 width / 2
                    color:                  _root.isVehicleTarget ? qgcPal.colorGreen : "transparent"

                    QGCLabel {
                        anchors.centerIn:   parent
                        font.pointSize:     ScreenTools.smallFontPointSize
                        font.bold:          true
                        color:              _root.isVehicleTarget ? qgcPal.window : _root._textColor
                        text:               _root.itemNumber
                    }
                }

                // What the item is, kept on the row whether or not it is open. Scanning a pattern is
                // reading this column -- takeoff, waypoint, waypoint, land -- and a name that
                // appeared only on the open row would leave the list saying nothing at a glance.
                QGCLabel {
                    objectName:         "localGrid_rowTypeLabel"
                    Layout.alignment:   Qt.AlignVCenter
                    Layout.fillWidth:   true
                    visible:            !_root.isCurrentItem
                    font.pointSize:     ScreenTools.smallFontPointSize
                    color:              _root._textColor
                    elide:              Text.ElideRight
                    text:               _root._commandName
                }

                // The open row turns that name into the control that changes it, the way the Plan
                // view's rows do. In the header rather than down among the position fields: it is
                // what the item *is*, not one of its measurements.
                QGCComboBox {
                    id:                 typeCombo
                    objectName:         "localGrid_rowTypeCombo"
                    Layout.alignment:   Qt.AlignVCenter
                    Layout.fillWidth:   true
                    visible:            _root.isCurrentItem
                    enabled:            _root._commandKnown
                    font.pointSize:     ScreenTools.smallFontPointSize
                    model:              [ qsTr("Waypoint"), qsTr("Takeoff"), qsTr("Land") ]

                    onActivated: (index) => _root._applyType(index)
                }

                // On the trailing edge, away from the leading edge the row is tapped on to open and
                // close it. A delete control under the thumb that is already opening rows is one
                // gloved mis-tap away from taking a waypoint out of the plan.
                QGCColoredImage {
                    objectName:             "localGrid_rowDeleteButton"
                    Layout.preferredWidth:  _root._iconSize
                    Layout.preferredHeight: _root._iconSize
                    Layout.alignment:       Qt.AlignVCenter
                    sourceSize.height:      _root._iconSize
                    fillMode:               Image.PreserveAspectFit
                    mipmap:                 true
                    smooth:                 true
                    source:                 "/res/TrashDelete.svg"
                    color:                  _root._textColor
                    // Only on the open row. A trash icon on every line of a list is an accident
                    // waiting for a gloved finger, and the Plan view holds to the same rule.
                    visible:                _root.isCurrentItem

                    QGCMouseArea {
                        fillItem:   parent
                        onClicked:  _root.removeRequested()
                    }
                }
            }
        }

        // setSource() rather than sourceComponent so the editor has its item before its own bindings
        // first run, which is how the Plan view avoids a pass of warnings about reading properties
        // off nothing
        Loader {
            id:                     editorLoader
            Layout.fillWidth:       true
            Layout.margins:         _root._margins
            visible:                _root.isCurrentItem
        }
    }

    function _loadEditor() {
        if (isCurrentItem) {
            editorLoader.setSource("qrc:/qml/QGroundControl/FlyView/LocalGridWaypointEditor.qml", {
                gridView:        Qt.binding(() => _root.gridView),
                visualItemIndex: Qt.binding(() => _root.visualItemIndex),
                north:           Qt.binding(() => _root.north),
                east:            Qt.binding(() => _root.east)
            })
        } else {
            editorLoader.setSource("")
        }
    }

    onIsCurrentItemChanged:  _loadEditor()
    Component.onCompleted:   _loadEditor()
}
