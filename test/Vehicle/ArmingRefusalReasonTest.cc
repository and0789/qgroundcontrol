#include "ArmingRefusalReasonTest.h"

#include "MockLink.h"
#include "Vehicle.h"

UT_REGISTER_TEST(ArmingRefusalReasonTest, TestLabel::Integration)

namespace {

/// What ArduPilot sends while the vehicle sits there with a check failing
const char *const kPrearmText = "PreArm: Need Position Estimate";

/// The same failing check, named the way ArduPilot names it during an arming attempt
const char *const kArmText = "Arm: Need Position Estimate";

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
