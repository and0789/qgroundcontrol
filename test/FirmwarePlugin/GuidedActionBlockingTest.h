#pragma once

#include "BaseClasses/VehicleTest.h"

/// What a guided action does to the rest of the application while it waits on the vehicle.
///
/// Starting a mission means waiting for a flight mode change and then for the arm, and QGC used to
/// do both by sleeping on the GUI thread and pumping the event loop with
/// QEventLoop::ExcludeUserInputEvents between naps. A refused arming ran the full length of every
/// timeout, and user input was withheld for all of it.
///
/// That is worse than a frozen window. A guided action is confirmed by holding a button down, so the
/// touch release that ended the hold was withheld too, and by the time it was let through the button
/// had been hidden and a modal dialog raised over it. Qt was never handed the touch grab back, and
/// from then on every mouse-driven control in the application ignored a finger -- while the map,
/// which reads raw touch, still answered one -- for the rest of the session.
class GuidedActionBlockingTest : public VehicleTest
{
    Q_OBJECT

public:
    explicit GuidedActionBlockingTest(QObject* parent = nullptr) : VehicleTest(parent) {}

private slots:
    void _aRefusedMissionStart_answersWithoutHoldingItsCaller();
    void _anArmedMissionStart_reachesMissionModeWithoutHoldingItsCaller();
};
