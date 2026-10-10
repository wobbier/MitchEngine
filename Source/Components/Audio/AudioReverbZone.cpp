#include "PCH.h"
#include "AudioReverbZone.h"
#include "Components/Transform.h"
#include "Debug/DebugDraw.h"

ME_REFLECT_ENUM( ReverbPreset, { { "Generic", ReverbPreset::Generic }, { "Padded Cell", ReverbPreset::PaddedCell }, { "Room", ReverbPreset::Room },
    { "Bathroom", ReverbPreset::Bathroom }, { "Living Room", ReverbPreset::LivingRoom }, { "Stone Room", ReverbPreset::StoneRoom },
    { "Auditorium", ReverbPreset::Auditorium }, { "Concert Hall", ReverbPreset::ConcertHall }, { "Cave", ReverbPreset::Cave },
    { "Arena", ReverbPreset::Arena }, { "Hangar", ReverbPreset::Hangar }, { "Carpeted Hallway", ReverbPreset::CarpetedHallway },
    { "Hallway", ReverbPreset::Hallway }, { "Stone Corridor", ReverbPreset::StoneCorridor }, { "Alley", ReverbPreset::Alley },
    { "Forest", ReverbPreset::Forest }, { "City", ReverbPreset::City }, { "Mountains", ReverbPreset::Mountains },
    { "Quarry", ReverbPreset::Quarry }, { "Plain", ReverbPreset::Plain }, { "Parking Lot", ReverbPreset::ParkingLot },
    { "Sewer Pipe", ReverbPreset::SewerPipe }, { "Underwater", ReverbPreset::Underwater } } )

ME_REFLECT_BEGIN( AudioReverbZone )
    ME_FIELD( Preset ).Tooltip( "The zone's acoustics" );
    ME_FIELD( MinDistance ).Range( 0.f, 500.f ).Tooltip( "Full reverb while the listener is this close" );
    ME_FIELD( MaxDistance ).Range( 0.f, 1000.f ).Tooltip( "No reverb from this zone beyond this distance" );
ME_REFLECT_END()


AudioReverbZone::AudioReverbZone()
    : Component( "AudioReverbZone" )
{
}


bool AudioReverbZone::IsActive() const
{
    return m_active;
}


#if USING( ME_EDITOR )
void AudioReverbZone::OnEditorInspect()
{
    // Show the zone's reach while it's inspected.
    if( Transform* transform = Parent ? Parent->TryGetComponent<Transform>() : nullptr )
    {
        const Vector3 center = transform->GetWorldPosition();
        DebugDraw::Sphere( center, MinDistance, Vector4( 0.3f, 0.8f, 1.f, 1.f ) );
        DebugDraw::Sphere( center, MaxDistance, Vector4( 0.3f, 0.8f, 1.f, 0.35f ) );
    }
}
#endif
