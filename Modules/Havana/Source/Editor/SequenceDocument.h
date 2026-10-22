#pragma once
#include "Dementia.h"
#if USING( ME_EDITOR )
#include "Animation/SequenceAsset.h"
#include "UndoStack.h"

class SequenceDocument
{
public:
    bool Open( const std::string& InPath );
    bool Save();
    void Commit( const json& InBefore, const std::string& InLabel = "Edit Sequence Clip" );
    void ApplySnapshot( const json& InData );
    bool IsDirty() const { return History.IsDirty(); }
    SequenceClipData Data;
    UndoStack History;
    std::string Path;
    std::string Error;
};
#endif
