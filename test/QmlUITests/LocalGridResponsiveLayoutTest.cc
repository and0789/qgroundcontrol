#include "LocalGridResponsiveLayoutTest.h"

#include <QtCore/QList>
#include <QtCore/QPointF>
#include <QtCore/QRectF>
#include <QtCore/QSizeF>
#include <QtGui/QRegion>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickWindow>
#include <QtTest/QTest>

#include "FlyViewSettings.h"
#include "LocalGridTestSupport.h"
#include "MockLink.h"
#include "SettingsManager.h"
#include "Vehicle.h"

UT_REGISTER_TEST(LocalGridResponsiveLayoutTest, TestLabel::Integration)

namespace {

using LocalGridTestSupport::giveTheVehicleAnOrigin;

/// The panels that stay on screen for the life of the view -- everything anchored independently
/// against the window's edges or against a neighbour's measured size. The click panel is left out on
/// purpose: it is transient and drawn above everything else by design, so it is checked separately
/// for staying inside the window and never for overlap with the others.
const QStringList kStandingPanels = {
    QStringLiteral("localGrid_readout"),
    QStringLiteral("localGrid_airspeed"),
    QStringLiteral("localGrid_missionList"),
    QStringLiteral("localGrid_scaleBar"),
};

/// Two of the fly view's own widgets are checked alongside the grid's, because the grid's panels are
/// anchored against their edges and a mistake there lands on top of them rather than off the window.
///
/// The tool strip is anchored top-left and grows down that edge; on a short window it reaches the
/// bottom, which is what drove the after-flight panel out of that corner.
///
/// The bottom-right row -- the telemetry bar and the instrument panel -- is the floor the whole
/// right-hand column is measured against. Leaving it out of this list is why an opened readout could
/// put its own view buttons, and the plan list under them, behind the compass with every test here
/// still passing.
const QStringList kOverlapCheckedPanels = kStandingPanels + QStringList{
    QStringLiteral("flyView_toolStrip"),
    QStringLiteral("flyView_bottomRightRowLayout"),
};

struct WindowSize {
    const char *name;
    int width;
    int height;

    /// The most the standing panels may cover of this size, in _chromeStaysWithinBudgetAtAnySize_test.
    /// Unused by the other tests here. 15% everywhere except phone landscape, which gets 18% -- of
    /// these sizes it has the least height to work with (400px, against 800/768/800/900 for the
    /// others), and these tests give the vehicle an origin, which is the state the readout folds
    /// itself in (see LocalGridReadout.qml's _standOpen). What is left standing there is the header
    /// and whatever warnings the vehicle is raising, and on 400px of height that is still a larger
    /// share of the window than the same panel is anywhere else.
    double chromeBudgetPercent;
};

/// Points on the shape the app actually has to run in, not just the desktop it was built on.
///
/// The 10-inch entry is a real ground station rather than a category: 1280x800 is the size this
/// feature is flown on, and it sits in the gap the other four leave -- wider than the tablet but
/// shorter than the desktop, which is the combination the right-hand column has the least room in.
const QList<WindowSize> kSizesToCheck = {
    {.name = "phone portrait", .width = 400, .height = 800, .chromeBudgetPercent = 15.0},
    {.name = "phone landscape", .width = 800, .height = 400, .chromeBudgetPercent = 18.0},
    {.name = "tablet", .width = 1024, .height = 768, .chromeBudgetPercent = 15.0},
    {.name = "10-inch ground station", .width = 1280, .height = 800, .chromeBudgetPercent = 15.0},
    {.name = "desktop", .width = 1600, .height = 900, .chromeBudgetPercent = 15.0},
};

/// Waits until \a item's mapped rect stops moving between two samples, so a measurement taken right
/// after a resize is not caught mid-relayout. There is no animation on any of these panels' anchors,
/// so two consecutive equal samples means the polish pass that follows a resize has already run.
bool waitForLayoutToSettle(QQuickItem *item)
{
    QRectF previous(-1, -1, -1, -1);  // Never equal to a real first sample
    return QTest::qWaitFor([item, &previous]() {
        const QRectF current(item->mapToScene(QPointF(0, 0)), QSizeF(item->width(), item->height()));
        const bool stable = (current == previous);
        previous = current;
        return stable;
    }, TestTimeout::shortMs());
}

/// A rect printed as plain numbers, for failure messages -- QRectF has no QString conversion of its
/// own, and pulling in QDebug/QTest::toString machinery for four numbers is not worth it here.
QString rectToString(const QRectF &rect)
{
    return QStringLiteral("(%1, %2, %3x%4)").arg(rect.x()).arg(rect.y()).arg(rect.width()).arg(rect.height());
}

} // namespace

QRectF LocalGridResponsiveLayoutTest::_windowRectFor(const QString &objectName)
{
    QQuickItem *const item = findVisibleItem(_rootItem, objectName, 500);
    if (item == nullptr) {
        return {};
    }
    return {item->mapToScene(QPointF(0, 0)), QSizeF(item->width(), item->height())};
}

/// The plan list is watched for settling: it is the panel furthest down the chain of anchors on the
/// right-hand edge, so it is the last to stop moving, and every panel here reflows in the same polish
/// pass anyway.
///
/// The scale bar stands in when the list is not on screen -- it is on the opposite corner and reflows
/// in the same pass, so it answers the same question, and a settle helper that reported "the layout
/// never settled" for a panel that had correctly gone away would blame the layout for a state the view
/// is supposed to have.
bool LocalGridResponsiveLayoutTest::_resizeAndSettle(int width, int height)
{
    _window->resize(width, height);
    QQuickItem *settleTarget = findVisibleItem(_rootItem, QStringLiteral("localGrid_missionList"), 1000);
    if (settleTarget == nullptr) {
        settleTarget = findVisibleItem(_rootItem, QStringLiteral("localGrid_scaleBar"), 1000);
    }
    if (settleTarget == nullptr) {
        return false;
    }
    return waitForLayoutToSettle(settleTarget);
}

/// MAV_CMD_EXTERNAL_POSITION_ESTIMATE and the grid's origin machinery are ArduPilot-specific, and the
/// vehicle this boots is an ArduCopter. A build with no ArduPilot plugin registered has no vehicle to
/// connect, which is a missing build option rather than a broken layout.
void LocalGridResponsiveLayoutTest::init()
{
    if (!apmFirmwareSupported()) {
        QSKIP("ArduPilot support not registered in this build");
    }
    QmlUITestBase::init();
}

void LocalGridResponsiveLayoutTest::cleanup()
{
    // Persisted, so leaving it on would put every later test's fly view on the grid
    SettingsManager::instance()->flyViewSettings()->showLocalGridView()->setRawValue(false);
    QmlUITestBase::cleanup();
}

/// No panel is allowed to hang off the edge of the window it is drawn in, at any of the four sizes
/// this is checked at -- a phone in portrait and in landscape, a tablet, and the desktop this view
/// was built against. A panel that runs off the window is a control the operator cannot reach.
///
/// Checked against a freshly-connected vehicle with an empty plan: none of the panels' content grows
/// large enough here to run off the window on its own. That is not this test's job -- growth that
/// pushes a panel into another panel is _panelsDoNotOverlapAtAnySize_test's job, and growth that pushes
/// a panel past the window edge outright has not been reproduced against any state this suite can
/// reach. Kept as a standing regression guard: a panel large enough to overflow on its own, at any of
/// these four sizes, is a real bug this exists to catch even though nothing here trips it today.
void LocalGridResponsiveLayoutTest::_panelsStayInsideThePhoneWindow_test()
{
    SettingsManager::instance()->flyViewSettings()->showLocalGridView()->setRawValue(true);

    runWithMockLink(
        [] { return MockLink::startAPMArduCopterMockLink(); },
        [this](const QPointer<MockLink> &mockLink, Vehicle *vehicle) {
            QVERIFY(vehicle);
            QVERIFY2(giveTheVehicleAnOrigin(vehicle, mockLink), "the vehicle never took an origin");

            QQuickItem *const gridView = findVisibleItem(_rootItem, QStringLiteral("localGridView"), 10000);
            QVERIFY2(gridView, "the local grid never became visible with the setting on");

            for (const WindowSize &size : kSizesToCheck) {
                QVERIFY2(_resizeAndSettle(size.width, size.height),
                         qPrintable(QStringLiteral("the layout never settled at %1 (%2x%3)")
                                        .arg(size.name).arg(size.width).arg(size.height)));

                const QRectF windowRect(0, 0, _window->width(), _window->height());

                for (const QString &panelName : kStandingPanels) {
                    const QRectF panelRect = _windowRectFor(panelName);
                    if (panelRect.isEmpty()) {
                        continue;  // Not currently shown -- nothing to run off the edge
                    }
                    QVERIFY2(windowRect.contains(panelRect),
                             qPrintable(QStringLiteral("%1 runs off the %2 window (%3x%4): panel rect %5 vs window rect %6")
                                            .arg(panelName, size.name)
                                            .arg(size.width).arg(size.height)
                                            .arg(rectToString(panelRect), rectToString(windowRect))));
                }
            }
        });
}

/// No two of the checked panels are allowed to cover each other. A control hidden under another
/// panel is indistinguishable, from the operator's seat, from a control that was never built.
///
/// Checked disarmed, which is the state the grid's own panels are on screen in at all -- the
/// after-flight panel stands down while the aircraft is armed, and with it the tallest thing the
/// right-hand column carries.
///
/// Each size is checked twice: once with the readout folded, which is how it starts, and once with it
/// open. Folded is not the interesting state and never was. The readout is the top of the right-hand
/// column and everything else in that column is anchored under it, so its open height is what decides
/// whether the column clears the instrument panel in the corner below -- and open is where an operator
/// leaves it, since folded it shows a range and a bearing and nothing else.
void LocalGridResponsiveLayoutTest::_panelsDoNotOverlapAtAnySize_test()
{
    SettingsManager::instance()->flyViewSettings()->showLocalGridView()->setRawValue(true);

    runWithMockLink(
        [] { return MockLink::startAPMArduCopterMockLink(); },
        [this](const QPointer<MockLink> &mockLink, Vehicle *vehicle) {
            QVERIFY(vehicle);
            QVERIFY2(giveTheVehicleAnOrigin(vehicle, mockLink), "the vehicle never took an origin");

            QQuickItem *const gridView = findVisibleItem(_rootItem, QStringLiteral("localGridView"), 10000);
            QVERIFY2(gridView, "the local grid never became visible with the setting on");

            for (const WindowSize &size : kSizesToCheck) {
                QVERIFY2(_resizeAndSettle(size.width, size.height),
                         qPrintable(QStringLiteral("the layout never settled at %1 (%2x%3)")
                                        .arg(size.name).arg(size.width).arg(size.height)));

                const auto checkNothingOverlaps = [this, &size](const QString &readoutState) {
                    QList<QPair<QString, QRectF>> visiblePanels;
                    for (const QString &panelName : kOverlapCheckedPanels) {
                        const QRectF panelRect = _windowRectFor(panelName);
                        if (!panelRect.isEmpty()) {
                            visiblePanels.append({panelName, panelRect});
                        }
                    }

                    for (qsizetype i = 0; i < visiblePanels.size(); ++i) {
                        for (qsizetype j = i + 1; j < visiblePanels.size(); ++j) {
                            const QRectF overlap = visiblePanels[i].second.intersected(visiblePanels[j].second);
                            QVERIFY2(overlap.isEmpty(),
                                     qPrintable(QStringLiteral("%1 and %2 overlap at %3 (%4x%5), readout %6: "
                                                               "%7 vs %8")
                                                    .arg(visiblePanels[i].first, visiblePanels[j].first, size.name)
                                                    .arg(size.width).arg(size.height).arg(readoutState,
                                                         rectToString(visiblePanels[i].second),
                                                         rectToString(visiblePanels[j].second))));
                        }
                    }
                };

                checkNothingOverlaps(QStringLiteral("folded"));
                if (QTest::currentTestFailed()) {
                    return;
                }

                QQuickItem *const readout = findVisibleItem(_rootItem, QStringLiteral("localGrid_readout"), 1000);
                QVERIFY2(readout, "the readout never appeared");
                readout->setProperty("collapsed", false);
                QVERIFY2(waitForLayoutToSettle(readout), "the column never settled after opening the readout");
                checkNothingOverlaps(QStringLiteral("open"));
                if (QTest::currentTestFailed()) {
                    return;
                }
                readout->setProperty("collapsed", true);
                QVERIFY2(waitForLayoutToSettle(readout), "the column never settled after folding the readout");
            }
        });
}

/// The right-hand column states how wide it is allowed to be -- a third of the view, or 30 characters,
/// whichever is narrower -- and the readout at the top of it did not keep to it. On a 400px view that
/// third is 133px and the readout took 225, better than half the window, squeezing the grid the whole
/// view exists to show and leaving nothing across the top for anything else to stand in.
///
/// The cause is the one its own comments warn about: a Layout does not shrink its children to fit, so
/// a cap on the panel is not a cap on what is inside it. Checked as a width the panel actually
/// measures rather than as a property it has been given, since the given one was already correct.
///
/// Checked with the readout open, which is when it holds the rows and buttons that drive its width.
void LocalGridResponsiveLayoutTest::_theRightColumnKeepsToItsShareOfTheWidth_test()
{
    SettingsManager::instance()->flyViewSettings()->showLocalGridView()->setRawValue(true);

    runWithMockLink(
        [] { return MockLink::startAPMArduCopterMockLink(); },
        [this](const QPointer<MockLink> &mockLink, Vehicle *vehicle) {
            QVERIFY(vehicle);
            QVERIFY2(giveTheVehicleAnOrigin(vehicle, mockLink), "the vehicle never took an origin");

            QQuickItem *const gridView = findVisibleItem(_rootItem, QStringLiteral("localGridView"), 10000);
            QVERIFY2(gridView, "the local grid never became visible with the setting on");

            for (const WindowSize &size : kSizesToCheck) {
                QVERIFY2(_resizeAndSettle(size.width, size.height),
                         qPrintable(QStringLiteral("the layout never settled at %1 (%2x%3)")
                                        .arg(size.name).arg(size.width).arg(size.height)));

                QQuickItem *const readout = findVisibleItem(_rootItem, QStringLiteral("localGrid_readout"), 1000);
                QVERIFY2(readout, "the readout never appeared");
                readout->setProperty("collapsed", false);
                QVERIFY2(waitForLayoutToSettle(readout), "the readout never settled after being opened");


                const qreal columnMaximum = gridView->property("_rightColumnMaximumWidth").toReal();
                QVERIFY(columnMaximum > 0);

                for (const QString &panelName : {QStringLiteral("localGrid_readout"),
                                                 QStringLiteral("localGrid_missionList"),
                                                 QStringLiteral("localGrid_missionStats")}) {
                    const QRectF panel = _windowRectFor(panelName);
                    if (panel.isEmpty()) {
                        continue;
                    }
                    QVERIFY2(panel.width() <= columnMaximum,
                             qPrintable(QStringLiteral("%1 took %2 of the column's %3 at %4 (%5x%6)")
                                            .arg(panelName).arg(panel.width()).arg(columnMaximum)
                                            .arg(size.name).arg(size.width).arg(size.height)));
                }

                readout->setProperty("collapsed", true);
                QVERIFY2(waitForLayoutToSettle(readout), "the readout never settled after being folded");
            }
        });
}

/// The non-GPS readout is opened over this view more than any other -- it is the panel of values a
/// GNSS-denied flight is judged by -- and it had no ceiling of its own. On a short window its last
/// sections, the EKF innovation ratios among them, were drawn past the bottom edge and could not be
/// reached at all, and on the way down it covered the grid's scale bar.
///
/// Opened through its setting rather than through the tool strip button, so what is checked is the
/// panel's own bounds rather than whether a button in a strip that may itself be scrolled can be
/// reached.
void LocalGridResponsiveLayoutTest::_theNonGpsPanelStaysOnScreenAndOffTheScaleBar_test()
{
    SettingsManager::instance()->flyViewSettings()->showLocalGridView()->setRawValue(true);

    runWithMockLink(
        [] { return MockLink::startAPMArduCopterMockLink(); },
        [this](const QPointer<MockLink> &mockLink, Vehicle *vehicle) {
            QVERIFY(vehicle);
            QVERIFY2(giveTheVehicleAnOrigin(vehicle, mockLink), "the vehicle never took an origin");

            QQuickItem *const gridView = findVisibleItem(_rootItem, QStringLiteral("localGridView"), 10000);
            QVERIFY2(gridView, "the local grid never became visible with the setting on");

            SettingsManager::instance()->flyViewSettings()->showNonGpsStatusPanel()->setRawValue(true);
            QVERIFY2(findVisibleItem(_rootItem, QStringLiteral("flyView_nonGpsStatusPanel"), 3000),
                     "the non-GPS panel never appeared with its setting on");

            for (const WindowSize &size : kSizesToCheck) {
                QVERIFY2(_resizeAndSettle(size.width, size.height),
                         qPrintable(QStringLiteral("the layout never settled at %1 (%2x%3)")
                                        .arg(size.name).arg(size.width).arg(size.height)));

                const QRectF panel    = _windowRectFor(QStringLiteral("flyView_nonGpsStatusPanel"));
                const QRectF scaleBar = _windowRectFor(QStringLiteral("localGrid_scaleBar"));
                QVERIFY2(!panel.isEmpty(), "the non-GPS panel went missing on a resize");

                QVERIFY2(panel.bottom() <= _window->height(),
                         qPrintable(QStringLiteral("the non-GPS panel ran off the bottom at %1 (%2x%3): %4")
                                        .arg(size.name).arg(size.width).arg(size.height)
                                        .arg(rectToString(panel))));
                QVERIFY2(scaleBar.isEmpty() || panel.intersected(scaleBar).isEmpty(),
                         qPrintable(QStringLiteral("the non-GPS panel covered the scale bar at %1 (%2x%3): "
                                                   "%4 vs %5")
                                        .arg(size.name).arg(size.width).arg(size.height)
                                        .arg(rectToString(panel), rectToString(scaleBar))));
            }

            SettingsManager::instance()->flyViewSettings()->showNonGpsStatusPanel()->setRawValue(false);
        });
}

/// The click panel is positioned at runtime from the point tapped, not anchored like the others --
/// LocalGridClickPanel::showAt() clamps it into the view, but only against the view's own bounds, with
/// no awareness of the insets the standing panels honour. Checked at the phone size and near the right
/// edge, where a naive clamp is most likely to still leave it hanging off the window.
///
/// Driven by a real click on the grid rather than by calling showAt() directly: the panel starts
/// invisible (LocalGridView.qml's dragArea.onClicked is what shows it), so a genuine click is also
/// the only way to find it at all -- findVisibleItem() cannot see an item whose own visible property
/// is still false.
///
/// Aimed at the right edge at mid-height rather than a corner: the bottom-right corner of the phone
/// window is where the on-screen flight controls sit, and a click that lands on one of those is
/// consumed there rather than reaching the grid -- correctly, since placing a waypoint under a flight
/// control is not a thing this feature is meant to allow. Mid-height keeps clear of the readout column
/// at the top and the mission actions panel at the bottom, so the click is unambiguously on the grid.
void LocalGridResponsiveLayoutTest::_clickPanelStaysInsideTheWindowNearAnEdge_test()
{
    SettingsManager::instance()->flyViewSettings()->showLocalGridView()->setRawValue(true);

    runWithMockLink(
        [] { return MockLink::startAPMArduCopterMockLink(); },
        [this](const QPointer<MockLink> &mockLink, Vehicle *vehicle) {
            QVERIFY(vehicle);
            QVERIFY2(giveTheVehicleAnOrigin(vehicle, mockLink), "the vehicle never took an origin");

            QQuickItem *const gridView = findVisibleItem(_rootItem, QStringLiteral("localGridView"), 10000);
            QVERIFY2(gridView, "the local grid never became visible with the setting on");

            QVERIFY2(_resizeAndSettle(400, 800), "the layout never settled at phone portrait (400x800)");

            // Near the grid's right edge, where a click panel sized by its own two multi-line
            // refusal labels is most likely to still be wider than the room a naive clamp leaves.
            QVERIFY2(clickItemFraction(QStringLiteral("localGridView"), 0.98, 0.5),
                     "the click near the grid's right edge never landed");

            QQuickItem *const clickPanel = findVisibleItem(_rootItem, QStringLiteral("localGrid_clickPanel"), 1000);
            QVERIFY2(clickPanel, "a click on the grid never opened the click panel");

            const QRectF windowRect(0, 0, _window->width(), _window->height());
            const QRectF panelRect(clickPanel->mapToScene(QPointF(0, 0)),
                                   QSizeF(clickPanel->width(), clickPanel->height()));
            QVERIFY2(windowRect.contains(panelRect),
                     qPrintable(QStringLiteral("the click panel runs off the phone window near the right edge: "
                                               "panel rect %1 vs window rect %2")
                                    .arg(rectToString(panelRect), rectToString(windowRect))));
        });
}

/// The two numbers at the top of the click panel -- how far north and east the point is -- are the
/// entire reason the panel opens before anything is placed. Opening it with its own top-left corner
/// exactly on the point that summoned it put those numbers under the fingertip still resting there,
/// on the one device where the operator cannot simply move the pointer away to read them.
///
/// Asserted as the property rather than as the offset: the point that was touched must not be
/// underneath the panel describing it. That survives the clamping near the edges, where the panel is
/// pushed back inside the view and ends up above or left of the touch instead of below and right of
/// it -- a different position, same requirement.
///
/// Driven at the tablet size and well away from every edge, so this measures the offset itself
/// rather than the edge clamp that _clickPanelStaysInsideTheWindowNearAnEdge_test already covers.
void LocalGridResponsiveLayoutTest::_clickPanelDoesNotCoverThePointItDescribes_test()
{
    SettingsManager::instance()->flyViewSettings()->showLocalGridView()->setRawValue(true);

    runWithMockLink(
        [] { return MockLink::startAPMArduCopterMockLink(); },
        [this](const QPointer<MockLink> &mockLink, Vehicle *vehicle) {
            QVERIFY(vehicle);
            QVERIFY2(giveTheVehicleAnOrigin(vehicle, mockLink), "the vehicle never took an origin");

            QQuickItem *const gridView = findVisibleItem(_rootItem, QStringLiteral("localGridView"), 10000);
            QVERIFY2(gridView, "the local grid never became visible with the setting on");

            QVERIFY2(_resizeAndSettle(1024, 768), "the layout never settled at tablet (1024x768)");

            const QRectF gridRect = _windowRectFor(QStringLiteral("localGridView"));
            QVERIFY2(!gridRect.isEmpty(), "the grid reported no rect to click inside");

            // Left of centre and above it: clear of the right-hand panel column, clear of the tool
            // strip in the top-left, and far enough from every edge that nothing is clamped.
            constexpr qreal fractionX = 0.4;
            constexpr qreal fractionY = 0.4;
            const QPointF clickPoint(gridRect.x() + (gridRect.width() * fractionX),
                                     gridRect.y() + (gridRect.height() * fractionY));

            QVERIFY2(clickItemFraction(QStringLiteral("localGridView"), fractionX, fractionY),
                     "the click on the middle of the grid never landed");

            QQuickItem *const clickPanel = findVisibleItem(_rootItem, QStringLiteral("localGrid_clickPanel"), 1000);
            QVERIFY2(clickPanel, "a click on the grid never opened the click panel");

            const QRectF panelRect(clickPanel->mapToScene(QPointF(0, 0)),
                                   QSizeF(clickPanel->width(), clickPanel->height()));
            QVERIFY2(!panelRect.contains(clickPoint),
                     qPrintable(QStringLiteral("the click panel opened on top of the point it describes: "
                                               "panel rect %1 covers the touch at (%2, %3)")
                                    .arg(rectToString(panelRect))
                                    .arg(clickPoint.x())
                                    .arg(clickPoint.y())));

            // Still inside the window: an offset that pushes the panel off the view trades one
            // failure for another
            const QRectF windowRect(0, 0, _window->width(), _window->height());
            QVERIFY2(windowRect.contains(panelRect),
                     qPrintable(QStringLiteral("the offset pushed the click panel out of the window: "
                                               "panel rect %1 vs window rect %2")
                                    .arg(rectToString(panelRect), rectToString(windowRect))));
        });
}

/// Panels are allowed to sit beside the grid; they are not allowed to become most of the view. This
/// is the check the other three tests do not do: a panel can stay inside the window and never overlap
/// another one while still covering so much of it that the grid -- the entire reason this view exists
/// over the map -- is a sliver down one edge.
///
/// Checked in the default state (freshly connected, empty plan) at each of the four sizes, not armed:
/// this is about how many panels are open at once, not about how tall any one of them can grow, so the
/// leanest realistic state is the fairer one to hold every size to the same number against.
///
/// Budgets are 15% everywhere except phone landscape (18% -- see the comment on WindowSize for why).
/// Measured before this test existed, a phone in portrait gave up 28.4% of the window to chrome and
/// landscape gave up 17.9%, both comfortably above their budgets here, so an accidental one-panel
/// regression has room to be caught before it reaches that scale again.
void LocalGridResponsiveLayoutTest::_chromeStaysWithinBudgetAtAnySize_test()
{
    SettingsManager::instance()->flyViewSettings()->showLocalGridView()->setRawValue(true);

    runWithMockLink(
        [] { return MockLink::startAPMArduCopterMockLink(); },
        [this](const QPointer<MockLink> &mockLink, Vehicle *vehicle) {
            QVERIFY(vehicle);
            QVERIFY2(giveTheVehicleAnOrigin(vehicle, mockLink), "the vehicle never took an origin");

            QQuickItem *const gridView = findVisibleItem(_rootItem, QStringLiteral("localGridView"), 10000);
            QVERIFY2(gridView, "the local grid never became visible with the setting on");

            for (const WindowSize &size : kSizesToCheck) {
                QVERIFY2(_resizeAndSettle(size.width, size.height),
                         qPrintable(QStringLiteral("the layout never settled at %1 (%2x%3)")
                                        .arg(size.name).arg(size.width).arg(size.height)));

                // Unioned rather than summed, so two panels that legitimately touch along an edge do
                // not get counted twice and fail a budget they have not actually gone over.
                QRegion covered;
                for (const QString &panelName : kStandingPanels) {
                    const QRectF panelRect = _windowRectFor(panelName);
                    if (!panelRect.isEmpty()) {
                        covered += panelRect.toAlignedRect();
                    }
                }

                qint64 coveredArea = 0;
                for (const QRect &piece : covered) {
                    coveredArea += static_cast<qint64>(piece.width()) * piece.height();
                }
                const qint64 windowArea = static_cast<qint64>(_window->width()) * _window->height();
                const double coveredPercent = (windowArea > 0)
                                                ? (100.0 * static_cast<double>(coveredArea) / static_cast<double>(windowArea))
                                                : 0.0;

                QVERIFY2(coveredPercent <= size.chromeBudgetPercent,
                         qPrintable(QStringLiteral("chrome covers %1 percent of the %2 window (%3x%4), over "
                                                   "the %5 percent budget")
                                        .arg(coveredPercent, 0, 'f', 1).arg(size.name)
                                        .arg(size.width).arg(size.height).arg(size.chromeBudgetPercent, 0, 'f', 0)));
            }
        });
}
