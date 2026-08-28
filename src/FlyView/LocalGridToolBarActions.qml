import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls

/// The plan's Open, Save, Upload and Clear -- with Download behind the overflow -- in the fly view
/// toolbar, for as long as the grid is showing an aircraft that is not flying.
///
/// One place, whatever the window is doing. These used to stand here only on a view too small to
/// carry the grid's own mission panel open, and live in that panel everywhere else: a control that
/// moves house with the window size is one an operator has to look for twice, and the two copies
/// were reachable by different routes on different screens.
///
/// Laid out the way the Plan view's own toolbar lays out the same set -- same icons, same order,
/// Download behind the hamburger (PlanToolBarIndicators.qml) -- because an operator who has sent a
/// plan from that toolbar has already learned this one. Driven straight through the panel's
/// pre-checks and confirmation dialogs rather than through a second copy of either.
///
/// The labels come off when the view is too narrow to carry them, leaving the icons in the same
/// order and the same places: the toolbar's other corner still has indicators to be read, and a row
/// that pushes those off the edge has taken something away in order to add itself.
RowLayout {
    id: _root

    /// The grid this drives -- its missionActions child is where these actually live
    property var gridView: null

    readonly property var  _actions:   gridView ? gridView.missionActions : null
    readonly property bool _syncing:   (_actions !== null) && _actions.syncing
    readonly property bool _iconsOnly: gridView ? gridView.compact : false

    /// Four fifths of the button QGC uses on a page.
    ///
    /// At full size these five read as slabs across the corner, heavier than the aircraft status they
    /// sit beside -- and status is what an operator scans this corner for. A toolbar control is a
    /// target for a deliberate press rather than something to be found in a hurry, so it can afford
    /// to be the quieter of the two. Applied to the height, the glyph and the padding together, so
    /// the proportions are the ones QGCButton was drawn with rather than a squashed version of them.
    readonly property real _scale: 0.8

    spacing: ScreenTools.defaultFontPixelWidth / 2

    component PlanActionButton: QGCButton {
        Layout.preferredHeight: Math.round(ScreenTools.toolbarHeight * _root._scale)
        Layout.alignment:       Qt.AlignVCenter
        // leftPadding/rightPadding rather than QGCButton's own _horizontalPadding, which is private
        // to it. Assigning these overrides the bindings it puts on them, which is the supported way
        // in and the only one that survives a change to how it works those out.
        leftPadding:            Math.round(ScreenTools.defaultFontPixelWidth * 2 * _root._scale)
        rightPadding:           leftPadding
        // Carries the icon as well as the label: QGCButton sizes its glyph off the label's height,
        // so this is the one handle that scales both and keeps them in proportion.
        pointSize:              ScreenTools.defaultFontPointSize * _root._scale
    }

    PlanActionButton {
        objectName:         "toolbar_localGridOpenButton"
        text:               _root._iconsOnly ? "" : qsTr("Open")
        iconSource:         "/qmlimages/Plan.svg"
        enabled:            (_root._actions !== null) && _root._actions.canLoad
        ToolTip.text:       qsTr("Open")
        ToolTip.visible:    hovered && _root._iconsOnly
        onClicked:          _root._actions.requestLoad()
    }

    PlanActionButton {
        objectName:         "toolbar_localGridSaveButton"
        text:               _root._iconsOnly ? "" : qsTr("Save")
        iconSource:         "/res/SaveToDisk.svg"
        enabled:            (_root._actions !== null) && _root._actions.canSave
        ToolTip.text:       qsTr("Save")
        ToolTip.visible:    hovered && _root._iconsOnly
        onClicked:          _root._actions.requestSave()
    }

    PlanActionButton {
        objectName:         "toolbar_localGridUploadButton"
        // Keeps its label through a transfer even where the others have given theirs up. An icon
        // that is merely greyed says the button is unavailable; it does not say a transfer is
        // running, which is the one thing an operator watching this corner needs to be told.
        text:               _root._syncing ? qsTr("Sending…") : (_root._iconsOnly ? "" : qsTr("Upload"))
        iconSource:         "/res/UploadToVehicle.svg"
        enabled:            (_root._actions !== null) && _root._actions.canUpload
        primary:            (_root._actions !== null) && _root._actions.uploadHighlighted
        ToolTip.text:       qsTr("Upload")
        ToolTip.visible:    hovered && _root._iconsOnly && !_root._syncing
        onClicked:          _root._actions.requestUpload()
    }

    PlanActionButton {
        objectName:         "toolbar_localGridClearButton"
        text:               _root._iconsOnly ? "" : qsTr("Clear")
        iconSource:         "/res/TrashCan.svg"
        enabled:            (_root._actions !== null) && _root._actions.canClear
        ToolTip.text:       qsTr("Clear")
        ToolTip.visible:    hovered && _root._iconsOnly
        onClicked:          _root._actions.requestClear()
    }

    // Download sits behind the overflow rather than in the row, which is where the Plan view's
    // toolbar puts it too: it is the one of the five reached once in a session, if at all, and the
    // four it would widen this row past are reached over and over while a pattern is built.
    PlanActionButton {
        objectName:         "toolbar_localGridOverflowButton"
        iconSource:         "qrc:/qmlimages/Hamburger.svg"
        ToolTip.text:       qsTr("More plan actions")
        ToolTip.visible:    hovered

        onClicked: {
            // mapToItem against globals.parent rather than mainWindow, the same way
            // PlanToolBarIndicators reaches this panel: mainWindow does not resolve as a mapToItem
            // target there either.
            const position = mapToItem(globals.parent, Qt.point(width, height / 2))
            const dropPanel = overflowPanelComponent.createObject(
                                    mainWindow, { clickRect: Qt.rect(position.x, position.y, 0, 0) })
            dropPanel.open()
        }
    }

    Component {
        id: overflowPanelComponent

        DropPanel {
            id: dropPanel

            sourceComponent: Component {
                ColumnLayout {
                    spacing: ScreenTools.defaultFontPixelHeight / 2

                    QGCButton {
                        objectName:         "toolbar_localGridDownloadButton"
                        Layout.fillWidth:   true
                        text:               qsTr("Download")
                        enabled:            (_root._actions !== null) && _root._actions.canDownload

                        onClicked: {
                            dropPanel.close()
                            _root._actions.requestDownload()
                        }
                    }
                }
            }
        }
    }
}
