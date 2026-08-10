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
    /// The number on the marker's face, which diverges from the index as soon as the plan holds
    /// anything that is not a plain waypoint
    property int  sequenceNumber:  0
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

    implicitHeight: contentColumn.implicitHeight + (_margins * 2)
    color:          isCurrentItem ? qgcPal.buttonHighlight : qgcPal.windowShade
    radius:         ScreenTools.defaultFontPixelHeight / 4
    // Dimmed rather than greyed, the way the Plan view does it, so the open row reads as the one in
    // hand without the others looking disabled
    opacity:        isCurrentItem ? 1.0 : 0.7

    readonly property real _margins:    ScreenTools.defaultFontPixelHeight / 4
    readonly property real _iconSize:   ScreenTools.defaultFontPixelHeight

    /// The open row is drawn on the highlight colour, so its text has to be the colour that goes on
    /// top of it rather than the ordinary one
    readonly property color _textColor: isCurrentItem ? qgcPal.buttonHighlightText : qgcPal.text

    readonly property string _commandName: (gridView && (visualItemIndex >= 0))
                                            ? gridView.waypointCommandName(visualItemIndex)
                                            : ""

    QGCPalette { id: qgcPal; colorGroupEnabled: enabled }

    MouseArea {
        anchors.fill:   parent
        onClicked:      _root.clicked()
    }

    ColumnLayout {
        id:                 contentColumn
        anchors.margins:    _root._margins
        anchors.left:       parent.left
        anchors.right:      parent.right
        anchors.top:        parent.top
        spacing:            _root._margins

        RowLayout {
            id:                 headerRow
            Layout.fillWidth:   true
            spacing:            ScreenTools.defaultFontPixelWidth / 2

            // Only on the open row. A trash icon on every line of a list is an accident waiting for
            // a gloved finger, and the Plan view holds to the same rule.
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
                visible:                _root.isCurrentItem

                QGCMouseArea {
                    fillItem:   parent
                    onClicked:  _root.removeRequested()
                }
            }

            // Carries the same green disc the marker on the grid wears, so the row and the marker
            // for the waypoint the vehicle is flying to are recognisably the same thing. Being
            // edited is shown by the row opening; this says nothing about that.
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
                    text:               _root.sequenceNumber
                }
            }

            // What the item is, kept on the row whether or not it is open. Scanning a pattern is
            // reading this column -- takeoff, waypoint, waypoint, land -- and a name that appeared
            // only on the open row would leave the list saying nothing at a glance.
            QGCLabel {
                Layout.alignment:   Qt.AlignVCenter
                Layout.fillWidth:   true
                font.pointSize:     ScreenTools.smallFontPointSize
                color:              _root._textColor
                elide:              Text.ElideRight
                text:               _root._commandName
            }
        }

        // setSource() rather than sourceComponent so the editor has its item before its own bindings
        // first run, which is how the Plan view avoids a pass of warnings about reading properties
        // off nothing
        Loader {
            id:                 editorLoader
            Layout.fillWidth:   true
            visible:            _root.isCurrentItem
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
