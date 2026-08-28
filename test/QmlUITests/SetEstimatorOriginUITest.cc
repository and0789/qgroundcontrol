#include "SetEstimatorOriginUITest.h"

#include <QtPositioning/QGeoCoordinate>
#include <QtQuick/QQuickItem>
#include <QtTest/QTest>

#include "Fact.h"
#include "FirmwarePlugin.h"
#include "FlyViewSettings.h"
#include "MockLink.h"
#include "ParameterManager.h"
#include "SettingsManager.h"
#include "Vehicle.h"

UT_REGISTER_TEST(SetEstimatorOriginUITest, TestLabel::Integration)

namespace {

// Somewhere real and away from the equator, so a latitude and longitude mix-up cannot pass
constexpr double kOriginLatitude = 47.3977419;
constexpr double kOriginLongitude = 8.5455938;

// AHRS_OPTIONS bit 3 (save the origin) and bit 4 (restore it at boot without GNSS)
constexpr int kRememberOriginBits = 24;
// An unrelated bit that must survive: DisableDCMFallbackVTOL
constexpr int kUnrelatedOptionBit = 2;

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

/// Telling the vehicle to remember its origin must set the two bits it needs and leave the rest of
/// AHRS_OPTIONS alone.
///
/// The parameter is a bitmask whose lower bits carry unrelated DCM fallback and airspeed behaviour,
/// so writing the mask whole would quietly change how the aircraft flies to buy an origin that
/// survives a reboot. And surviving a reboot is the point: without it the origin dies with every
/// power cycle, and the aircraft comes back with no home, no local position on the wire, and a
/// mission that raises an internal error on its first takeoff.
void SetEstimatorOriginUITest::_rememberingTheOriginSetsOnlyItsOwnBits_test()
{
    FlyViewSettings* const flyViewSettings = SettingsManager::instance()->flyViewSettings();
    flyViewSettings->showLocalGridView()->setRawValue(true);

    runWithMockLink(
        [] { return MockLink::startAPMArduCopterMockLink(); },
        [this](const QPointer<MockLink>& mockLink, Vehicle* vehicle) {
            Q_UNUSED(mockLink);
            QVERIFY(vehicle);

            Fact* const options = vehicle->parameterManager()->getParameter(ParameterManager::defaultComponentId,
                                                                            QStringLiteral("AHRS_OPTIONS"));
            QVERIFY2(options, "the mock ArduCopter must carry the parameter this dialog offers");
            options->containerSetRawValue(kUnrelatedOptionBit);

            QVERIFY2(clickButton(QStringLiteral("localGrid_setOriginButton")),
                     "the grid offers no way to set an origin");
            QVERIFY(findVisibleItem(_rootItem, QStringLiteral("setOrigin_latitudeField"), 3000));

            QQuickItem* const rememberCheckBox =
                findVisibleItem(_rootItem, QStringLiteral("setOrigin_rememberCheckBox"), 1000);
            QVERIFY2(rememberCheckBox, "a vehicle whose firmware can remember an origin must be offered it");
            QVERIFY2(!rememberCheckBox->property("checked").toBool(), "the box has to start where the parameter is");

            QVERIFY(clickButton(QStringLiteral("setOrigin_rememberCheckBox")));
            QTRY_COMPARE_WITH_TIMEOUT(options->rawValue().toInt(), kUnrelatedOptionBit | kRememberOriginBits,
                                      TestTimeout::shortMs());
            QVERIFY2(rememberCheckBox->property("checked").toBool(),
                     "the box has to follow the parameter it just wrote");

            // And back off again, leaving what it never owned
            QVERIFY(clickButton(QStringLiteral("setOrigin_rememberCheckBox")));
            QTRY_COMPARE_WITH_TIMEOUT(options->rawValue().toInt(), kUnrelatedOptionBit, TestTimeout::shortMs());
            QVERIFY(!rememberCheckBox->property("checked").toBool());
        });
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

            // The readout's button has done its job and gone. It is highlighted and full width
            // because an origin is the one thing that blocks everything else, and once there is one
            // it would be a rarely-pressed button standing in the middle of the live numbers.
            QVERIFY(verifyVisibility(QStringLiteral("localGrid_setOriginButton"), false,
                                     QStringLiteral("once the vehicle has an origin")));

            // Still reachable, one click in, from the marker for the thing it moves. An origin in the
            // wrong region is not a cosmetic mistake -- it fails the pre-arm compass check -- so
            // correcting one must not require reconnecting the vehicle.
            QQuickItem *const gridView = findVisibleItem(_rootItem, QStringLiteral("localGridView"), 5000);
            QVERIFY(gridView);
            QVERIFY(QMetaObject::invokeMethod(gridView, "centreOnOrigin"));

            QVERIFY(clickButton(QStringLiteral("localGrid_originMarkerLabel")));
            QVERIFY2(waitForDialog(QStringLiteral("Correct Position")), "the correction dialog never opened");
            QVERIFY2(findVisibleItem(_rootItem, QStringLiteral("correctPosition_changeOriginButton"), 2000),
                     "the origin can no longer be changed once one is set");
        });
}
