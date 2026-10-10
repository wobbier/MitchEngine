#pragma once
#include "ECS/Component.h"
#include "ECS/ComponentDetail.h"
#include "Audio/AudioTypes.h"

#if USING( ME_FMOD )
namespace FMOD {
    class Reverb3D;
}
#endif

// A place with its own acoustics (FMOD 3D reverb): the preset is fully heard while the listener is
// within MinDistance of the entity and fades out by MaxDistance; overlapping zones blend. 3D sources
// feed the reverb (AudioSource::ReverbMix), 2D ones (music, UI) stay dry. AudioCore owns the FMOD
// reverb and keeps it at the entity.
class AudioReverbZone
    : public Component<AudioReverbZone>
{
    ME_REFLECTABLE( AudioReverbZone )
    friend class AudioCore;
public:
    AudioReverbZone();

    ReverbPreset Preset = ReverbPreset::Room;
    float MinDistance = 5.f;
    float MaxDistance = 15.f;

    // The zone's reverb is live (audio running, zone active).
    bool IsActive() const;
#if USING( ME_FMOD )
    FMOD::Reverb3D* GetReverb() const
    {
        return m_reverb;
    }
#endif

#if USING( ME_EDITOR )
    void OnEditorInspect() override;
#endif

private:
#if USING( ME_FMOD )
    FMOD::Reverb3D* m_reverb = nullptr;
#endif
    ReverbPreset m_appliedPreset = ReverbPreset::Generic;
    bool m_presetApplied = false;
    bool m_active = false;
};

ME_REGISTER_COMPONENT_FOLDER( AudioReverbZone, "Audio" )
