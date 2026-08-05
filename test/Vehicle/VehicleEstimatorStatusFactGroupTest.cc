#include "VehicleEstimatorStatusFactGroupTest.h"

#include <QtTest/QTest>

#include "Fact.h"
#include "VehicleEstimatorStatusFactGroup.h"

namespace {

constexpr uint8_t kSystemId = 1;
constexpr uint8_t kComponentId = MAV_COMP_ID_AUTOPILOT1;

/// The flag word an ArduPilot vehicle reports once its EKF is fusing optical flow and rangefinder:
/// attitude, both velocities, relative and vertical position, AGL and the predicted relative
/// position are all set, and constant position mode is off.
constexpr uint16_t kHealthyArduPilotFlags = 0x016F;

bool factBool(FactGroup &factGroup, const char *factName)
{
    Fact *const fact = factGroup.getFact(QString::fromLatin1(factName));
    return fact && fact->rawValue().toBool();
}

double factValue(FactGroup &factGroup, const char *factName)
{
    Fact *const fact = factGroup.getFact(QString::fromLatin1(factName));
    return fact ? fact->rawValue().toDouble() : qQNaN();
}

} // namespace

void VehicleEstimatorStatusFactGroupTest::_noTelemetryBeforeAnyMessage_test()
{
    VehicleEstimatorStatusFactGroup factGroup;

    // The health facts default to false, so telemetryAvailable is the only thing that separates
    // "estimator is unhealthy" from "nothing has arrived"
    QVERIFY(!factGroup.telemetryAvailable());
}

void VehicleEstimatorStatusFactGroupTest::_estimatorStatusPopulatesFacts_test()
{
    VehicleEstimatorStatusFactGroup factGroup;

    mavlink_estimator_status_t estimatorStatus{};
    estimatorStatus.flags = ESTIMATOR_ATTITUDE | ESTIMATOR_VELOCITY_HORIZ | ESTIMATOR_POS_HORIZ_REL;
    estimatorStatus.vel_ratio = 0.25F;
    estimatorStatus.pos_horiz_ratio = 0.5F;

    mavlink_message_t message{};
    (void) mavlink_msg_estimator_status_encode(kSystemId, kComponentId, &message, &estimatorStatus);
    factGroup.handleMessage(nullptr, message);

    QVERIFY(factGroup.telemetryAvailable());
    QVERIFY(factBool(factGroup, "goodAttitudeEsimate"));
    QVERIFY(factBool(factGroup, "goodHorizVelEstimate"));
    QVERIFY(factBool(factGroup, "goodHorizPosRelEstimate"));
    QVERIFY(!factBool(factGroup, "goodConstPosModeEstimate"));
    QCOMPARE(factValue(factGroup, "velRatio"), 0.25);
    QCOMPARE(factValue(factGroup, "horizPosRatio"), 0.5);
}

void VehicleEstimatorStatusFactGroupTest::_ekfStatusReportPopulatesFacts_test()
{
    VehicleEstimatorStatusFactGroup factGroup;

    mavlink_ekf_status_report_t ekfStatus{};
    ekfStatus.flags = kHealthyArduPilotFlags;
    ekfStatus.velocity_variance = 0.25F;
    ekfStatus.pos_horiz_variance = 0.5F;
    ekfStatus.terrain_alt_variance = 0.125F;

    mavlink_message_t message{};
    (void) mavlink_msg_ekf_status_report_encode(kSystemId, kComponentId, &message, &ekfStatus);
    factGroup.handleMessage(nullptr, message);

    QVERIFY2(factGroup.telemetryAvailable(), "ArduPilot's estimator message must count as telemetry");

    QVERIFY(factBool(factGroup, "goodAttitudeEsimate"));
    QVERIFY(factBool(factGroup, "goodHorizVelEstimate"));
    QVERIFY(factBool(factGroup, "goodVertVelEstimate"));
    QVERIFY(factBool(factGroup, "goodHorizPosRelEstimate"));
    QVERIFY(factBool(factGroup, "goodVertPosAbsEstimate"));
    QVERIFY(factBool(factGroup, "goodVertPosAGLEstimate"));
    QVERIFY(factBool(factGroup, "goodPredHorizPosRelEstimate"));

    // Without GPS these stay clear, and constant position mode being off is what says the EKF is
    // actually aiding rather than coasting
    QVERIFY(!factBool(factGroup, "goodConstPosModeEstimate"));
    QVERIFY(!factBool(factGroup, "goodHorizPosAbsEstimate"));

    QCOMPARE(factValue(factGroup, "velRatio"), 0.25);
    QCOMPARE(factValue(factGroup, "horizPosRatio"), 0.5);
    QCOMPARE(factValue(factGroup, "haglRatio"), 0.125);
}

void VehicleEstimatorStatusFactGroupTest::_unrelatedMessageIgnored_test()
{
    VehicleEstimatorStatusFactGroup factGroup;

    mavlink_heartbeat_t heartbeat{};
    heartbeat.type = MAV_TYPE_QUADROTOR;

    mavlink_message_t message{};
    (void) mavlink_msg_heartbeat_encode(kSystemId, kComponentId, &message, &heartbeat);
    factGroup.handleMessage(nullptr, message);

    QVERIFY(!factGroup.telemetryAvailable());
}

UT_REGISTER_TEST(VehicleEstimatorStatusFactGroupTest, TestLabel::Unit, TestLabel::Vehicle)
