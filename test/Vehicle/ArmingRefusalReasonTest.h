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
};
