#pragma once
#include "Dementia.h"
#include <memory>
#include <string>
#include <vector>

#if USING( ME_EDITOR )

// One reversible editor operation. Commands are pushed *after* the change was applied, so Redo is
// only called after an Undo. Implementations identify entities by GUID, never by pointer, so they
// stay valid across deletes and re-creates.
class UndoCommand
{
public:
    virtual ~UndoCommand() = default;

    virtual void Undo() = 0;
    virtual void Redo() = 0;
    virtual std::string GetName() const = 0;

    // Fold a newer command into this one (e.g. consecutive edits of the same field). Return true if
    // merged; `InNext` is then discarded.
    virtual bool MergeWith( const UndoCommand& InNext ) { return false; }
};

// Linear undo history with transactions (several commands undone as one step), merging, a size
// limit, and save-point tracking for the dirty flag.
class UndoStack
{
public:
    static UndoStack& Get();

    void Push( std::unique_ptr<UndoCommand> InCommand );

    // Commands pushed between Begin/End become a single history entry (nestable).
    void BeginTransaction( const std::string& InName );
    void EndTransaction();

    bool CanUndo() const;
    bool CanRedo() const;
    void Undo();
    void Redo();

    // Names for the history panel (oldest first) and the index of the next undo target.
    std::vector<std::string> GetHistory() const;
    int GetCursor() const { return m_cursor; }
    void JumpTo( int InCursor );

    void MarkSaved();
    bool IsDirty() const;
    // Marks the scene modified without an undoable command (e.g. after a play session).
    void MarkDirty();

    void Clear();
    bool IsApplying() const { return m_isApplying; }

    // While suspended (play mode) pushes are dropped and undo/redo are unavailable; the history
    // from before is kept and becomes usable again once the edit-time scene is restored.
    void SetSuspended( bool InSuspended ) { m_isSuspended = InSuspended; }
    bool IsSuspended() const { return m_isSuspended; }

    std::string GetUndoName() const;
    std::string GetRedoName() const;

private:
    class Transaction;

    std::vector<std::unique_ptr<UndoCommand>> m_commands;
    int m_cursor = 0;            // commands [0, m_cursor) are applied
    int m_savedCursor = 0;       // -1 = the saved state was discarded from history
    bool m_forcedDirty = false;
    bool m_isApplying = false;
    bool m_isSuspended = false;
    std::vector<std::unique_ptr<Transaction>> m_openTransactions;
    static constexpr size_t kMaxCommands = 256;
};

// RAII transaction.
class UndoTransaction
{
public:
    explicit UndoTransaction( const std::string& InName ) { UndoStack::Get().BeginTransaction( InName ); }
    ~UndoTransaction() { UndoStack::Get().EndTransaction(); }
    UndoTransaction( const UndoTransaction& ) = delete;
    UndoTransaction& operator=( const UndoTransaction& ) = delete;
};

#endif
