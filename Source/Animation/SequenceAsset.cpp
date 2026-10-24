#include "PCH.h"
#include "SequenceAsset.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include "VFS/VFS.h"
#include <filesystem>
#include <set>
#include <limits>

ME_REFLECT_BEGIN( SequenceClipTrack )
    ME_FIELD( Binding );
    ME_FIELD( Component );
    ME_FIELD( Property );
    ME_FIELD( Enabled );
    ME_FIELD( Keys );
ME_REFLECT_END()

ME_REFLECT_BEGIN( SequenceClipData )
    ME_FIELD( Name );
    ME_FIELD( FrameRate );
    ME_FIELD( DurationFrames );
    ME_FIELD( Tracks );
ME_REFLECT_END()


json SequenceClipData::ToJson() const
{
    json data;
    Reflection::ToJson( StaticType(), this, data );
    data["Version"] = 1;
    return data;
}


bool SequenceClipData::FromJson( const json& InData, std::string& OutError )
{
    OutError.clear();
    auto fail = [&]( const char* reason ) { OutError = reason; return false; };
    if( !InData.is_object() || !InData.contains( "Version" ) || InData["Version"] != 1 ) return fail( "Unsupported sequence asset version" );
    if( !InData.contains( "Name" ) || !InData["Name"].is_string() || !InData.contains( "Tracks" ) || !InData["Tracks"].is_array() ) return fail( "Missing sequence name or tracks" );
    for( const char* field : { "FrameRate", "DurationFrames" } )
        if( !InData.contains( field ) || !InData[field].is_number_integer() || InData[field].get<double>() < 1. || InData[field].get<double>() > ( std::string( field ) == "FrameRate" ? 240. : 100000. ) ) return fail( "Invalid sequence timing" );
    const int duration = InData["DurationFrames"].get<int>();
    for( const auto& track : InData["Tracks"] )
    {
        if( !track.is_object() || track.contains( "Target" ) ) return fail( "Clip tracks use named bindings, not entity GUIDs" );
        for( const char* field : { "Binding", "Component", "Property" } )
            if( !track.contains( field ) || !track[field].is_string() || track[field].get<std::string>().empty() ) return fail( "A track needs a binding, component and property" );
        if( !track.contains( "Keys" ) || !track["Keys"].is_array() ) return fail( "Missing key array" );
        std::set<int> frames;
        for( const auto& key : track["Keys"] )
        {
            if( !key.is_object() || !key.contains( "Frame" ) || !key["Frame"].is_number_integer() || key["Frame"].get<double>() < 0. || key["Frame"].get<double>() > duration ) return fail( "Key outside clip duration" );
            if( !frames.insert( key["Frame"].get<int>() ).second ) return fail( "Duplicate key frame on a track" );
            if( !key.contains( "Value" ) || !key["Value"].is_array() || key["Value"].size() != 4 ) return fail( "Keys need four finite value components" );
            for( const auto& value : key["Value"] )
                if( !value.is_number() || !std::isfinite( value.get<double>() ) || std::abs( value.get<double>() ) > std::numeric_limits<float>::max() ) return fail( "Invalid key value" );
            if( !key.contains( "Interpolation" ) || !key["Interpolation"].is_string() ) return fail( "Missing key interpolation" );
            const std::string mode = key["Interpolation"].get<std::string>();
            if( mode != "Linear" && mode != "SmoothStep" && mode != "Hold" && mode != "FocalLength" && mode != "SmoothFocalLength" ) return fail( "Unknown key interpolation" );
        }
    }
    SequenceClipData parsed;
    Reflection::FromJson( StaticType(), &parsed, InData );
    for( auto& track : parsed.Tracks ) std::sort( track.Keys.begin(), track.Keys.end(), []( const SequenceKey& a, const SequenceKey& b ) { return a.Frame < b.Frame; } );
    *this = std::move( parsed );
    return true;
}


bool SequenceClipData::Load( const std::string& InPath, std::string& OutError )
{
    std::string text;
    if( !VFS::Get().ReadText( Path( InPath ), text ) || text.empty() )
    {
        OutError = "Cannot open sequence asset: " + InPath;
        return false;
    }
    return FromJson( json::parse( text, nullptr, false ), OutError );
}


bool SequenceClipData::Save( const std::string& InPath, std::string& OutError ) const
{
    SequenceClipData checked;
    const json data = ToJson();
    if( !checked.FromJson( data, OutError ) ) return false;
    const Path path( InPath );
    std::error_code error;
    std::filesystem::create_directories( std::filesystem::path( path.FullPath ).parent_path(), error );
    if( error ) { OutError = error.message(); return false; }
    std::ofstream file( path.FullPath, std::ios::trunc );
    if( !file || !( file << data.dump( 4 ) << '\n' ).flush() ) { OutError = "Cannot write sequence asset: " + InPath; return false; }
    OutError.clear();
    return true;
}


std::vector<std::string> SequenceClipData::GetSlots() const
{
    std::vector<std::string> slots;
    for( const auto& track : Tracks )
        if( std::find( slots.begin(), slots.end(), track.Binding ) == slots.end() ) slots.push_back( track.Binding );
    return slots;
}


SequenceClipData SequenceClipData::Capture( const Sequence& InSequence )
{
    SequenceClipData data;
    data.Name = InSequence.Name;
    data.FrameRate = InSequence.FrameRate;
    data.DurationFrames = InSequence.DurationFrames;
    for( const auto& track : InSequence.Tracks ) data.Tracks.push_back( { track.Binding, track.Component, track.Property, track.Enabled, track.Keys } );
    return data;
}


void SequenceClipData::Apply( Sequence& InSequence ) const
{
    InSequence.StopPreview();
    InSequence.Name = Name;
    InSequence.FrameRate = FrameRate;
    InSequence.DurationFrames = DurationFrames;
    InSequence.Tracks.clear();
    for( const auto& source : Tracks )
    {
        SequenceTrack track;
        track.Binding = source.Binding;
        track.Component = source.Component;
        track.Property = source.Property;
        track.Enabled = source.Enabled;
        track.Keys = source.Keys;
        InSequence.Tracks.push_back( std::move( track ) );
    }
    InSequence.ResolveBindings();
}


bool SequenceResource::Load()
{
    SequenceClipData loaded;
    if( !loaded.Load( FilePath.FullPath, Error ) ) return false;
    Data = std::move( loaded );
    ++Version;
    return true;
}
