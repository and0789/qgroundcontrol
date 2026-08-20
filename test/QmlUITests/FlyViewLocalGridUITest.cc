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

/// The airspeed panel is on the grid only while a sensor is talking, and goes when one stops.
///
/// The second half is the part that needed writing. FactGroup::telemetryAvailable is never set back
/// to false, so a panel gated on it would appear at the first message and then stand there for the
/// rest of the flight showing whatever a pitot last reported before it was unplugged -- which reads
/// exactly like a sensor measuring still air.
void FlyViewLocalGridUITest::_theAirspeedPanelFollowsTheSensor_test()
{
    SettingsManager::instance()->flyViewSettings()->showLocalGridView()->setRawValue(true);

    runWithMockLink(
        [] { return MockLink::startAPMArduCopterMockLink(MockConfiguration::OptionEnableAirspeed); },
        [this](const QPointer<MockLink> &mockLink, Vehicle * /*vehicle*/) {
            QQuickItem *const readout = findVisibleItem(_rootItem, QStringLiteral("localGrid_readout"), 10000);
            QVERIFY2(readout, "the readout never appeared on the grid");

            QQuickItem *const panel = findVisibleItem(_rootItem, QStringLiteral("localGrid_airspeed"),
                                                      TestTimeout::longMs());
            QVERIFY2(panel, "the airspeed panel never appeared for a vehicle reporting a sensor");

            // A reading rather than dashes. The panel appearing proves a message arrived; it says
            // nothing about whether anything was decoded out of it.
            QQuickItem *const value = findVisibleItem(_rootItem, QStringLiteral("localGrid_airspeedValue"),
                                                      TestTimeout::longMs());
            QVERIFY2(value, "the panel appeared without its airspeed reading");
            QVERIFY_TRUE_WAIT(value->property("text").toString().contains(QStringLiteral("m/s")),
                              TestTimeout::longMs());

            // The sensor stops. Not a disconnect -- the vehicle stays up and every other panel on the
            // grid carries on, which is what an unplugged pitot actually looks like.
            mockLink->setAirspeedEnabled(false);

            // Waited for rather than checked once: the group gives the sensor several seconds of
            // silence before calling it gone, so that it survives a slow RAW_SENSORS stream.
            QVERIFY_TRUE_WAIT(findVisibleItem(_rootItem, QStringLiteral("localGrid_airspeed"), 0) == nullptr,
                              TestTimeout::longMs());

            // The panel above it is still there, so what just happened was the sensor going and not
            // the grid falling over.
            QVERIFY2(findVisibleItem(_rootItem, QStringLiteral("localGrid_readout"), 0),
                     "the position readout went with the airspeed panel");
        });
}

/// The tool strip is two strips in one place, and the whole safety of that rests on only one of them
/// being on screen at a time. Boots the real strip and switches modes on it, because none of what
/// makes this work is visible from the grid's own properties: the flying buttons stand down through
/// bindings inside their own files, and the plan buttons show what is armed through Binding elements
/// declared a file away from the actions they drive.
///
/// The armed tool is the part with a history. The strip writes checked back into its action, which
/// destroys an ordinary binding on it, and the strip also unchecks every other button when one goes
/// down -- so a plan tool would either stick lit after the grid disarmed it, or switch the mode off
/// underneath itself the moment it was armed.
void FlyViewLocalGridUITest::_planModeSwapsTheStripWithoutLosingTheMode_test()
{
    SettingsManager::instance()->flyViewSettings()->showLocalGridView()->setRawValue(true);

    runWithMockLink(
        [] { return MockLink::startAPMArduCopterMockLink(); },
        [this](const QPointer<MockLink> &/*mockLink*/, Vehicle * /*vehicle*/) {
            QQuickItem *const gridView = findVisibleItem(_rootItem, QStringLiteral("localGridView"), 10000);
            QVERIFY2(gridView, "the local grid never became visible with the setting on");

            QVERIFY2(findVisibleItem(_rootItem, QStringLiteral("flyToolStrip_nonGpsStatusButton"), 10000),
                     "the flying strip never came up");
            QVERIFY2(findVisibleItem(_rootItem, QStringLiteral("flyToolStrip_planTakeoffButton"), 0) == nullptr,
                     "a plan button was on the strip before plan mode was ever entered");

            QVERIFY2(clickButton(QStringLiteral("flyToolStrip_planButton")), "the Plan button could not be clicked");
            QVERIFY_TRUE_WAIT(gridView->property("planEditMode").toBool(), TestTimeout::longMs());

            // The swap, both ways round: the plan's inserts are on the strip and the buttons that
            // command the aircraft are not
            QVERIFY2(
                findVisibleItem(_rootItem, QStringLiteral("flyToolStrip_planTakeoffButton"), TestTimeout::longMs()),
                "the plan inserts never reached the strip");
            QVERIFY_TRUE_WAIT(
                findVisibleItem(_rootItem, QStringLiteral("flyToolStrip_nonGpsStatusButton"), 0) == nullptr,
                TestTimeout::longMs());
            QVERIFY2(verifyChecked(QStringLiteral("flyToolStrip_planButton"), true, QStringLiteral("in plan mode")),
                     "the Plan button does not show the mode it just entered");

            // Arming a tool must not put the mode out. Set on the grid rather than clicked, because
            // what is under test is the strip following the grid -- which is the direction that
            // breaks when the plan is disarmed by something other than a click.
            gridView->setProperty("armedTool", QStringLiteral("waypoint"));
            QVERIFY_TRUE_WAIT(findVisibleItem(_rootItem, QStringLiteral("flyToolStrip_planWaypointButton"), 0)
                                  ->property("checked")
                                  .toBool(),
                              TestTimeout::longMs());
            QVERIFY2(verifyChecked(QStringLiteral("flyToolStrip_planButton"), true,
                                   QStringLiteral("with a plan tool armed")),
                     "arming a plan tool switched the mode off underneath it");

            // Leaving the mode puts the tool down, and the button has to let go with it
            QVERIFY2(clickButton(QStringLiteral("flyToolStrip_planButton")),
                     "the Plan button could not be clicked again");
            QVERIFY_TRUE_WAIT(!gridView->property("planEditMode").toBool(), TestTimeout::longMs());
            QCOMPARE(gridView->property("armedTool").toString(), QString());
            QVERIFY_TRUE_WAIT(
                findVisibleItem(_rootItem, QStringLiteral("flyToolStrip_nonGpsStatusButton"), 0) != nullptr,
                TestTimeout::longMs());
        });
}
