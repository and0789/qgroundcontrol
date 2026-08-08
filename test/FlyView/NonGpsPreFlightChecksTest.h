#pragma once

#include "BaseClasses/VehicleTest.h"

/// Tests the pre-flight checks that only apply to a vehicle navigating without GNSS.
///
/// The checks are QML, and the state they read lives across several fact groups, so they are
/// instantiated here against a real vehicle rather than asserted about on paper. A check that reads
/// a renamed fact does not fail loudly -- it silently reads undefined and passes, which is the one
/// failure mode a pre-flight check must never have.
class NonGpsPreFlightChecksTest : public VehicleTestAPM
{
    Q_OBJECT

public:
    explicit NonGpsPreFlightChecksTest(QObject *parent = nullptr);

protected slots:
    void init() override;

private slots:
    void _vehicleWithGNSS_checksAreHiddenAndPassing_test();
    void _vehicleWithoutGNSS_checksAreShown_test();
    void _missingEstimatorOrigin_failsWithoutOverride_test();
    void _flowAndRangefinderReporting_passChecks_test();
    void _ekfNotReady_failsWithoutOverride_test();
    void _gpsCheck_doesNotBlockAGnssDeniedVehicle_test();
};
