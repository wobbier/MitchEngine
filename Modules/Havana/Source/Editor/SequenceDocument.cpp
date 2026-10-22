#include "SequenceDocument.h"
#if USING( ME_EDITOR )
#include "Resource/ResourceCache.h"
#include "Path.h"

namespace
{
    class ClipEdit final : public UndoCommand
    {
    public:
        ClipEdit( SequenceDocument& InDocument, json InBefore, json InAfter, std::string InLabel )
            : Document( InDocument ), Before( std::move( InBefore ) ), After( std::move( InAfter ) ), Label( std::move( InLabel ) ) {}
        void Undo() override { Document.ApplySnapshot( Before ); }
        void Redo() override { Document.ApplySnapshot( After ); }
        std::string GetName() const override { return Label; }
    private:
        SequenceDocument& Document;
        json Before, After;
        std::string Label;
    };
}


bool SequenceDocument::Open( const std::string& InPath )
{
    SequenceClipData data;
    if( !data.Load( InPath, Error ) ) return false;
    Data = std::move( data );
    Path = ::Path( InPath ).GetLocalPathString();
    History.Clear();
    History.MarkSaved();
    return true;
}


bool SequenceDocument::Save()
{
    if( !Data.Save( Path, Error ) ) return false;
    ResourceCache::GetInstance().LoadMetadata( ::Path( Path ) );
    ResourceCache::GetInstance().OnFilesChanged( { ::Path( Path ).FullPath } );
    History.MarkSaved();
    return true;
}


void SequenceDocument::Commit( const json& InBefore, const std::string& InLabel )
{
    const json after = Data.ToJson();
    if( InBefore != after ) History.Push( std::make_unique<ClipEdit>( *this, InBefore, after, InLabel ) );
}


void SequenceDocument::ApplySnapshot( const json& InData )
{
    Data.FromJson( InData, Error );
}
#endif
