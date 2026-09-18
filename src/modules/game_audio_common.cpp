#include "modules/modules.h"

namespace runtime {

float LinearGainToDb(float gain) {
    return gain > 0.00001f ? 20.0f * std::log10(gain) : -100.0f;
}

float ReadEffectsGainDb() {
    const auto* hardware = reinterpret_cast<const volatile std::uint8_t*>(
        kAudioHardwareAddress
    );
    const auto ReadFloat = [hardware](std::size_t offset) {
        return *reinterpret_cast<const volatile float*>(hardware + offset);
    };
    const auto gain =
        std::max(ReadFloat(kEffectMasterScaleOffset), 0.0f) *
        std::max(ReadFloat(kEffectsFaderScaleOffset), 0.0f) *
        std::max(ReadFloat(kNonStreamFaderScaleOffset), 0.0f);
    return LinearGainToDb(gain);
}

AudioVector ReadPlaceablePosition(void* entity) {
    if (!entity) {
        return {};
    }
    const auto* bytes = static_cast<const std::uint8_t*>(entity);
    const auto* matrix = *reinterpret_cast<const std::uint8_t* const*>(
        bytes + 0x14
    );
    if (matrix) {
        return *reinterpret_cast<const AudioVector*>(matrix + 0x30);
    }
    return *reinterpret_cast<const AudioVector*>(bytes + 0x4);
}

AudioVector PositionRelativeToCamera(const AudioVector& position) {
    AudioVector relative{};
    reinterpret_cast<VectorRelativeToCameraFn>(
        kVectorRelativeToCameraAddress
    )(&relative, &position);
    return relative;
}

float VectorMagnitude(const AudioVector& value) {
    return std::sqrt(
        value.x * value.x +
        value.y * value.y +
        value.z * value.z
    );
}

void PublishCameraTransform() {
    const AudioVector zero{};
    const AudioVector unitX{1.0f, 0.0f, 0.0f};
    const AudioVector unitY{0.0f, 1.0f, 0.0f};
    const AudioVector unitZ{0.0f, 0.0f, 1.0f};
    const auto origin = PositionRelativeToCamera(zero);
    const auto xPoint = PositionRelativeToCamera(unitX);
    const auto yPoint = PositionRelativeToCamera(unitY);
    const auto zPoint = PositionRelativeToCamera(unitZ);
    WeaponBackendUpdateCameraTransform({
        origin,
        {
            xPoint.x - origin.x,
            xPoint.y - origin.y,
            xPoint.z - origin.z
        },
        {
            yPoint.x - origin.x,
            yPoint.y - origin.y,
            yPoint.z - origin.z
        },
        {
            zPoint.x - origin.x,
            zPoint.y - origin.y,
            zPoint.z - origin.z
        }
    });
}

} // namespace runtime
