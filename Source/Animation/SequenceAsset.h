#pragma once
#include "Components/Animation/Sequence.h"
#include "Resource/Resource.h"
#include "Resource/MetaRegistry.h"

// Clip files contain binding names, never scene entity identities. A Sequence instance supplies
// those bindings, so a single animation can be reused on other cameras, actors and prefabs.
struct SequenceClipTrack
{
    ME_REFLECTABLE( SequenceClipTrack )
public:
    std::string Binding;
    std::string Component = "Transform";
    std::string Property = "Position";
    bool Enabled = true;
    std::vector<SequenceKey> Keys;
};

struct SequenceClipData
{
    ME_REFLECTABLE( SequenceClipData )
public:
    std::string Name = "Sequence";
    int FrameRate = 30;
    int DurationFrames = 240;
    std::vector<SequenceClipTrack> Tracks;

    json ToJson() const;
    bool FromJson( const json& InData, std::string& OutError );
    bool Load( const std::string& InPath, std::string& OutError );
    bool Save( const std::string& InPath, std::string& OutError ) const;
    std::vector<std::string> GetSlots() const;
    static SequenceClipData Capture( const Sequence& InSequence );
    void Apply( Sequence& InSequence ) const;
};

class SequenceResource final : public Resource
{
public:
    explicit SequenceResource( const Path& InPath ) : Resource( InPath ) {}
    bool Load() override;
    void Reload() override { Load(); }
    SequenceClipData Data;
    uint32_t Version = 0;
    std::string Error;
};

struct SequenceMetadata final : public MetaBase
{
    explicit SequenceMetadata( const Path& InPath ) : MetaBase( InPath ) {}
    std::string GetExtension2() const override { return "sequence"; }
    void OnSerialize( json& ) override {}
    void OnDeserialize( const json& ) override {}
};

ME_REGISTER_METADATA( "sequence", SequenceMetadata )
