#pragma once

#include "BaseClasses/VehicleTest.h"

/// Guards the data contract of NonGpsStatusPanel.qml. The panel reads its values straight off
/// vehicle fact groups, and QML resolves those names at runtime, so a renamed or removed fact
/// shows up as an empty panel rather than as a build failure. This test fails instead.
class NonGpsStatusPanelTest : public VehicleTest
{
    Q_OBJECT

private slots:
    void _panelFactsExist_test();
    void _panelVehicleValuesExist_test();
    void _panelBuildsWithoutBindingErrors_test();
    void _magRatioStartsUnknownRatherThanZero_test();
    void _rangefinderZeroIsNotColouredAsAReading_test();
};
