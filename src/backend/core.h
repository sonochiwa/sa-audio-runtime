#pragma once

#include "backend/prelude.h"

namespace backend {

constexpr std::uint32_t kQueueCapacity = 8192;
constexpr std::uint32_t kQueueMask = kQueueCapacity - 1;
constexpr std::size_t kMaxOriginalSounds = 400;
constexpr std::size_t kWeaponBankId = 143;
constexpr std::size_t kBulletHitBankId = 27;

struct Voice {
    IDirectSoundBuffer* buffer{};
    IDirectSound3DBuffer* spatialBuffer{};
    float mixVolumeDb{-100.0f};
    float outputGainDb{};
    bool isFrontEnd{};
    float normalizationReferenceDb{};
    bool pendingStart{};
    bool suspended{};
    bool isUnpausable{};
    std::int16_t soundId{-1};
    float headroomDb{};
    AudioVector worldPosition{};
    AudioVector relativePosition{};
    float sourceVolumeDb{};
    float rollOffFactor{};
    bool followsCamera{};
    bool isTail{};
    std::uint32_t environmentFrame{};
    std::uintptr_t minigunSourceKey{};
    bool looping{};
    bool minigunTailFading{};
    std::uint32_t minigunFadeFrame{};
    bool isBulletHit{};
    std::uintptr_t vehicleSourceKey{};
    bool isVehicleOneShot{};
    std::uint32_t vehicleGeneration{};
    std::uint32_t sampleRate{};
    float playbackSpeed{1.0f};
    float dopplerScale{};
    std::int16_t vehicleBankId{-1};
    bool vehicleLoopPending{};
    DWORD vehiclePreloopRewriteOffset{};
    DWORD vehiclePreloopRewriteBytes{};
    bool vehicleStopFading{};
    ULONGLONG vehicleStopFadeStartedAt{};
    float vehicleStopFadeStartDb{-100.0f};
    bool volumeFadeActive{};
    ULONGLONG volumeFadeStartedAt{};
    DWORD volumeFadeDurationMs{};
    float volumeFadeStartDb{-100.0f};
    float volumeFadeTargetDb{-100.0f};
    bool isRuntimeEffect{};
    bool isStatefulEffect{};
    bool reportsCompletion{};
};

struct MinigunSource {
    std::uintptr_t key{};
    ULONGLONG lastHeartbeat{};
    AudioJob job{};
};

struct VehicleSource {
    AudioJob job{};
    ULONGLONG lastHeartbeat{};
    std::uint32_t lastStartedGeneration{};
};

struct VirtualRuntimeSource {
    AudioJob job{};
    ULONGLONG lastUpdateAt{};
    DWORD durationMs{};
    double playPositionMs{};
    bool looping{};
};

enum class VoiceGroup : std::uint8_t {
    Weapons,
    Vehicles,
    Runtime
};

struct VehicleBank {
    OriginalSoundBank samples;
    std::array<IDirectSoundBuffer*, kMaxOriginalSounds> baseBuffers{};
    bool loadAttempted{};
    bool loaded{};
};
extern SRWLOCK gOverrideLock;
extern std::unordered_map<std::uint32_t, std::string> gDynamicOverridePaths;
extern std::array<std::string, 9> gPackOverridePaths;
std::uint32_t GetDynamicOverrideKey( std::int16_t bankId, std::int16_t soundId );
int GetPackIdForBank(std::int16_t bankId);
const char* GetPackName(int packId);
void ApplyDynamicOverrides(std::int16_t bankId, OriginalSoundBank& bank);
void ReleaseVehicleBanks( std::unordered_map<std::int16_t, std::unique_ptr<VehicleBank>>& banks );
extern HMODULE gModule;
extern HANDLE gThread;
extern HANDLE gWakeEvent;
extern HANDLE gStopEvent;
extern std::array<AudioJob, kQueueCapacity> gJobs;
extern std::atomic<std::uint32_t> gWrite;
extern std::atomic<std::uint32_t> gRead;
extern std::atomic<bool> gReady;
extern std::atomic<std::uint32_t> gResetEpoch;
extern std::atomic<bool> gReplaceOriginal;
extern std::atomic<bool> gReplaceVehicles;
extern std::atomic<bool> gReplaceDialogues;
extern std::atomic<bool> gDeviceRecoveryRequested;
extern SRWLOCK gCoalescedJobLock;
extern std::vector<AudioJob> gCoalescedJobs;
extern SRWLOCK gCompletionLock;
extern std::deque<AudioCompletion> gCompletions;
extern std::array<std::atomic<float>, 12> gCameraTransform;
extern std::atomic<std::uint32_t> gCameraTransformGeneration;
extern std::atomic<bool> gCanSeeOutside;
extern std::atomic<std::uint32_t> gEnvironmentFrame;
constexpr auto kRuntimeSoundBankCount =
    static_cast<std::size_t>(RuntimeSoundBank::Count);
extern std::array< std::array<std::string, kMaxOriginalSounds>, kRuntimeSoundBankCount > gOverridePaths;
extern std::array< std::array<std::int8_t, kMaxOriginalSounds>, kRuntimeSoundBankCount > gOverrideActions;
extern std::string gArchiveOverridePath;
extern std::string gLookupOverridePath;
extern bool gBankSourcesDirty;
extern bool gDynamicBanksDirty;
void RequestDeviceRecovery();
bool AudioCallSucceeded(HRESULT result);
bool IsCoalescedSourceJob(AudioJobType type);
std::uint64_t GetCoalescedJobKey(const AudioJob& job);
void TakeCoalescedJobs(std::vector<AudioJob>& jobs);
void ClearCoalescedJobs();
void QueueCompletion(const AudioCompletion& completion);
std::string GetModuleDirectory();
std::string GetGameDirectory();
HWND FindProcessWindow();
bool CreateBaseBuffer( IDirectSound8* directSound, const OriginalPcmSample& sample, IDirectSoundBuffer** output );
bool CreatePreloopBuffer( IDirectSound8* directSound, const OriginalPcmSample& sample, IDirectSoundBuffer** output, DWORD& initialPosition, DWORD& rewriteOffset, DWORD& rewriteBytes );
bool EnsureBaseBuffer( IDirectSound8* directSound, const OriginalPcmSample& sample, IDirectSoundBuffer*& buffer );
void ApplyPendingOverrides( RuntimeSoundBank runtimeBank, OriginalSoundBank& bank, std::array<IDirectSoundBuffer*, kMaxOriginalSounds>& baseBuffers );
void ApplyCurrentOverrides( RuntimeSoundBank runtimeBank, OriginalSoundBank& bank );
bool TakeBankSourceUpdate( std::string& archivePath, std::string& lookupPath );
bool TakeDynamicBankUpdate();
void PublishCompletion(const Voice& voice);
void PublishDialogueStarted( const AudioJob& job, const OriginalPcmSample& sample );
void PublishVehiclePlayTimeMetadata( const AudioJob& job, const OriginalPcmSample& sample );
void PublishCompletion(const AudioJob& job);
void CleanupVoices(std::vector<Voice>& voices);
VoiceGroup GetVoiceGroup(const Voice& voice);
float GetVoicePriority(const Voice& voice);
std::size_t GetVoiceGroupLimit(VoiceGroup group);
bool ReserveVoiceSlot( std::vector<Voice>& voices, VoiceGroup incomingGroup, float incomingPriority );
void PlaySample( IDirectSound8* directSound, OriginalSoundBank& bank, std::array<IDirectSoundBuffer*, kMaxOriginalSounds>& baseBuffers, std::vector<Voice>& voices, std::int16_t soundId, float speed, const AudioVector& position, float volumeDb, bool forcedFront, float outputGainDb, const AudioVector& worldPosition, float sourceVolumeDb, float rollOffFactor, bool isTail, std::uintptr_t minigunSourceKey = 0, bool looping = false, bool isBulletHit = false );
void StartPendingVoices(std::vector<Voice>& voices);
void SuspendVoices(std::vector<Voice>& voices);
void ResumeVoices(std::vector<Voice>& voices);
void StopAndReleaseVoices( std::vector<Voice>& voices, bool publishCompletions = true );
void StopVoicesByOwner( std::vector<Voice>& voices, int ownerGroup );
bool IsGamePaused();
float RebalanceVoiceMixer(std::vector<Voice>& voices);
void UpdateVoiceVolumeFades(std::vector<Voice>& voices);
float Magnitude(const AudioVector& value);
float GetDistanceAttenuation(float distance);
float GetDirectionalMikeAttenuation(const AudioVector& direction);
float GetHeadroomDb( OriginalSoundBank& bank, std::int16_t soundId );
float CalculateWorldVolume( OriginalSoundBank& bank, std::int16_t soundId, float sourceVolumeDb, float rollOffFactor, const AudioVector& relativePosition );
float CalculateFrontVolume( OriginalSoundBank& bank, std::int16_t soundId, float sourceVolumeDb );
bool ReadCameraTransform(AudioCameraTransform& transform);
AudioVector TransformWorldPosition( const AudioCameraTransform& transform, const AudioVector& position );
void UpdateVoicePositions(std::vector<Voice>& voices);
void UpdateVoiceEnvironment(std::vector<Voice>& voices);
void ProcessGunLayers( IDirectSound8* directSound, OriginalSoundBank& bank, std::array<IDirectSoundBuffer*, kMaxOriginalSounds>& baseBuffers, std::vector<Voice>& voices, const AudioJob& job, std::uintptr_t minigunSourceKey = 0, bool looping = false );
void StopMinigunVoices( std::vector<Voice>& voices, std::uintptr_t sourceKey, bool fadeTail );
void ProcessMinigunJob( IDirectSound8* directSound, OriginalSoundBank& bank, std::array<IDirectSoundBuffer*, kMaxOriginalSounds>& baseBuffers, std::vector<Voice>& voices, std::vector<MinigunSource>& sources, const AudioJob& job );
void UpdateMinigunSources( IDirectSound8* directSound, OriginalSoundBank& bank, std::array<IDirectSoundBuffer*, kMaxOriginalSounds>& baseBuffers, std::vector<Voice>& voices, std::vector<MinigunSource>& sources );
void ProcessBulletHit( IDirectSound8* directSound, OriginalSoundBank& bank, std::array<IDirectSoundBuffer*, kMaxOriginalSounds>& baseBuffers, std::vector<Voice>& voices, const AudioJob& job );
VehicleBank* GetVehicleBank( std::unordered_map<std::int16_t, std::unique_ptr<VehicleBank>>& banks, const std::string& lookupPath, const std::string& archivePath, std::int16_t bankId );
VehicleBank* GetDialogueBank( std::unordered_map<std::int16_t, std::unique_ptr<VehicleBank>>& banks, const std::string& gameDirectory, const std::string& lookupPath, std::int16_t bankId );
Voice* FindVehicleVoice( std::vector<Voice>& voices, std::uintptr_t sourceKey );
Voice* FindRestartableVehicleLoop( std::vector<Voice>& voices, const AudioJob& job );
void RestartVehicleLoop( Voice& voice, const AudioJob& job, float listenerVolume );
std::vector<Voice>::iterator FindQuietestStatefulVoice( std::vector<Voice>& voices );
void StopVehicleVoice( std::vector<Voice>& voices, std::uintptr_t sourceKey );
void UpdateVehicleStopFades(std::vector<Voice>& voices);
bool CreateVehicleVoice( IDirectSound8* directSound, VehicleBank& bank, std::vector<Voice>& voices, const AudioJob& job, float listenerVolume );
void ContinueVehicleLoops( const std::string& lookupPath, const std::string& archivePath, std::unordered_map<std::int16_t, std::unique_ptr<VehicleBank>>& banks, std::vector<Voice>& voices );
void ProcessVehicleJob( std::unordered_map<std::uintptr_t, VehicleSource>& sources, std::vector<Voice>& voices, const AudioJob& job );
void ProcessVehicleOneShot( IDirectSound8* directSound, const std::string& lookupPath, const std::string& archivePath, std::unordered_map<std::int16_t, std::unique_ptr<VehicleBank>>& banks, std::vector<Voice>& voices, const AudioJob& job );
void ProcessDialogueJob( IDirectSound8* directSound, const std::string& gameDirectory, const std::string& lookupPath, std::unordered_map<std::int16_t, std::unique_ptr<VehicleBank>>& banks, std::vector<Voice>& voices, std::unordered_map<std::uintptr_t, VirtualRuntimeSource>& virtualSources, const AudioJob& job );
void VirtualizeRuntimeVoices( std::vector<Voice>& voices, std::unordered_map<std::uintptr_t, VirtualRuntimeSource>& sources, bool force );
void UpdateVirtualRuntimeSources( IDirectSound8* directSound, const std::string& gameDirectory, const std::string& lookupPath, std::unordered_map<std::int16_t, std::unique_ptr<VehicleBank>>& banks, std::vector<Voice>& voices, std::unordered_map<std::uintptr_t, VirtualRuntimeSource>& sources );
void UpdateVehicleSources( IDirectSound8* directSound, const std::string& lookupPath, const std::string& archivePath, std::unordered_map<std::int16_t, std::unique_ptr<VehicleBank>>& banks, std::unordered_map<std::uintptr_t, VehicleSource>& sources, std::vector<Voice>& voices );
bool InitialiseListener( IDirectSound8* directSound, IDirectSound3DListener** output );
bool CreateAudioDevice( IDirectSound8** directSoundOutput, IDirectSound3DListener** listenerOutput );
bool IsAudioDeviceHealthy(IDirectSound8* directSound);
void ReleaseBaseBuffers( std::array<IDirectSoundBuffer*, kMaxOriginalSounds>& buffers );
DWORD WINAPI BackendThread(void*);

} // namespace backend
