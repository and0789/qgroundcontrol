import QGroundControl
import QGroundControl.Controls

/// The between-flights repairs, in the strip the rest of this view's tools already live in.
///
/// It was a pill across the top of the grid. That announced itself well -- it appeared where the eye
/// goes -- but it put a one-off control on a view whose every other tool is in this column, and it
/// stood over the pattern it was offering to move. Here it costs the grid nothing and is where an
/// operator already looks for what this view can do.
///
/// It still appears only when a flight has left something to repair, so the strip is no longer for it
/// than it was before. That is what buys the row: this strip is close to full on a short window, and
/// a control that is there for a minute after each landing is a fairer tenant than one standing all
/// flight.
///
/// At the foot of the grid's own group and above the flying actions, which is the order the work is
/// done in: set the view up, read the sensors, repair what the last flight left, then fly.
ToolStripAction {
    id: _root

    objectName: "flyToolStrip_afterFlightButton"

    /// The grid whose between-flights work this opens
    property var gridView: null

    readonly property var _actions: gridView ? gridView.missionActions : null

    text:       qsTr("After Flight")
    iconSource: "/InstrumentValueIcons/wrench.svg"
    visible:    (_actions !== null) && _actions.hasAfterFlightWork

    onTriggered: _root._actions.showAfterFlightDialog()
}
