import QGroundControl
import QGroundControl.Controls

ToolStripAction {
    property int    actionID
    property string message

    property var _guidedController: globals.guidedControllerFlyView

    /// True while the strip is showing the local grid's plan-building controls instead of the
    /// flying ones. Every guided action commands the aircraft the moment it is confirmed, so none
    /// of them belongs on screen beside a plan being drawn -- and the plan buttons take their
    /// places, which only works if they let go of them.
    ///
    /// Pause is the exception and stays put: it stops what is already happening, and an operator
    /// editing a plan while the aircraft flies one is exactly who needs it within reach.
    readonly property bool _planEditMode: globals.localGridViewFlyView
                                            ? globals.localGridViewFlyView.planEditMode
                                            : false

    onTriggered: {
        _guidedController.closeAll()
        _guidedController.confirmAction(actionID)
    }
}
