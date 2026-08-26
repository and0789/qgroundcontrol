#pragma once

#include "QmlUITestBase.h"

/// Drives the buttons on a QGCPopupDialog with a finger as well as a mouse.
///
/// Every message QGC raises from C++ -- an arming refusal among them -- lands on one of these
/// dialogs, and its accept button is the only way off it. A dialog that cannot be dismissed by the
/// device the operator is holding is a dead end in the middle of a flight, so the two devices are
/// driven through the same button and asserted against the same outcome.
class PopupDialogTouchUITest : public QmlUITestBase
{
    Q_OBJECT

private slots:
    void _aFingerDismissesAnAppMessageTheWayAMouseDoes_test();
};
