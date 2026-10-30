#pragma once
#include "Components/Animation/Sequence.h"

namespace SequenceEditing
{
    // Frame identities survive sorting; a group move returns the new selection identities.
    struct Key
    {
        size_t Track = 0;
        int Frame = 0;
        bool operator==( const Key& InOther ) const { return Track == InOther.Track && Frame == InOther.Frame; }
    };
    using Selection = std::vector<Key>;

    bool Contains( const Selection& InSelection, Key InKey );
    void BoxSelect( const std::vector<SequenceTrack>& InTracks, float InFirstFrame, float InLastFrame, int InFirstRow, int InLastRow, Selection& InOutSelection, bool InAdd = false );
    // Atomic: collisions with unselected keys reject the entire move. Bounds clamp the common
    // delta, preserving spacing across tracks. No key is silently overwritten.
    bool Move( const std::vector<SequenceTrack>& InOriginal, const Selection& InSelection, int InDelta, int InDuration,
        std::vector<SequenceTrack>& OutTracks, Selection& OutSelection, int* OutAppliedDelta = nullptr );
    bool Delete( std::vector<SequenceTrack>& InTracks, const Selection& InSelection );

    // Copied keys keep their track and their offset from the earliest copied frame (GAME-4).
    struct ClipboardKey
    {
        size_t Track = 0;
        int Offset = 0;
        SequenceKey Key;
    };
    using Clipboard = std::vector<ClipboardKey>;
    Clipboard Copy( const std::vector<SequenceTrack>& InTracks, const Selection& InSelection );
    // Pastes with the earliest key at InFrame, replacing keys already on those frames (a paste is
    // explicit, like keying a value). Atomic: a key past the end, a missing or locked track
    // rejects all of it. OutSelection is the pasted keys.
    bool Paste( std::vector<SequenceTrack>& InTracks, const Clipboard& InClipboard, int InFrame, int InDuration, Selection& OutSelection );
    // Copies the selection to just after its last key.
    bool Duplicate( std::vector<SequenceTrack>& InTracks, const Selection& InSelection, int InDuration, Selection& OutSelection );
}
