#include "LocalGridPositionCorrectionUITest.h"

#include <cmath>

#include <QtPositioning/QGeoCoordinate>
#include <QtQuick/QQuickItem>
#include <QtTest/QTest>

#include "FirmwarePlugin.h"
#include "FlyViewSettings.h"
#include "MockLink.h"
#include "SettingsManager.h"
#include "Vehicle.h"

UT_REGISTER_TEST(LocalGridPositionCorrectionUITest, TestLabel::Integration)

namespace {

// Somewhere real and away from the equator, so a latitude and longitude mix-up cannot pass
constexpr double kOriginLatitude = 47.3977419;
constexpr double kOriginLongitude = 8.5455938;

/// Gives the vehicle an estimator origin, which is the frame a correction is measured inside of.
///
/// Caching the COMMAND_INT form as unsupported drives the legacy message straight away: MockLink
/// refuses every COMMAND_INT, and waiting out that probe on each test is time spent proving something
/// this test is not about.
bool giveTheVehicleAnOrigin(Vehicle *vehicle, MockLink *mockLink)
{
    FirmwarePluginInstanceData *const instanceData = vehicle->firmwarePluginInstanceData();
    if (!instanceData) {
        return false;
    }

    instanceData->setCommandSupported(MAV_CMD_DO_SET_GLOBAL_ORIGIN,
                                      FirmwarePluginInstanceData::CommandSupportedResult::UNSUPPORTED);
    vehicle->setEstimatorOrigin(QGeoCoordinate(kOriginLatitude, kOriginLongitude, 0));
    if (!QTest::qWaitFor([mockLink]() {
            return mockLink->receivedMavlinkMessageCount(MAVLINK_MSG_ID_SET_GPS_GLOBAL_ORIGIN) >= 1;
        }, TestTimeout::longMs())) {
        return false;
    }

    vehicle->requestEstimatorOrigin();
    return QTest::qWaitFor([vehicle]() { return vehicle->estimatorOrigin().isValid(); }, TestTimeout::longMs());
}

/// Puts the origin marker on screen. The grid follows the aircraft, and MockLink flies its own sweep,
/// so by the time the UI has booted the origin can be anywhere -- including off the view entirely.
bool centreTheGridOnTheOrigin(QQuickItem *gridView)
{
    return QMetaObject::invokeMethod(gridView, "centreOnOrigin");
}

} // namespace

/// MAV_CMD_EXTERNAL_POSITION_ESTIMATE is an ArduPilot command, and the vehicle this boots is an
/// ArduCopter. A build with no ArduPilot plugin registered has no vehicle to connect, which is a
/// missing build option rather than a broken correction.
void LocalGridPositionCorrectionUITest::init()
{
    if (!apmFirmwareSupported()) {
        QSKIP("ArduPilot support not registered in this build");
    }
    QmlUITestBase::init();
}

void LocalGridPositionCorrectionUITest::cleanup()
{
    // Persisted, so leaving it on would put every later test's fly view on the grid
    SettingsManager::instance()->flyViewSettings()->showLocalGridView()->setRawValue(false);
    QmlUITestBase::cleanup();
}

/// The whole path: the marker on the grid, the coordinate it resolves to, the command that carries it
/// and the answer that comes back.
///
/// Standing the aircraft on the origin is the correction an operator can actually be sure of -- it is
/// the one point on the field they marked themselves -- so it is the one offered in a single click.
void LocalGridPositionCorrectionUITest::_originMarkerCorrectsThePositionToTheOrigin_test()
{
    // Set before the UI boots, so the fly view comes up on the grid rather than switching to it
    SettingsManager::instance()->flyViewSettings()->showLocalGridView()->setRawValue(true);

    runWithMockLink(
        [] { return MockLink::startAPMArduCopterMockLink(); },
        [this](const QPointer<MockLink> &mockLink, Vehicle *vehicle) {
            QVERIFY(vehicle);
            QVERIFY2(giveTheVehicleAnOrigin(vehicle, mockLink), "the vehicle never took an origin");

            QQuickItem *const gridView = findVisibleItem(_rootItem, QStringLiteral("localGridView"), 10000);
            QVERIFY2(gridView, "the local grid never became visible with the setting on");
            QVERIFY(centreTheGridOnTheOrigin(gridView));

            QVERIFY2(clickButton(QStringLiteral("localGrid_originMarkerLabel")),
                     "the origin marker offers no way to correct a position");
            QVERIFY2(waitForDialog(QStringLiteral("Correct Position")), "the correction dialog never opened");

            // Stated before it is sent. The failure this whole feature repairs is a position that
            // reads plausibly and is wrong, so a dialog that would not show the operator what it is
            // about to claim would be repeating the fault it exists to fix.
            QQuickItem *const coordinateLabel =
                findVisibleItem(_rootItem, QStringLiteral("correctPosition_coordinateLabel"), 1000);
            QVERIFY(coordinateLabel);
            const QString shown = coordinateLabel->property("text").toString();
            QVERIFY2(shown.contains(QStringLiteral("47.397741")) && shown.contains(QStringLiteral("8.545593")),
                     qPrintable(QStringLiteral("the dialog names a different point than the origin: %1").arg(shown)));

            mockLink->clearReceivedMavlinkMessageCounts();
            QVERIFY(verifyEnabled(QStringLiteral("correctPosition_sendButton"), true,
                                  QStringLiteral("disarmed and with an origin")));
            QVERIFY(clickButton(QStringLiteral("correctPosition_sendButton")));

            QTRY_VERIFY_WITH_TIMEOUT(mockLink->receivedMavlinkMessageCount(MAVLINK_MSG_ID_COMMAND_INT) >= 1,
                                     TestTimeout::longMs());

            mavlink_message_t message{};
            QVERIFY(mockLink->lastReceivedMavlinkMessage(MAVLINK_MSG_ID_COMMAND_INT, message));
            mavlink_command_int_t command{};
            mavlink_msg_command_int_decode(&message, &command);

            QCOMPARE(command.command, static_cast<uint16_t>(MAV_CMD_EXTERNAL_POSITION_ESTIMATE));
            QCOMPARE(command.frame, static_cast<uint8_t>(MAV_FRAME_GLOBAL));
            QCOMPARE(command.x, static_cast<int32_t>(std::lround(kOriginLatitude * 1e7)));
            QCOMPARE(command.y, static_cast<int32_t>(std::lround(kOriginLongitude * 1e7)));
            // Refused outright by the vehicle if this is anything but NaN: the command carries no
            // height, because what drifts is the horizontal frame
            QVERIFY2(std::isnan(command.z), "the altitude must be NaN or the vehicle refuses the command");

            // MockLink answers every COMMAND_INT as unsupported, which is a refusal an operator will
            // genuinely meet -- the feature is compiled out of boards with 1 MB of flash. What is
            // asserted is that it arrives as a sentence rather than as silence, which is the entire
            // reason this command is acknowledged rather than fired and forgotten.
            QQuickItem *const result =
                findVisibleItem(_rootItem, QStringLiteral("correctPosition_result"), 3000);
            QVERIFY2(result, "a refused correction told the operator nothing at all");
            const QString reason = result->property("text").toString();
            QVERIFY2(!reason.isEmpty(), "a refusal that says nothing is the failure this exists to prevent");
            QVERIFY2(!reason.contains(QStringLiteral("MAV_RESULT")),
                     qPrintable(QStringLiteral("the reason has to be readable, not an enum name, got: %1").arg(reason)));

            QVERIFY(rejectDialog());
        });
}

/// A correction is a step change in where the aircraft believes it is. Any mode holding position reads
/// that step as having been blown off course and flies the whole of it back, immediately and at
/// whatever speed the mode allows -- so this is a thing to do on the ground.
///
/// The lock is in the dialog rather than on the way into it. An entry point that goes dead in flight
/// teaches the operator the feature is broken; a dialog that opens and says why teaches them when to
/// use it.
void LocalGridPositionCorrectionUITest::_correctionIsLockedWhileArmed_test()
{
    SettingsManager::instance()->flyViewSettings()->showLocalGridView()->setRawValue(true);

    runWithMockLink(
        [] { return MockLink::startAPMArduCopterMockLink(); },
        [this](const QPointer<MockLink> &mockLink, Vehicle *vehicle) {
            QVERIFY(vehicle);
            QVERIFY2(giveTheVehicleAnOrigin(vehicle, mockLink), "the vehicle never took an origin");

            QQuickItem *const gridView = findVisibleItem(_rootItem, QStringLiteral("localGridView"), 10000);
            QVERIFY2(gridView, "the local grid never became visible with the setting on");
            QVERIFY(centreTheGridOnTheOrigin(gridView));

            vehicle->setArmedShowError(true);
            QTRY_VERIFY_WITH_TIMEOUT(vehicle->armed(), TestTimeout::longMs());

            mockLink->clearReceivedMavlinkMessageCounts();

            QVERIFY(clickButton(QStringLiteral("localGrid_originMarkerLabel")));
            QVERIFY2(waitForDialog(QStringLiteral("Correct Position")),
                     "the dialog has to open in flight, or there is nowhere to read why it cannot be used");

            QVERIFY(verifyEnabled(QStringLiteral("correctPosition_sendButton"), false,
                                  QStringLiteral("while the vehicle is armed")));
            QVERIFY(verifyVisibility(QStringLiteral("correctPosition_armedWarning"), true,
                                     QStringLiteral("while the vehicle is armed")));

            QCOMPARE(mockLink->receivedMavlinkMessageCount(MAVLINK_MSG_ID_COMMAND_INT), 0);

            QVERIFY(rejectDialog());
            vehicle->setArmedShowError(false);
            QTRY_VERIFY_WITH_TIMEOUT(!vehicle->armed(), TestTimeout::longMs());
        });
}
