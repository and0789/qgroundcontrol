#pragma once

#include "BaseClasses/VehicleTest.h"

/// Covers telling a vehicle where it actually is, for an estimator whose frame has slid away from
/// the ground beneath it.
///
/// What is asserted is the shape of the command and what comes back from a refusal. ArduPilot
/// refuses this one outright unless it arrives in the global frame with no altitude, so the shape is
/// not a detail -- get it wrong and the feature is silently dead on every vehicle. And a refusal
/// carries the diagnosis: firmware built without the feature, or an estimator that has stopped
/// aiding and has nothing to correct against.
class VehicleExternalPositionEstimateTest : public VehicleTest
{
    Q_OBJECT

private slots:
    void _commandCarriesThePositionWithNoAltitude_test();
    void _invalidCoordinateIsRefusedWithoutSending_test();
    void _refusalIsReportedWithAReason_test();
};
