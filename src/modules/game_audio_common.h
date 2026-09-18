#pragma once

#include "modules/prelude.h"

namespace runtime {

float LinearGainToDb(float gain);
float ReadEffectsGainDb();
AudioVector ReadPlaceablePosition(void* entity);
AudioVector PositionRelativeToCamera(const AudioVector& position);
float VectorMagnitude(const AudioVector& value);
void PublishCameraTransform();

} // namespace runtime
