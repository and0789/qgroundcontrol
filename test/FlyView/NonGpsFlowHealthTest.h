#pragma once

#include "BaseClasses/VehicleTest.h"

class QObject;

/// Tests NonGpsFlowHealth.qml, which decides whether the EKF is accepting the optical flow
/// readings. Its verdict is what a pilot acts on, so the cases that must not silently pass are
/// "no flow arrived at all" and "the limit parameter is missing".
class NonGpsFlowHealthTest : public VehicleTest
{
    Q_OBJECT

public:
    explicit NonGpsFlowHealthTest(QObject *parent = nullptr);

private slots:
    void _noSamplesReportsNoData_test();
    void _accumulatesSamples_test();
    void _countsRejectedAboveLimit_test();
    void _missingLimitParameterGivesNoVerdict_test();
    void _samplesAgeOutOfWindow_test();
    void _limitFoundWhenVehicleArrivesLater_test();
};
