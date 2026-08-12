#include "FlyViewLocalGridUITest.h"

#include <QtCore/QSet>
#include <QtGui/QImage>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickWindow>
#include <QtTest/QTest>

#include "FlyViewSettings.h"
#include "MockLink.h"
#include "SettingsManager.h"

UT_REGISTER_TEST(FlyViewLocalGridUITest, TestLabel::Integration)

namespace {

/// @return the number of distinct colours in the image, giving up once @a limit is reached.
/// A view that never painted comes back as one flat colour.
int countDistinctColours(const QImage &image, int limit)
{
    QSet<QRgb> colours;
    for (int y = 0; (y < image.height()) && (colours.size() < limit); y++) {
        for (int x = 0; (x < image.width()) && (colours.size() < limit); x++) {
            colours.insert(image.pixel(x, y));
        }
    }
    return static_cast<int>(colours.size());
}

} // namespace

/// The grid is flown on ArduPilot, and the vehicle this boots is an ArduCopter. A build with no
/// ArduPilot plugin registered has no vehicle to connect, which is a missing build option rather than
/// a broken grid.
void FlyViewLocalGridUITest::init()
{
    if (!apmFirmwareSupported()) {
        QSKIP("ArduPilot support not registered in this build");
    }
    QmlUITestBase::init();
}

void FlyViewLocalGridUITest::cleanup()
{
    // The setting is persisted, so leaving it on would put every later test's fly view on the grid
    SettingsManager::instance()->flyViewSettings()->showLocalGridView()->setRawValue(false);
    QmlUITestBase::cleanup();
}

void FlyViewLocalGridUITest::_gridReplacesTheMapAndPaints_test()
{
    // Set before the UI boots, so the fly view comes up on the grid rather than switching to it
    SettingsManager::instance()->flyViewSettings()->showLocalGridView()->setRawValue(true);

    runWithMockLink(
        [] { return MockLink::startAPMArduCopterMockLink(); },
        [this](const QPointer<MockLink> & /*mockLink*/, Vehicle * /*vehicle*/) {
            QQuickItem *const gridView = findVisibleItem(_rootItem, QStringLiteral("localGridView"), 10000);
            QVERIFY2(gridView, "the local grid never became visible with the setting on");
            QVERIFY2((gridView->width() > 0) && (gridView->height() > 0), "the grid was given no room to draw in");

            // Let the canvases paint and a few telemetry frames land
            QTRY_VERIFY_WITH_TIMEOUT(gridView->property("positionValid").toBool(), TestTimeout::longMs());

            const QImage frame = _window->grabWindow();
            QVERIFY2(!frame.isNull(), "the window produced no frame to check");

            // A band in the upper middle: clear of the toolbar, the tool strip, the compass rose, the
            // readout and scale bar, and the vehicle marker sitting dead centre while the view is
            // following it. Only grid lines and their labels fall here, so a flat colour means the
            // grid did not draw. Sampling the centre instead would pass on the vehicle marker alone.
            const QRect gridOnly(frame.width() * 2 / 5, frame.height() / 5,
                                 frame.width() / 5, frame.height() / 10);
            QVERIFY(gridOnly.isValid());

            const int colours = countDistinctColours(frame.copy(gridOnly), 16);
            QVERIFY2(colours >= 3,
                     qPrintable(QStringLiteral("the grid area rendered %1 distinct colours, which is a blank field")
                                    .arg(colours)));
        });
}

/// A warning is a sentence where the rest of the panel is a column of numbers, and a layout takes its
/// width from the longest line a child would draw unwrapped. One arriving used to take the readout --
/// and the mission list that follows its width -- across most of the view, with the six numbers
/// spread out over the gap.
void FlyViewLocalGridUITest::_aWarningWrapsRatherThanWideningTheReadout_test()
{
    SettingsManager::instance()->flyViewSettings()->showLocalGridView()->setRawValue(true);

    runWithMockLink(
        [] { return MockLink::startAPMArduCopterMockLink(); },
        [this](const QPointer<MockLink> & /*mockLink*/, Vehicle * /*vehicle*/) {
            QQuickItem *const readout = findVisibleItem(_rootItem, QStringLiteral("localGrid_readout"), 10000);
            QVERIFY2(readout, "the readout never appeared on the grid");

            // The mock vehicle's reported position moves about while it sits disarmed, which is the
            // drift the grid warns about. That warning is the longest sentence this panel carries, so
            // it is the one that stretched it.
            QQuickItem *const drift = findVisibleItem(_rootItem, QStringLiteral("localGrid_driftWarning"),
                                                      TestTimeout::longMs());
            QVERIFY2(drift, "the drift warning never appeared, so there was no long sentence to measure");

            // Waited for rather than read straight off: a label handed its text reports the width it
            // would draw on one line until the column it sits in has run once more, so a check made
            // in the same turn as the warning appearing measures the state being corrected.
            QTRY_VERIFY_WITH_TIMEOUT(drift->property("lineCount").toInt() > 1, TestTimeout::shortMs());

            // Loose on purpose. What is under test is that a sentence wraps instead of setting the
            // panel's width, not any particular column width -- unwrapped this one took 92% of the
            // window.
            QVERIFY2(readout->width() < (_window->width() * 0.4),
                     qPrintable(QStringLiteral("one warning made the readout %1 px of a %2 px window")
                                    .arg(readout->width()).arg(_window->width())));
            QVERIFY2(drift->width() <= readout->width(),
                     qPrintable(QStringLiteral("the warning is %1 px wide inside a %2 px panel")
                                    .arg(drift->width()).arg(readout->width())));
        });
}

/// An operator who cannot arm needs the autopilot's own reason, and the fly view's banner for it
/// stands in the middle of the screen for thirty-five seconds and then takes the reason away again.
/// The grid says it on the panel the operator is already reading, and keeps saying it.
void FlyViewLocalGridUITest::_theReasonTheVehicleWillNotArmIsOnThePanel_test()
{
    SettingsManager::instance()->flyViewSettings()->showLocalGridView()->setRawValue(true);

    runWithMockLink(
        [] { return MockLink::startAPMArduCopterMockLink(); },
        [this](const QPointer<MockLink> &mockLink, Vehicle * /*vehicle*/) {
            QQuickItem *const readout = findVisibleItem(_rootItem, QStringLiteral("localGrid_readout"), 10000);
            QVERIFY2(readout, "the readout never appeared on the grid");

            // Silent until there is a refusal to report. The mock vehicle passes its own pre-arm
            // check, so a line here before anything is sent would be a line that is always there.
            QVERIFY(verifyVisibility(QStringLiteral("localGrid_armingWarning"), false,
                                     QStringLiteral("before any refusal")));

            // The real path a reason travels: a status text off the link, which Vehicle picks out of
            // the message stream. "Arm: " rather than "PreArm: " because that is how ArduPilot names
            // the failing check during an arming attempt, which is the moment the operator asked.
            mockLink->sendStatusTextMessage(MAV_SEVERITY_CRITICAL,
                                            QStringLiteral("Arm: Need Position Estimate"));

            QQuickItem *const arming = findVisibleItem(_rootItem, QStringLiteral("localGrid_armingWarning"),
                                                       TestTimeout::longMs());
            QVERIFY2(arming, "the panel never said the vehicle would not arm");
            QVERIFY2(arming->property("text").toString().contains(QStringLiteral("Need Position Estimate")),
                     qPrintable(QStringLiteral("the panel said \"%1\" rather than the autopilot's reason")
                                    .arg(arming->property("text").toString())));
        });
}

/// A vehicle can refuse to arm and never say why: ArduPilot volunteers a reason once every thirty
/// seconds and ARMING_OPTIONS bit 0 stops even that. Vehicle asks rather than waits -- on its own
/// timer, for as long as the refusal stands unexplained, and whether or not this panel or any other
/// is on screen. What the grid adds is stating the refusal while that question is outstanding,
/// rather than looking exactly like a grid nobody has tried to arm from.
void FlyViewLocalGridUITest::_aSilentRefusalMakesTheVehicleAskWhy_test()
{
    SettingsManager::instance()->flyViewSettings()->showLocalGridView()->setRawValue(true);

    runWithMockLink(
        [] { return MockLink::startAPMArduCopterMockLink(); },
        [this](const QPointer<MockLink> &mockLink, Vehicle * /*vehicle*/) {
            QQuickItem *const readout = findVisibleItem(_rootItem, QStringLiteral("localGrid_readout"), 10000);
            QVERIFY2(readout, "the readout never appeared on the grid");

            mockLink->clearReceivedMavCommandCounts();

            // The state the operator is stuck in: the vehicle's own pre-arm check bit says it will
            // not arm, and no status text has arrived to say what is failing.
            mockLink->setPrearmCheckFailing(true);

            QQuickItem *const arming = findVisibleItem(_rootItem, QStringLiteral("localGrid_armingWarning"),
                                                       TestTimeout::longMs());
            QVERIFY2(arming, "the panel stayed silent about a vehicle that was refusing to arm");

            // Sent by the vehicle off the back of the refusal, not by the panel drawing the line
            // above -- the grid being open is what makes the answer visible here, not what asks.
            QVERIFY_TRUE_WAIT(mockLink->receivedMavCommandCount(MAV_CMD_RUN_PREARM_CHECKS) >= 1,
                              TestTimeout::longMs());
        });
}
