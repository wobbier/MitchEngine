#include "UndoStack.h"

#if USING( ME_EDITOR )

#include "CLog.h"
#include <algorithm>

class UndoStack::Transaction
    : public UndoCommand
{
public:
    explicit Transaction( std::string InName ) : m_name( std::move( InName ) ) {}

    void Undo() override
    {
        for( auto it = Commands.rbegin(); it != Commands.rend(); ++it )
        {
            ( *it )->Undo();
        }
    }

    void Redo() override
    {
        for( auto& command : Commands )
        {
            command->Redo();
        }
    }

    std::string GetName() const override { return m_name; }

    bool AffectsSavedState() const override
    {
        for( const auto& command : Commands ) if( command->AffectsSavedState() ) return true;
        return false;
    }

    std::vector<std::unique_ptr<UndoCommand>> Commands;

private:
    std::string m_name;
};


UndoStack::UndoStack() = default;


UndoStack::~UndoStack() = default;


UndoStack& UndoStack::Get()
{
    static UndoStack instance;
    return instance;
}


void UndoStack::Push( std::unique_ptr<UndoCommand> InCommand )
{
    if( !InCommand || m_isApplying || m_isSuspended )
    {
        return;
    }

    if( !m_openTransactions.empty() )
    {
        auto& commands = m_openTransactions.back()->Commands;
        if( commands.empty() || commands.back()->AffectsSavedState() != InCommand->AffectsSavedState() || !commands.back()->MergeWith( *InCommand ) )
        {
            commands.push_back( std::move( InCommand ) );
        }
        return;
    }

    // Anything that could be redone is discarded by a new edit.
    if( m_cursor < static_cast<int>( m_commands.size() ) )
    {
        if( m_savedCursor > m_cursor )
        {
            bool discardedData = false;
            for( int i = m_cursor; i < m_savedCursor; ++i ) discardedData |= m_commands[i]->AffectsSavedState();
            m_savedCursor = discardedData ? -1 : m_cursor;
        }
        m_commands.erase( m_commands.begin() + m_cursor, m_commands.end() );
    }

    if( m_cursor > 0 && m_cursor != m_savedCursor && m_commands[m_cursor - 1]->AffectsSavedState() == InCommand->AffectsSavedState() && m_commands[m_cursor - 1]->MergeWith( *InCommand ) )
    {
        return;
    }

    m_commands.push_back( std::move( InCommand ) );
    ++m_cursor;

    if( m_commands.size() > kMaxCommands )
    {
        const bool discardedData = m_commands.front()->AffectsSavedState();
        m_commands.erase( m_commands.begin() );
        --m_cursor;
        m_savedCursor = m_savedCursor > 0 ? m_savedCursor - 1 : ( m_savedCursor == 0 && !discardedData ? 0 : -1 );
    }
}


void UndoStack::BeginTransaction( const std::string& InName )
{
    m_openTransactions.push_back( std::make_unique<Transaction>( InName ) );
}


void UndoStack::EndTransaction()
{
    if( m_openTransactions.empty() )
    {
        return;
    }
    std::unique_ptr<Transaction> transaction = std::move( m_openTransactions.back() );
    m_openTransactions.pop_back();
    if( transaction->Commands.empty() )
    {
        return;
    }
    if( transaction->Commands.size() == 1 && m_openTransactions.empty() )
    {
        // A one-command transaction is just that command (keeps its own name and merging).
        Push( std::move( transaction->Commands.front() ) );
        return;
    }
    Push( std::move( transaction ) );
}


bool UndoStack::CanUndo() const
{
    return m_cursor > 0 && m_openTransactions.empty() && !m_isSuspended;
}


bool UndoStack::CanRedo() const
{
    return m_cursor < static_cast<int>( m_commands.size() ) && m_openTransactions.empty() && !m_isSuspended;
}


void UndoStack::Undo()
{
    if( !CanUndo() )
    {
        return;
    }
    m_isApplying = true;
    --m_cursor;
    m_commands[m_cursor]->Undo();
    m_isApplying = false;
    CLog::Log( CLog::LogType::Info, "Undo: " + m_commands[m_cursor]->GetName() );
}


void UndoStack::Redo()
{
    if( !CanRedo() )
    {
        return;
    }
    m_isApplying = true;
    m_commands[m_cursor]->Redo();
    ++m_cursor;
    m_isApplying = false;
    CLog::Log( CLog::LogType::Info, "Redo: " + m_commands[m_cursor - 1]->GetName() );
}


std::vector<std::string> UndoStack::GetHistory() const
{
    std::vector<std::string> names;
    names.reserve( m_commands.size() );
    for( const auto& command : m_commands )
    {
        names.push_back( command->GetName() );
    }
    return names;
}


void UndoStack::JumpTo( int InCursor )
{
    while( m_cursor > InCursor && CanUndo() )
    {
        Undo();
    }
    while( m_cursor < InCursor && CanRedo() )
    {
        Redo();
    }
}


void UndoStack::MarkSaved()
{
    m_savedCursor = m_cursor;
    m_forcedDirty = false;
}


bool UndoStack::IsDirty() const
{
    if( m_forcedDirty || m_savedCursor < 0 ) return true;
    for( int i = std::min( m_cursor, m_savedCursor ); i < std::max( m_cursor, m_savedCursor ); ++i )
        if( m_commands[i]->AffectsSavedState() ) return true;
    return false;
}


void UndoStack::MarkDirty()
{
    m_forcedDirty = true;
}


void UndoStack::Clear()
{
    m_commands.clear();
    m_openTransactions.clear();
    m_cursor = 0;
    m_savedCursor = 0;
    m_forcedDirty = false;
}


std::string UndoStack::GetUndoName() const
{
    return CanUndo() ? m_commands[m_cursor - 1]->GetName() : std::string();
}


std::string UndoStack::GetRedoName() const
{
    return CanRedo() ? m_commands[m_cursor]->GetName() : std::string();
}

#endif
