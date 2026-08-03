#pragma once

#include "FactGroup.h"

/// Optical flow telemetry, primarily of interest for GPS-denied flight and flow sensor calibration.
///
/// Fed by both OPTICAL_FLOW (sent by ArduPilot) and OPTICAL_FLOW_RAD (sent by PX4). Those two
/// messages carry the same information in different forms, so OPTICAL_FLOW_RAD is converted from
/// integrated angles to rates and both end up in the same facts.
///
/// Note on the flowComp facts: the OPTICAL_FLOW spec labels flow_comp_m_x/y as m/s, but ArduPilot
/// fills them with (flow rate - body rate) in rad/s. The facts follow what the sender actually
/// provides, which is the gyro compensated flow rate.
class VehicleOpticalFlowFactGroup : public FactGroup
{
    Q_OBJECT
    Q_PROPERTY(Fact *quality            READ quality            CONSTANT)
    Q_PROPERTY(Fact *flowCompX          READ flowCompX          CONSTANT)
    Q_PROPERTY(Fact *flowCompY          READ flowCompY          CONSTANT)
    Q_PROPERTY(Fact *flowCompMagnitude  READ flowCompMagnitude  CONSTANT)
    Q_PROPERTY(Fact *flowRateX          READ flowRateX          CONSTANT)
    Q_PROPERTY(Fact *flowRateY          READ flowRateY          CONSTANT)
    Q_PROPERTY(Fact *groundDistance     READ groundDistance     CONSTANT)

public:
    explicit VehicleOpticalFlowFactGroup(QObject *parent = nullptr);

    Fact *quality() { return &_qualityFact; }
    Fact *flowCompX() { return &_flowCompXFact; }
    Fact *flowCompY() { return &_flowCompYFact; }
    Fact *flowCompMagnitude() { return &_flowCompMagnitudeFact; }
    Fact *flowRateX() { return &_flowRateXFact; }
    Fact *flowRateY() { return &_flowRateYFact; }
    Fact *groundDistance() { return &_groundDistanceFact; }

    // Overrides from FactGroup
    void handleMessage(Vehicle *vehicle, const mavlink_message_t &message) final;

private:
    void _handleOpticalFlow(const mavlink_message_t &message);
    void _handleOpticalFlowRad(const mavlink_message_t &message);
    void _setFlowComp(double flowCompX, double flowCompY);
    void _setGroundDistance(double groundDistance);

    Fact _qualityFact = Fact(0, QStringLiteral("quality"), FactMetaData::valueTypeUint32);
    Fact _flowCompXFact = Fact(0, QStringLiteral("flowCompX"), FactMetaData::valueTypeDouble);
    Fact _flowCompYFact = Fact(0, QStringLiteral("flowCompY"), FactMetaData::valueTypeDouble);
    Fact _flowCompMagnitudeFact = Fact(0, QStringLiteral("flowCompMagnitude"), FactMetaData::valueTypeDouble);
    Fact _flowRateXFact = Fact(0, QStringLiteral("flowRateX"), FactMetaData::valueTypeDouble);
    Fact _flowRateYFact = Fact(0, QStringLiteral("flowRateY"), FactMetaData::valueTypeDouble);
    Fact _groundDistanceFact = Fact(0, QStringLiteral("groundDistance"), FactMetaData::valueTypeDouble);
};
