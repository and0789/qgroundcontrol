#include "VehicleOpticalFlowFactGroup.h"
#include "Vehicle.h"

#include <cmath>

VehicleOpticalFlowFactGroup::VehicleOpticalFlowFactGroup(QObject *parent)
    : FactGroup(500, QStringLiteral(":/json/Vehicle/OpticalFlowFact.json"), parent)
{
    _addFact(&_qualityFact);
    _addFact(&_flowCompXFact);
    _addFact(&_flowCompYFact);
    _addFact(&_flowCompMagnitudeFact);
    _addFact(&_flowRateXFact);
    _addFact(&_flowRateYFact);
    _addFact(&_groundDistanceFact);

    _flowCompXFact.setRawValue(qQNaN());
    _flowCompYFact.setRawValue(qQNaN());
    _flowCompMagnitudeFact.setRawValue(qQNaN());
    _flowRateXFact.setRawValue(qQNaN());
    _flowRateYFact.setRawValue(qQNaN());
    _groundDistanceFact.setRawValue(qQNaN());
}

void VehicleOpticalFlowFactGroup::handleMessage(Vehicle *vehicle, const mavlink_message_t &message)
{
    Q_UNUSED(vehicle);

    switch (message.msgid) {
    case MAVLINK_MSG_ID_OPTICAL_FLOW:
        _handleOpticalFlow(message);
        break;
    case MAVLINK_MSG_ID_OPTICAL_FLOW_RAD:
        _handleOpticalFlowRad(message);
        break;
    default:
        return;
    }

    _setTelemetryAvailable(true);
}

void VehicleOpticalFlowFactGroup::_handleOpticalFlow(const mavlink_message_t &message)
{
    mavlink_optical_flow_t opticalFlow{};
    mavlink_msg_optical_flow_decode(&message, &opticalFlow);

    quality()->setRawValue(opticalFlow.quality);
    flowRateX()->setRawValue(opticalFlow.flow_rate_x);
    flowRateY()->setRawValue(opticalFlow.flow_rate_y);
    _setFlowComp(opticalFlow.flow_comp_m_x, opticalFlow.flow_comp_m_y);
    _setGroundDistance(opticalFlow.ground_distance);
}

void VehicleOpticalFlowFactGroup::_handleOpticalFlowRad(const mavlink_message_t &message)
{
    mavlink_optical_flow_rad_t opticalFlow{};
    mavlink_msg_optical_flow_rad_decode(&message, &opticalFlow);

    quality()->setRawValue(opticalFlow.quality);
    _setGroundDistance(opticalFlow.distance);

    // The integrated angles only become rates once divided by the interval they were integrated over.
    if (opticalFlow.integration_time_us == 0) {
        flowRateX()->setRawValue(qQNaN());
        flowRateY()->setRawValue(qQNaN());
        _setFlowComp(qQNaN(), qQNaN());
        return;
    }

    const double integrationTimeSecs = opticalFlow.integration_time_us / 1e6;
    const double rateX = opticalFlow.integrated_x / integrationTimeSecs;
    const double rateY = opticalFlow.integrated_y / integrationTimeSecs;

    flowRateX()->setRawValue(rateX);
    flowRateY()->setRawValue(rateY);
    _setFlowComp(rateX - (opticalFlow.integrated_xgyro / integrationTimeSecs),
                 rateY - (opticalFlow.integrated_ygyro / integrationTimeSecs));
}

void VehicleOpticalFlowFactGroup::_setFlowComp(double flowCompX, double flowCompY)
{
    this->flowCompX()->setRawValue(flowCompX);
    this->flowCompY()->setRawValue(flowCompY);
    flowCompMagnitude()->setRawValue(std::hypot(flowCompX, flowCompY));
}

void VehicleOpticalFlowFactGroup::_setGroundDistance(double groundDistance)
{
    // Both messages use a negative distance to signal that the distance is unknown.
    this->groundDistance()->setRawValue((groundDistance < 0) ? qQNaN() : groundDistance);
}
