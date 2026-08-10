import QtQuick
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls
import QGroundControl.FlyView

/// The plan as a list, beside the grid it is drawn on.
///
/// One row per item, the way the Plan view lists them, so a pattern can be read off as a sequence --
/// takeoff, waypoint, waypoint, land -- rather than by clicking each marker in turn to find out what
/// it is. The row being edited is the only one open.
///
/// Rows come from the same missionPoints the markers are drawn from, so a row and its marker can
/// never name different items. That array is what the grid can place: an item carrying no coordinate
/// is drawn nowhere and listed nowhere either, which is the grid's existing rule rather than one
/// introduced here.
Rectangle {
    id: _root

    property var gridView: null

    // Outlined, because the grid behind it is painted in this same window colour. Without the border
    // the panel had no edge at all: folded away it left a header floating over the grid with nothing
    // to say it was a panel, or that it could be clicked to bring the plan back.
    color:          qgcPal.window
    radius:         ScreenTools.defaultFontPixelHeight / 4
    border.color:   qgcPal.groupBorder
    border.width:   1

    /// Folded away to leave the grid clear, keeping the header so it can be found again. A panel
    /// that closed completely would be a panel the operator has to remember a way back to.
    property bool collapsed: false

    /// What to give this panel for a height while it is folded
    readonly property real collapsedHeight: headerRow.implicitHeight + (_margins * 2)

    /// How many items the list is showing, which is not the plan's item count: the home position is
    /// not one of these, and neither is anything without a coordinate
    readonly property alias rowCount: itemList.count

    readonly property var _points:   gridView ? gridView.missionPoints : []
    readonly property int _selected: gridView ? gridView.selectedWaypointIndex : -1

    readonly property real _margins: ScreenTools.defaultFontPixelHeight / 3

    QGCPalette { id: qgcPal; colorGroupEnabled: enabled }

    ColumnLayout {
        anchors.fill:       parent
        anchors.margins:    _root._margins
        spacing:            _root._margins

        // The row is wrapped so the mouse area covering it has a sibling to anchor to. Anchored
        // straight onto the RowLayout it would be an anchored child of a layout, which Qt calls
        // undefined behaviour and warns about on every build of the grid.
        Item {
            Layout.fillWidth:   true
            implicitHeight:     headerRow.implicitHeight

            RowLayout {
                id:                     headerRow
                anchors.left:           parent.left
                anchors.right:          parent.right
                anchors.verticalCenter: parent.verticalCenter
                spacing:                ScreenTools.defaultFontPixelWidth / 2

                QGCColoredImage {
                    Layout.preferredWidth:  ScreenTools.defaultFontPixelHeight * 0.75
                    Layout.preferredHeight: Layout.preferredWidth
                    Layout.alignment:       Qt.AlignVCenter
                    source:                 "/InstrumentValueIcons/cheveron-right.svg"
                    color:                  qgcPal.text
                    rotation:               _root.collapsed ? 0 : 90
                }

                QGCLabel {
                    Layout.alignment:   Qt.AlignVCenter
                    font.bold:          true
                    font.pointSize:     ScreenTools.smallFontPointSize
                    color:              qgcPal.text
                    text:               qsTr("Mission Items")
                }

                QGCLabel {
                    Layout.alignment:   Qt.AlignVCenter
                    Layout.fillWidth:   true
                    font.pointSize:     ScreenTools.smallFontPointSize
                    color:              qgcPal.colorGrey
                    text:               qsTr("%1 items").arg(_root.rowCount)
                }
            }

            QGCMouseArea {
                objectName: "localGrid_missionListHeader"
                fillItem:   parent
                onClicked:  _root.collapsed = !_root.collapsed
            }
        }

        // Said out loud rather than left as an empty box, which reads as a panel that has failed to
        // load rather than as a plan with nothing in it yet
        QGCLabel {
            Layout.fillWidth:   true
            visible:            !_root.collapsed && (_root.rowCount === 0)
            wrapMode:           Text.WordWrap
            font.pointSize:     ScreenTools.smallFontPointSize
            color:              qgcPal.colorGrey
            text:               qsTr("No items yet. Click the grid to place one.")
        }

        QGCListView {
            id:                 itemList
            objectName:         "localGrid_missionListView"
            Layout.fillWidth:   true
            Layout.fillHeight:  true
            visible:            !_root.collapsed && (_root.rowCount > 0)
            spacing:            _root._margins

            // The count rather than the array. missionPoints is rebuilt from scratch whenever any
            // item's coordinate changes, and handing that array over as the model would tear down
            // and rebuild every row -- including the open one, mid-edit, with the focus and the
            // half-typed field in it. A plain count only changes when an item is added or removed.
            model: _root._points.length

            delegate: LocalGridMissionItemRow {
                id: itemRow

                required property int index

                // Re-read out of the rebuilt array rather than captured, so a row follows its item
                // without being replaced. Guarded: the count is applied a beat before the array it
                // came from on the pass where an item is removed.
                readonly property var point: _root._points[index] ?? null

                width:           ListView.view.width
                visible:         point !== null
                gridView:        _root.gridView
                visualItemIndex: point ? point.index : -1
                sequenceNumber:  point ? point.sequence : 0
                north:           point ? point.north : NaN
                east:            point ? point.east : NaN
                isCurrentItem:   point ? (_root._selected === point.index) : false
                isVehicleTarget: point ? point.isVehicleTarget : false

                // Clicking the open row closes it. The floating panel had a Close button and the
                // list has no room for one per row, so the row that opened is the way back out --
                // otherwise something is always open and the grid is always partly covered.
                onClicked: {
                    if (!_root.gridView || !point) {
                        return
                    }
                    if (_root._selected === point.index) {
                        _root.gridView.clearWaypointSelection()
                    } else {
                        _root.gridView.selectWaypoint(point.index)
                    }
                }

                // Selected first rather than trusting that a row showing a delete control is already
                // the selected one. removeSelectedWaypoint acts on the grid's selection, and the two
                // agreeing is an invariant of the row's own visibility rule -- not something this
                // call should depend on.
                onRemoveRequested: {
                    if (_root.gridView && point) {
                        _root.gridView.selectWaypoint(point.index)
                        _root.gridView.removeSelectedWaypoint()
                    }
                }
            }
        }
    }
}
