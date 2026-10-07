#pragma once

#include <cstdint>

// Debug autotest: what the scene measures. Every sound request is counted
// per scene step by bank slot, event, sound and where it went (a game slot,
// a runtime proxy, or nowhere, which is what a worker one-shot returns), and
// the worker and the proxies are sampled while a step runs.
namespace autotest {

void InstallProbe(bool runtimePass);
void BeginStep(const char* name);
void EndStep();
// Logs the worker and proxy state, and in the runtime pass compares every
// proxy voice's volume with the game's own calculation for the same sound.
void Sample();
std::uint32_t StepRequests(std::int32_t event);
std::uint32_t VehicleSourceTimeouts();
std::uint32_t MinigunStops();
// Engine, horn, siren, skid and other sound pointers that the vehicles hold
// and that point neither into the game's sound slots nor at a live proxy.
std::uint32_t CountDanglingVehicleSounds(const char* when);

} // namespace autotest
