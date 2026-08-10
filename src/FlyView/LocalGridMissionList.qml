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
    ///
    /// Starts folded: an empty plan has nothing to show, and a panel standing open over the grid to
    /// say so is covering the one picture the operator has.
    property bool collapsed: true

    /// The most room this panel may take. Past it the rows scroll rather than the panel running off
    /// the bottom of the view. Zero for no limit.
    property real maximumHeight: 0

    /// Sized to what it is holding rather than to the space it is given. Stretched to fill, the
    /// layout had nothing that wanted the extra height and spread it between the header and the
    /// empty-plan line instead, leaving both stranded in the middle of a tall empty box.
    implicitHeight: contentColumn.implicitHeight + (_margins * 2)

    /// What to give this panel for a height while it is folded
    readonly property real collapsedHeight: headerBlock.implicitHeight + (_margins * 2)

    /// How many items the list is showing, which is not the plan's item count: the home position is
    /// not one of these, and neither is anything without a coordinate
    readonly property int rowCount: _points.length

    readonly property var _points:   gridView ? gridView.missionPoints : []
    readonly property int _selected: gridView ? gridView.selectedWaypointIndex : -1

    readonly property real _margins: ScreenTools.defaultFontPixelHeight / 3

    /// What is left for the rows once the header and the margins have had theirs
    readonly property real _listMaximumHeight: (maximumHeight > 0)
                                                ? Math.max(0, maximumHeight - collapsedHeight - _margins)
                                                : Number.POSITIVE_INFINITY

    /// Opens itself when the plan gets its first item and folds away again when the last one goes,
    /// so the panel is only in front of the grid while it has something to say. A toggle in between
    /// is the operator's and is left alone.
    property int _lastRowCount: 0

    onRowCountChanged: {
        if ((_lastRowCount === 0) && (rowCount > 0)) {
            collapsed = false
        } else if ((_lastRowCount > 0) && (rowCount === 0)) {
            collapsed = true
        }
        _lastRowCount = rowCount
    }

    /// Brings the open row into view, so a waypoint added to a long plan is somewhere the operator
    /// can see rather than below the fold of a panel they then have to scroll by hand.
    ///
    /// Only ever moves a row that is not fully visible. Anything more would take the panel away from
    /// under an operator who has scrolled it somewhere on purpose.
    function _scrollToSelected() {
        if (collapsed || (_selected < 0)) {
            return
        }

        for (var i = 0; i < _points.length; i++) {
            if (_points[i].index !== _selected) {
                continue
            }
            const row = rowRepeater.itemAt(i)
            if (!row) {
                return
            }
            if (row.y < itemList.contentY) {
                itemList.contentY = row.y
            } else if ((row.y + row.height) > (itemList.contentY + itemList.height)) {
                itemList.contentY = Math.max(0, (row.y + row.height) - itemList.height)
            }
            return
        }
    }

    // Deferred by a turn of the event loop rather than run on the spot. Selecting a row loads its
    // editor, which is most of the row's height, and scrolling to where it was before that has
    // finished aims at the wrong place.
    property Timer _scrollTimer: Timer {
        interval:       0
        onTriggered:    _root._scrollToSelected()
    }

    on_SelectedChanged: _scrollTimer.restart()

    // The open row growing its editor can push itself out of view on its own
    Connections {
        target: rowColumn
        function onHeightChanged() { _root._scrollTimer.restart() }
    }

    QGCPalette { id: qgcPal; colorGroupEnabled: enabled }

    ColumnLayout {
        id:                 contentColumn
        anchors.left:       parent.left
        anchors.right:      parent.right
        anchors.top:        parent.top
        anchors.margins:    _root._margins
        spacing:            _root._margins

        // The row is wrapped so the mouse area covering it has a sibling to anchor to. Anchored
        // straight onto the RowLayout it would be an anchored child of a layout, which Qt calls
        // undefined behaviour and warns about on every build of the grid.
        Item {
            id:                 headerBlock
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

        // A Column of every row inside a Flickable, rather than a ListView.
        //
        // The panel is sized to its rows, and asking a ListView how tall its rows are cannot answer
        // that: a ListView only builds the delegates that fit inside it, so its contentHeight
        // depends on its height, and binding the height back to contentHeight leaves both stuck at
        // zero. A Column's height is simply the sum of its children, which nothing else depends on.
        //
        // The cost is that every row is built rather than only the visible ones. A plan is tens of
        // items, the markers on the grid are already built the same way, and the expensive part --
        // the editor -- is still loaded only for the open row.
        QGCFlickable {
            id:                 itemList
            objectName:         "localGrid_missionListView"
            Layout.fillWidth:   true
            // As tall as the rows need, up to whatever room the view has left. Only once the plan
            // outgrows that does this clamp and the list start scrolling, so a short plan gets a
            // short panel rather than a full-height one with a gap under it.
            Layout.preferredHeight: Math.min(rowColumn.height, _root._listMaximumHeight)
            visible:            !_root.collapsed && (_root.rowCount > 0)
            contentWidth:       width
            contentHeight:      rowColumn.height

            Column {
                id:         rowColumn
                width:      itemList.width
                spacing:    _root._margins

                Repeater {
                    id: rowRepeater

                    // The count rather than the array. missionPoints is rebuilt from scratch
                    // whenever any item's coordinate changes, and handing that array over as the
                    // model would tear down and rebuild every row -- including the open one,
                    // mid-edit, with the focus and the half-typed field in it. A plain count only
                    // changes when an item is added or removed.
                    model: _root._points.length

                    LocalGridMissionItemRow {
                        id: itemRow

                        required property int index

                        // Re-read out of the rebuilt array rather than captured, so a row follows
                        // its item without being replaced. Guarded: the count is applied a beat
                        // before the array it came from on the pass where an item is removed.
                        readonly property var point: _root._points[index] ?? null

                        width:           rowColumn.width
                        visible:         point !== null
                        gridView:        _root.gridView
                        visualItemIndex: point ? point.index : -1
                        sequenceNumber:  point ? point.sequence : 0
                        north:           point ? point.north : NaN
                        east:            point ? point.east : NaN
                        isCurrentItem:   point ? (_root._selected === point.index) : false
                        isVehicleTarget: point ? point.isVehicleTarget : false

                        // Clicking the open row closes it. The floating panel had a Close button and
                        // the list has no room for one per row, so the row that opened is the way
                        // back out -- otherwise something is always open and the grid always partly
                        // covered.
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

                        // Selected first rather than trusting that a row showing a delete control is
                        // already the selected one. removeSelectedWaypoint acts on the grid's
                        // selection, and the two agreeing is an invariant of the row's own
                        // visibility rule -- not something this call should depend on.
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
    }
}
