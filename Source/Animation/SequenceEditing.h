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
}
