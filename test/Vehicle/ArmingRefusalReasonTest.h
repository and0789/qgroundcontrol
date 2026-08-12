#pragma once

#include "BaseClasses/VehicleTest.h"

/// How the reason an autopilot refuses to arm reaches QGC.
///
/// Two things have to hold for an operator to be told why. The reason has to be recognised when the
/// vehicle volunteers it, and it has to be obtainable when the vehicle does not -- ArduPilot reports
/// on a thirty second cycle that ARMING_OPTIONS bit 0 can switch off entirely, so waiting is not a
/// plan.
///
/// The recognition half is the subtle one. ArduPilot names the same failing check two different ways
/// depending on when you ask: "PreArm: ..." while the vehicle sits there, and "Arm: ..." for the
/// whole of an arming attempt, because AP_Arming::arm() raises running_arming_checks before running
/// the pre-arm checks and check_failed() takes its prefix from that flag. Matching one name and not
/// the other loses the reason in precisely the case where somebody has just pressed arm and is
/// waiting to be told what went wrong.
///
/// Whether the vehicle is refusing at all is a second question, answered by armingBlocked. It is
/// asked separately because the two signals that answer it expire on different schedules: the
/// SYS_STATUS check bit lasts as long as the refusal does but never says why, and the reason is a
/// message QGC lets go of after thirty-five seconds. Either one alone is a refusal; neither alone is
/// the whole picture.
class ArmingRefusalReasonTest : public VehicleTest
{
    Q_OBJECT

public:
    explicit ArmingRefusalReasonTest(QObject *parent = nullptr) : VehicleTest(parent) {}

private slots:
    void _prearmPrefixedText_becomesTheRefusalReason();
    void _armPrefixedText_becomesTheRefusalReason();
    void _ordinaryText_isNotMistakenForARefusal();
    void _requestingAReport_asksTheAutopilotToRunItsChecks();
    void _requestingAReportWhileArmed_asksNothing();
    void _theCheckBitAlone_isARefusal();
    void _theReasonAlone_isARefusal();
    void _arming_endsTheRefusal();
    void _aSilentRefusal_makesTheVehicleAskWhy();
    void _aStackThatCannotReport_isNotAskedTwice();
    void _aRefusedArm_carriesTheReasonIntoTheDialog();
};

/// The same question put to the one firmware that answers it.
///
/// ArduPilot is alone in implementing MAV_CMD_RUN_PREARM_CHECKS, so it is the only vehicle where the
/// whole loop closes: the check bit goes down, QGC asks why without anybody pressing anything, and
/// the answer arrives as the status text a volunteered reason would have come in as. Everything up
/// to the ask can be tested against any stack; the answer coming back cannot.
class ArmingRefusalReasonTestAPM : public VehicleTestAPM
{
    Q_OBJECT

public:
    explicit ArmingRefusalReasonTestAPM(QObject *parent = nullptr) : VehicleTestAPM(parent) {}

private slots:
    void _theAnswerToAnAsk_becomesTheReason();
};
