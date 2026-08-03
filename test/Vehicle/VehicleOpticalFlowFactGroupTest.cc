#include "VehicleOpticalFlowFactGroupTest.h"

#include <QtTest/QTest>

#include <cmath>

#include "Fact.h"
#include "VehicleOpticalFlowFactGroup.h"

namespace {

constexpr uint8_t kSystemId = 1;
constexpr uint8_t kComponentId = MAV_COMP_ID_AUTOPILOT1;

mavlink_message_t encodeOpticalFlow(const mavlink_optical_flow_t &opticalFlow)
{
    mavlink_message_t message{};
    (void) mavlink_msg_optical_flow_encode(kSystemId, kComponentId, &message, &opticalFlow);
    return message;
}

mavlink_message_t encodeOpticalFlowRad(const mavlink_optical_flow_rad_t &opticalFlow)
{
    mavlink_message_t message{};
    (void) mavlink_msg_optical_flow_rad_encode(kSystemId, kComponentId, &message, &opticalFlow);
    return message;
}

double factValue(FactGroup &factGroup, const char *factName)
{
    Fact *const fact = factGroup.getFact(QString::fromLatin1(factName));
    return fact ? fact->rawValue().toDouble() : qQNaN();
}

} // namespace

void VehicleOpticalFlowFactGroupTest::_initialValues_test()
{
    VehicleOpticalFlowFactGroup factGroup;

    // Metadata only carries units and descriptions if OpticalFlowFact.json is registered as a resource
    Fact *const flowCompX = factGroup.getFact(QStringLiteral("flowCompX"));
    QVERIFY(flowCompX);
    QCOMPARE(flowCompX->rawUnits(), QStringLiteral("rad/s"));
    QCOMPARE(flowCompX->shortDescription(), QStringLiteral("Flow X (comp)"));

    QVERIFY(!factGroup.telemetryAvailable());
    QVERIFY(qIsNaN(factValue(factGroup, "flowCompX")));
    QVERIFY(qIsNaN(factValue(factGroup, "flowCompY")));
    QVERIFY(qIsNaN(factValue(factGroup, "flowCompMagnitude")));
    QVERIFY(qIsNaN(factValue(factGroup, "flowRateX")));
    QVERIFY(qIsNaN(factValue(factGroup, "flowRateY")));
    QVERIFY(qIsNaN(factValue(factGroup, "groundDistance")));
}

void VehicleOpticalFlowFactGroupTest::_opticalFlow_test()
{
    VehicleOpticalFlowFactGroup factGroup;

    mavlink_optical_flow_t opticalFlow{};
    opticalFlow.quality = 180;
    opticalFlow.flow_comp_m_x = 0.25f;
    opticalFlow.flow_comp_m_y = -0.5f;
    opticalFlow.flow_rate_x = 1.5f;
    opticalFlow.flow_rate_y = -2.0f;
    opticalFlow.ground_distance = 3.25f;

    const mavlink_message_t message = encodeOpticalFlow(opticalFlow);
    factGroup.handleMessage(nullptr, message);

    QVERIFY(factGroup.telemetryAvailable());
    QCOMPARE(factValue(factGroup, "quality"), 180.0);
    QCOMPARE(factValue(factGroup, "flowCompX"), 0.25);
    QCOMPARE(factValue(factGroup, "flowCompY"), -0.5);
    QCOMPARE(factValue(factGroup, "flowCompMagnitude"), std::hypot(0.25, -0.5));
    QCOMPARE(factValue(factGroup, "flowRateX"), 1.5);
    QCOMPARE(factValue(factGroup, "flowRateY"), -2.0);
    QCOMPARE(factValue(factGroup, "groundDistance"), 3.25);
}

void VehicleOpticalFlowFactGroupTest::_opticalFlowUnknownGroundDistance_test()
{
    VehicleOpticalFlowFactGroup factGroup;

    // A negative ground distance means the sensor does not know the distance
    mavlink_optical_flow_t opticalFlow{};
    opticalFlow.ground_distance = -1.0f;

    const mavlink_message_t message = encodeOpticalFlow(opticalFlow);
    factGroup.handleMessage(nullptr, message);

    QVERIFY(qIsNaN(factValue(factGroup, "groundDistance")));
}

void VehicleOpticalFlowFactGroupTest::_opticalFlowRad_test()
{
    VehicleOpticalFlowFactGroup factGroup;

    // Integrated over 0.25s, so the rates are 4x the integrated angles
    mavlink_optical_flow_rad_t opticalFlow{};
    opticalFlow.quality = 90;
    opticalFlow.integration_time_us = 250000;
    opticalFlow.integrated_x = 0.5f;
    opticalFlow.integrated_y = -1.0f;
    opticalFlow.integrated_xgyro = 0.25f;
    opticalFlow.integrated_ygyro = 0.5f;
    opticalFlow.distance = 2.5f;

    const mavlink_message_t message = encodeOpticalFlowRad(opticalFlow);
    factGroup.handleMessage(nullptr, message);

    QVERIFY(factGroup.telemetryAvailable());
    QCOMPARE(factValue(factGroup, "quality"), 90.0);
    QCOMPARE(factValue(factGroup, "flowRateX"), 2.0);
    QCOMPARE(factValue(factGroup, "flowRateY"), -4.0);
    QCOMPARE(factValue(factGroup, "groundDistance"), 2.5);

    // flowComp is the flow rate with the gyro rate removed
    QCOMPARE(factValue(factGroup, "flowCompX"), 1.0);
    QCOMPARE(factValue(factGroup, "flowCompY"), -6.0);
    QCOMPARE(factValue(factGroup, "flowCompMagnitude"), std::hypot(1.0, -6.0));
}

void VehicleOpticalFlowFactGroupTest::_opticalFlowRadZeroIntegrationTime_test()
{
    VehicleOpticalFlowFactGroup factGroup;

    // Rates cannot be recovered without an integration interval
    mavlink_optical_flow_rad_t opticalFlow{};
    opticalFlow.quality = 90;
    opticalFlow.integration_time_us = 0;
    opticalFlow.integrated_x = 0.5f;
    opticalFlow.integrated_y = -1.0f;
    opticalFlow.distance = 2.5f;

    const mavlink_message_t message = encodeOpticalFlowRad(opticalFlow);
    factGroup.handleMessage(nullptr, message);

    QCOMPARE(factValue(factGroup, "quality"), 90.0);
    QCOMPARE(factValue(factGroup, "groundDistance"), 2.5);
    QVERIFY(qIsNaN(factValue(factGroup, "flowRateX")));
    QVERIFY(qIsNaN(factValue(factGroup, "flowRateY")));
    QVERIFY(qIsNaN(factValue(factGroup, "flowCompX")));
    QVERIFY(qIsNaN(factValue(factGroup, "flowCompY")));
    QVERIFY(qIsNaN(factValue(factGroup, "flowCompMagnitude")));
}

void VehicleOpticalFlowFactGroupTest::_unrelatedMessageIgnored_test()
{
    VehicleOpticalFlowFactGroup factGroup;

    mavlink_heartbeat_t heartbeat{};
    heartbeat.type = MAV_TYPE_QUADROTOR;
    heartbeat.autopilot = MAV_AUTOPILOT_ARDUPILOTMEGA;

    mavlink_message_t message{};
    (void) mavlink_msg_heartbeat_encode(kSystemId, kComponentId, &message, &heartbeat);
    factGroup.handleMessage(nullptr, message);

    QVERIFY(!factGroup.telemetryAvailable());
    QVERIFY(qIsNaN(factValue(factGroup, "flowCompX")));
}

UT_REGISTER_TEST(VehicleOpticalFlowFactGroupTest, TestLabel::Unit, TestLabel::Vehicle)
