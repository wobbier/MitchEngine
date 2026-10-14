#pragma once
#include "JSON.h"

namespace Moonlight
{
    // Rebase untouched imported values; retain edits, including explicitly removed texture slots.
    inline json MergeImportedMaterial( const json& InCurrent, const json& InPrevious, const json& InImported )
    {
        if( InCurrent == InPrevious ) return InImported;
        if( !InCurrent.is_object() || !InImported.is_object() ) return InCurrent;
        json result = InCurrent;
        for( auto it = InImported.begin(); it != InImported.end(); ++it )
        {
            const bool previous = InPrevious.is_object() && InPrevious.contains( it.key() );
            if( !InCurrent.contains( it.key() ) )
            {
                if( !previous ) result[it.key()] = it.value();
            }
            else if( previous ) result[it.key()] = MergeImportedMaterial( InCurrent[it.key()], InPrevious[it.key()], it.value() );
            else if( InCurrent[it.key()].is_object() && it.value().is_object() )
                result[it.key()] = MergeImportedMaterial( InCurrent[it.key()], json::object(), it.value() );
        }
        return result;
    }
}
