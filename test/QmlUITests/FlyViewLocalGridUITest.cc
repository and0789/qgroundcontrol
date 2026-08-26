#include "FlyViewLocalGridUITest.h"

#include <QtCore/QSet>
#include <QtGui/QImage>
#include <QtGui/QPointingDevice>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickWindow>
#include <QtTest/QTest>

#include "FlyViewSettings.h"
#include "LocalGridTestSupport.h"
#include "MockLink.h"
#include "SettingsManager.h"
#include "Vehicle.h"

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

/// A warning is a sentence, and a layout takes its width from the longest line a child would draw
/// unwrapped. Inside the readout one arriving used to take that panel -- and the mission list that
/// follows its width -- across most of the view; then, once the column was held to its share, the
/// sentence was cut off by the ceiling instead. Sentences live in the band across the top now, which
/// is bounded by the two columns rather than squeezed between them.
///
/// What is checked is that the band still keeps to itself: it wraps rather than setting its own width
/// from the sentence, it stays clear of the readout column, and it shows the sentence whole.
void FlyViewLocalGridUITest::_aWarningWrapsRatherThanWideningTheGrid_test()
{
    SettingsManager::instance()->flyViewSettings()->showLocalGridView()->setRawValue(true);

    runWithMockLink(
        [] { return MockLink::startAPMArduCopterMockLink(); },
        [this](const QPointer<MockLink> & /*mockLink*/, Vehicle * /*vehicle*/) {
            QQuickItem *const readout = findVisibleItem(_rootItem, QStringLiteral("localGrid_readout"), 10000);
            QVERIFY2(readout, "the readout never appeared on the grid");

            // The mock vehicle's reported position moves about while it sits disarmed, which is the
            // drift the grid warns about. That warning is the longest sentence this view carries, so
            // it is the one that stretched things.
            QQuickItem *const drift = findVisibleItem(_rootItem, QStringLiteral("localGrid_driftWarning"),
                                                      TestTimeout::longMs());
            QVERIFY2(drift, "the drift warning never appeared, so there was no long sentence to measure");

            QQuickItem *const banner = findVisibleItem(_rootItem, QStringLiteral("localGrid_warnings"), 1000);
            QVERIFY2(banner, "the warning band never appeared with a warning in it");

            // Waited for rather than read straight off: a label handed its text reports the width it
            // would draw on one line until the column it sits in has run once more, so a check made
            // in the same turn as the warning appearing measures the state being corrected.
            QTRY_VERIFY_WITH_TIMEOUT(drift->property("lineCount").toInt() > 1, TestTimeout::shortMs());

            // Loose on purpose. What is under test is that a sentence wraps instead of setting the
            // band's width, not any particular width -- unwrapped this one took 92% of the window.
            QVERIFY2(banner->width() < (_window->width() * 0.7),
                     qPrintable(QStringLiteral("one warning made the band %1 px of a %2 px window")
                                    .arg(banner->width()).arg(_window->width())));
            QVERIFY2(drift->width() <= banner->width(),
                     qPrintable(QStringLiteral("the warning is %1 px wide inside a %2 px band")
                                    .arg(drift->width()).arg(banner->width())));

            // Whole, not cut off. contentHeight is what the text needs; height is what it was given,
            // and a sentence clipped by its container is the failure this move was made for.
            QVERIFY2(drift->property("contentHeight").toReal() <= drift->height() + 1.0,
                     qPrintable(QStringLiteral("the warning was clipped: %1 px of text in %2 px")
                                    .arg(drift->property("contentHeight").toReal()).arg(drift->height())));

            // And clear of the column it came out of
            const QRectF bannerRect(banner->mapToScene(QPointF(0, 0)), QSizeF(banner->width(), banner->height()));
            const QRectF readoutRect(readout->mapToScene(QPointF(0, 0)), QSizeF(readout->width(), readout->height()));
            QVERIFY2(bannerRect.intersected(readoutRect).isEmpty(),
                     "the warning band ran into the readout column");
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

/// Shaping the pattern lives behind a drop panel, which is the one place a plan control can go and
/// leave no trace of having gone missing. Turning the pattern and pinning the nose are reached from
/// nowhere else -- rotatePlan and insertConditionYaw have no other caller in the app -- and their own
/// tests drive those functions directly, so both stayed green through a spell where nothing on screen
/// could reach either.
///
/// Opening the panel must also leave the mode alone. A drop panel checks its button, and the strip
/// unchecks every other button when one goes down.
void FlyViewLocalGridUITest::_shapingThePatternIsReachableInPlanMode_test()
{
    SettingsManager::instance()->flyViewSettings()->showLocalGridView()->setRawValue(true);

    runWithMockLink(
        [] { return MockLink::startAPMArduCopterMockLink(); },
        [this](const QPointer<MockLink> & /*mockLink*/, Vehicle * /*vehicle*/) {
            QQuickItem *const gridView = findVisibleItem(_rootItem, QStringLiteral("localGridView"), 10000);
            QVERIFY2(gridView, "the local grid never became visible with the setting on");

            QVERIFY2(findVisibleItem(_rootItem, QStringLiteral("flyToolStrip_planShapeButton"), 0) == nullptr,
                     "the Shape button was on the strip outside plan mode");

            QVERIFY2(clickButton(QStringLiteral("flyToolStrip_planButton")), "the Plan button could not be clicked");
            QVERIFY_TRUE_WAIT(gridView->property("planEditMode").toBool(), TestTimeout::longMs());

            QVERIFY2(clickButton(QStringLiteral("flyToolStrip_planShapeButton")),
                     "the Shape button never reached the strip in plan mode");

            // The three controls that have no other way in
            for (const QString &control : {QStringLiteral("localGrid_planRotateButton"),
                                           QStringLiteral("localGrid_planMoveButton"),
                                           QStringLiteral("localGrid_planYawButton")}) {
                QVERIFY2(findVisibleItem(_rootItem, control, TestTimeout::longMs()),
                         qPrintable(QStringLiteral("%1 is reachable from nowhere in the app").arg(control)));
            }

            // Opening a panel is not picking up a tool, so the mode underneath it stands
            QVERIFY2(gridView->property("planEditMode").toBool(), "opening the Shape panel switched plan mode off");
            QVERIFY2(verifyChecked(QStringLiteral("flyToolStrip_planButton"), true,
                                   QStringLiteral("with the Shape panel open")),
                     "opening the Shape panel unchecked the Plan button");
        });
}

/// A finger and a mouse are the same gesture to the operator, so the grid has to treat them that
/// way: tap to open the panel that reads out a point, drag to pan.
///
/// Driven with both devices in one test, and asserted against each other rather than against fixed
/// numbers. What broke on a touch screen was never the gesture logic -- it is the same code either
/// way -- but whether the touch reached that code at all, and a mouse pass standing beside the touch
/// pass is what tells those two failures apart.
void FlyViewLocalGridUITest::_aFingerWorksTheGridTheSameWayAMouseDoes_test()
{
    SettingsManager::instance()->flyViewSettings()->showLocalGridView()->setRawValue(true);

    runWithMockLink(
        [] { return MockLink::startAPMArduCopterMockLink(); },
        [this](const QPointer<MockLink>& /*mockLink*/, Vehicle* /*vehicle*/) {
            QQuickItem* const gridView = findVisibleItem(_rootItem, QStringLiteral("localGridView"), 10000);
            QVERIFY2(gridView, "the local grid never became visible with the setting on");

            QObject* const transform = gridView->property("gridTransform").value<QObject*>();
            QVERIFY2(transform, "the grid has no transform to pan");

            // Left to itself the grid recentres on the vehicle, which would move the centre this test
            // measures for a reason that has nothing to do with the gesture under test
            gridView->setProperty("followVehicle", false);

            // Clear of the tool strip down the left edge, the readout column on the right, and the
            // origin marker at the centre -- a press landing on any of them is a press the grid
            // itself never sees
            const QPoint target =
                gridView->mapToScene(QPointF(gridView->width() * 0.3, gridView->height() * 0.35)).toPoint();

            const auto panDistance = [transform]() {
                return qAbs(transform->property("centreEast").toReal()) +
                       qAbs(transform->property("centreNorth").toReal());
            };

            /// Runs the drag as a press, ten steps, and a release, so the view sees the same stream of
            /// moves a hand produces rather than one jump from start to finish
            constexpr int kDragSteps = 10;
            constexpr int kDragPixels = 150;

            // --- The mouse pass, which is the behaviour being matched ---

            QTest::mouseClick(_window, Qt::LeftButton, Qt::NoModifier, target);
            QVERIFY2(verifyVisibility(QStringLiteral("localGrid_clickPanel"), true, QStringLiteral("after a click")),
                     "a mouse click on bare grid did not open the click panel");

            QTest::mousePress(_window, Qt::LeftButton, Qt::NoModifier, target);
            for (int step = 1; step <= kDragSteps; step++) {
                QTest::mouseMove(_window, target + QPoint((kDragPixels * step) / kDragSteps, 0));
            }
            QTest::mouseRelease(_window, Qt::LeftButton, Qt::NoModifier, target + QPoint(kDragPixels, 0));

            QTRY_VERIFY_WITH_TIMEOUT(panDistance() > 0.0, TestTimeout::longMs());
            const qreal mousePan = panDistance();

            // --- The same two gestures from a finger ---

            transform->setProperty("centreEast", 0.0);
            transform->setProperty("centreNorth", 0.0);
            gridView->setProperty("followVehicle", false);

            // The drag above already put the panel the click opened away -- a press on the grid closes
            // it. Asserted rather than assumed, because a tap tested against a panel that was still on
            // screen would pass without the tap having done anything at all.
            QVERIFY2(verifyVisibility(QStringLiteral("localGrid_clickPanel"), false, QStringLiteral("after a drag")),
                     "the click panel stayed open through a drag, leaving the tap below with nothing to prove");

            QPointingDevice* const finger = QTest::createTouchDevice();

            {
                QTest::QTouchEventSequence drag = QTest::touchEvent(_window, finger);
                drag.press(0, target).commit();
                for (int step = 1; step <= kDragSteps; step++) {
                    drag.move(0, target + QPoint((kDragPixels * step) / kDragSteps, 0)).commit();
                }
                drag.release(0, target + QPoint(kDragPixels, 0)).commit();
            }

            QTRY_VERIFY_WITH_TIMEOUT(panDistance() > 0.0, TestTimeout::longMs());
            // The same drag over the same pixels, so the ground covered has to match what the mouse
            // covered. A finger that pans a fraction of the distance is as broken as one that does
            // not pan at all.
            QVERIFY2(qAbs(panDistance() - mousePan) < (mousePan * 0.05),
                     qPrintable(QStringLiteral("a finger panned %1 m where the mouse panned %2 m")
                                    .arg(panDistance())
                                    .arg(mousePan)));

            {
                QTest::QTouchEventSequence tap = QTest::touchEvent(_window, finger);
                tap.press(0, target).commit();
                tap.release(0, target).commit();
            }
            QVERIFY2(verifyVisibility(QStringLiteral("localGrid_clickPanel"), true, QStringLiteral("after a tap")),
                     "a tap on bare grid did not open the click panel, though a mouse click does");

            // --- Zoom, which is the one gesture the two devices do differently ---
            //
            // A wheel notch and a pinch spread both mean zoom in, and both have to arrive: the pinch
            // is what a single finger was competing with for the same touch points, so a fix that
            // gave the finger its tap back by taking the pinch away would trade one half of the
            // gesture set for the other.

            const auto zoom = [transform]() { return transform->property("metresPerPixel").toReal(); };

            const qreal zoomBeforeWheel = zoom();
            QTest::wheelEvent(_window, QPointF(target), QPoint(0, 120));
            QTRY_VERIFY_WITH_TIMEOUT(zoom() < zoomBeforeWheel, TestTimeout::longMs());

            const qreal zoomBeforePinch = zoom();
            {
                constexpr int kSpread = 50;
                constexpr int kSteps = 10;
                QTest::QTouchEventSequence pinch = QTest::touchEvent(_window, finger);
                pinch.press(0, target + QPoint(-kSpread, 0)).press(1, target + QPoint(kSpread, 0)).commit();
                for (int step = 1; step <= kSteps; step++) {
                    pinch.move(0, target + QPoint(-kSpread - (step * 10), 0))
                        .move(1, target + QPoint(kSpread + (step * 10), 0))
                        .commit();
                }
                pinch.release(0, target + QPoint(-kSpread - (kSteps * 10), 0))
                    .release(1, target + QPoint(kSpread + (kSteps * 10), 0))
                    .commit();
            }
            QTRY_VERIFY_WITH_TIMEOUT(zoom() < zoomBeforePinch, TestTimeout::longMs());
        });
}

/// Picking a marker up and putting it somewhere else is the gesture the grid exists for, and the one
/// furthest from a mouse: it is a press, a travel and a release on a target the size of a fingertip.
///
/// It runs through the marker's own MouseArea rather than the grid's, so it fails and recovers
/// separately from the tap and the pan -- a view that pans under a finger while every waypoint on it
/// is nailed down is still a view a pattern cannot be built on.
void FlyViewLocalGridUITest::_aFingerCanPickUpAWaypointAndMoveIt_test()
{
    SettingsManager::instance()->flyViewSettings()->showLocalGridView()->setRawValue(true);

    runWithMockLink([] { return MockLink::startAPMArduCopterMockLink(); },
                    [this](const QPointer<MockLink>& mockLink, Vehicle* vehicle) {
                        QVERIFY(vehicle);
                        // Nothing can be placed on a grid with no origin -- the frame the point would be measured
                        // in does not exist yet
                        QVERIFY2(LocalGridTestSupport::giveTheVehicleAnOrigin(vehicle, mockLink),
                                 "the vehicle never took an origin");

                        QQuickItem* const gridView = findVisibleItem(_rootItem, QStringLiteral("localGridView"), 10000);
                        QVERIFY2(gridView, "the local grid never became visible with the setting on");

                        QObject* const transform = gridView->property("gridTransform").value<QObject*>();
                        QVERIFY2(transform, "the grid has no transform to place a waypoint through");

                        gridView->setProperty("followVehicle", false);

                        // Placed through the view's own function rather than by tapping, so a regression in the
                        // tap cannot take this test down with it -- what is under test here is the marker
                        const QPointF placeAt(gridView->width() * 0.35, gridView->height() * 0.4);
                        QVariant added;
                        QVERIFY(QMetaObject::invokeMethod(gridView, "addWaypointAtPixel", Q_RETURN_ARG(QVariant, added),
                                                          Q_ARG(QVariant, QVariant(placeAt.x())),
                                                          Q_ARG(QVariant, QVariant(placeAt.y()))));
                        QVERIFY2(added.toBool(), "the grid refused the waypoint this test is about to drag");

                        /// Where the one waypoint on the grid says it is, in metres east of the origin
                        const auto waypointEast = [gridView]() {
                            const QVariantList points = gridView->property("missionPoints").toList();
                            for (const QVariant& point : points) {
                                const QVariantMap fields = point.toMap();
                                if (!fields.value(QStringLiteral("isPinned")).toBool()) {
                                    return fields.value(QStringLiteral("east")).toReal();
                                }
                            }
                            return qQNaN();
                        };

                        const qreal eastBefore = waypointEast();
                        QVERIFY2(!qIsNaN(eastBefore), "the waypoint that was just added is not on the grid");

                        // Nothing has panned or zoomed since it was placed, and the marker is bound to the same
                        // transform the placement went through, so it is still under the pixel it was put at
                        const QPoint marker = gridView->mapToScene(placeAt).toPoint();

                        constexpr int kDragSteps = 10;
                        constexpr int kDragPixels = 90;

                        QPointingDevice* const finger = QTest::createTouchDevice();
                        {
                            QTest::QTouchEventSequence drag = QTest::touchEvent(_window, finger);
                            drag.press(0, marker).commit();
                            for (int step = 1; step <= kDragSteps; step++) {
                                drag.move(0, marker + QPoint((kDragPixels * step) / kDragSteps, 0)).commit();
                            }
                            drag.release(0, marker + QPoint(kDragPixels, 0)).commit();
                        }

                        QTRY_VERIFY_WITH_TIMEOUT(waypointEast() > eastBefore, TestTimeout::longMs());

                        // Dragging east must move the waypoint, not the view under it: a marker that stays put
                        // while the grid pans is the same gesture producing the opposite result
                        QVERIFY2(qFuzzyIsNull(transform->property("centreEast").toReal()),
                                 "dragging a waypoint panned the grid instead of moving the point");
                    });
}

/// One drag is one thing to take back.
///
/// The marker reports where it is on every frame of a drag, and the view recorded an undo entry for
/// each of those reports -- so each overwrote the one before it and undo took the waypoint back to
/// where it stood one frame earlier. On a drag that crossed the grid the control offered "Undo move"
/// and moved the point by a pixel, which reads as a control that does not work.
void FlyViewLocalGridUITest::_undoingAMoveTakesTheWaypointBackToWhereItWasPickedUp_test()
{
    SettingsManager::instance()->flyViewSettings()->showLocalGridView()->setRawValue(true);

    runWithMockLink([] { return MockLink::startAPMArduCopterMockLink(); },
                    [this](const QPointer<MockLink>& mockLink, Vehicle* vehicle) {
                        QVERIFY(vehicle);
                        QVERIFY2(LocalGridTestSupport::giveTheVehicleAnOrigin(vehicle, mockLink),
                                 "the vehicle never took an origin");

                        QQuickItem* const gridView = findVisibleItem(_rootItem, QStringLiteral("localGridView"), 10000);
                        QVERIFY2(gridView, "the local grid never became visible with the setting on");

                        gridView->setProperty("followVehicle", false);

                        const QPointF placeAt(gridView->width() * 0.35, gridView->height() * 0.4);
                        QVariant added;
                        QVERIFY(QMetaObject::invokeMethod(gridView, "addWaypointAtPixel", Q_RETURN_ARG(QVariant, added),
                                                          Q_ARG(QVariant, QVariant(placeAt.x())),
                                                          Q_ARG(QVariant, QVariant(placeAt.y()))));
                        QVERIFY2(added.toBool(), "the grid refused the waypoint this test is about to drag");

                        /// Where the one waypoint on the grid says it is, in metres east of the origin
                        const auto waypointEast = [gridView]() {
                            const QVariantList points = gridView->property("missionPoints").toList();
                            for (const QVariant& point : points) {
                                const QVariantMap fields = point.toMap();
                                if (!fields.value(QStringLiteral("isPinned")).toBool()) {
                                    return fields.value(QStringLiteral("east")).toReal();
                                }
                            }
                            return qQNaN();
                        };

                        const qreal eastAtPickup = waypointEast();
                        QVERIFY2(!qIsNaN(eastAtPickup), "the waypoint that was just added is not on the grid");

                        // Many steps rather than a couple: the fault this covers keeps only the last one, so a
                        // drag of two frames would leave undo looking very nearly right
                        constexpr int kDragSteps = 10;
                        constexpr int kDragPixels = 90;

                        const QPoint marker = gridView->mapToScene(placeAt).toPoint();
                        QPointingDevice* const finger = QTest::createTouchDevice();
                        {
                            QTest::QTouchEventSequence drag = QTest::touchEvent(_window, finger);
                            drag.press(0, marker).commit();
                            for (int step = 1; step <= kDragSteps; step++) {
                                drag.move(0, marker + QPoint((kDragPixels * step) / kDragSteps, 0)).commit();
                            }
                            drag.release(0, marker + QPoint(kDragPixels, 0)).commit();
                        }

                        QTRY_VERIFY_WITH_TIMEOUT(waypointEast() > eastAtPickup, TestTimeout::longMs());
                        const qreal draggedBy = waypointEast() - eastAtPickup;

                        QVERIFY2(gridView->property("canUndo").toBool(),
                                 "a drag that moved a waypoint offered no undo");

                        QVariant undone;
                        QVERIFY(QMetaObject::invokeMethod(gridView, "undoLastAction", Q_RETURN_ARG(QVariant, undone)));
                        QVERIFY2(undone.toBool(), "the undo entry left by the drag took nothing back");

                        // Measured against the drag rather than against a distance in metres, which would mean
                        // pinning down the grid's scale. Undoing one frame of a ten frame drag leaves nine
                        // tenths of it standing; undoing the drag leaves none of it.
                        QTRY_VERIFY_WITH_TIMEOUT(qAbs(waypointEast() - eastAtPickup) < (draggedBy / 10),
                                                 TestTimeout::longMs());
                    });
}

/// The panel describing a point has to leave the point visible.
///
/// It opens a touch target past the tap so a finger resting there does not cover the two numbers
/// that are the whole reason it exists. Where those did not fit -- which on a phone-sized view is
/// any point in the lower half of it -- the panel was slid to the edge of the view instead, landing
/// on top of the very point it was describing. Flipped to the other side of the tap it stays beside
/// the point wherever on the grid that point is.
void FlyViewLocalGridUITest::_theClickPanelOpensBesideThePointRatherThanOverIt_test()
{
    SettingsManager::instance()->flyViewSettings()->showLocalGridView()->setRawValue(true);

    runWithMockLink([] { return MockLink::startAPMArduCopterMockLink(); },
                    [this](const QPointer<MockLink>& mockLink, Vehicle* vehicle) {
                        QVERIFY(vehicle);
                        QVERIFY2(LocalGridTestSupport::giveTheVehicleAnOrigin(vehicle, mockLink),
                                 "the vehicle never took an origin");

                        QQuickItem* const gridView = findVisibleItem(_rootItem, QStringLiteral("localGridView"), 10000);
                        QVERIFY2(gridView, "the local grid never became visible with the setting on");

                        // Found rather than looked for visible: the panel exists from the start and is hidden
                        // until a point is clicked
                        QQuickItem* const panel = gridView->findChild<QQuickItem*>(QStringLiteral("localGrid_clickPanel"));
                        QVERIFY2(panel, "the grid has no click panel");

                        // Low in the view, which is where there is no room to open below the tap and where the
                        // panel used to be slid down on to the point instead
                        const QPointF point(gridView->width() * 0.5, gridView->height() * 0.92);
                        QVERIFY(QMetaObject::invokeMethod(panel, "showAt", Q_ARG(QVariant, QVariant(point.x())),
                                                          Q_ARG(QVariant, QVariant(point.y()))));

                        QTRY_VERIFY_WITH_TIMEOUT(panel->isVisible() && (panel->height() > 0), TestTimeout::longMs());

                        /// Where the panel is, in the grid's own pixels -- the same ones showAt was given
                        const auto panelRect = [panel]() {
                            return QRectF(panel->x(), panel->y(), panel->width(), panel->height());
                        };

                        QTRY_VERIFY_WITH_TIMEOUT(!panelRect().contains(point), TestTimeout::longMs());
                        QVERIFY2(!panelRect().contains(point),
                                 qPrintable(QStringLiteral("the panel at %1,%2 %3x%4 opened on top of the point "
                                                           "%5,%6 it describes")
                                                .arg(panelRect().x())
                                                .arg(panelRect().y())
                                                .arg(panelRect().width())
                                                .arg(panelRect().height())
                                                .arg(point.x())
                                                .arg(point.y())));

                        // The panel is beside the point rather than on it, so something has to say which point
                        QQuickItem* const marker =
                            gridView->findChild<QQuickItem*>(QStringLiteral("localGrid_clickPointMarker"));
                        QVERIFY2(marker, "nothing marks the point the panel is describing");
                        QTRY_VERIFY_WITH_TIMEOUT(marker->isVisible(), TestTimeout::longMs());

                        const QPointF markerCentre(marker->x() + (marker->width() / 2),
                                                   marker->y() + (marker->height() / 2));
                        QVERIFY2((markerCentre - point).manhattanLength() < 1.0,
                                 "the marker is not on the point the panel describes");
                    });
}

/// The after-flight work is an entry on the tool strip and a dialog behind it, rather than a panel
/// standing on the view.
///
/// A panel was tried in both corners this view has and neither is free -- the tool strip grows down
/// the whole left edge on a short window, and the right-hand column has 138px to divide between the
/// readout, its warnings and the plan list on an 800x400 view. Both times what the panel got was a
/// title with its buttons squeezed out. In the strip it costs the grid nothing, and the dialog has
/// room at any window size -- which is what this checks: that the entry appears when a flight has
/// left something to repair, and opens onto the repairs.
void FlyViewLocalGridUITest::_theAfterFlightStripEntryOpensItsRepairs_test()
{
    SettingsManager::instance()->flyViewSettings()->showLocalGridView()->setRawValue(true);

    runWithMockLink(
        [] { return MockLink::startAPMArduCopterMockLink(); },
        [this](const QPointer<MockLink>& mockLink, Vehicle* vehicle) {
            QQuickItem* const gridView = findVisibleItem(_rootItem, QStringLiteral("localGridView"), 10000);
            QVERIFY2(gridView, "the local grid never became visible with the setting on");
            QVERIFY2(LocalGridTestSupport::giveTheVehicleAnOrigin(vehicle, mockLink),
                     "the vehicle never took an origin");

            // MockLink flies its reported position five metres either side of the origin, which is the
            // drift the correction exists to repair -- so the prompt has something to offer without a
            // plan being drawn at all.
            QVERIFY2(findVisibleItem(_rootItem, QStringLiteral("flyToolStrip_afterFlightButton"), 5000),
                     "the strip never offered the after-flight work with the estimator reporting drift");
            QVERIFY2(clickButton(QStringLiteral("flyToolStrip_afterFlightButton")),
                     "the after-flight tool strip button could not be clicked");

            QVERIFY2(waitForDialog(QStringLiteral("After a flight")), "the after-flight dialog never opened");
            QVERIFY2(findVisibleItem(_rootItem, QStringLiteral("localGrid_standOnOriginButton"), 2000),
                     "the dialog opened without the correction that made the entry appear");
            QVERIFY2(findVisibleItem(_rootItem, QStringLiteral("localGrid_afterFlightSection"), 1000),
                     "the dialog opened without its after-flight section");

            QTest::keyClick(_window, Qt::Key_Escape);

            // The cost of putting it here rather than on the grid: this strip is close to full on a
            // short window, and a row added to a full column is a row that may end up under the fold.
            // clickButton refuses a press that lands outside the window, so this is the check that the
            // entry can still be reached at the size where the strip has least room -- and it is the
            // size an operator meets it at, since that is the ground station this is flown from.
            _window->resize(800, 400);
            QVERIFY2(clickButton(QStringLiteral("flyToolStrip_afterFlightButton")),
                     "the after-flight entry could not be reached on a short strip");
            QVERIFY2(waitForDialog(QStringLiteral("After a flight")),
                     "the after-flight dialog never opened on a short window");
            QTest::keyClick(_window, Qt::Key_Escape);

            // And again in plan edit mode, which is the strip at its longest: the plan's inserts stand
            // in for the flying buttons one for one and add a row of their own on top, so a short
            // window in that mode is where an entry runs out of column first.
            QVERIFY2(clickButton(QStringLiteral("flyToolStrip_planButton")), "the Plan button could not be clicked");
            QVERIFY_TRUE_WAIT(gridView->property("planEditMode").toBool(), TestTimeout::longMs());
            QVERIFY2(clickButton(QStringLiteral("flyToolStrip_afterFlightButton")),
                     "the after-flight entry could not be reached on the strip in plan edit mode");
            QVERIFY2(waitForDialog(QStringLiteral("After a flight")),
                     "the after-flight dialog never opened in plan edit mode");
            QTest::keyClick(_window, Qt::Key_Escape);
        });
}

/// The plan's transfers live in the toolbar, in one place, for as long as the grid is showing an
/// aircraft on the ground -- desktop or phone, plan mode or not.
///
/// They used to appear here only on a view too small to hold the grid's own mission panel open and
/// mid-plan-edit, and to live in that panel the rest of the time. Two homes reached by different
/// routes on different screens is a control an operator has to look for twice, and the size that
/// decided which was in force is the one thing they cannot see.
///
/// The corner is shared rather than handed over now: MainStatusIndicator and FlightModeIndicator
/// stay where they are, and the row gives up its labels instead. Armed is the one state that still
/// takes the row off the toolbar -- every action in it is ground work.
void FlyViewLocalGridUITest::_theToolbarCarriesThePlanActionsAtEverySize_test()
{
    SettingsManager::instance()->flyViewSettings()->showLocalGridView()->setRawValue(true);

    runWithMockLink(
        [] { return MockLink::startAPMArduCopterMockLink(); },
        [this](const QPointer<MockLink>& /*mockLink*/, Vehicle* vehicle) {
            QQuickItem* const gridView = findVisibleItem(_rootItem, QStringLiteral("localGridView"), 10000);
            QVERIFY2(gridView, "the local grid never became visible with the setting on");

            // The default test window is not compact and nothing has entered plan edit mode: the two
            // conditions the row used to require. Both indicators are here as well, which is the
            // half of this that a straight widening of the old gate would have broken.
            QVERIFY2(!gridView->property("compact").toBool(), "the default test window is already compact");
            QVERIFY2(!gridView->property("planEditMode").toBool(), "the grid started in plan edit mode");
            QVERIFY2(verifyVisibility(QStringLiteral("toolbar_localGridPlanActions"), true,
                                      QStringLiteral("not compact, not planning, disarmed")),
                     "the plan actions were missing from a roomy toolbar outside plan mode");
            QVERIFY2(verifyVisibility(QStringLiteral("toolbar_mainStatusIndicator"), true,
                                      QStringLiteral("not compact, not planning, disarmed")),
                     "the status indicator gave up its corner to the plan actions");
            QVERIFY2(verifyVisibility(QStringLiteral("toolbar_flightModeIndicator"), true,
                                      QStringLiteral("not compact, not planning, disarmed")),
                     "the flight mode indicator gave up its corner to the plan actions");

            // Every action has to be reachable, Download included -- it is the one behind the
            // overflow, and a menu that opens onto nothing is the same as the button not being there
            for (const QString& button :
                 {QStringLiteral("toolbar_localGridOpenButton"), QStringLiteral("toolbar_localGridSaveButton"),
                  QStringLiteral("toolbar_localGridUploadButton"), QStringLiteral("toolbar_localGridClearButton"),
                  QStringLiteral("toolbar_localGridOverflowButton")}) {
                QVERIFY2(findVisibleItem(_rootItem, button, 0),
                         qPrintable(button + QStringLiteral(" is missing from the toolbar row")));
            }
            QVERIFY2(clickButton(QStringLiteral("toolbar_localGridOverflowButton")),
                     "the overflow button could not be clicked");
            QVERIFY2(findVisibleItem(_rootItem, QStringLiteral("toolbar_localGridDownloadButton"), 2000),
                     "Download never appeared in the overflow panel");

            // The workflow behind these buttons lives in the grid's after-flight panel, which is off
            // the screen here -- no origin has been set and nothing has been drawn, so it has nothing
            // to offer. Its confirmations still have to reach the operator: parented to that panel
            // they would go wherever it went, which is the whole of the toolbar's Clear, Download and
            // Upload silently doing nothing.
            QVERIFY2(verifyVisibility(QStringLiteral("flyToolStrip_afterFlightButton"), false,
                                      QStringLiteral("no origin, no plan")),
                     "the after-flight button was on the strip with nothing to repair");
            QVERIFY2(clickButton(QStringLiteral("toolbar_localGridDownloadButton")),
                     "Download could not be clicked in the overflow panel");
            QVERIFY2(waitForDialog(QStringLiteral("Download")),
                     "the confirmation went missing with the panel that owns it");
            QVERIFY2(rejectDialog(), "the confirmation could not be dismissed");

            // The toolbar's transfer bar reads these three off the same panel, through a var
            // property that answers undefined rather than failing for a name that is not there --
            // so a rename here leaves a bar that never appears and nothing that says why.
            QObject* const missionActions = gridView->property("missionActions").value<QObject*>();
            QVERIFY2(missionActions, "the grid stopped publishing its mission actions");
            for (const char* name : {"syncing", "syncProgress", "syncJustCompleted"}) {
                QVERIFY2(missionActions->property(name).isValid(),
                         qPrintable(QStringLiteral("the toolbar's transfer bar reads %1 and it is gone")
                                        .arg(QLatin1StringView(name))));
            }

            // Shrunk to a size the grid itself reports as compact -- the same condition
            // LocalGridResponsiveLayoutTest drives its phone-portrait cases from. The row stays put;
            // only the labels go, so the buttons keep their order and their places.
            _window->resize(400, 800);
            QVERIFY_TRUE_WAIT(gridView->property("compact").toBool(), TestTimeout::longMs());

            QVERIFY2(verifyVisibility(QStringLiteral("toolbar_localGridPlanActions"), true,
                                      QStringLiteral("compact, disarmed")),
                     "the plan actions left the toolbar once the grid went compact");
            QQuickItem* const uploadButton =
                findVisibleItem(_rootItem, QStringLiteral("toolbar_localGridUploadButton"), 1000);
            QVERIFY2(uploadButton, "Upload left the toolbar once the grid went compact");
            QVERIFY2(uploadButton->property("text").toString().isEmpty(),
                     "the buttons kept their labels on a view with no room for them");
            // The row is only worth adding to this corner if it does not push what was already there
            // off the end of it. Checked at the shape a small ground station actually runs in rather
            // than at the narrowest window Qt will make: the toolbar flicks horizontally, so an
            // overflow does not clip anything, it just puts the battery behind a scroll -- which is
            // no way to read a battery. Measured here at 470 for the row and 688 for the battery.
            _window->resize(800, 440);
            QVERIFY_TRUE_WAIT(gridView->property("compact").toBool(), TestTimeout::longMs());

            const auto rightEdgeOf = [this](const QString& objectName) {
                QQuickItem* const item = findVisibleItem(_rootItem, objectName, 1000);
                return item ? (item->mapToScene(QPointF(0, 0)).x() + item->width()) : -1.0;
            };
            const qreal rowRight     = rightEdgeOf(QStringLiteral("toolbar_localGridPlanActions"));
            const qreal batteryRight = rightEdgeOf(QStringLiteral("toolbar_batteryIndicator"));
            QVERIFY2(rowRight > 0 && rowRight <= _window->width(),
                     qPrintable(QStringLiteral("the plan actions ran off an 800x440 toolbar: right edge %1")
                                    .arg(rowRight)));
            QVERIFY2(batteryRight > 0 && batteryRight <= _window->width(),
                     qPrintable(QStringLiteral("the plan actions pushed the battery indicator out of a "
                                               "800x440 toolbar: right edge %1").arg(batteryRight)));

            // Arming takes the row off and leaves the indicators alone, since they never moved
            vehicle->setArmed(true, false);
            QTRY_VERIFY_WITH_TIMEOUT(vehicle->armed(), TestTimeout::longMs());

            QVERIFY2(verifyVisibility(QStringLiteral("toolbar_localGridPlanActions"), false, QStringLiteral("armed")),
                     "the plan action row kept the corner with the aircraft armed");
            QVERIFY2(verifyVisibility(QStringLiteral("toolbar_mainStatusIndicator"), true, QStringLiteral("armed")),
                     "the status indicator was missing with the aircraft armed");

            vehicle->setArmed(false, false);
            QTRY_VERIFY_WITH_TIMEOUT(!vehicle->armed(), TestTimeout::longMs());
        });
}
