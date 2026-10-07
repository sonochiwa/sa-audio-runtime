#pragma once

// Debug autotest: the scene, a fixed sequence of steps that makes every
// sound family the runtime takes over, at the Los Santos airport. The
// vanilla pass plays the same steps with the runtime off, except the steps
// that switch the runtime itself.
namespace autotest {

void StartScene(bool runtimePass);
// Runs one game frame of the scene; true once the last step has ended.
bool SceneFrame();

} // namespace autotest
