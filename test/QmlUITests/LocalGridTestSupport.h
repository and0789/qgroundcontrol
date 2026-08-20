#pragma once

class MockLink;
class Vehicle;

/// Shared fixtures for tests that drive the local grid through the running UI.
namespace LocalGridTestSupport {

// Somewhere real and away from the equator, so a latitude and longitude mix-up cannot pass
constexpr double OriginLatitude = 47.3977419;
constexpr double OriginLongitude = 8.5455938;

/// Gives the vehicle an estimator origin, which is the frame everything on the grid is measured
/// inside of.
///
/// Caching the COMMAND_INT form as unsupported drives the legacy message straight away: MockLink
/// refuses every COMMAND_INT, and waiting out that probe on each test is time spent proving something
/// most callers are not about.
bool giveTheVehicleAnOrigin(Vehicle *vehicle, MockLink *mockLink);

} // namespace LocalGridTestSupport
