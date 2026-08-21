#include "ArmingRefusalReasonTest.h"

#include <QtCore/QElapsedTimer>
#include <QtCore/QRegularExpression>
#include <QtTest/QSignalSpy>

#include "MockLink.h"
#include "Vehicle.h"

UT_REGISTER_TEST(ArmingRefusalReasonTest, TestLabel::Integration, TestLabel::Vehicle)
UT_REGISTER_TEST(ArmingRefusalReasonTestAPM, TestLabel::Integration, TestLabel::Vehicle)

namespace {

/// What ArduPilot sends while the vehicle sits there with a check failing
const char *const kPrearmText = "PreArm: Need Position Estimate";

/// The same failing check, named the way ArduPilot names it during an arming attempt
const char *const kArmText = "Arm: Need Position Estimate";

/// Waits for the vehicle's answer to \a command and returns its MAV_RESULT, or -1 if none arrived.
///
/// Results for other commands are stepped over rather than waited on: a connected vehicle has
/// traffic of its own, and the one being waited for is not always the next to land.
int waitForCommandResult(QSignalSpy &resultSpy, MAV_CMD command)
{
    QElapsedTimer elapsedTimer;
    elapsedTimer.start();

    while (true) {
        const int remainingMs = TestTimeout::longMs() - static_cast<int>(elapsedTimer.elapsed());
        if (remainingMs <= 0) {
            return -1;
        }
        if (resultSpy.isEmpty() && !resultSpy.wait(remainingMs)) {
            return -1;
        }

        const QList<QVariant> arguments = resultSpy.takeFirst();
        if ((arguments.count() == 5) && (arguments.at(2).toInt() == command)) {
            return arguments.at(3).toInt();
        }
    }
}

} // namespace

void ArmingRefusalReasonTest::_prearmPrefixedText_becomesTheRefusalReason()
{
    QVERIFY(_vehicle);
    QVERIFY(_vehicle->prearmError().isEmpty());

    _mockLink->sendStatusTextMessage(MAV_SEVERITY_CRITICAL, QString::fromLatin1(kPrearmText));

    QVERIFY_TRUE_WAIT(_vehicle->prearmError() == QString::fromLatin1(kPrearmText), TestTimeout::longMs());
}

/// The regression this whole file exists for. An operator presses arm, ArduPilot refuses and says
/// exactly why -- under the other prefix, because it is mid-attempt. Recognising only "PreArm" left
/// that reason nowhere on screen at the one moment it was asked for.
void ArmingRefusalReasonTest::_armPrefixedText_becomesTheRefusalReason()
{
    QVERIFY(_vehicle);
    QVERIFY(_vehicle->prearmError().isEmpty());

    _mockLink->sendStatusTextMessage(MAV_SEVERITY_CRITICAL, QString::fromLatin1(kArmText));

    QVERIFY_TRUE_WAIT(_vehicle->prearmError() == QString::fromLatin1(kArmText), TestTimeout::longMs());
}

/// "Arm" is a common enough word that matching it loosely would turn ordinary chatter into a standing
/// arming warning. Only the "Arm: " ArduPilot actually tags a failed check with counts.
void ArmingRefusalReasonTest::_ordinaryText_isNotMistakenForARefusal()
{
    QVERIFY(_vehicle);

    _mockLink->sendStatusTextMessage(MAV_SEVERITY_INFO, QStringLiteral("Armed by rudder input"));
    _mockLink->sendStatusTextMessage(MAV_SEVERITY_INFO, QStringLiteral("Arming motors"));

    // Nothing to wait for, so let the messages land and then assert they changed nothing
    QTest::qWait(TestTimeout::shortMs() / 4);
    QVERIFY2(_vehicle->prearmError().isEmpty(),
             qPrintable(QStringLiteral("ordinary chatter was read as a refusal: \"%1\"")
                            .arg(_vehicle->prearmError())));
}

/// Waiting for the autopilot to volunteer a reason is not a plan: the cycle is thirty seconds and
/// ARMING_OPTIONS bit 0 stops it entirely. This is the ask that gets an answer on demand.
void ArmingRefusalReasonTest::_requestingAReport_asksTheAutopilotToRunItsChecks()
{
    QVERIFY(_vehicle);
    QVERIFY(!_vehicle->armed());

    _mockLink->clearReceivedMavCommandCounts();
    _vehicle->requestPrearmCheckReport();

    QVERIFY_TRUE_WAIT(_mockLink->receivedMavCommandCount(MAV_CMD_RUN_PREARM_CHECKS) == 1,
                      TestTimeout::longMs());
}

/// Armed, there is no pre-arm check to run and ArduPilot rejects the command outright. Asking anyway
/// would spend a command to be told so.
void ArmingRefusalReasonTest::_requestingAReportWhileArmed_asksNothing()
{
    QVERIFY(_vehicle);

    _vehicle->setArmed(true, false /* showError */);
    QVERIFY_TRUE_WAIT(_vehicle->armed(), TestTimeout::longMs());

    _mockLink->clearReceivedMavCommandCounts();
    _vehicle->requestPrearmCheckReport();

    QTest::qWait(TestTimeout::shortMs() / 4);
    QCOMPARE(_mockLink->receivedMavCommandCount(MAV_CMD_RUN_PREARM_CHECKS), 0);

    _vehicle->setArmed(false, false /* showError */);
    QVERIFY_TRUE_WAIT(!_vehicle->armed(), TestTimeout::longMs());
}

/// The steady half of the answer. SYS_STATUS carries the pre-arm check on every frame, so the bit
/// going down is a refusal that holds -- and it is the only thing that does, since the reason it
/// stands for may be half a minute away or never coming.
void ArmingRefusalReasonTest::_theCheckBitAlone_isARefusal()
{
    QVERIFY(_vehicle);
    QVERIFY(!_vehicle->armingBlocked());

    _mockLink->setPrearmCheckFailing(true);

    QVERIFY_TRUE_WAIT(_vehicle->armingBlocked(), TestTimeout::longMs());
    QVERIFY2(_vehicle->prearmError().isEmpty(),
             "the bit alone was supposed to be enough, but a reason had arrived as well");
}

/// The other half, and the reason the bit is not simply read on its own: a vehicle that never
/// publishes the check bit is not a vehicle reporting itself willing. A reason having been sent is
/// itself proof the autopilot refused.
void ArmingRefusalReasonTest::_theReasonAlone_isARefusal()
{
    QVERIFY(_vehicle);
    QVERIFY(!_vehicle->armingBlocked());

    // The stock mock publishes no pre-arm bit at all, so this is a refusal stated only in words
    _mockLink->sendStatusTextMessage(MAV_SEVERITY_CRITICAL, QString::fromLatin1(kPrearmText));

    QVERIFY_TRUE_WAIT(_vehicle->armingBlocked(), TestTimeout::longMs());
}

/// Armed is the one state that settles it. Whatever the autopilot said on the way here, it is not
/// refusing now, and a warning that outlives what it warns about is worse than none.
void ArmingRefusalReasonTest::_arming_endsTheRefusal()
{
    QVERIFY(_vehicle);

    _mockLink->sendStatusTextMessage(MAV_SEVERITY_CRITICAL, QString::fromLatin1(kPrearmText));
    QVERIFY_TRUE_WAIT(_vehicle->armingBlocked(), TestTimeout::longMs());

    _vehicle->setArmed(true, false /* showError */);
    QVERIFY_TRUE_WAIT(_vehicle->armed(), TestTimeout::longMs());

    QVERIFY_TRUE_WAIT(!_vehicle->armingBlocked(), TestTimeout::longMs());
    QVERIFY2(!_vehicle->prearmError().isEmpty(),
             "the reason was dropped as well, when only the refusal it stood for had ended");

    _vehicle->setArmed(false, false /* showError */);
    QVERIFY_TRUE_WAIT(!_vehicle->armed(), TestTimeout::longMs());
}

/// The ask happens on the vehicle's own account. It used to belong to a panel in the fly view, which
/// meant the question was only put while that panel was on screen -- and the operator who needs the
/// answer is the one who has just pressed arm somewhere else.
void ArmingRefusalReasonTest::_aSilentRefusal_makesTheVehicleAskWhy()
{
    QVERIFY(_vehicle);

    _mockLink->clearReceivedMavCommandCounts();
    _mockLink->setPrearmCheckFailing(true);

    QVERIFY_TRUE_WAIT(_mockLink->receivedMavCommandCount(MAV_CMD_RUN_PREARM_CHECKS) >= 1,
                      TestTimeout::longMs());
}

/// PX4 has no such command. Asking it every ten seconds for the rest of the flight would be a
/// question that has already been answered as fully as it ever will be.
void ArmingRefusalReasonTest::_aStackThatCannotReport_isNotAskedTwice()
{
    QVERIFY(_vehicle);

    _mockLink->clearReceivedMavCommandCounts();

    _vehicle->requestPrearmCheckReport();
    QVERIFY_TRUE_WAIT(_mockLink->receivedMavCommandCount(MAV_CMD_RUN_PREARM_CHECKS) == 1,
                      TestTimeout::longMs());

    // The ask carries its own result handler, and a command that has one is answered straight to
    // that handler without a result signal going out -- so there is nothing to spy on for the answer
    // landing. The queue says it instead: the entry is dropped once the answer has been taken in,
    // which is the moment after which the vehicle knows this stack cannot report.
    QVERIFY_TRUE_WAIT(!_vehicle->isMavCommandPending(_vehicle->defaultComponentId(), MAV_CMD_RUN_PREARM_CHECKS),
                      TestTimeout::longMs());

    // Asked again with the answer already in hand, and nothing goes out
    _vehicle->requestPrearmCheckReport();

    QTest::qWait(TestTimeout::shortMs() / 4);
    QCOMPARE(_mockLink->receivedMavCommandCount(MAV_CMD_RUN_PREARM_CHECKS), 1);
}

/// What the operator sees at the moment they ask. Pressing arm on a vehicle that will not arm used
/// to raise a modal saying the command failed and nothing else, with the reason sitting in a panel
/// behind it -- so the reason goes into the dialog that just said no.
void ArmingRefusalReasonTest::_aRefusedArm_carriesTheReasonIntoTheDialog()
{
    QVERIFY(_vehicle);

    _mockLink->sendStatusTextMessage(MAV_SEVERITY_CRITICAL, QString::fromLatin1(kPrearmText));
    QVERIFY_TRUE_WAIT(_vehicle->prearmError() == QString::fromLatin1(kPrearmText), TestTimeout::longMs());

    // A failing check is what makes the mock refuse the arm command, the same as on a real vehicle
    _mockLink->setPrearmCheckFailing(true);

    expectAppMessage(QRegularExpression(QStringLiteral("command failed.*%1").arg(QString::fromLatin1(kPrearmText)),
                                        QRegularExpression::DotMatchesEverythingOption));

    QSignalSpy resultSpy(_vehicle, &Vehicle::mavCommandResult);
    _vehicle->setArmed(true, true /* showError */);

    QCOMPARE(waitForCommandResult(resultSpy, MAV_CMD_COMPONENT_ARM_DISARM), static_cast<int>(MAV_RESULT_FAILED));
    verifyExpectedLogMessage();

    QVERIFY2(!_vehicle->armed(), "the vehicle armed despite refusing the command");
}

/// The whole loop, on the firmware that can close it: a check fails, nobody says why, QGC asks, and
/// the autopilot's own words for it come back and stay on screen.
void ArmingRefusalReasonTestAPM::_theAnswerToAnAsk_becomesTheReason()
{
    QVERIFY(_vehicle);
    QVERIFY(_vehicle->prearmError().isEmpty());

    _mockLink->clearReceivedMavCommandCounts();
    _mockLink->setPrearmCheckFailing(true);

    QVERIFY_TRUE_WAIT(_mockLink->receivedMavCommandCount(MAV_CMD_RUN_PREARM_CHECKS) >= 1,
                      TestTimeout::longMs());
    QVERIFY_TRUE_WAIT(_vehicle->prearmError() == QString::fromLatin1(MockLink::kPrearmCheckFailureText),
                      TestTimeout::longMs());
    QVERIFY2(_vehicle->armingBlocked(),
             "the reason arrived and the refusal it explains went away with it");
}
