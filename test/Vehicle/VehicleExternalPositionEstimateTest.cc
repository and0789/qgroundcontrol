#include "VehicleExternalPositionEstimateTest.h"

#include <cmath>

#include <QtPositioning/QGeoCoordinate>
#include <QtTest/QSignalSpy>
#include <QtTest/QTest>

#include "MockLink.h"
#include "Vehicle.h"

/// The command has to reach the vehicle as a COMMAND_INT in the global frame carrying no altitude.
/// ArduPilot's handler answers DENIED to anything else before it looks at the position at all, so a
/// wrong shape here is a feature that never works on any vehicle and says nothing about why.
void VehicleExternalPositionEstimateTest::_commandCarriesThePositionWithNoAltitude_test()
{
    QVERIFY(vehicle());
    QVERIFY(mockLink());

    mockLink()->clearReceivedMavlinkMessageCounts();

    const QGeoCoordinate here(47.3977419, 8.5455938);
    vehicle()->sendExternalPositionEstimate(here, 0.5F);

    QTRY_VERIFY_WITH_TIMEOUT(mockLink()->receivedMavlinkMessageCount(MAVLINK_MSG_ID_COMMAND_INT) >= 1,
                             TestTimeout::mediumMs());

    mavlink_message_t message{};
    QVERIFY(mockLink()->lastReceivedMavlinkMessage(MAVLINK_MSG_ID_COMMAND_INT, message));

    mavlink_command_int_t command{};
    mavlink_msg_command_int_decode(&message, &command);

    QCOMPARE(command.command, static_cast<uint16_t>(MAV_CMD_EXTERNAL_POSITION_ESTIMATE));
    QCOMPARE(command.frame, static_cast<uint8_t>(MAV_FRAME_GLOBAL));

    // Refused outright by the vehicle if this is anything but NaN. The command carries no height on
    // purpose: what drifts is the horizontal frame.
    QVERIFY2(std::isnan(command.z), "the altitude must be NaN or the vehicle refuses the command");

    // COMMAND_INT carries the position as degrees times 1e7
    QCOMPARE(command.x, static_cast<int32_t>(std::lround(here.latitude() * 1e7)));
    QCOMPARE(command.y, static_cast<int32_t>(std::lround(here.longitude() * 1e7)));

    QCOMPARE(command.param3, 0.5F);
    QVERIFY2(command.param1 >= 0.0F, "a timestamp in the sender's own domain is still a timestamp");
}

/// Nothing is sent for a position that is not one, and the caller is told rather than left waiting
/// for an answer that will never come.
void VehicleExternalPositionEstimateTest::_invalidCoordinateIsRefusedWithoutSending_test()
{
    QVERIFY(vehicle());
    QVERIFY(mockLink());

    mockLink()->clearReceivedMavlinkMessageCounts();

    QSignalSpy resultSpy(vehicle(), &Vehicle::externalPositionEstimateResult);
    QVERIFY(resultSpy.isValid());

    vehicle()->sendExternalPositionEstimate(QGeoCoordinate());

    QCOMPARE(resultSpy.count(), 1);
    QVERIFY2(!resultSpy.at(0).at(0).toBool(), "an invalid position cannot be accepted");
    QVERIFY2(!resultSpy.at(0).at(1).toString().isEmpty(), "a refusal has to say why");

    QCOMPARE(mockLink()->receivedMavlinkMessageCount(MAVLINK_MSG_ID_COMMAND_INT), 0);
}

/// A refusal is where the value of this is. MockLink does not implement the command, so what comes
/// back is the "this firmware cannot do it" path -- which is a real answer an operator will meet on
/// a board whose flash was too small to compile the feature in.
void VehicleExternalPositionEstimateTest::_refusalIsReportedWithAReason_test()
{
    QVERIFY(vehicle());

    QSignalSpy resultSpy(vehicle(), &Vehicle::externalPositionEstimateResult);
    QVERIFY(resultSpy.isValid());

    vehicle()->sendExternalPositionEstimate(QGeoCoordinate(47.3977419, 8.5455938));

    QVERIFY_SIGNAL_WAIT(resultSpy, TestTimeout::longMs());

    QVERIFY2(!resultSpy.at(0).at(0).toBool(), "a vehicle that cannot do this has not done it");

    const QString reason = resultSpy.at(0).at(1).toString();
    QVERIFY2(!reason.isEmpty(), "a refusal that says nothing is the failure this exists to prevent");
    QVERIFY2(!reason.contains(QStringLiteral("MAV_RESULT")),
             qPrintable(QStringLiteral("the reason has to be readable, not an enum name, got: %1").arg(reason)));
}

UT_REGISTER_TEST(VehicleExternalPositionEstimateTest, TestLabel::Integration, TestLabel::Vehicle)
