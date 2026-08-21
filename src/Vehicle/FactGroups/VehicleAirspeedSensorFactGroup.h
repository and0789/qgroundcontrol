#pragma once

#include <QtCore/QTimer>

#include "FactGroup.h"

/// What a differential pressure airspeed sensor reports, straight from the sensor.
///
/// Vehicle::airSpeed comes from VFR_HUD, which on a multirotor is whatever the estimator settled
/// on -- and ArduPilot only lets the sensor drive that once ARSPD_USE is set and the sensor passes
/// its health check. The AIRSPEED message is sent for every enabled sensor regardless, so this is
/// the group to read when the question is what the sensor itself is measuring.
class VehicleAirspeedSensorFactGroup : public FactGroup
{
    Q_OBJECT
    Q_PROPERTY(Fact *airspeed       READ airspeed       CONSTANT)
    Q_PROPERTY(Fact *diffPressure   READ diffPressure   CONSTANT)
    Q_PROPERTY(Fact *temperature    READ temperature    CONSTANT)
    Q_PROPERTY(Fact *sensorId       READ sensorId       CONSTANT)
    Q_PROPERTY(Fact *healthy        READ healthy        CONSTANT)
    Q_PROPERTY(Fact *inUse          READ inUse          CONSTANT)
    Q_PROPERTY(bool available       READ available      NOTIFY availableChanged)

public:
    explicit VehicleAirspeedSensorFactGroup(QObject *parent = nullptr);

    /// True while a sensor is actually reporting.
    ///
    /// FactGroup::telemetryAvailable cannot answer this. Nothing ever sets it back to false, so once
    /// one AIRSPEED message has arrived it stays true for the life of the vehicle -- which would
    /// leave a panel on screen showing the last numbers a sensor gave before it was unplugged. This
    /// goes false when the messages stop.
    bool available() const { return _available; }

    Fact *airspeed() { return &_airspeedFact; }
    Fact *diffPressure() { return &_diffPressureFact; }
    Fact *temperature() { return &_temperatureFact; }
    Fact *sensorId() { return &_sensorIdFact; }
    Fact *healthy() { return &_healthyFact; }
    Fact *inUse() { return &_inUseFact; }

    // Overrides from FactGroup
    void handleMessage(Vehicle *vehicle, const mavlink_message_t &message) final;

signals:
    void availableChanged(bool available);

protected:
    void _handleAirspeed(const mavlink_message_t &message);
    void _setAvailable(bool available);

    /// How long the messages may stop for before the sensor counts as gone.
    ///
    /// AIRSPEED rides the RAW_SENSORS stream, which an operator on a slow link may well have turned
    /// down to 1 Hz, and a vehicle with two sensors alternates between them -- so the gap between two
    /// messages about the same sensor is already seconds rather than milliseconds. Long enough to
    /// ride out that and a few dropped packets; short enough that unplugging the sensor clears the
    /// panel while the operator is still standing over it wondering why.
    static const int _sensorTimeoutMSecs = 5 * 1000;

    bool _available = false;
    QTimer _sensorTimeoutTimer;

    Fact _airspeedFact = Fact(0, QStringLiteral("airspeed"), FactMetaData::valueTypeDouble);
    Fact _diffPressureFact = Fact(0, QStringLiteral("diffPressure"), FactMetaData::valueTypeDouble);
    Fact _temperatureFact = Fact(0, QStringLiteral("temperature"), FactMetaData::valueTypeDouble);
    Fact _sensorIdFact = Fact(0, QStringLiteral("sensorId"), FactMetaData::valueTypeUint8);
    Fact _healthyFact = Fact(0, QStringLiteral("healthy"), FactMetaData::valueTypeBool);
    Fact _inUseFact = Fact(0, QStringLiteral("inUse"), FactMetaData::valueTypeBool);
};
