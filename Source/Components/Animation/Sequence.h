#pragma once
#include "ECS/Component.h"
#include "ECS/ComponentDetail.h"
#include "Events/Event.h"

enum class SequenceInterpolation { Linear, SmoothStep, Hold, FocalLength, SmoothFocalLength };
class SequenceResource;

struct SequenceKey
{
    ME_REFLECTABLE( SequenceKey )
public:
    int Frame = 0;
    // Scalars use x; vectors use xy/xyz/xyzw; quaternions use xyzw.
    Vector4 Value;
    SequenceInterpolation Interpolation = SequenceInterpolation::SmoothStep;
};

struct SequenceTrack
{
    ME_REFLECTABLE( SequenceTrack )
public:
    EntityHandle Target;
    std::string Binding;
    std::string Component = "Transform";
    std::string Property = "Position";
    bool Enabled = true;
    std::vector<SequenceKey> Keys;

    BaseComponent* ResolveComponent() const;
    Reflection::PropertyRef ResolveProperty() const;
    bool Capture( int InFrame );
    void SetKey( const SequenceKey& InKey );
    bool Sample( float InFrame, Vector4& OutValue ) const;
    bool Apply( float InFrame ) const;

    // Writable, serializable floating-point/vector/quaternion fields, including nested structs.
    static std::vector<std::string> GetProperties( BaseComponent& InComponent );
};

struct SequenceBinding
{
    ME_REFLECTABLE( SequenceBinding )
public:
    std::string Slot;
    EntityHandle Target;
};

// A named moment for gameplay (GAME-4). Runtime playback fires it once each time it crosses the
// marker's frame: a SequenceEvent natively, and SequenceEvent / ISequenceReceiver in C#.
struct SequenceMarker
{
    ME_REFLECTABLE( SequenceMarker )
public:
    int Frame = 0;
    std::string Name;
    // The receiving object. Clip assets name a binding slot; with neither, it's the player itself.
    EntityHandle Target;
    std::string Binding;
    // Free-form arguments for the receiver.
    std::string Payload;
    float Number = 0.f;

    // An unbound slot has no receiver: the event still reaches EventBus subscribers.
    EntityHandle ResolveReceiver( const EntityHandle& InPlayer ) const { return Target ? Target : Binding.empty() ? InPlayer : EntityHandle(); }
};

enum class SequenceEventKind { Marker, Started, Finished, Interrupted, Looped };

// Fired synchronously from the sequence's update (ScriptCore queues it for C#). Notifications
// (Started, Finished, Interrupted, Looped) are sent to the player's own entity.
class SequenceEvent
    : public Event<SequenceEvent>
{
public:
    SequenceEvent()
        : Event()
    {
    }

    EntityHandle Player;
    EntityHandle Receiver;
    SequenceEventKind Kind = SequenceEventKind::Marker;
    std::string Name;
    std::string Payload;
    float Number = 0.f;
    int Frame = 0;
    std::string Clip;
};

struct SequenceClipEntry
{
    ME_REFLECTABLE( SequenceClipEntry )
public:
    std::string Name;
    std::string Clip;
};

// Authored object/property animation, independent of imported skeletal clips. Tracks reference
// scene entities by GUID and resolve fields each sample, so component removal cannot stale pointers.
class Sequence final : public Component<Sequence>
{
    ME_REFLECTABLE( Sequence )
    friend class SequenceCore;
public:
    Sequence();
    std::string Name = "Sequence";
    int FrameRate = 30;
    int DurationFrames = 240;
    bool PlayOnStart = true;
    bool Loop = false;
    float Speed = 1.f;
    std::vector<SequenceTrack> Tracks;
    // Kept sorted by frame; simultaneous markers fire in list order.
    std::vector<SequenceMarker> Markers;
    std::string Clip;
    std::vector<SequenceBinding> Bindings;
    std::vector<SequenceClipEntry> Clips;

    bool RefreshClip( bool InForce = false );
    void ResolveBindings();
    void SetBinding( const std::string& InSlot, EntityHandle InTarget );
    void PrepareAssetBindings();
    bool ExportClip( const std::string& InPath, std::string& OutError );
    const std::string& GetClipError() const { return m_clipError; }

    void Play( bool InRestart = true );
    // Named entries and direct asset playback share this player's bindings. Switching clips
    // starts at frame zero; resume applies only when the requested asset is already active.
    bool HasClip( const std::string& InName ) const;
    bool PlayClip( const std::string& InName, bool InRestart = true );
    bool PlayAsset( const std::string& InPath, bool InRestart = true );
    const std::string& GetActiveClipPath() const { return m_activeClip.empty() ? Clip : m_activeClip; }
    void Pause();
    // Stop rewinds and applies the first frame. A completed sequence holds its last frame.
    // Stopping (or replacing) running playback sends Interrupted; Pause sends nothing.
    void Stop();
    // Seeking never fires markers: playback continues from the new time.
    void Seek( float InSeconds );
    bool IsPlaying() const { return m_playing; }
    float GetTime() const { return static_cast<float>( m_time ); }
    float GetFrame() const;
    float GetDuration() const;
    void Apply( float InFrame ) const;

    void StartPreview( float InFrame = 0.f, bool InPlaying = false );
    void StopPreview();
    void RestorePreviewValues();
    void TouchPreview() { m_previewIdle = 0; }
    bool IsPreviewing() const { return m_preview; }
    void OnDisable() override;
    void OnDestroy() override;
    void OnPropertyChanged( const std::string& InFieldName ) override;
#if USING( ME_EDITOR )
    void OnEditorInspect() override;
#endif

private:
    struct SavedProperty
    {
        EntityHandle Target;
        std::string Component;
        std::string Property;
        json Value;
    };
    std::vector<SavedProperty> m_saved;
    double m_time = 0.;
    bool m_playing = false;
    bool m_initialized = false;
    bool m_preview = false;
    bool m_previewNeedsApply = false;
    int m_previewIdle = 0;
    SharedPtr<SequenceResource> m_clipResource;
    std::string m_loadedClip;
    std::string m_clipError;
    uint32_t m_clipVersion = 0;
    std::string m_activeClip;
    // Markers fire for times in (m_markerTime, now]; Play from the start puts it just before 0.
    double m_markerTime = 0.;
    void Advance( float InDeltaSeconds );
    void FireMarkers( double InFrom, double InTo );
    void Notify( SequenceEventKind InKind ) const;
    void SortMarkers();
    void OnDeserialize( const json& InJson ) override;
    void OnSerialize( json& OutJson ) override;
};

ME_REGISTER_COMPONENT_FOLDER( Sequence, "Animation" )
