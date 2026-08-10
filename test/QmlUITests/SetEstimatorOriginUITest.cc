#include "SetEstimatorOriginUITest.h"

#include <QtPositioning/QGeoCoordinate>
#include <QtQuick/QQuickItem>
#include <QtTest/QTest>

#include "FirmwarePlugin.h"
#include "FlyViewSettings.h"
#include "MockLink.h"
#include "SettingsManager.h"
#include "Vehicle.h"

UT_REGISTER_TEST(SetEstimatorOriginUITest, TestLabel::Integration)

namespace {

// Somewhere real and away from the equator, so a latitude and longitude mix-up cannot pass
constexpr double kOriginLatitude = 47.3977419;
constexpr double kOriginLongitude = 8.5455938;

} // namespace

/// The dialog exists for ArduPilot's estimator origin, and the vehicle this boots is an ArduCopter. A
/// build with no ArduPilot plugin registered has no vehicle to connect, which is a missing build
/// option rather than a broken dialog.
void SetEstimatorOriginUITest::init()
{
    if (!apmFirmwareSupported()) {
        QSKIP("ArduPilot support not registered in this build");
    }
    QmlUITestBase::init();
}

void SetEstimatorOriginUITest::cleanup()
{
    FlyViewSettings *const flyViewSettings = SettingsManager::instance()->flyViewSettings();
    // All three persist, so leaving them set would follow every later test into its own fly view
    flyViewSettings->showLocalGridView()->setRawValue(false);
    flyViewSettings->lastEstimatorOriginLatitude()->setRawValue(0);
    flyViewSettings->lastEstimatorOriginLongitude()->setRawValue(0);

    QmlUITestBase::cleanup();
}

/// The whole point of the dialog: an origin set from the grid, with no map involved at any step.
void SetEstimatorOriginUITest::_originCanBeSetFromTheGridWithoutAMap_test()
{
    FlyViewSettings *const flyViewSettings = SettingsManager::instance()->flyViewSettings();
    flyViewSettings->showLocalGridView()->setRawValue(true);
    flyViewSettings->lastEstimatorOriginLatitude()->setRawValue(0);
    flyViewSettings->lastEstimatorOriginLongitude()->setRawValue(0);

    runWithMockLink(
        [] { return MockLink::startAPMArduCopterMockLink(); },
        [this](const QPointer<MockLink> &mockLink, Vehicle *vehicle) {
            QVERIFY(vehicle);
            QVERIFY2(!vehicle->estimatorOrigin().isValid(), "the mock vehicle starts without an origin");

            // MockLink refuses the COMMAND_INT form, and setEstimatorOrigin reports a refusal to the
            // user. Caching it as unsupported drives the legacy message straight away, so the test
            // exercises the dialog rather than the command probe.
            FirmwarePluginInstanceData *const instanceData = vehicle->firmwarePluginInstanceData();
            QVERIFY(instanceData);
            instanceData->setCommandSupported(MAV_CMD_DO_SET_GLOBAL_ORIGIN,
                                              FirmwarePluginInstanceData::CommandSupportedResult::UNSUPPORTED);
            mockLink->clearReceivedMavlinkMessageCounts();

            // The button only exists while there is no origin, which is exactly when it is needed
            QVERIFY2(clickButton(QStringLiteral("localGrid_setOriginButton")),
                     "the grid offers no way to set an origin");

            QQuickItem *const latitudeField =
                findVisibleItem(_rootItem, QStringLiteral("setOrigin_latitudeField"), 3000);
            QVERIFY2(latitudeField, "the origin dialog never opened");
            QQuickItem *const longitudeField =
                findVisibleItem(_rootItem, QStringLiteral("setOrigin_longitudeField"), 1000);
            QVERIFY(longitudeField);

            // Nothing remembered and no ground station fix, so nothing is offered. A convenient
            // default here would be a coordinate on the wrong continent, which arms nothing: the
            // autopilot checks the compass against the magnetic model at the origin.
            QVERIFY2(latitudeField->property("text").toString().isEmpty(),
                     "an unknown origin must not be prefilled with an invented coordinate");
            QVERIFY(verifyEnabled(QStringLiteral("setOrigin_applyTypedButton"), false,
                                  QStringLiteral("with no coordinate typed")));

            latitudeField->setProperty("text", QString::number(kOriginLatitude, 'f', 7));
            longitudeField->setProperty("text", QString::number(kOriginLongitude, 'f', 7));

            QVERIFY(clickButton(QStringLiteral("setOrigin_applyTypedButton")));

            // What the vehicle actually receives is the only thing that matters here
            QTRY_VERIFY_WITH_TIMEOUT(
                mockLink->receivedMavlinkMessageCount(MAVLINK_MSG_ID_SET_GPS_GLOBAL_ORIGIN) == 1,
                TestTimeout::longMs());

            mavlink_message_t message{};
            QVERIFY(mockLink->lastReceivedMavlinkMessage(MAVLINK_MSG_ID_SET_GPS_GLOBAL_ORIGIN, message));
            mavlink_set_gps_global_origin_t origin{};
            mavlink_msg_set_gps_global_origin_decode(&message, &origin);
            QCOMPARE(origin.latitude, static_cast<int32_t>(kOriginLatitude * 1e7));
            QCOMPARE(origin.longitude, static_cast<int32_t>(kOriginLongitude * 1e7));

            // Remembered, so the next flight from the same place is one button
            QCOMPARE(SettingsManager::instance()->flyViewSettings()->lastEstimatorOriginLatitude()->rawValue().toDouble(),
                     kOriginLatitude);
            QCOMPARE(SettingsManager::instance()->flyViewSettings()->lastEstimatorOriginLongitude()->rawValue().toDouble(),
                     kOriginLongitude);

            // And the vehicle reports it back, which is what turns the rest of the non-GPS UI on
            vehicle->requestEstimatorOrigin();
            QTRY_VERIFY_WITH_TIMEOUT(vehicle->estimatorOrigin().isValid(), TestTimeout::longMs());
            QVERIFY(qAbs(vehicle->estimatorOrigin().latitude() - kOriginLatitude) < 0.0000001);

            // Still reachable with an origin already set. An origin in the wrong region is not a
            // cosmetic mistake -- it fails the pre-arm compass check -- so correcting one must not
            // require reconnecting the vehicle.
            QQuickItem *const originButton =
                findVisibleItem(_rootItem, QStringLiteral("localGrid_setOriginButton"), 2000);
            QVERIFY2(originButton, "the origin can no longer be changed once one is set");
            QVERIFY2(originButton->property("text").toString().contains(QStringLiteral("Change")),
                     "the button must say it changes the existing origin rather than setting a first one");
        });
}
