#include <doctest/doctest.h>
#include "../../Modules/Havana/Source/Editor/UndoStack.h"
#if USING( ME_EDITOR )

namespace
{
    class HistoryChange final : public UndoCommand
    {
    public:
        explicit HistoryChange( bool data = true ) : Data( data ) {}
        void Undo() override {}
        void Redo() override {}
        std::string GetName() const override { return "Change"; }
        bool AffectsSavedState() const override { return Data; }
    private:
        bool Data;
    };
    void Push( UndoStack& stack, bool data = true ) { stack.Push( std::make_unique<HistoryChange>( data ) ); }
}

TEST_CASE( "Editor history: tool settings undo chronologically without dirtying saved data" )
{
    UndoStack stack;
    Push( stack, false ); CHECK_FALSE( stack.IsDirty() );
    Push( stack ); CHECK( stack.IsDirty() );
    Push( stack, false ); stack.Undo(); CHECK( stack.IsDirty() );
    stack.Undo(); CHECK_FALSE( stack.IsDirty() );
    stack.Undo(); CHECK_FALSE( stack.IsDirty() );
    stack.Redo(); stack.Redo(); stack.MarkSaved();
    stack.Redo(); CHECK_FALSE( stack.IsDirty() );
    stack.Undo(); CHECK_FALSE( stack.IsDirty() );
    stack.Undo(); CHECK( stack.IsDirty() );
}

TEST_CASE( "Editor history: replacing only saved tool settings retains the source save point" )
{
    UndoStack stack;
    Push( stack ); Push( stack, false ); stack.MarkSaved();
    stack.Undo(); Push( stack, false ); CHECK_FALSE( stack.IsDirty() );
    stack.Undo(); stack.Undo(); CHECK( stack.IsDirty() );
    Push( stack, false ); CHECK( stack.IsDirty() ); // The saved height edit was discarded.
    stack.Undo(); CHECK( stack.IsDirty() );
}

TEST_CASE( "Editor history: pruning tool settings preserves clean data and transactions classify their contents" )
{
    UndoStack stack;
    for( int i = 0; i < 300; ++i ) Push( stack, false );
    CHECK( stack.GetHistory().size() == 256 ); CHECK_FALSE( stack.IsDirty() );
    stack.JumpTo( 0 ); CHECK_FALSE( stack.IsDirty() );
    stack.Clear();
    stack.BeginTransaction( "Tools" ); Push( stack, false ); Push( stack, false ); stack.EndTransaction();
    CHECK_FALSE( stack.IsDirty() ); CHECK( stack.GetHistory().size() == 1 );
    stack.BeginTransaction( "Mixed" ); Push( stack, false ); Push( stack ); stack.EndTransaction();
    CHECK( stack.IsDirty() ); stack.Undo(); CHECK_FALSE( stack.IsDirty() );
    stack.MarkDirty(); stack.Undo(); CHECK( stack.IsDirty() );
    stack.MarkSaved(); CHECK_FALSE( stack.IsDirty() );
    stack.Clear(); Push( stack );
    for( int i = 0; i < 256; ++i ) Push( stack, false );
    stack.JumpTo( 0 ); CHECK( stack.IsDirty() ); // Pruned source changes cannot be undone back to disk.
}
#endif
