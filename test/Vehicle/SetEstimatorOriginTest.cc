#include "SetEstimatorOriginTest.h"

#include <QtCore/QtNumeric>
#include <QtPositioning/QGeoCoordinate>

#include "FirmwarePlugin.h"
#include "MockLink.h"
#include "Vehicle.h"

namespace {
using CommandSupportedResult = FirmwarePluginInstanceData::CommandSupportedResult;

// Arbitrary valid origin (Zurich) used for all cases.
const QGeoCoordinate kOrigin(47.3977419, 8.5455938, 488.0);

// What the Fly view map click actually hands to setEstimatorOrigin: same place, but built
// from a screen position, so it carries no altitude at all and altitude() is NaN.
const QGeoCoordinate kMapClickOrigin(47.3977419, 8.5455938);
}

/// When the command is already known to be unsupported, setEstimatorOrigin must skip the
/// command entirely and send the deprecated SET_GPS_GLOBAL_ORIGIN message directly.
void SetEstimatorOriginTest::_cachedUnsupported_sendsLegacyMessageOnly()
{
    QVERIFY(_vehicle);
    FirmwarePluginInstanceData* instanceData = _vehicle->firmwarePluginInstanceData();
    QVERIFY(instanceData);

    instanceData->setCommandSupported(MAV_CMD_DO_SET_GLOBAL_ORIGIN, CommandSupportedResult::UNSUPPORTED);

    _mockLink->clearReceivedMavCommandCounts();
    _mockLink->clearReceivedMavlinkMessageCounts();
    _vehicle->setEstimatorOrigin(kOrigin);

    QVERIFY_TRUE_WAIT(_mockLink->receivedMavlinkMessageCount(MAVLINK_MSG_ID_SET_GPS_GLOBAL_ORIGIN) == 1,
                      TestTimeout::longMs());
    QCOMPARE(_mockLink->receivedMavCommandCount(MAV_CMD_DO_SET_GLOBAL_ORIGIN), 0);
}

/// When the command is already known to be supported, setEstimatorOrigin must send it as a
/// COMMAND_INT and must never fall back to the deprecated message (even if the vehicle NAKs).
void SetEstimatorOriginTest::_cachedSupported_sendsCommandIntOnly()
{
    QVERIFY(_vehicle);
    FirmwarePluginInstanceData* instanceData = _vehicle->firmwarePluginInstanceData();
    QVERIFY(instanceData);

    instanceData->setCommandSupported(MAV_CMD_DO_SET_GLOBAL_ORIGIN, CommandSupportedResult::SUPPORTED);

    _mockLink->clearReceivedMavCommandCounts();
    _mockLink->clearReceivedMavlinkMessageCounts();
    // No fallback handler is installed on this path, so MockLink's NAK reaches the generic error
    // reporting. setEstimatorOrigin asks for that reporting on purpose: an origin the vehicle
    // refuses used to be swallowed whole, leaving the estimator silently without an origin.
    expectAppMessage(QRegularExpression(QStringLiteral("command not supported")));
    _vehicle->setEstimatorOrigin(kOrigin);

    QVERIFY_TRUE_WAIT(_mockLink->receivedMavCommandCount(MAV_CMD_DO_SET_GLOBAL_ORIGIN) == 1,
                      TestTimeout::longMs());
    QCOMPARE(_mockLink->receivedMavlinkMessageCount(MAVLINK_MSG_ID_SET_GPS_GLOBAL_ORIGIN), 0);
    // The cached-supported path installs no fallback handler, so the vehicle's UNSUPPORTED ack
    // must not re-cache the command or trigger the legacy message.
    QVERIFY(instanceData->getCommandSupported(MAV_CMD_DO_SET_GLOBAL_ORIGIN) == CommandSupportedResult::SUPPORTED);
    verifyExpectedLogMessage();
}

/// With support unknown, setEstimatorOrigin probes with a COMMAND_INT. MockLink NAKs it with
/// MAV_RESULT_UNSUPPORTED, which must trigger the SET_GPS_GLOBAL_ORIGIN fallback and cache the
/// command as unsupported so subsequent calls skip the probe.
void SetEstimatorOriginTest::_probeUnsupported_fallsBackAndCachesUnsupported()
{
    QVERIFY(_vehicle);
    FirmwarePluginInstanceData* instanceData = _vehicle->firmwarePluginInstanceData();
    QVERIFY(instanceData);
    QVERIFY(instanceData->getCommandSupported(MAV_CMD_DO_SET_GLOBAL_ORIGIN) == CommandSupportedResult::UNKNOWN);

    _mockLink->clearReceivedMavCommandCounts();
    _mockLink->clearReceivedMavlinkMessageCounts();
    _vehicle->setEstimatorOrigin(kOrigin);

    // Probe goes out as a COMMAND_INT, then the legacy message is sent once the NAK arrives.
    QVERIFY_TRUE_WAIT(_mockLink->receivedMavlinkMessageCount(MAVLINK_MSG_ID_SET_GPS_GLOBAL_ORIGIN) == 1,
                      TestTimeout::longMs());
    QCOMPARE(_mockLink->receivedMavCommandCount(MAV_CMD_DO_SET_GLOBAL_ORIGIN), 1);
    QVERIFY(instanceData->getCommandSupported(MAV_CMD_DO_SET_GLOBAL_ORIGIN) == CommandSupportedResult::UNSUPPORTED);

    // Now that it is cached unsupported, a second call must skip the command and go straight to
    // the legacy message.
    _mockLink->clearReceivedMavCommandCounts();
    _mockLink->clearReceivedMavlinkMessageCounts();
    _vehicle->setEstimatorOrigin(kOrigin);

    QVERIFY_TRUE_WAIT(_mockLink->receivedMavlinkMessageCount(MAVLINK_MSG_ID_SET_GPS_GLOBAL_ORIGIN) == 1,
                      TestTimeout::longMs());
    QCOMPARE(_mockLink->receivedMavCommandCount(MAV_CMD_DO_SET_GLOBAL_ORIGIN), 0);
}

/// A map click carries no altitude, so centerCoord.altitude() is NaN. The COMMAND_INT must still
/// go out with a finite param7: an autopilot rejects a NaN location outright (ArduPilot answers
/// MAV_RESULT_DENIED from location_from_command_t), which left the origin silently unset.
void SetEstimatorOriginTest::_mapClickCoordinate_commandCarriesFiniteAltitude()
{
    QVERIFY(_vehicle);
    FirmwarePluginInstanceData* instanceData = _vehicle->firmwarePluginInstanceData();
    QVERIFY(instanceData);

    // Guard the premise: this is the coordinate shape the Fly view really produces.
    QVERIFY(kMapClickOrigin.isValid());
    QVERIFY(qIsNaN(kMapClickOrigin.altitude()));

    instanceData->setCommandSupported(MAV_CMD_DO_SET_GLOBAL_ORIGIN, CommandSupportedResult::SUPPORTED);

    _mockLink->clearReceivedMavCommandCounts();
    _mockLink->clearReceivedMavlinkMessageCounts();
    expectAppMessage(QRegularExpression(QStringLiteral("command not supported")));
    _vehicle->setEstimatorOrigin(kMapClickOrigin);

    QVERIFY_TRUE_WAIT(_mockLink->receivedMavCommandCount(MAV_CMD_DO_SET_GLOBAL_ORIGIN) == 1,
                      TestTimeout::longMs());

    mavlink_message_t message{};
    QVERIFY(_mockLink->lastReceivedMavlinkMessage(MAVLINK_MSG_ID_COMMAND_INT, message));
    mavlink_command_int_t commandInt{};
    mavlink_msg_command_int_decode(&message, &commandInt);

    QCOMPARE(commandInt.command, static_cast<uint16_t>(MAV_CMD_DO_SET_GLOBAL_ORIGIN));
    QVERIFY(qIsFinite(commandInt.z));
    // The position itself must survive the altitude substitution untouched.
    QCOMPARE(commandInt.x, static_cast<int32_t>(kMapClickOrigin.latitude() * 1e7));
    QCOMPARE(commandInt.y, static_cast<int32_t>(kMapClickOrigin.longitude() * 1e7));
    verifyExpectedLogMessage();
}

/// The deprecated fallback message packs the altitude into an int32 of millimetres, where a NaN is
/// undefined behaviour rather than an honest value. It must receive a finite altitude too.
///
/// Note on strength: unlike its COMMAND_INT sibling this case does not discriminate on every
/// platform. Converting a NaN double to int32 is undefined, and arm64 happens to saturate it to
/// the same 0 the fix produces, so the assertion below passes on this host even against the
/// unfixed code. It is kept as a wire-format guard -- an altitude-less click must travel as 0 mm,
/// not as some future non-zero substitute. The authoritative regression guard for the NaN bug is
/// _mapClickCoordinate_commandCarriesFiniteAltitude, which fails on any platform without the fix.
void SetEstimatorOriginTest::_mapClickCoordinate_legacyMessageCarriesFiniteAltitude()
{
    QVERIFY(_vehicle);
    FirmwarePluginInstanceData* instanceData = _vehicle->firmwarePluginInstanceData();
    QVERIFY(instanceData);

    instanceData->setCommandSupported(MAV_CMD_DO_SET_GLOBAL_ORIGIN, CommandSupportedResult::UNSUPPORTED);

    _mockLink->clearReceivedMavCommandCounts();
    _mockLink->clearReceivedMavlinkMessageCounts();
    _vehicle->setEstimatorOrigin(kMapClickOrigin);

    QVERIFY_TRUE_WAIT(_mockLink->receivedMavlinkMessageCount(MAVLINK_MSG_ID_SET_GPS_GLOBAL_ORIGIN) == 1,
                      TestTimeout::longMs());

    mavlink_message_t message{};
    QVERIFY(_mockLink->lastReceivedMavlinkMessage(MAVLINK_MSG_ID_SET_GPS_GLOBAL_ORIGIN, message));
    mavlink_set_gps_global_origin_t origin{};
    mavlink_msg_set_gps_global_origin_decode(&message, &origin);

    QCOMPARE(origin.altitude, 0);
    QCOMPARE(origin.latitude, static_cast<int32_t>(kMapClickOrigin.latitude() * 1e7));
    QCOMPARE(origin.longitude, static_cast<int32_t>(kMapClickOrigin.longitude() * 1e7));
}

/// An invalid coordinate must be dropped rather than sent as a zeroed origin, which the vehicle
/// would happily accept as a real location somewhere off West Africa.
void SetEstimatorOriginTest::_invalidCoordinate_sendsNothing()
{
    QVERIFY(_vehicle);
    FirmwarePluginInstanceData* instanceData = _vehicle->firmwarePluginInstanceData();
    QVERIFY(instanceData);

    instanceData->setCommandSupported(MAV_CMD_DO_SET_GLOBAL_ORIGIN, CommandSupportedResult::SUPPORTED);

    _mockLink->clearReceivedMavCommandCounts();
    _mockLink->clearReceivedMavlinkMessageCounts();

    _vehicle->setEstimatorOrigin(QGeoCoordinate());

    // Nothing is expected on the wire, so wait on a call that does send: once the valid origin
    // has arrived, anything the invalid one might have queued would have arrived before it.
    expectAppMessage(QRegularExpression(QStringLiteral("command not supported")));
    _vehicle->setEstimatorOrigin(kMapClickOrigin);
    QVERIFY_TRUE_WAIT(_mockLink->receivedMavCommandCount(MAV_CMD_DO_SET_GLOBAL_ORIGIN) == 1,
                      TestTimeout::longMs());

    QCOMPARE(_mockLink->receivedMavCommandCount(MAV_CMD_DO_SET_GLOBAL_ORIGIN), 1);
    QCOMPARE(_mockLink->receivedMavlinkMessageCount(MAVLINK_MSG_ID_SET_GPS_GLOBAL_ORIGIN), 0);
    verifyExpectedLogMessage();
}

/// Until the vehicle reports an origin, Vehicle::estimatorOrigin must stay invalid. A vehicle
/// without one answers the request with zeros, and reading that as a real coordinate would put the
/// origin in the Gulf of Guinea -- and make QGC claim an origin exists when none does.
void SetEstimatorOriginTest::_vehicleWithoutOrigin_reportsInvalidCoordinate()
{
    QVERIFY(_vehicle);
    QVERIFY(!_vehicle->estimatorOrigin().isValid());
}

/// Once an origin is set, the vehicle reports it and Vehicle must surface it. This is the state
/// that decides whether a mission can fly at all: without an origin the vehicle has no home, a
/// takeoff to an altitude relative to home never completes, and the mission stalls on its first
/// item with nothing shown to the user.
void SetEstimatorOriginTest::_originSetOnVehicle_isReportedBack()
{
    QVERIFY(_vehicle);
    FirmwarePluginInstanceData* instanceData = _vehicle->firmwarePluginInstanceData();
    QVERIFY(instanceData);
    QVERIFY(!_vehicle->estimatorOrigin().isValid());

    // Cached-unsupported drives the legacy SET_GPS_GLOBAL_ORIGIN message, which MockLink records.
    instanceData->setCommandSupported(MAV_CMD_DO_SET_GLOBAL_ORIGIN, CommandSupportedResult::UNSUPPORTED);
    _vehicle->setEstimatorOrigin(kOrigin);
    QVERIFY_TRUE_WAIT(_mockLink->receivedMavlinkMessageCount(MAVLINK_MSG_ID_SET_GPS_GLOBAL_ORIGIN) == 1,
                      TestTimeout::longMs());

    QSignalSpy originSpy(_vehicle, &Vehicle::estimatorOriginChanged);
    _vehicle->requestEstimatorOrigin();

    QVERIFY_TRUE_WAIT(_vehicle->estimatorOrigin().isValid(), TestTimeout::longMs());
    QCOMPARE(originSpy.count(), 1);
    QVERIFY(qAbs(_vehicle->estimatorOrigin().latitude() - kOrigin.latitude()) < 0.0000001);
    QVERIFY(qAbs(_vehicle->estimatorOrigin().longitude() - kOrigin.longitude()) < 0.0000001);
}

/// A vehicle that has lost its origin does not say so -- ArduPilot answers a request for
/// GPS_GLOBAL_ORIGIN with silence when it has none. Requesting must therefore drop what was known
/// first, or QGC keeps reporting an origin from a vehicle that rebooted or was replaced. Caught in
/// SITL on 8 Aug: the panel read "Set" with a previous run's coordinate while the live vehicle had
/// none, and the mission failed at takeoff with an ArduPilot internal error for an uninitialised
/// current location.
void SetEstimatorOriginTest::_requestAfterOriginLost_clearsStaleValue()
{
    QVERIFY(_vehicle);
    FirmwarePluginInstanceData* instanceData = _vehicle->firmwarePluginInstanceData();
    QVERIFY(instanceData);

    instanceData->setCommandSupported(MAV_CMD_DO_SET_GLOBAL_ORIGIN, CommandSupportedResult::UNSUPPORTED);
    _vehicle->setEstimatorOrigin(kOrigin);
    QVERIFY_TRUE_WAIT(_mockLink->receivedMavlinkMessageCount(MAVLINK_MSG_ID_SET_GPS_GLOBAL_ORIGIN) == 1,
                      TestTimeout::longMs());
    _vehicle->requestEstimatorOrigin();
    QVERIFY_TRUE_WAIT(_vehicle->estimatorOrigin().isValid(), TestTimeout::longMs());

    // The vehicle loses its origin, exactly as a reboot would leave it.
    _mockLink->clearEstimatorOrigin();

    _vehicle->requestEstimatorOrigin();
    QVERIFY_TRUE_WAIT(!_vehicle->estimatorOrigin().isValid(), TestTimeout::longMs());
}

UT_REGISTER_TEST(SetEstimatorOriginTest, TestLabel::Integration, TestLabel::Vehicle)
