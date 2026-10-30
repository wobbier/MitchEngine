#include "PCH.h"
#include "SequenceEditing.h"
#include <algorithm>
#include <limits>

bool SequenceEditing::Contains( const Selection& InSelection, Key InKey )
{
    return std::find( InSelection.begin(), InSelection.end(), InKey ) != InSelection.end();
}


SequenceEditing::Clipboard SequenceEditing::Copy( const std::vector<SequenceTrack>& InTracks, const Selection& InSelection )
{
    Clipboard clipboard;
    int first = std::numeric_limits<int>::max();
    for( const auto& key : InSelection ) first = std::min( first, key.Frame );
    for( size_t row = 0; row < InTracks.size(); ++row )
        for( const auto& key : InTracks[row].Keys )
            if( Contains( InSelection, { row, key.Frame } ) ) clipboard.push_back( { row, key.Frame - first, key } );
    return clipboard;
}


bool SequenceEditing::Paste( std::vector<SequenceTrack>& InTracks, const Clipboard& InClipboard, int InFrame, int InDuration, Selection& OutSelection )
{
    if( InClipboard.empty() || InFrame < 0 ) return false;
    for( const auto& entry : InClipboard )
        if( entry.Track >= InTracks.size() || InTracks[entry.Track].Locked || InFrame + entry.Offset > InDuration ) return false;
    OutSelection.clear();
    for( const auto& entry : InClipboard )
    {
        SequenceKey key = entry.Key;
        key.Frame = InFrame + entry.Offset;
        InTracks[entry.Track].SetKey( key );
        OutSelection.push_back( { entry.Track, key.Frame } );
    }
    return true;
}


bool SequenceEditing::Duplicate( std::vector<SequenceTrack>& InTracks, const Selection& InSelection, int InDuration, Selection& OutSelection )
{
    if( InSelection.empty() ) return false;
    int last = 0;
    for( const auto& key : InSelection ) last = std::max( last, key.Frame );
    return Paste( InTracks, Copy( InTracks, InSelection ), last + 1, InDuration, OutSelection );
}


void SequenceEditing::BoxSelect( const std::vector<SequenceTrack>& InTracks, float InFirstFrame, float InLastFrame, int InFirstRow, int InLastRow, Selection& InOutSelection, bool InAdd )
{
    if( !InAdd ) InOutSelection.clear();
    if( InFirstFrame > InLastFrame ) std::swap( InFirstFrame, InLastFrame );
    if( InFirstRow > InLastRow ) std::swap( InFirstRow, InLastRow );
    for( int row = std::max( InFirstRow, 0 ); row <= InLastRow && row < static_cast<int>( InTracks.size() ); ++row )
    {
        if( InTracks[row].Locked ) continue;
        for( const auto& key : InTracks[row].Keys )
            if( key.Frame >= InFirstFrame && key.Frame <= InLastFrame && !Contains( InOutSelection, { static_cast<size_t>( row ), key.Frame } ) )
                InOutSelection.push_back( { static_cast<size_t>( row ), key.Frame } );
    }
}


bool SequenceEditing::Move( const std::vector<SequenceTrack>& InOriginal, const Selection& InSelection, int InDelta, int InDuration,
    std::vector<SequenceTrack>& OutTracks, Selection& OutSelection, int* OutAppliedDelta )
{
    if( InSelection.empty() || InDuration < 0 ) return false;
    int first = std::numeric_limits<int>::max(), last = 0;
    for( const auto& key : InSelection )
    {
        if( key.Track >= InOriginal.size() || InOriginal[key.Track].Locked || std::none_of( InOriginal[key.Track].Keys.begin(), InOriginal[key.Track].Keys.end(), [&]( const SequenceKey& k ) { return k.Frame == key.Frame; } ) ) return false;
        first = std::min( first, key.Frame );
        last = std::max( last, key.Frame );
    }
    if( last > InDuration || first < 0 ) return false;
    const int delta = std::clamp( InDelta, -first, InDuration - last );
    for( const auto& key : InSelection )
        for( const auto& other : InOriginal[key.Track].Keys )
            if( other.Frame == key.Frame + delta && !Contains( InSelection, { key.Track, other.Frame } ) ) return false;
    auto tracks = InOriginal;
    Selection selection;
    for( size_t row = 0; row < tracks.size(); ++row )
    {
        for( auto& key : tracks[row].Keys )
            if( Contains( InSelection, { row, key.Frame } ) ) key.Frame += delta;
        std::sort( tracks[row].Keys.begin(), tracks[row].Keys.end(), []( const SequenceKey& a, const SequenceKey& b ) { return a.Frame < b.Frame; } );
    }
    for( const auto& key : InSelection ) selection.push_back( { key.Track, key.Frame + delta } );
    OutTracks = std::move( tracks );
    OutSelection = std::move( selection );
    if( OutAppliedDelta ) *OutAppliedDelta = delta;
    return true;
}


bool SequenceEditing::Delete( std::vector<SequenceTrack>& InTracks, const Selection& InSelection )
{
    bool changed = false;
    for( size_t row = 0; row < InTracks.size(); ++row )
    {
        if( InTracks[row].Locked ) continue;
        auto& keys = InTracks[row].Keys;
        const size_t before = keys.size();
        keys.erase( std::remove_if( keys.begin(), keys.end(), [&]( const SequenceKey& key ) { return Contains( InSelection, { row, key.Frame } ); } ), keys.end() );
        changed |= keys.size() != before;
    }
    return changed;
}
