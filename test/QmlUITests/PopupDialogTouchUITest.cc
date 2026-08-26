#include "PopupDialogTouchUITest.h"

#include <QtCore/QRegularExpression>
#include <QtCore/QScopeGuard>
#include <QtGui/QPointingDevice>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickWindow>
#include <QtTest/QSignalSpy>
#include <QtTest/QTest>

#include "AppMessages.h"
#include "FlyViewSettings.h"
#include "MissionController.h"
#include "MockLink.h"
#include "PlanMasterController.h"
#include "SettingsManager.h"
#include "Vehicle.h"

UT_REGISTER_TEST(PopupDialogTouchUITest, TestLabel::Integration)

namespace {

/// Taps @a item once with a single finger.
///
/// A fingertip is a contact patch rather than a point, and its centre wanders a few pixels before
/// it lifts. A tap that presses and releases on the same pixel in the same instant is the one
/// gesture a hand never makes, so the release is offset from the press.
void tapWithAFinger(QQuickWindow* window, QQuickItem* item)
{
    const QPoint target = item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint();

    QPointingDevice* const finger = QTest::createTouchDevice();
    QTest::QTouchEventSequence tap = QTest::touchEvent(window, finger);
    tap.press(0, target).commit();
    tap.move(0, target + QPoint(3, 4)).commit();
    tap.release(0, target + QPoint(3, 4)).commit();
}

/// Puts the Plan view's mission into the unsaved state the close check reads, without editing it
/// through the UI. The plan need not hold real items for the check to fire.
bool forcePlanViewMissionDirty(QQuickWindow* window)
{
    QQuickItem* const planView = window ? window->findChild<QQuickItem*>(QStringLiteral("mainView_plan")) : nullptr;
    if (!planView) {
        QTest::qFail("Could not find Plan view item (mainView_plan)", __FILE__, __LINE__);
        return false;
    }

    auto* const masterController = qvariant_cast<PlanMasterController*>(planView->property("_planMasterController"));
    if (!masterController) {
        QTest::qFail("Plan view _planMasterController property is not a PlanMasterController", __FILE__, __LINE__);
        return false;
    }

    masterController->missionController()->setDirty(true);
    return true;
}

}  // namespace

/// A refused arming raises the vehicle-error banner and then an app message over it, which is the
/// state the dialog is dismissed from in the field: the fly view underneath, the local grid on it,
/// and a banner already standing between the two.
///
/// Driven with both devices and asserted against each other, because the accept button's own logic
/// is the same either way -- what differs is whether the press reaches it at all, and a mouse pass
/// beside the touch pass is what tells those two failures apart.
void PopupDialogTouchUITest::_aFingerDismissesAnAppMessageTheWayAMouseDoes_test()
{
    if (!apmFirmwareSupported()) {
        QSKIP("ArduPilot support not registered in this build");
    }

    SettingsManager::instance()->flyViewSettings()->showLocalGridView()->setRawValue(true);

    runWithMockLink(
        [] { return MockLink::startAPMArduCopterMockLink(); },
        [this](const QPointer<MockLink>& /*mockLink*/, Vehicle* /*vehicle*/) {
            QVERIFY2(findVisibleItem(_rootItem, QStringLiteral("localGridView"), 10000),
                     "the local grid never became visible with the setting on");

            // The refusal arrives as a banner first, which is a second popup left standing open
            // underneath the modal one -- the state the accept button is actually pressed in
            QGC::showCriticalVehicleMessage(QStringLiteral("Arm: RC not found"));

            const QString message = QStringLiteral("Unable to start mission: Vehicle failed to arm.");

            /// Raises the dialog and returns its accept button, or nullptr once the test has failed.
            const auto raiseDialog = [this, &message]() -> QQuickItem* {
                expectAppMessage(QRegularExpression(QRegularExpression::escape(message)));
                QGC::showAppMessage(message);
                verifyExpectedLogMessage();
                return findVisibleItem(_rootItem, QStringLiteral("popupDialog_acceptButton"), 3000);
            };

            // --- The mouse pass, which is the behaviour being matched ---

            QVERIFY2(raiseDialog(), "the app message never raised a dialog to dismiss");
            QVERIFY2(acceptDialog(), "a mouse could not click the accept button");
            QTRY_VERIFY_WITH_TIMEOUT(!findVisibleItem(_rootItem, QStringLiteral("popupDialog_acceptButton"), 100),
                                     TestTimeout::longMs());

            // --- The same dismissal from a finger ---

            QQuickItem* const acceptButton = raiseDialog();
            QVERIFY2(acceptButton, "the app message never raised a second dialog to dismiss");

            // The dialog itself, so the tap can be counted rather than only seen to have closed it.
            // The button reaches the accept through two routes -- its own clicked() and the touch
            // handler beside it -- and a tap that travels both accepts twice, which on a dialog
            // guarding something destructive does the thing twice.
            QObject* dialog = nullptr;
            for (QObject* ancestor = acceptButton; ancestor; ancestor = ancestor->parent()) {
                if (ancestor->metaObject()->indexOfSignal("accepted()") >= 0) {
                    dialog = ancestor;
                    break;
                }
            }
            QVERIFY2(dialog, "the accept button has no dialog above it to count accepts on");
            QSignalSpy acceptSpy(dialog, SIGNAL(accepted()));

            tapWithAFinger(_window, acceptButton);

            QVERIFY2(waitForCondition(
                         [this] {
                             return findVisibleItem(_rootItem, QStringLiteral("popupDialog_acceptButton"), 100) ==
                                    nullptr;
                         },
                         TestTimeout::longMs(), QStringLiteral("dialog dismissed by a finger")),
                     "a tap on the accept button left the dialog open, though a mouse click closes it");

            QCOMPARE(acceptSpy.count(), 1);

            // The probe overlay this branch carries has to be recording, or the build shipped to
            // read a device with is a build with an empty panel on it
            QQuickItem* const probeLog = findVisibleItem(_rootItem, QStringLiteral("touchProbe_log"), 2000);
            QVERIFY2(probeLog, "the touch probe overlay is not in the window");
            QVERIFY2(probeLog->property("text").toString() != QStringLiteral("(nothing yet)"),
                     "the touch probe recorded no pointer events at all");
        });
}

/// Closing the app is the one dialog an operator cannot walk away from.
///
/// With nothing unsaved the app just closes, so the warning never appears and a touch screen never
/// meets it. Open a plan and the "Unsaved Mission" warning stands in the way -- and it is raised
/// with Yes/No, which means closePolicy stays NoAutoClose and Escape does not dismiss it either.
/// On a tablet, with the buttons unreachable and no key to press, that dialog is a dead end: the
/// app can no longer be closed at all.
void PopupDialogTouchUITest::_aFingerCanAnswerTheWarningThatStandsBetweenTheAppAndClosing_test()
{
    // Incidental one-time startup message when the map cache DB is upgraded.
    ignoreLogMessage("API.QGCApplication.AppMessage", QtDebugMsg,
                     QRegularExpression(QStringLiteral("Offline Map Cache database has been upgraded")));

    startUI();
    if (QTest::currentTestFailed()) {
        return;
    }

    // Accepting the warning runs the close through, which shuts the links down and closes the
    // window itself, so only the engine is left to tear down.
    bool appClosed = false;
    const auto guard = qScopeGuard([&] {
        if (!appClosed) {
            closeUIWindow();
        }
        destroyUIEngine();
    });

    // No vehicle is connected, so an upload cannot have happened and a dirty plan is enough to
    // raise the warning on its own -- the state the operator was in with the mission that failed.
    if (!forcePlanViewMissionDirty(_window)) {
        return;
    }

    // The close is started with a mouse: what is under test is the warning's buttons, not the
    // toolbar entry that raises it.
    QVERIFY2(clickToolSelectDropdownButton(QStringLiteral("toolbar_viewClose")),
             "Failed to click Close button in tool select dropdown");
    QVERIFY2(waitForDialog(QStringLiteral("Unsaved Mission")), "the close raised no unsaved-mission warning");

    QQuickItem* const acceptButton = findVisibleItem(_rootItem, QStringLiteral("popupDialog_acceptButton"), 3000);
    QVERIFY2(acceptButton, "the unsaved-mission warning has no accept button to answer it with");

    tapWithAFinger(_window, acceptButton);

    QVERIFY2(QTest::qWaitFor([this] { return _window && !_window->isVisible(); }, TestTimeout::longMs()),
             "the app did not close after a finger answered the unsaved-mission warning");
    appClosed = true;
}
