#pragma once

#include "QmlUITestBase.h"

/// Drives a position correction through the running UI, from the grid to the wire.
///
/// An estimator navigating on optical flow drifts, and until now the only remedy in QGC was a power
/// cycle -- which loses the origin, the plan and the flight with it. The repair is a command telling
/// the vehicle where it actually is, and the two things that have to be right about it are the
/// coordinate it carries and the answer it comes back with. A correction sent to the wrong point is
/// worse than none, and one refused in silence leaves the operator flying a position that never moved.
class LocalGridPositionCorrectionUITest : public QmlUITestBase
{
    Q_OBJECT

protected slots:
    void init() override;
    void cleanup() override;

private slots:
    void _originMarkerCorrectsThePositionToTheOrigin_test();
    void _correctionIsLockedWhileArmed_test();
};
