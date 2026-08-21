#include "LocalGridTestSupport.h"

#include <QtPositioning/QGeoCoordinate>
#include <QtTest/QTest>

#include "FirmwarePlugin.h"
#include "MockLink.h"
#include "UnitTest.h"
#include "Vehicle.h"

namespace LocalGridTestSupport {

bool giveTheVehicleAnOrigin(Vehicle *vehicle, MockLink *mockLink)
{
    FirmwarePluginInstanceData *const instanceData = vehicle->firmwarePluginInstanceData();
    if (instanceData == nullptr) {
        return false;
    }

    instanceData->setCommandSupported(MAV_CMD_DO_SET_GLOBAL_ORIGIN,
                                      FirmwarePluginInstanceData::CommandSupportedResult::UNSUPPORTED);
    vehicle->setEstimatorOrigin(QGeoCoordinate(OriginLatitude, OriginLongitude, 0));
    if (!QTest::qWaitFor([mockLink]() {
            return mockLink->receivedMavlinkMessageCount(MAVLINK_MSG_ID_SET_GPS_GLOBAL_ORIGIN) >= 1;
        }, TestTimeout::longMs())) {
        return false;
    }

    vehicle->requestEstimatorOrigin();
    return QTest::qWaitFor([vehicle]() { return vehicle->estimatorOrigin().isValid(); }, TestTimeout::longMs());
}

} // namespace LocalGridTestSupport
