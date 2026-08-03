#pragma once

#include "UnitTest.h"

/// Tests OPTICAL_FLOW and OPTICAL_FLOW_RAD handling in VehicleOpticalFlowFactGroup by feeding
/// the fact group encoded messages directly, without needing a vehicle connection.
class VehicleOpticalFlowFactGroupTest : public UnitTest
{
    Q_OBJECT

private slots:
    void _initialValues_test();
    void _opticalFlow_test();
    void _opticalFlowUnknownGroundDistance_test();
    void _opticalFlowRad_test();
    void _opticalFlowRadZeroIntegrationTime_test();
    void _unrelatedMessageIgnored_test();
};
