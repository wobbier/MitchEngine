#include "Core/WorkStatus.h"
#include <algorithm>


WorkStatus& WorkStatus::Get()
{
    static WorkStatus instance;
    return instance;
}


uint64_t WorkStatus::Begin( const std::string& InLabel )
{
    std::lock_guard<std::mutex> lock( m_mutex );
    const uint64_t id = m_nextId++;
    m_tasks.push_back( Task{ id, InLabel, -1.f } );
    return id;
}


void WorkStatus::SetProgress( uint64_t InId, float InProgress )
{
    std::lock_guard<std::mutex> lock( m_mutex );
    for( Task& task : m_tasks )
    {
        if( task.Id == InId )
        {
            task.Progress = std::min( InProgress, 1.f );
        }
    }
}


void WorkStatus::End( uint64_t InId )
{
    std::lock_guard<std::mutex> lock( m_mutex );
    m_tasks.erase( std::remove_if( m_tasks.begin(), m_tasks.end(), [InId]( const Task& InTask ) { return InTask.Id == InId; } ), m_tasks.end() );
}


std::vector<WorkStatus::Task> WorkStatus::GetTasks() const
{
    std::lock_guard<std::mutex> lock( m_mutex );
    return m_tasks;
}


void WorkStatus::Notify( Level InLevel, const std::string& InMessage, const std::string& InActionId, const std::string& InActionLabel )
{
    std::lock_guard<std::mutex> lock( m_mutex );
    m_toasts.push_back( Toast{ InLevel, InMessage, InActionId, InActionLabel } );
    if( m_toasts.size() > 32 )
    {
        m_toasts.erase( m_toasts.begin() );
    }
}


std::vector<WorkStatus::Toast> WorkStatus::TakeToasts()
{
    std::lock_guard<std::mutex> lock( m_mutex );
    std::vector<Toast> toasts;
    toasts.swap( m_toasts );
    return toasts;
}
