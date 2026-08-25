#include "LocalGridPositionCorrectionUITest.h"

#include <cmath>

#include <QtQuick/QQuickItem>
#include <QtTest/QTest>

#include "FlyViewSettings.h"
#include "LocalGridTestSupport.h"
#include "MockLink.h"
#include "SettingsManager.h"
#include "Vehicle.h"

UT_REGISTER_TEST(LocalGridPositionCorrectionUITest, TestLabel::Integration)

namespace {

using LocalGridTestSupport::OriginLatitude;
using LocalGridTestSupport::OriginLongitude;
using LocalGridTestSupport::giveTheVehicleAnOrigin;

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
            QCOMPARE(command.x, static_cast<int32_t>(std::lround(OriginLatitude * 1e7)));
            QCOMPARE(command.y, static_cast<int32_t>(std::lround(OriginLongitude * 1e7)));
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

/// Standing the aircraft back on the origin is one press, and it is not the dialog.
///
/// The dialog exists to let an operator check a claim they judged by eye off a grid with nothing on
/// it to judge against -- the offsets, the coordinate, the fallback, the last resort. None of that
/// applies to the origin: it is a mark on the ground the aircraft was carried back to, and every
/// field in that dialog reads 0.00 for it. A page of confirmation for a claim with nothing in it to
/// confirm is a page the operator learns to click through.
///
/// It lives with the between-flights controls rather than on the click panel, because it is not
/// about the point that was clicked. It is the first of the two remedies for a drifted frame, and
/// it sits directly above the other one.
void LocalGridPositionCorrectionUITest::_standingOnTheOriginIsOnePressNotADialog_test()
{
    SettingsManager::instance()->flyViewSettings()->showLocalGridView()->setRawValue(true);

    runWithMockLink(
        [] { return MockLink::startAPMArduCopterMockLink(); },
        [this](const QPointer<MockLink> &mockLink, Vehicle *vehicle) {
            QVERIFY(vehicle);
            QVERIFY2(giveTheVehicleAnOrigin(vehicle, mockLink), "the vehicle never took an origin");

            QQuickItem *const gridView = findVisibleItem(_rootItem, QStringLiteral("localGridView"), 10000);
            QVERIFY2(gridView, "the local grid never became visible with the setting on");

            mockLink->clearReceivedMavlinkMessageCounts();

            // The between-flights work is an entry on the fly view's tool strip and a dialog behind
            // it, since neither corner of this view has standing room on a small screen. Opening that
            // dialog is the one press this test is not counting -- what it is about is what happens
            // after the correction itself is pressed.
            QVERIFY2(findVisibleItem(_rootItem, QStringLiteral("flyToolStrip_afterFlightButton"), 5000),
                     "the tool strip never offered the between-flights work");
            QVERIFY2(clickButton(QStringLiteral("flyToolStrip_afterFlightButton")),
                     "the after-flight tool strip button could not be clicked");

            QVERIFY2(clickButton(QStringLiteral("localGrid_standOnOriginButton")),
                     "the between-flights controls offer no way to stand the aircraft on the origin");

            // No dialog. The send button is the one thing only that dialog has, so its absence is
            // what says the press went straight out.
            QVERIFY2(!findVisibleItem(_rootItem, QStringLiteral("correctPosition_sendButton"), 500),
                     "the press opened the correction dialog instead of sending the correction");

            QTRY_VERIFY_WITH_TIMEOUT(mockLink->receivedMavlinkMessageCount(MAVLINK_MSG_ID_COMMAND_INT) >= 1,
                                     TestTimeout::longMs());

            mavlink_message_t message{};
            QVERIFY(mockLink->lastReceivedMavlinkMessage(MAVLINK_MSG_ID_COMMAND_INT, message));
            mavlink_command_int_t command{};
            mavlink_msg_command_int_decode(&message, &command);

            QCOMPARE(command.command, static_cast<uint16_t>(MAV_CMD_EXTERNAL_POSITION_ESTIMATE));
            QCOMPARE(command.x, static_cast<int32_t>(std::lround(OriginLatitude * 1e7)));
            QCOMPARE(command.y, static_cast<int32_t>(std::lround(OriginLongitude * 1e7)));

            // MockLink does not implement the command, so what comes back is a refusal -- and the
            // operator has to be told, or they walk to the aircraft believing a position that never
            // moved. The same label carries the drift figure when the vehicle takes it.
            QQuickItem *const result =
                findVisibleItem(_rootItem, QStringLiteral("localGrid_standOnOriginResult"), 3000);
            QVERIFY2(result, "nothing said whether the correction was taken");
            QVERIFY2(!result->property("text").toString().isEmpty(),
                     "the result line is visible but says nothing");

            // Not asserted here: the wording this same label carries when a vehicle *takes* the
            // correction, which names how far the estimator had drifted. MockLink has no
            // implementation of the command to accept it with, and answering for it from the test
            // races the refusal that is already on its way back from the press above.
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
