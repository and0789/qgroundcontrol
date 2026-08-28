import QtQuick
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls

/// Says in numbers what the grid says in a picture: where the vehicle is, and how far and on what
/// bearing that is from the origin.
///
/// The bearing and range pair is the one a return leg is flown on, and reading it off a picture by
/// eye is exactly the estimate this whole way of flying is trying to replace.
Rectangle {
    id: _root

    property var gridView: null

    /// Raised when the operator asks to set an origin, handled by the view that owns this readout
    signal setOriginRequested()

    /// Sized to what it holds. A fixed width was tried and was wrong twice over: a Layout does not
    /// shrink its children to fit but lets them overflow, and a panel whose width is read off a
    /// layout anchored to both its edges is a loop that QML breaks by answering zero. The mission
    /// list below follows this width instead, which is what keeps the right edge one column.
    // Held to maximumWidth rather than merely sized from what it holds. Sizing alone was the bug: the
    // cap was documented as a limit on the warning text, so anything else inside that insisted on its
    // own width -- three buttons across a row, a column of numbers -- widened the panel past the share
    // of the view the column says it may have, and the plan list that follows this width went with it.
    // The pieces below are each able to give now, so the clamp narrows the panel rather than leaving
    // its contents hanging over the edge of it.
    implicitWidth:  Math.min(layout.implicitWidth + (_margins * 2),
                             (maximumWidth > 0) ? maximumWidth : Number.POSITIVE_INFINITY)
    implicitHeight: layout.implicitHeight + (_margins * 2)
    color:          qgcPal.window
    opacity:        0.8
    radius:         ScreenTools.defaultFontPixelHeight / 4
    // Outlined like the mission list beside it, and for the reason that panel found: folded away,
    // an unbordered header is a line of text floating on the grid with nothing to say it is a panel
    // or that clicking it brings the numbers back.
    border.color:   qgcPal.groupBorder
    border.width:   1

    /// Folded to leave the grid clear, keeping the header and its summary so it can be found again.
    ///
    /// Starts folded, because a panel that opens before there is any telemetry stands over the grid
    /// showing six dashes and a line saying it has nothing -- which is the largest this panel ever
    /// gets and the least it ever says.
    property bool collapsed: true

    property real _margins: ScreenTools.defaultFontPixelHeight / 3

    /// The most room this panel may take, set from outside. Negative for no limit.
    ///
    /// This panel is the top of the right-hand column and used to have no ceiling at all: opened on a
    /// short window it grew until it -- and everything anchored under it -- ran down through the
    /// instrument panel in the bottom-right corner. Past the ceiling the body scrolls, the same way
    /// the mission list's rows and the after-flight panel's controls already do.
    ///
    /// Negative rather than zero for the reason LocalGridMissionActions carries: zero is what a caller
    /// works out when there is genuinely no room, and read as "no limit" it turns the ceiling off in
    /// the one state it exists for.
    property real maximumHeight: -1

    /// What to give this panel for a height while it is folded, so a caller can reserve room for it
    /// without asking how tall its contents would be
    readonly property real collapsedHeight: headerBlock.implicitHeight + (_margins * 2)

    /// What is left for the scrollable body once the fixed header and the outer margins have had theirs
    readonly property real _bodyMaximumHeight: (maximumHeight >= 0)
                                                ? Math.max(0, maximumHeight - headerBlock.implicitHeight
                                                                - layout.spacing - (_margins * 2))
                                                : Number.POSITIVE_INFINITY

    /// The most this panel is allowed to widen to, set from outside. Zero for no limit.
    ///
    /// Kept as a cap on the warning text below rather than as an explicit width on this panel: this
    /// item sizes itself from its content (see implicitWidth above), and a Layout that is handed a
    /// width narrower than what its children ask for does not shrink them to fit -- it lets them
    /// overflow. Narrowing the sentence that drives the width is what actually narrows the panel.
    property real maximumWidth: 0

    /// Set from outside when the view is too narrow to give this panel's numbers four columns and
    /// still meet maximumWidth. The trade the comment on localGrid_readoutNumbers already documents --
    /// two pairs to a row instead of six rows of one, half the width for double the height -- taken
    /// the other way: full width, half the columns.
    property bool compactColumns: false

    /// Whether the three view buttons fit across the column side by side.
    ///
    /// Measured off what each button asks for on its own, never off the row's own width: a row that is
    /// three across is wider than one that is stacked, so asking the row would make the answer change
    /// the question and leave the layout oscillating between the two.
    readonly property bool _viewButtonsFitOneRow: (maximumWidth <= 0)
                                                    || ((centreOnVehicleButton.implicitWidth
                                                            + centreOnOriginButton.implicitWidth
                                                            + clearTrailButton.implicitWidth
                                                            + (ScreenTools.defaultFontPixelWidth * 2))
                                                        <= (maximumWidth - (_margins * 2)))

    QGCPalette { id: qgcPal; colorGroupEnabled: enabled }

    // This panel sits over the grid's own wheel-to-zoom and drag-to-pan area, and its body scrolls --
    // so a drag that runs past the end of the numbers, or a wheel over a body with nothing to scroll,
    // fell through and panned the grid underneath. The plan list beside it has carried the same guard
    // since it grew a scrolling body; this one gained the body later and did not.
    DeadMouseArea {
        anchors.fill: parent
    }

    readonly property var  _transform: gridView ? gridView.gridTransform : null
    readonly property bool _valid:     gridView ? gridView.positionValid : false
    readonly property real _north:     gridView ? gridView.vehicleNorth : NaN
    readonly property real _east:      gridView ? gridView.vehicleEast : NaN

    /// NaN when the vehicle has not reported one, which is a different thing from north
    readonly property real _heading:   gridView ? gridView.vehicleHeadingDegrees : NaN






    readonly property real _range:   _valid ? Math.sqrt((_north * _north) + (_east * _east)) : NaN
    /// Compass bearing from the origin to the vehicle. atan2 takes east over north, not the usual
    /// y over x, which is what turns a maths angle into a bearing measured clockwise from north.
    readonly property real _bearing: _valid ? ((Math.atan2(_east, _north) * 180 / Math.PI) + 360) % 360 : NaN

    function _distanceText(metres) {
        if (!_transform || isNaN(metres)) {
            return qsTr("--")
        }
        // Rounded to the shown precision first, then nudged off negative zero: adding zero to -0
        // gives +0, while toFixed alone would print "-0.0". Without it a vehicle a millimetre south
        // of the origin reads as minus nothing, and on a panel whose whole job is telling north from
        // south, a minus sign carrying no distance is a direction the operator has to talk
        // themselves out of.
        const rounded = Math.round(_transform.toDisplay(metres) * 10) / 10
        return (rounded + 0).toFixed(1) + " " + _transform.displayUnits
    }

    /// True while this panel is the job in hand rather than a reference: telemetry is arriving, and
    /// the frame those numbers are measured in has not been set yet. That is the one state where
    /// this panel is what the operator has to deal with before anything else on the view means
    /// anything, so it earns the whole of the right-hand column.
    ///
    /// Setting the origin ends it. From then on the six numbers are read now and then rather than
    /// worked on, and the header still carries the range and bearing a return leg is flown on --
    /// which is the pair actually read in the air. Standing open past that point costs the column
    /// its height: LocalGridMissionList is anchored under this panel and capped by what is left down
    /// to the bottom edge, so an open readout is a plan list that cannot be opened far enough to
    /// read.
    readonly property bool _standOpen: _valid && !(gridView && gridView.originKnown)

    /// Acted on at the transitions only, never bound straight to collapsed: a fold or an unfold in
    /// between belongs to the operator, and a binding would take it back off them on the next frame.
    property bool _wasStandOpen: false

    on_StandOpenChanged: {
        if (_standOpen !== _wasStandOpen) {
            collapsed = !_standOpen
            _wasStandOpen = _standOpen
        }
    }

    /// Building a plan folds it, because the column it sits at the top of is where the plan is read.
    ///
    /// One way only. Entering the mode is a statement about what the operator is doing now, so it
    /// may take the space; leaving it is not a request to have this panel back over the grid, and an
    /// unfold there would undo a fold the operator had made for themselves. The header keeps saying
    /// the range and bearing throughout either way.
    Connections {
        target:  _root.gridView
        enabled: _root.gridView !== null

        function onPlanEditModeChanged() {
            if (_root.gridView.planEditMode) {
                _root.collapsed = true
            }
        }
    }

    /// What the header carries while folded: the pair a return leg is flown on. Without it, folding
    /// costs a click on the very thing the operator most wants to see, and the panel would be opened
    /// again every time it was closed.
    readonly property string _summary: _valid && !isNaN(_range) && !isNaN(_bearing)
                                        ? qsTr("%1 @ %2°").arg(_distanceText(_range)).arg(Math.round(_bearing))
                                        : qsTr("--")

    ColumnLayout {
        id:                 layout
        anchors.margins:    _root._margins
        anchors.left:       parent.left
        // Anchored to both edges, so a clamped panel hands the clamp down to what it holds instead of
        // letting it draw past the background
        anchors.right:      parent.right
        anchors.top:        parent.top
        spacing:            0

        // The row is wrapped so the mouse area covering it has a sibling to anchor to. Anchored
        // straight onto the RowLayout it would be an anchored child of a layout, which Qt calls
        // undefined behaviour and warns about on every build of the grid.
        Item {
            id:                 headerBlock
            Layout.fillWidth:   true
            implicitHeight:     headerRow.implicitHeight
            // This panel takes its width from what it holds (see implicitWidth above), and an Item
            // does not pick up a RowLayout child's width on its own. Left out, the moment the numbers
            // below were folded away this header -- the only child still contributing anything --
            // had nothing to contribute either, and the whole panel collapsed to a stub the width of
            // its chevron: no title, no summary, and nothing left to recognise or aim at to open it
            // again.
            implicitWidth:      headerRow.implicitWidth

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
                    objectName:         "localGrid_readoutTitle"
                    Layout.alignment:   Qt.AlignVCenter
                    font.pointSize:     ScreenTools.smallFontPointSize
                    font.bold:          true
                    text:               qsTr("Local Position")
                }

                // Only while folded. Open, the same pair is two rows below in full, and repeating it
                // in the header reads as a second measurement rather than the same one.
                QGCLabel {
                    objectName:         "localGrid_readoutSummary"
                    Layout.alignment:   Qt.AlignVCenter
                    Layout.fillWidth:   true
                    horizontalAlignment: Text.AlignRight
                    visible:            _root.collapsed
                    font.pointSize:     ScreenTools.smallFontPointSize
                    color:              qgcPal.colorGrey
                    text:               _root._summary
                }
            }

            QGCMouseArea {
                objectName: "localGrid_readoutHeader"
                fillItem:   parent
                onClicked:  _root.collapsed = !_root.collapsed
            }
        }

        // Everything under the fixed header above, wrapped so it can scroll when the column has
        // less room than the numbers, warnings and buttons want. The same shape the mission list
        // and the after-flight panel already use: this panel is the top of the right-hand column
        // and had nothing capping it at all, so an open readout on a short window pushed the whole
        // column down through the instrument panel in the corner below it.
        QGCFlickable {
            id:                     bodyFlickable
            // Gated on the fold again now that the warnings have left for the band across the top.
            // While they were here it could not be, since a stale position is exactly what an operator
            // who folded the numbers away still has to be told.
            visible:                !_root.collapsed && (_root._bodyMaximumHeight > 0)
            // A Flickable does not pick up its contentItem's natural size the way a Layout would,
            // and this panel is sized from what it holds -- so without this it would collapse to
            // the width of its own header the moment the numbers moved in here.
            implicitWidth:          bodyLayout.implicitWidth
            Layout.fillWidth:       true
            Layout.preferredHeight: Math.min(bodyLayout.implicitHeight, _root._bodyMaximumHeight)
            contentWidth:           width
            contentHeight:          bodyLayout.implicitHeight

            ColumnLayout {
                id:         bodyLayout
                width:      bodyFlickable.width
                spacing:    layout.spacing

                GridLayout {
                    objectName:     "localGrid_readoutNumbers"
                    visible:        !_root.collapsed
                    // Two pairs to a row rather than six rows of one. The first two rows each hold one idea
                    // whole: where the vehicle is in the frame's own axes, then the same position said as the
                    // range and bearing a return leg is flown on. The third pairs the two that are left over
                    // and means nothing by being together -- which is the price of the halved height, and
                    // cheap at six numbers.
                    //
                    // compactColumns takes the same trade the other way: one pair to a row, full width for
                    // each label, half again the height. maximumWidth alone cannot narrow this panel -- a
                    // Layout does not shrink its children to fit -- so on a narrow view the column count
                    // itself has to be what changes.
                    Layout.fillWidth: true
                    columns:        _root.compactColumns ? 2 : 4
                    columnSpacing:  ScreenTools.defaultFontPixelWidth
                    rowSpacing:     0

                    QGCLabel { font.pointSize: ScreenTools.smallFontPointSize; text: qsTr("North") }
                    QGCLabel {
                        font.pointSize:         ScreenTools.smallFontPointSize
                        horizontalAlignment:    Text.AlignRight
                        Layout.fillWidth:       true
                        // The value column is what has to give when the labels beside it and the
                        // column's own share of the view leave it short. A reading long enough to
                        // elide is one an aircraft flying on optical flow is already well outside.
                        Layout.minimumWidth:    0
                        elide:                  Text.ElideRight
                        text:                   _root._distanceText(_root._north)
                    }

                    QGCLabel { font.pointSize: ScreenTools.smallFontPointSize; text: qsTr("East") }
                    QGCLabel {
                        font.pointSize:         ScreenTools.smallFontPointSize
                        horizontalAlignment:    Text.AlignRight
                        Layout.fillWidth:       true
                        // The value column is what has to give when the labels beside it and the
                        // column's own share of the view leave it short. A reading long enough to
                        // elide is one an aircraft flying on optical flow is already well outside.
                        Layout.minimumWidth:    0
                        elide:                  Text.ElideRight
                        text:                   _root._distanceText(_root._east)
                    }

                    QGCLabel { font.pointSize: ScreenTools.smallFontPointSize; text: qsTr("Range") }
                    QGCLabel {
                        font.pointSize:         ScreenTools.smallFontPointSize
                        horizontalAlignment:    Text.AlignRight
                        Layout.fillWidth:       true
                        // The value column is what has to give when the labels beside it and the
                        // column's own share of the view leave it short. A reading long enough to
                        // elide is one an aircraft flying on optical flow is already well outside.
                        Layout.minimumWidth:    0
                        elide:                  Text.ElideRight
                        text:                   _root._distanceText(_root._range)
                    }

                    // Beside the range, because the two are read as one figure: how far, and which way.
                    QGCLabel { font.pointSize: ScreenTools.smallFontPointSize; text: qsTr("Bearing") }
                    QGCLabel {
                        font.pointSize:         ScreenTools.smallFontPointSize
                        horizontalAlignment:    Text.AlignRight
                        Layout.fillWidth:       true
                        // The value column is what has to give when the labels beside it and the
                        // column's own share of the view leave it short. A reading long enough to
                        // elide is one an aircraft flying on optical flow is already well outside.
                        Layout.minimumWidth:    0
                        elide:                  Text.ElideRight
                        text:                   isNaN(_root._bearing) ? qsTr("--") : Math.round(_root._bearing) + "°"
                    }

                    // Kept as a number rather than a rose of its own -- the fly view's instrument panel
                    // already draws one, and reading a heading off a dial by eye is the estimate this whole
                    // grid exists to replace.
                    QGCLabel { objectName: "localGrid_headingLabel"; font.pointSize: ScreenTools.smallFontPointSize; text: qsTr("Heading") }
                    QGCLabel {
                        objectName:             "localGrid_headingValue"
                        font.pointSize:         ScreenTools.smallFontPointSize
                        horizontalAlignment:    Text.AlignRight
                        Layout.fillWidth:       true
                        // The value column is what has to give when the labels beside it and the
                        // column's own share of the view leave it short. A reading long enough to
                        // elide is one an aircraft flying on optical flow is already well outside.
                        Layout.minimumWidth:    0
                        elide:                  Text.ElideRight
                        // Dashes rather than a zero. The instrument panel's compass reads its heading as
                        // zero when the vehicle has not sent one, which points confidently at north; this
                        // one says it does not know.
                        text:                   isNaN(_root._heading) ? qsTr("--") : Math.round(_root._heading) + "°"
                    }

                    // Distance along the trail rather than from the origin. Drift on this kind of navigation
                    // accumulates with ground covered, so this is the denominator the error is quoted
                    // against -- and "range 0.4 m after flying 80 m" is a very different result from
                    // "range 0.4 m after hovering".
                    QGCLabel { font.pointSize: ScreenTools.smallFontPointSize; text: qsTr("Flown") }
                    QGCLabel {
                        font.pointSize:         ScreenTools.smallFontPointSize
                        horizontalAlignment:    Text.AlignRight
                        Layout.fillWidth:       true
                        // The value column is what has to give when the labels beside it and the
                        // column's own share of the view leave it short. A reading long enough to
                        // elide is one an aircraft flying on optical flow is already well outside.
                        Layout.minimumWidth:    0
                        elide:                  Text.ElideRight
                        text:                   _root.gridView ? _root._distanceText(_root.gridView.trailLengthMetres) : qsTr("--")
                    }
                }

                // Only while there is no origin, which is the one state where nothing else on this view means
                // anything -- so it earns the width and the highlight. Once an origin exists, changing it is
                // a rare and consequential thing that moves the frame every position and waypoint is measured
                // in, and it moves to the correction dialog on the origin marker rather than standing in the
                // middle of a panel of live numbers.
                QGCButton {
                    objectName:         "localGrid_setOriginButton"
                    Layout.fillWidth:   true
                    Layout.topMargin:   ScreenTools.defaultFontPixelHeight / 4
                    visible:            _root.gridView ? !_root.gridView.originKnown : false
                    primary:            true
                    text:               qsTr("Set Estimator Origin…")
                    onClicked:          _root.setOriginRequested()
                }

                // Folded away with the numbers. These aim the camera and clear a drawn line -- nothing about
                // them is urgent, and an operator who has folded the panel to see the grid is not looking for
                // them. They stay here rather than joining the mission strip in the far corner: that panel
                // already carries a Clear that wipes the flight plan, and a Clear trail beside it would be two
                // buttons a glance apart with very different consequences.
                //
                // One row where three of them fit across the column, and one per row where they do not. They
                // were one row on every width, and that is what pushed this whole panel past the width the
                // column says it may have: three buttons carrying their own labels need 213px, and a third of
                // a 400px view is 133. A Layout does not shrink its children to fit -- the same rule the
                // numbers above change their column count for -- so the arrangement is what has to give.
                //
                // The trade is deliberately the opposite way round on a narrow view than on a short one.
                // Stacking spends height to save width, and a view narrow enough to force it is a phone in
                // portrait, which has height to spare and none of the width. Driven off each button's own
                // natural width rather than off the row's, because the row's changes with the answer.
                GridLayout {
                    objectName:         "localGrid_readoutViewButtons"
                    visible:            !_root.collapsed
                    Layout.fillWidth:   true
                    Layout.topMargin:   ScreenTools.defaultFontPixelHeight / 4
                    columnSpacing:      ScreenTools.defaultFontPixelWidth
                    rowSpacing:         ScreenTools.defaultFontPixelHeight / 4
                    columns:            _root._viewButtonsFitOneRow ? 3 : 1

                    QGCButton {
                        id:               centreOnVehicleButton
                        Layout.fillWidth: true
                        text:             qsTr("Vehicle")
                        enabled:          _root._valid && _root.gridView && !_root.gridView.followVehicle
                        onClicked:        _root.gridView.centreOnVehicle()
                    }

                    QGCButton {
                        id:               centreOnOriginButton
                        Layout.fillWidth: true
                        text:             qsTr("Origin")
                        enabled:          _root.gridView !== null
                        onClicked:        _root.gridView.centreOnOrigin()
                    }

                    QGCButton {
                        id:               clearTrailButton
                        Layout.fillWidth: true
                        text:             qsTr("Clear trail")
                        enabled:          _root.gridView && (_root.gridView.trailPointCount > 0)
                        onClicked:        _root.gridView.clearTrail()
                    }
                }
            }
        }
    }
}
