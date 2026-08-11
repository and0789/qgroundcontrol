#include "ToolbarIndicatorUITest.h"

#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickWindow>
#include <QtTest/QTest>

#include "MockLink.h"
#include "MultiVehicleManager.h"
#include "Vehicle.h"

#include <QtCore/QPointer>

UT_REGISTER_TEST(ToolbarIndicatorUITest, TestLabel::Integration)

// ---------------------------------------------------------------------------
// _exerciseIndicator
// ---------------------------------------------------------------------------

bool ToolbarIndicatorUITest::_exerciseIndicator(QQuickItem *indicatorItem, const QString &indicatorName, bool expectExpand)
{
    if (!indicatorItem || !_window) {
        return false;
    }

    // 1. Click the indicator — verify the drawer opens
    const QPointF indicatorCenter = indicatorItem->mapToScene(
        QPointF(indicatorItem->width() / 2.0, indicatorItem->height() / 2.0));
    QTest::mouseClick(_window, Qt::LeftButton, Qt::NoModifier, indicatorCenter.toPoint());

    if (!findVisibleItem(_rootItem, QStringLiteral("indicatorDrawerLoader"), 2000)) {
        qWarning() << indicatorName << ": drawer did not open after clicking indicator";
        return false;
    }

    QTest::qWait(_pageDelay);

    // 2. Expand — verify the expand button is present when expected, and that
    //    expanded content appears after clicking it
    if (expectExpand) {
        QQuickItem *expandBtn = findVisibleItem(_rootItem, QStringLiteral("indicatorDrawerExpandButton"), 500);
        if (!expandBtn) {
            qWarning() << indicatorName << ": expand button not found but was expected";
            return false;
        }
        const QPointF expandCenter = expandBtn->mapToScene(
            QPointF(expandBtn->width() / 2.0, expandBtn->height() / 2.0));
        QTest::mouseClick(_window, Qt::LeftButton, Qt::NoModifier, expandCenter.toPoint());
        QTest::qWait(_pageDelay);

        if (!findVisibleItem(_rootItem, QStringLiteral("indicatorExpandedLoader"), 2000)) {
            qWarning() << indicatorName << ": expanded content did not appear after clicking expand button";
            return false;
        }
    }

    // 3. Close the drawer with Escape — verify it closes
    QTest::keyClick(_window, Qt::Key_Escape);

    const bool drawerClosed = waitForCondition(
        [&] { return findVisibleItem(_rootItem, QStringLiteral("indicatorDrawerLoader"), 0) == nullptr; },
        2000,
        QStringLiteral("indicatorDrawerLoader hidden"));
    if (!drawerClosed) {
        qWarning() << indicatorName << ": drawer did not close after pressing Escape";
        return false;
    }

    return true;
}

// ---------------------------------------------------------------------------
// _runIndicatorTest
// ---------------------------------------------------------------------------

void ToolbarIndicatorUITest::_runIndicatorTest(
    const std::function<MockLink *()> &factory,
    const QString &vehicleName)
{
    runWithMockLink(factory, [&](QPointer<MockLink> /*mockLink*/, Vehicle * /*vehicle*/) {
    // -------------------------------------------------------------------------
    // Ensure we are on the Fly view (default after vehicle connects)
    // -------------------------------------------------------------------------
    QVERIFY2(findVisibleItem(_rootItem, QStringLiteral("mainView_fly"), 3000),
             qPrintable(QStringLiteral("%1: Fly view not visible").arg(vehicleName)));

    // -------------------------------------------------------------------------
    // Table of indicators to exercise: { objectName, displayName, expectExpand }
    // -------------------------------------------------------------------------
    struct IndicatorSpec {
        const char *objectName;
        const char *displayName;
        bool        expectExpand;
    };
    static const IndicatorSpec kIndicators[] = {
        { "toolbar_mainStatusIndicator",    "MainStatus",   true  },
        { "toolbar_flightModeIndicator",    "FlightMode",   true  },
        { "toolbar_gpsIndicator",           "GPS",          true  },
        { "toolbar_batteryIndicator",       "Battery",      true  },
        { "toolbar_remoteIDIndicator",      "RemoteID",     true  },
        { "toolbar_gimbalIndicator",        "Gimbal",       true  },
        { "toolbar_escIndicator",           "ESC",          false },
        { "toolbar_telemetryRSSIIndicator", "TelemetryRSSI",false },
    };

    for (const IndicatorSpec &spec : kIndicators) {
        const QString objName     = QString::fromLatin1(spec.objectName);
        const QString displayName = vehicleName + QLatin1Char('/') + QLatin1String(spec.displayName);

        QQuickItem *item = findVisibleItem(_rootItem, objName, 2000);
        QVERIFY2(item,
                 qPrintable(QStringLiteral("%1: %2 not found in toolbar").arg(vehicleName, objName)));
        QVERIFY2(_exerciseIndicator(item, displayName, spec.expectExpand),
                 qPrintable(QStringLiteral("%1: exercise failed").arg(displayName)));
    }
    });
}

// ---------------------------------------------------------------------------
// Per-vehicle-type test slots
// ---------------------------------------------------------------------------

void ToolbarIndicatorUITest::_testPX4Indicators()
{
    _runIndicatorTest(
        [] { return MockLink::startPX4MockLink(MockConfiguration::OptionEnableGimbal); },
        QStringLiteral("PX4"));
}

void ToolbarIndicatorUITest::_testAPMCopterIndicators()
{
    if (!apmFirmwareSupported()) {
        QSKIP("ArduPilot support not registered in this build");
    }

    _runIndicatorTest(
        [] { return MockLink::startAPMArduCopterMockLink(MockConfiguration::OptionEnableGimbal); },
        QStringLiteral("APMCopter"));
}

// ---------------------------------------------------------------------------
// Reboot
// ---------------------------------------------------------------------------

/// Opens the vehicle status drawer and returns the reboot button, scrolled into
/// the drawer's viewport. Records a test failure and returns nullptr if either
/// the drawer or the button never appears.
QQuickItem *ToolbarIndicatorUITest::_openDrawerAndFindReboot()
{
    QQuickItem *const indicator = findVisibleItem(_rootItem, QStringLiteral("toolbar_mainStatusIndicator"), 3000);
    if (!indicator) {
        QTest::qFail("the main status indicator never appeared in the toolbar", __FILE__, __LINE__);
        return nullptr;
    }

    const QPointF centre = indicator->mapToScene(QPointF(indicator->width() / 2.0, indicator->height() / 2.0));
    QTest::mouseClick(_window, Qt::LeftButton, Qt::NoModifier, centre.toPoint());

    if (!findVisibleItem(_rootItem, QStringLiteral("indicatorDrawerLoader"), 3000)) {
        QTest::qFail("the status drawer did not open", __FILE__, __LINE__);
        return nullptr;
    }

    QQuickItem *const reboot = findVisibleItem(_rootItem, QStringLiteral("mainStatus_rebootButton"), 3000);
    if (!reboot) {
        QTest::qFail("the drawer carried no reboot button", __FILE__, __LINE__);
        return nullptr;
    }
    return reboot;
}

/// Rebooting an autopilot that is armed drops it out of the sky, so the control is
/// offered only on the ground. ArduPilot refuses the command anyway, but a button
/// that can only fail teaches the operator nothing about why.
void ToolbarIndicatorUITest::_rebootIsOfferedOnlyWithTheVehicleOnTheGround_test()
{
    runWithMockLink(
        [] { return MockLink::startPX4MockLink(); },
        [this](const QPointer<MockLink> & /*mockLink*/, Vehicle *vehicle) {
            QQuickItem *const reboot = _openDrawerAndFindReboot();
            QVERIFY(reboot);
            QVERIFY2(reboot->isEnabled(), "the reboot button was out with the vehicle disarmed");

            vehicle->setArmed(true, false);
            QTRY_VERIFY_WITH_TIMEOUT(vehicle->armed(), TestTimeout::longMs());
            QTRY_VERIFY_WITH_TIMEOUT(!reboot->isEnabled(), TestTimeout::shortMs());

            vehicle->setArmed(false, false);
            QTRY_VERIFY_WITH_TIMEOUT(!vehicle->armed(), TestTimeout::longMs());
            QTRY_VERIFY_WITH_TIMEOUT(reboot->isEnabled(), TestTimeout::shortMs());

            QTest::keyClick(_window, Qt::Key_Escape);
        });
}

/// The whole point of the control: held down, the vehicle actually restarts. QGC
/// closes the vehicle out when the autopilot accepts, which is what makes the
/// active vehicle going away the honest thing to wait for.
void ToolbarIndicatorUITest::_holdingRebootRestartsTheVehicle_test()
{
    runWithMockLink(
        [] { return MockLink::startPX4MockLink(); },
        [this](const QPointer<MockLink> & /*mockLink*/, Vehicle * /*vehicle*/) {
            QQuickItem *const reboot = _openDrawerAndFindReboot();
            QVERIFY(reboot);

            // Held, not clicked. A tap on a QGCDelayButton does nothing but show its
            // "Hold to Confirm" hint, which is the guard being tested here as much as
            // the reboot is.
            const QPointF centre = reboot->mapToScene(QPointF(reboot->width() / 2.0, reboot->height() / 2.0));
            QVERIFY2(_window->geometry().contains(centre.toPoint()),
                     "the reboot button sits outside the window, so it cannot be pressed");

            QTest::mouseClick(_window, Qt::LeftButton, Qt::NoModifier, centre.toPoint());
            QVERIFY2(MultiVehicleManager::instance()->activeVehicle(),
                     "a tap rebooted the vehicle, so the hold-to-confirm guard is not doing anything");

            QTest::mousePress(_window, Qt::LeftButton, Qt::NoModifier, centre.toPoint());
            const bool rebooted = waitForCondition(
                [] { return MultiVehicleManager::instance()->activeVehicle() == nullptr; },
                TestTimeout::longMs(),
                QStringLiteral("the vehicle closed out after the reboot was accepted"));
            QTest::mouseRelease(_window, Qt::LeftButton, Qt::NoModifier, centre.toPoint());
            QVERIFY2(rebooted, "holding the button never rebooted the vehicle");
        });
}
