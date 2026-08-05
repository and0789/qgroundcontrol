#pragma once

#include "UnitTest.h"

/// Tests VehicleEstimatorStatusFactGroup. PX4 and ArduPilot report estimator health through two
/// different messages, and handling only PX4's left the group silently empty on ArduPilot: every
/// health flag sat at its default false, which reads as a failing estimator rather than as no data.
class VehicleEstimatorStatusFactGroupTest : public UnitTest
{
    Q_OBJECT

private slots:
    void _noTelemetryBeforeAnyMessage_test();
    void _estimatorStatusPopulatesFacts_test();
    void _ekfStatusReportPopulatesFacts_test();
    void _unrelatedMessageIgnored_test();
};
