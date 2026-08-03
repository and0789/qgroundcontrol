#include "NonGpsStatusPanelTest.h"

#include <QtTest/QTest>

#include "FactGroup.h"
#include "Vehicle.h"

namespace {

struct PanelFact_s {
    const char *factGroupName;
    const char *factName;
};

/// Every value NonGpsStatusPanel.qml displays, in the same order as the panel
constexpr PanelFact_s kPanelFacts[] = {
    { "opticalFlow", "quality" },
    { "opticalFlow", "flowCompMagnitude" },
    { "opticalFlow", "flowCompX" },
    { "opticalFlow", "flowCompY" },
    { "opticalFlow", "groundDistance" },
    { "distanceSensor", "rotationPitch270" },
    { "estimatorStatus", "goodHorizPosRelEstimate" },
    { "estimatorStatus", "goodHorizVelEstimate" },
    { "estimatorStatus", "goodConstPosModeEstimate" },
    { "estimatorStatus", "velRatio" },
    { "estimatorStatus", "horizPosRatio" },
    { "estimatorStatus", "haglRatio" },
    { "localPosition", "x" },
    { "localPosition", "y" },
    { "localPosition", "z" },
    { "localPosition", "vx" },
    { "localPosition", "vy" },
    { "vibration", "xAxis" },
    { "vibration", "yAxis" },
    { "vibration", "zAxis" },
};

} // namespace

void NonGpsStatusPanelTest::_panelFactsExist_test()
{
    QVERIFY(vehicle());

    for (const PanelFact_s &panelFact : kPanelFacts) {
        FactGroup *const factGroup = vehicle()->getFactGroup(QString::fromLatin1(panelFact.factGroupName));
        QVERIFY2(factGroup, panelFact.factGroupName);
        QVERIFY2(factGroup->factExists(QString::fromLatin1(panelFact.factName)),
                 qPrintable(QStringLiteral("%1.%2 is displayed by the panel but does not exist")
                                .arg(QLatin1String(panelFact.factGroupName), QLatin1String(panelFact.factName))));
    }
}

UT_REGISTER_TEST(NonGpsStatusPanelTest, TestLabel::Integration, TestLabel::Vehicle)
