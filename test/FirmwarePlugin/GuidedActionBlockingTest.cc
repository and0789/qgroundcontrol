#include "GuidedActionBlockingTest.h"

#include <QtCore/QElapsedTimer>
#include <QtCore/QRegularExpression>

#include <algorithm>

#include "LogManager.h"
#include "MockLink.h"
#include "Vehicle.h"

UT_REGISTER_TEST(GuidedActionBlockingTest, TestLabel::Integration, TestLabel::Vehicle)

namespace {

/// A mission start that waits on the vehicle inline cannot be back inside this.
///
/// Against a mock that refuses to arm the old waits added up to at least 1.6 seconds -- a mode
/// change answered on the next heartbeat, then the arm timeout in full -- and against one that
/// answers nothing at all, 5.4. The gap between that and a call that only queues MAVLink is wide
/// enough that a loaded machine cannot close it.
constexpr int kPromptAnswerMs = 500;

const char* const kArmRefusedMessage = "Unable to start mission: Vehicle rejected arming.";

bool appMessageCaptured(const QString& message)
{
    const auto messages = LogManager::capturedMessages();
    return std::any_of(messages.cbegin(), messages.cend(),
                       [&message](const LogEntry& entry) { return entry.message.contains(message); });
}

}  // namespace

/// The regression this file exists for.
///
/// Both halves are needed and neither is enough alone. Coming back at once is what says the wait is
/// no longer being done inline; the refusal still reaching the operator is what says the wait is
/// still being done at all, since an action that gave up waiting would pass the first assertion on
/// its own.
void GuidedActionBlockingTest::_aRefusedMissionStart_answersWithoutHoldingItsCaller()
{
    QVERIFY(vehicle());
    QVERIFY(!vehicle()->armed());

    // The state an operator is in when a pre-arm check is failing: the mode change goes through and
    // the arm behind it is refused, which is where the old code spent the longest.
    mockLink()->setPrearmCheckFailing(true);

    const QString armRefused = QString::fromLatin1(kArmRefusedMessage);
    expectAppMessage(QRegularExpression(QRegularExpression::escape(armRefused)));

    QElapsedTimer callDuration;
    callDuration.start();
    vehicle()->startMission();
    const qint64 elapsedMs = callDuration.elapsed();

    QVERIFY2(elapsedMs < kPromptAnswerMs,
             qPrintable(QStringLiteral("startMission() held its caller for %1ms").arg(elapsedMs)));

    QVERIFY_TRUE_WAIT(appMessageCaptured(armRefused), TestTimeout::mediumMs());
    verifyExpectedLogMessage();

    QVERIFY(!vehicle()->armed());
}

/// The same sequence when the vehicle accepts it, which the restructuring had to leave intact. An
/// armed vehicle skips the arm entirely, so what is left is the mode change -- still waited on, and
/// still waited on somewhere other than the caller's stack.
void GuidedActionBlockingTest::_anArmedMissionStart_reachesMissionModeWithoutHoldingItsCaller()
{
    QVERIFY(vehicle());

    mockLink()->setArmed(true);
    QVERIFY_TRUE_WAIT(vehicle()->armed(), TestTimeout::mediumMs());

    QElapsedTimer callDuration;
    callDuration.start();
    vehicle()->startMission();
    const qint64 elapsedMs = callDuration.elapsed();

    QVERIFY2(elapsedMs < kPromptAnswerMs,
             qPrintable(QStringLiteral("startMission() held its caller for %1ms").arg(elapsedMs)));

    QVERIFY_TRUE_WAIT(vehicle()->flightMode() == QStringLiteral("Mission"), TestTimeout::mediumMs());
    QVERIFY(vehicle()->armed());
}
