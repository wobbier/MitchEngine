#include "PCH.h"
#include "Sequence.h"
#include "Animation/SequenceAsset.h"
#include "Components/Camera.h"
#include "Components/Animation/Animator.h"
#include "Components/Audio/AudioSource.h"
#include "Resource/ResourceCache.h"
#include <algorithm>
#include <cmath>
#if USING( ME_EDITOR )
#include "Events/EditorEvents.h"
#include <imgui.h>
#endif

ME_REFLECT_ENUM( SequenceInterpolation, { { "Linear", SequenceInterpolation::Linear }, { "SmoothStep", SequenceInterpolation::SmoothStep }, { "Hold", SequenceInterpolation::Hold },
    { "FocalLength", SequenceInterpolation::FocalLength }, { "SmoothFocalLength", SequenceInterpolation::SmoothFocalLength } } )

ME_REFLECT_BEGIN( SequenceKey )
    ME_FIELD( Frame );
    ME_FIELD( Value );
    ME_FIELD( Interpolation );
ME_REFLECT_END()

ME_REFLECT_BEGIN( SequenceTrack )
    ME_FIELD( Target );
    ME_FIELD( Binding );
    ME_FIELD( Component );
    ME_FIELD( Property );
    ME_FIELD( Enabled );
    ME_FIELD( Keys );
ME_REFLECT_END()

ME_REFLECT_BEGIN( SequenceBinding )
    ME_FIELD( Slot );
    ME_FIELD( Target );
ME_REFLECT_END()

ME_REFLECT_ENUM( SequenceMarkerAction, { { "Event", SequenceMarkerAction::Event }, { "PlayAudio", SequenceMarkerAction::PlayAudio },
    { "StopAudio", SequenceMarkerAction::StopAudio }, { "PlayAnimation", SequenceMarkerAction::PlayAnimation } } )

ME_REFLECT_BEGIN( SequenceMarker )
    ME_FIELD( Frame );
    ME_FIELD( Name );
    ME_FIELD( Target );
    ME_FIELD( Binding );
    ME_FIELD( Payload );
    ME_FIELD( Number );
    ME_FIELD( Action );
ME_REFLECT_END()


void SequenceMarker::RunAction( const EntityHandle& InReceiver ) const
{
    if( Action == SequenceMarkerAction::Event || !InReceiver ) return;
    if( Action == SequenceMarkerAction::PlayAnimation )
    {
        if( Animator* animator = InReceiver->TryGetComponent<Animator>(); animator && !Payload.empty() ) animator->Play( Payload, std::max( Number, 0.f ) );
        return;
    }
    AudioSource* source = InReceiver->TryGetComponent<AudioSource>();
    if( !source ) return;
    if( Action == SequenceMarkerAction::StopAudio ) source->Stop( false );
    else if( Payload.empty() ) source->Play();
    else source->PlayOneShot( Path( Payload ), Number > 0.f ? Number : 1.f );
}

ME_REFLECT_BEGIN( SequenceCut )
    ME_FIELD( Frame );
    ME_FIELD( Target );
    ME_FIELD( Binding );
ME_REFLECT_END()

ME_REFLECT_BEGIN( SequenceClipEntry )
    ME_FIELD( Name );
    ME_FIELD( Clip ).Asset( "Sequence" );
ME_REFLECT_END()

ME_REFLECT_BEGIN( Sequence )
    ME_FIELD( Clip ).Asset( "Sequence" );
    ME_FIELD( Bindings );
    ME_FIELD( Clips );
    ME_FIELD( Name ).CustomInspector();
    ME_FIELD( FrameRate ).Range( 1.f, 240.f ).CustomInspector();
    ME_FIELD( DurationFrames ).Range( 1.f, 100000.f ).CustomInspector();
    ME_FIELD( PlayOnStart );
    ME_FIELD( Loop );
    ME_FIELD( Speed ).Range( 0.f, 16.f );
    ME_FIELD( Tracks ).CustomInspector();
    ME_FIELD( Markers ).CustomInspector();
    ME_FIELD( Cuts ).CustomInspector();
ME_REFLECT_END()

namespace
{
    bool Supported( Reflection::PropertyType InType )
    {
        using P = Reflection::PropertyType;
        return InType == P::Float || InType == P::Double || InType == P::Vector2 || InType == P::Vector3 || InType == P::Vector4 || InType == P::Quaternion;
    }


    bool Finite( const Vector4& InValue )
    {
        return std::isfinite( InValue.x ) && std::isfinite( InValue.y ) && std::isfinite( InValue.z ) && std::isfinite( InValue.w );
    }


    void CollectProperties( const Reflection::TypeInfo& InType, void* InObject, const std::string& InPrefix, std::vector<std::string>& OutPaths, int InDepth = 0 )
    {
        if( InDepth > 16 ) return;
        InType.ForEachField( InObject, [&]( const Reflection::FieldInfo& field, void* owner ) {
            if( field.Has( Reflection::FieldFlags::Hidden | Reflection::FieldFlags::ReadOnly | Reflection::FieldFlags::NoSerialize ) ) return;
            const std::string path = InPrefix + field.Name;
            if( Supported( field.GetType() ) ) OutPaths.push_back( path );
            else if( field.GetType() == Reflection::PropertyType::Struct )
                CollectProperties( field.Ops->GetStructType(), field.Get( owner ), path + ".", OutPaths, InDepth + 1 );
        } );
    }


    Vector4 ReadValue( const Reflection::PropertyRef& InProperty )
    {
        using P = Reflection::PropertyType;
        switch( InProperty.Ops->Type )
        {
        case P::Float: return Vector4( *static_cast<float*>( InProperty.Value ), 0.f, 0.f, 0.f );
        case P::Double: return Vector4( static_cast<float>( *static_cast<double*>( InProperty.Value ) ), 0.f, 0.f, 0.f );
        case P::Vector2: { const auto& v = *static_cast<Vector2*>( InProperty.Value ); return Vector4( v.x, v.y, 0.f, 0.f ); }
        case P::Vector3: { const auto& v = *static_cast<Vector3*>( InProperty.Value ); return Vector4( v.x, v.y, v.z, 0.f ); }
        case P::Vector4: return *static_cast<Vector4*>( InProperty.Value );
        case P::Quaternion: { const auto& q = *static_cast<Quaternion*>( InProperty.Value ); return Vector4( q.x, q.y, q.z, q.w ); }
        default: return Vector4();
        }
    }


    bool WriteValue( const Reflection::PropertyRef& InProperty, Vector4 InValue )
    {
        if( !Finite( InValue ) ) return false;
        using P = Reflection::PropertyType;
        if( InProperty.Field && InProperty.Field->HasRange && ( InProperty.Ops->Type == P::Float || InProperty.Ops->Type == P::Double ) )
            InValue.x = std::clamp( InValue.x, InProperty.Field->Min, InProperty.Field->Max );
        switch( InProperty.Ops->Type )
        {
        case P::Float: *static_cast<float*>( InProperty.Value ) = InValue.x; break;
        case P::Double: *static_cast<double*>( InProperty.Value ) = InValue.x; break;
        case P::Vector2: *static_cast<Vector2*>( InProperty.Value ) = Vector2( InValue.x, InValue.y ); break;
        case P::Vector3: *static_cast<Vector3*>( InProperty.Value ) = Vector3( InValue.x, InValue.y, InValue.z ); break;
        case P::Vector4: *static_cast<Vector4*>( InProperty.Value ) = InValue; break;
        case P::Quaternion:
        {
            const float length = glm::length( InValue.InternalVector );
            if( length < 1e-6f || !std::isfinite( length ) ) return false;
            *static_cast<Quaternion*>( InProperty.Value ) = Quaternion( InValue.x / length, InValue.y / length, InValue.z / length, InValue.w / length );
            break;
        }
        default: return false;
        }
        return true;
    }
}


std::vector<std::string> SequenceTrack::GetProperties( BaseComponent& InComponent )
{
    std::vector<std::string> paths;
    // Do not let sequences animate their own authoring/playback settings (or another sequence).
    if( InComponent.GetName() != "Sequence" && InComponent.GetTypeInfo() )
        CollectProperties( *InComponent.GetTypeInfo(), InComponent.GetReflectedObject(), "", paths );
    return paths;
}


BaseComponent* SequenceTrack::ResolveComponent() const
{
    return Target ? Target->GetComponentByName( Component ) : nullptr;
}


Reflection::PropertyRef SequenceTrack::ResolveProperty() const
{
    BaseComponent* component = ResolveComponent();
    if( !component || component->GetName() == "Sequence" ) return {};
    const Reflection::TypeInfo* type = component->GetTypeInfo();
    void* object = component->GetReflectedObject();
    std::string_view remaining = Property;
    while( type && !remaining.empty() )
    {
        const size_t dot = remaining.find( '.' );
        void* owner = nullptr;
        const auto* field = type->FindField( remaining.substr( 0, dot ), object, &owner );
        if( !field || field->Has( Reflection::FieldFlags::Hidden | Reflection::FieldFlags::ReadOnly | Reflection::FieldFlags::NoSerialize ) ) return {};
        object = field->Get( owner );
        if( dot == std::string_view::npos ) return Supported( field->GetType() ) ? Reflection::PropertyRef{ field, field->Ops, object } : Reflection::PropertyRef{};
        if( field->GetType() != Reflection::PropertyType::Struct ) return {};
        type = &field->Ops->GetStructType();
        remaining.remove_prefix( dot + 1 );
    }
    return {};
}


void SequenceTrack::SetKey( const SequenceKey& InKey )
{
    if( InKey.Frame < 0 || !Finite( InKey.Value ) ) return;
    Keys.erase( std::remove_if( Keys.begin(), Keys.end(), [&]( const SequenceKey& key ) { return key.Frame == InKey.Frame; } ), Keys.end() );
    Keys.push_back( InKey );
    std::stable_sort( Keys.begin(), Keys.end(), []( const SequenceKey& a, const SequenceKey& b ) { return a.Frame < b.Frame; } );
}


bool SequenceTrack::Capture( int InFrame )
{
    const auto property = ResolveProperty();
    if( !property || InFrame < 0 ) return false;
    SequenceKey key;
    key.Frame = InFrame;
    key.Value = ReadValue( property );
    if( !Finite( key.Value ) ) return false;
    SetKey( key );
    return true;
}


bool SequenceTrack::Sample( float InFrame, Vector4& OutValue ) const
{
    if( !std::isfinite( InFrame ) || Keys.empty() ) return false;
    // Also handles directly edited / unsorted data; the last key at a duplicate frame wins.
    const SequenceKey* before = nullptr;
    const SequenceKey* after = nullptr;
    for( const auto& key : Keys )
    {
        if( key.Frame < 0 || !Finite( key.Value ) ) continue;
        if( key.Frame <= InFrame && ( !before || key.Frame >= before->Frame ) ) before = &key;
        if( key.Frame > InFrame && ( !after || key.Frame <= after->Frame ) ) after = &key;
    }
    if( !before && !after ) return false;
    if( !before ) before = after;
    if( !after ) after = before;
    float t = before == after ? 0.f : ( InFrame - before->Frame ) / ( after->Frame - before->Frame );
    if( before->Interpolation == SequenceInterpolation::Hold ) t = 0.f;
    else if( before->Interpolation == SequenceInterpolation::SmoothStep || before->Interpolation == SequenceInterpolation::SmoothFocalLength ) t = t * t * ( 3.f - 2.f * t );
    const auto property = ResolveProperty();
    if( !property ) return false;
    if( before->Interpolation == SequenceInterpolation::FocalLength || before->Interpolation == SequenceInterpolation::SmoothFocalLength )
    {
        if( Component != "Camera" || Property != "FieldOfView" || property.Ops->Type != Reflection::PropertyType::Float ) return false;
        const double a = before->Value.x, b = after->Value.x;
        if( a <= 0. || a >= 180. || b <= 0. || b >= 180. ) return false;
        // Focal length is proportional to cot(FOV/2). Interpolating it with the same easing as
        // camera position keeps depth * tan(FOV/2) constant throughout a dolly zoom.
        const double radians = glm::pi<double>() / 180.;
        const double focal = ( 1. - t ) / std::tan( a * radians * 0.5 ) + t / std::tan( b * radians * 0.5 );
        OutValue = Vector4( static_cast<float>( 2. * std::atan( 1. / focal ) / radians ), 0.f, 0.f, 0.f );
    }
    else if( property.Ops->Type == Reflection::PropertyType::Quaternion )
    {
        const glm::vec4 a = before->Value.InternalVector, b = after->Value.InternalVector;
        if( glm::length( a ) < 1e-6f || glm::length( b ) < 1e-6f ) return false;
        const glm::quat q = glm::slerp( glm::normalize( glm::quat( a.w, a.x, a.y, a.z ) ), glm::normalize( glm::quat( b.w, b.x, b.y, b.z ) ), t );
        OutValue = Vector4( q.x, q.y, q.z, q.w );
    }
    else OutValue = Vector4( glm::mix( before->Value.InternalVector, after->Value.InternalVector, t ) );
    return Finite( OutValue );
}


bool SequenceTrack::Apply( float InFrame ) const
{
    BaseComponent* component = ResolveComponent();
    if( !Enabled || !component || !component->IsEnabled() || !Target->IsActiveInHierarchy() ) return false;
    Vector4 value;
    const auto property = ResolveProperty();
    if( !property || !Sample( InFrame, value ) || !WriteValue( property, value ) ) return false;
    component->OnPropertyChanged( Property.substr( 0, Property.find( '.' ) ) );
    return true;
}


Sequence::Sequence() : Component( "Sequence" ) {}


float Sequence::GetDuration() const
{
    return static_cast<float>( std::max( DurationFrames, 1 ) ) / std::clamp( FrameRate, 1, 240 );
}


float Sequence::GetFrame() const
{
    // The duration in seconds may not be representable as a float; still hit the exact end key.
    if( m_time >= GetDuration() ) return static_cast<float>( std::max( DurationFrames, 1 ) );
    return static_cast<float>( m_time * std::clamp( FrameRate, 1, 240 ) );
}


void Sequence::Apply( float InFrame ) const
{
    for( const auto& track : Tracks ) track.Apply( InFrame );
    const EntityHandle camera = GetCutCamera( InFrame );
    if( camera && camera->HasComponent<Camera>() ) camera->GetComponent<Camera>().SetCurrent();
}


EntityHandle Sequence::GetCutCamera( float InFrame ) const
{
    EntityHandle camera;
    // A small tolerance: a sample on a cut's frame shows the new camera.
    for( const SequenceCut& cut : Cuts )
    {
        if( static_cast<float>( cut.Frame ) > InFrame + 1e-3f ) break;
        camera = cut.Target;
    }
    return camera;
}


void Sequence::Play( bool InRestart )
{
    if( !RefreshClip() ) return;
    m_initialized = true;
    const bool restart = InRestart || m_time >= GetDuration();
    if( restart && m_playing ) Notify( SequenceEventKind::Interrupted );
    if( restart ) m_time = 0.;
    // A resume after Pause continues; playback from the start fires the markers on frame 0.
    const bool starting = restart || ( !m_playing && m_time <= 0. );
    m_playing = true;
    if( starting )
    {
        m_markerTime = -1.;
        Notify( SequenceEventKind::Started );
    }
    Apply( GetFrame() );
}


bool Sequence::HasClip( const std::string& InName ) const
{
    if( InName.empty() ) return false;
    const SequenceClipEntry* found = nullptr;
    for( const auto& entry : Clips ) if( entry.Name == InName )
    {
        if( found || entry.Clip.empty() ) return false;
        found = &entry;
    }
    return found != nullptr;
}


bool Sequence::PlayClip( const std::string& InName, bool InRestart )
{
    if( !HasClip( InName ) ) return false;
    for( const auto& entry : Clips ) if( entry.Name == InName ) return PlayAsset( entry.Clip, InRestart );
    return false;
}


bool Sequence::PlayAsset( const std::string& InPath, bool InRestart )
{
    if( InPath.empty() ) return false;
    const std::string path = Path( InPath ).GetLocalPathString();
    // Validate before touching the current playback. A missing/broken clip must not interrupt it.
    auto resource = ResourceCache::GetInstance().Get<SequenceResource>( Path( path ) );
    if( !resource || resource->Version == 0 ) return false;
    if( GetActiveClipPath() != path )
    {
        if( m_playing ) Notify( SequenceEventKind::Interrupted );
        m_playing = false;
        StopPreview();
        m_activeClip = path;
        m_time = 0.;
        RefreshClip( true );
    }
    Play( InRestart );
    return true;
}


void Sequence::Pause()
{
    m_initialized = true;
    m_playing = false;
}


void Sequence::Stop()
{
    if( m_playing ) Notify( SequenceEventKind::Interrupted );
    Pause();
    Seek( 0.f );
}


void Sequence::Seek( float InSeconds )
{
    m_initialized = true;
    if( !std::isfinite( InSeconds ) ) return;
    m_time = std::clamp( InSeconds, 0.f, GetDuration() );
    m_markerTime = m_time;
    Apply( GetFrame() );
}


void Sequence::Advance( float InDeltaSeconds )
{
    // A paused preview leaves the pose editable with the inspector/gizmo for key capture.
    // Saves temporarily restore authored values and request one reapplication on the next update.
    if( m_preview && !m_playing && !m_previewNeedsApply ) return;
    if( m_playing && std::isfinite( InDeltaSeconds ) && InDeltaSeconds > 0.f && std::isfinite( Speed ) && Speed > 0.f )
    {
        m_time += static_cast<double>( InDeltaSeconds ) * Speed;
        const double duration = GetDuration();
        // Markers are tested on the unwrapped time, so each lap's crossings fire once.
        if( Loop )
        {
            FireMarkers( m_markerTime, m_time );
            m_time = std::fmod( m_time, duration );
        }
        else if( m_time >= duration )
        {
            m_time = duration;
            m_playing = false;
            FireMarkers( m_markerTime, m_time );
            Notify( SequenceEventKind::Finished );
        }
        else FireMarkers( m_markerTime, m_time );
        m_markerTime = m_time;
    }
    // Only a live preview or playback owns the values. Pausing/finishing leaves the last sample.
    Apply( GetFrame() );
    m_previewNeedsApply = false;
}


void Sequence::FireMarkers( double InFrom, double InTo )
{
    if( m_preview || !Parent || ( Markers.empty() && !Loop ) ) return;
    const double duration = GetDuration();
    const double rate = std::clamp( FrameRate, 1, 240 );
    // A hitch at high speed on a short loop fires at most this many laps' markers.
    constexpr int kMaxLaps = 8;
    const int firstLap = std::max( 0, static_cast<int>( std::floor( InFrom / duration ) ) );
    const int lastLap = Loop ? static_cast<int>( std::floor( InTo / duration ) ) : firstLap;
    for( int lap = firstLap; lap <= lastLap && lap < firstLap + kMaxLaps; ++lap )
    {
        const double start = lap * duration;
        for( const SequenceMarker& marker : Markers )
        {
            const double at = start + marker.Frame / rate;
            if( at <= InFrom || at > InTo ) continue;
            SequenceEvent event;
            event.Player = Parent;
            event.Receiver = marker.ResolveReceiver( Parent );
            event.Kind = SequenceEventKind::Marker;
            event.Name = marker.Name;
            event.Payload = marker.Payload;
            event.Number = marker.Number;
            event.Frame = marker.Frame;
            event.Clip = GetActiveClipPath();
            event.Fire();
            marker.RunAction( event.Receiver );
        }
        if( Loop && start + duration > InFrom && start + duration <= InTo ) Notify( SequenceEventKind::Looped );
    }
}


void Sequence::Notify( SequenceEventKind InKind ) const
{
    if( m_preview || !Parent ) return;
    SequenceEvent event;
    event.Player = Parent;
    event.Receiver = Parent;
    event.Kind = InKind;
    event.Name = Name;
    event.Frame = static_cast<int>( std::lround( GetFrame() ) );
    event.Clip = GetActiveClipPath();
    event.Fire();
}


void Sequence::SortMarkers()
{
    std::stable_sort( Markers.begin(), Markers.end(), []( const SequenceMarker& a, const SequenceMarker& b ) { return a.Frame < b.Frame; } );
}


void Sequence::StartPreview( float InFrame, bool InPlaying )
{
    if( !m_preview )
    {
        m_saved.clear();
        for( const auto& track : Tracks )
        {
            const auto property = track.ResolveProperty();
            if( !track.Enabled || !property ) continue;
            SavedProperty saved{ track.Target, track.Component, track.Property, {} };
            Reflection::ValueToJson( *property.Ops, property.Value, saved.Value );
            m_saved.push_back( std::move( saved ) );
        }
        m_savedCamera = Camera::CurrentCamera && Camera::CurrentCamera->Parent ? Camera::CurrentCamera->Parent : EntityHandle();
        m_preview = true;
    }
    m_previewIdle = 0;
    m_playing = InPlaying;
    Seek( InFrame / std::clamp( FrameRate, 1, 240 ) );
}


void Sequence::RestorePreviewValues()
{
    m_previewNeedsApply = m_preview;
    for( const auto& saved : m_saved )
    {
        SequenceTrack track;
        track.Target = saved.Target;
        track.Component = saved.Component;
        track.Property = saved.Property;
        const auto property = track.ResolveProperty();
        if( property && Reflection::ValueFromJson( *property.Ops, property.Value, saved.Value ) )
            track.ResolveComponent()->OnPropertyChanged( saved.Property.substr( 0, saved.Property.find( '.' ) ) );
    }
    if( !Cuts.empty() && m_savedCamera && m_savedCamera->HasComponent<Camera>() ) m_savedCamera->GetComponent<Camera>().SetCurrent();
}


void Sequence::StopPreview()
{
    if( !m_preview ) return;
    RestorePreviewValues();
    m_saved.clear();
    m_savedCamera.Reset();
    m_preview = false;
    m_previewNeedsApply = false;
    m_playing = false;
    m_time = 0.;
}


void Sequence::OnDisable() { StopPreview(); }


void Sequence::OnDestroy() { StopPreview(); }


void Sequence::OnPropertyChanged( const std::string& )
{
    StopPreview();
    FrameRate = std::clamp( FrameRate, 1, 240 );
    DurationFrames = std::clamp( DurationFrames, 1, 100000 );
    Speed = std::isfinite( Speed ) ? std::clamp( Speed, 0.f, 16.f ) : 1.f;
    for( SequenceMarker& marker : Markers ) marker.Frame = std::clamp( marker.Frame, 0, DurationFrames );
    SortMarkers();
    for( SequenceCut& cut : Cuts ) cut.Frame = std::clamp( cut.Frame, 0, DurationFrames );
    std::stable_sort( Cuts.begin(), Cuts.end(), []( const SequenceCut& a, const SequenceCut& b ) { return a.Frame < b.Frame; } );
    RefreshClip();
    ResolveBindings();
}


void Sequence::OnDeserialize( const json& InJson )
{
    StopPreview();
    m_activeClip.clear();
    Reflection::FromJson( StaticType(), this, InJson );
    OnPropertyChanged( "" );
    m_time = 0.;
    m_playing = false;
    m_initialized = false;
    RefreshClip( true );
}


void Sequence::OnSerialize( json& OutJson )
{
    if( Clip.empty() ) Reflection::ToJson( StaticType(), this, OutJson );
    else
    {
        // Shared animation data belongs to the asset document. Scene saves contain only the
        // asset reference, object bindings and this instance's playback settings.
        OutJson["Clip"] = Clip;
        Reflection::ValueToJson( Reflection::GetValueOps<std::vector<SequenceBinding>>(), &Bindings, OutJson["Bindings"] );
        Reflection::ValueToJson( Reflection::GetValueOps<std::vector<SequenceClipEntry>>(), &Clips, OutJson["Clips"] );
        OutJson["PlayOnStart"] = PlayOnStart;
        OutJson["Loop"] = Loop;
        OutJson["Speed"] = Speed;
    }
}


bool Sequence::RefreshClip( bool InForce )
{
    const std::string path = GetActiveClipPath();
    if( path.empty() )
    {
        m_clipResource.reset();
        m_loadedClip.clear();
        m_clipError.clear();
        return true;
    }
    if( InForce || m_loadedClip != path || !m_clipResource )
    {
        StopPreview();
        m_loadedClip = path;
        m_clipVersion = 0;
        m_clipResource = ResourceCache::GetInstance().Get<SequenceResource>( Path( path ) );
    }
    if( !m_clipResource || m_clipResource->Version == 0 )
    {
        m_clipError = m_clipResource ? m_clipResource->Error : "Cannot load sequence asset: " + path;
        Tracks.clear();
        return false;
    }
    m_clipError = m_clipResource->Error;
    if( InForce || m_clipVersion != m_clipResource->Version )
    {
        m_clipResource->Data.Apply( *this );
        m_clipVersion = m_clipResource->Version;
        m_time = std::clamp( m_time, 0., double( GetDuration() ) );
        m_markerTime = std::min( m_markerTime, m_time );
    }
    return true;
}


void Sequence::ResolveBindings()
{
    auto resolve = [&]( const std::string& InSlot, EntityHandle& OutTarget ) {
        if( InSlot.empty() ) return;
        OutTarget.Reset();
        for( const auto& binding : Bindings )
            if( binding.Slot == InSlot ) { OutTarget = binding.Target; break; }
    };
    for( auto& track : Tracks ) resolve( track.Binding, track.Target );
    for( auto& marker : Markers ) resolve( marker.Binding, marker.Target );
    for( auto& cut : Cuts ) resolve( cut.Binding, cut.Target );
}


void Sequence::SetBinding( const std::string& InSlot, EntityHandle InTarget )
{
    StopPreview();
    for( auto& binding : Bindings )
        if( binding.Slot == InSlot ) { binding.Target = InTarget; ResolveBindings(); return; }
    Bindings.push_back( { InSlot, InTarget } );
    ResolveBindings();
}


void Sequence::PrepareAssetBindings()
{
    auto prepare = [&]( std::string& InOutSlot, const EntityHandle& InTarget ) {
        if( !InOutSlot.empty() ) return;
        for( const auto& binding : Bindings ) if( InTarget && binding.Target == InTarget ) { InOutSlot = binding.Slot; break; }
        if( !InOutSlot.empty() ) return;
        const std::string base = InTarget ? InTarget->GetName() : "Target";
        InOutSlot = base;
        int suffix = 2;
        auto exists = [&]() { return std::any_of( Bindings.begin(), Bindings.end(), [&]( const SequenceBinding& b ) { return b.Slot == InOutSlot; } ); };
        while( exists() ) InOutSlot = base + " " + std::to_string( suffix++ );
        Bindings.push_back( { InOutSlot, InTarget } );
    };
    for( auto& track : Tracks ) prepare( track.Binding, track.Target );
    // A marker for the player itself needs no slot: every instance's player receives it.
    for( auto& marker : Markers ) if( marker.Target && marker.Target != Parent ) prepare( marker.Binding, marker.Target );
    for( auto& cut : Cuts ) prepare( cut.Binding, cut.Target );
}


bool Sequence::ExportClip( const std::string& InPath, std::string& OutError )
{
    StopPreview();
    const auto tracks = Tracks;
    const auto bindings = Bindings;
    PrepareAssetBindings();
    if( !SequenceClipData::Capture( *this ).Save( InPath, OutError ) ) { Tracks = tracks; Bindings = bindings; return false; }
    const Path path( InPath );
    ResourceCache::GetInstance().OnFilesChanged( { path.FullPath } );
    Clip = path.GetLocalPathString();
    return RefreshClip( true );
}


#if USING( ME_EDITOR )
void Sequence::OnEditorInspect()
{
    ImGui::Text( "%s — %d frames at %d fps", Name.c_str(), DurationFrames, FrameRate );
    ImGui::TextDisabled( Clip.empty() ? "Animation data is embedded in this scene." : "Animation data is edited and saved in its clip asset." );
    if( !m_clipError.empty() ) ImGui::TextWrapped( "%s", m_clipError.c_str() );
    if( ImGui::Button( "Edit in Sequencer" ) )
    {
        OpenSequenceEvent event;
        event.ClipPath = Clip;
        event.PlayerGUID = Parent ? Parent->GetGUID() : 0;
        event.Fire();
    }
}
#endif
