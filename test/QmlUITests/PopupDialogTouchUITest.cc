#include "PopupDialogTouchUITest.h"

#include <QtCore/QRegularExpression>
#include <QtGui/QPointingDevice>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickWindow>
#include <QtTest/QSignalSpy>
#include <QtTest/QTest>

#include "AppMessages.h"
#include "FlyViewSettings.h"
#include "MockLink.h"
#include "SettingsManager.h"
#include "Vehicle.h"

UT_REGISTER_TEST(PopupDialogTouchUITest, TestLabel::Integration)

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

            const QPoint target =
                acceptButton->mapToScene(QPointF(acceptButton->width() / 2, acceptButton->height() / 2)).toPoint();

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

            // A fingertip is a contact patch rather than a point, and its centre wanders a few
            // pixels before it lifts. A tap that presses and releases on the same pixel is the one
            // gesture a hand never makes, so the release is offset from the press.
            QPointingDevice* const finger = QTest::createTouchDevice();
            {
                QTest::QTouchEventSequence tap = QTest::touchEvent(_window, finger);
                tap.press(0, target).commit();
                tap.move(0, target + QPoint(3, 4)).commit();
                tap.release(0, target + QPoint(3, 4)).commit();
            }

            QVERIFY2(waitForCondition(
                         [this] {
                             return findVisibleItem(_rootItem, QStringLiteral("popupDialog_acceptButton"), 100) ==
                                    nullptr;
                         },
                         TestTimeout::longMs(), QStringLiteral("dialog dismissed by a finger")),
                     "a tap on the accept button left the dialog open, though a mouse click closes it");

            QCOMPARE(acceptSpy.count(), 1);
        });
}
