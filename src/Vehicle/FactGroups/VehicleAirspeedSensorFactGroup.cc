#include "VehicleAirspeedSensorFactGroup.h"
#include "Vehicle.h"

VehicleAirspeedSensorFactGroup::VehicleAirspeedSensorFactGroup(QObject *parent)
    : FactGroup(1000, QStringLiteral(":/json/Vehicle/AirspeedSensorFact.json"), parent)
{
    _addFact(&_airspeedFact);
    _addFact(&_diffPressureFact);
    _addFact(&_temperatureFact);
    _addFact(&_sensorIdFact);
    _addFact(&_healthyFact);
    _addFact(&_inUseFact);

    _airspeedFact.setRawValue(std::numeric_limits<double>::quiet_NaN());
    _diffPressureFact.setRawValue(std::numeric_limits<double>::quiet_NaN());
    _temperatureFact.setRawValue(std::numeric_limits<double>::quiet_NaN());

    _sensorTimeoutTimer.setInterval(_sensorTimeoutMSecs);
    _sensorTimeoutTimer.setSingleShot(true);
    (void) connect(&_sensorTimeoutTimer, &QTimer::timeout, this, [this]() {
        // The readings go with the sensor. Left standing they are still numbers, and a number that
        // stopped being measured reads exactly like one that is not changing -- which on an airspeed
        // panel is the difference between a still day and an unplugged pitot.
        _airspeedFact.setRawValue(std::numeric_limits<double>::quiet_NaN());
        _diffPressureFact.setRawValue(std::numeric_limits<double>::quiet_NaN());
        _temperatureFact.setRawValue(std::numeric_limits<double>::quiet_NaN());
        _healthyFact.setRawValue(false);
        _inUseFact.setRawValue(false);
        _setAvailable(false);
    });
}

void VehicleAirspeedSensorFactGroup::_setAvailable(bool available)
{
    if (available != _available) {
        _available = available;
        emit availableChanged(_available);
    }
}

void VehicleAirspeedSensorFactGroup::handleMessage(Vehicle *vehicle, const mavlink_message_t &message)
{
    Q_UNUSED(vehicle);

    switch (message.msgid) {
    case MAVLINK_MSG_ID_AIRSPEED:
        _handleAirspeed(message);
        break;
    default:
        break;
    }
}

void VehicleAirspeedSensorFactGroup::_handleAirspeed(const mavlink_message_t &message)
{
    mavlink_airspeed_t airspeed{};
    mavlink_msg_airspeed_decode(&message, &airspeed);

    // A vehicle with more than one sensor sends them round-robin, one per send slot, so this group
    // shows whichever reported last. sensorId says which one that was rather than leaving the
    // reader to guess from numbers that alternate.
    _sensorIdFact.setRawValue(airspeed.id);

    _airspeedFact.setRawValue(airspeed.airspeed);

    // ArduPilot fills raw_press straight from AP_Airspeed::get_differential_pressure(), which is
    // Pascal, although the MAVLink field is documented as hPa. Reported here as sent, in Pascal.
    _diffPressureFact.setRawValue(airspeed.raw_press);

    // INT16_MAX is the message's way of saying the sensor gave no temperature
    if (airspeed.temperature == std::numeric_limits<int16_t>::max()) {
        _temperatureFact.setRawValue(std::numeric_limits<double>::quiet_NaN());
    } else {
        _temperatureFact.setRawValue(airspeed.temperature / 100.0);
    }

    _healthyFact.setRawValue((airspeed.flags & AIRSPEED_SENSOR_UNHEALTHY) == 0);
    _inUseFact.setRawValue((airspeed.flags & AIRSPEED_SENSOR_USING) != 0);

    _sensorTimeoutTimer.start();
    _setAvailable(true);

    _setTelemetryAvailable(true);
}
