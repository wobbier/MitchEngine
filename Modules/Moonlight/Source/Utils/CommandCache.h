#pragma once
#include <deque>
#include <queue>
#include <mutex>
#include <vector>
#include <cstdint>

template<typename T>
struct CommandCache
{
    unsigned int Push( const T& inCommand );
    void Update( unsigned int Id, T& inCommand );
    void Pop( unsigned int Id );
    void Clear();

    T* Get( unsigned int Id );

    // Change tracking (opt-in, ME-390): with TrackChanges on, Push, Pop, Update and Clear note the
    // ids they touch so a consumer can refresh what it derives from the commands (the renderer's
    // retained shadow casters) instead of rescanning every command each frame. Anyone writing a
    // command in place (through Get() or Commands[]) calls MarkChanged( id ) too. Changed is one
    // byte per id, so parallel writers of different ids never race; AllChanged says "rescan
    // everything" (after Clear, or when tracking starts).
    bool TrackChanges = false;
    std::vector<uint8_t> Changed;
    bool AllChanged = true;
    void MarkChanged( unsigned int Id )
    {
        if( TrackChanges && Id < Changed.size() )
        {
            Changed[Id] = 1;
        }
    }

    // A deque: Get() hands out pointers that callers keep (CameraData*, MeshCommand*), and a deque's
    // push_back never moves the existing elements the way a growing vector does.
    std::deque<T> Commands;
    std::queue<unsigned int> FreeIndicies;
    std::mutex CommandMutex;
};

template<typename T>
void CommandCache<T>::Clear()
{
    CommandMutex.lock();
    Commands.clear();
    FreeIndicies = std::queue<unsigned int>();
    Changed.clear();
    AllChanged = true;
    CommandMutex.unlock();
}

template<typename T>
unsigned int CommandCache<T>::Push( const T& inCommand )
{
    std::lock_guard<std::mutex> lock( CommandMutex );
    unsigned int index = 0;
    if( !FreeIndicies.empty() )
    {
        index = FreeIndicies.front();
        FreeIndicies.pop();
        Commands[index] = std::move( inCommand );
    }
    else
    {
        Commands.push_back( std::move( inCommand ) );
        index = static_cast<unsigned int>( Commands.size() - 1 );
    }
    if( TrackChanges )
    {
        if( Changed.size() < Commands.size() )
        {
            Changed.resize( Commands.size(), 0 );
        }
        Changed[index] = 1;
    }

    return index;
}

template<typename T>
void CommandCache<T>::Update( unsigned int Id, T& inCommand )
{
    if( Id >= Commands.size() )
    {
        return;
    }

    Commands[Id] = inCommand;
    MarkChanged( Id );
}

template<typename T>
void CommandCache<T>::Pop( unsigned int Id )
{
    if( Id >= Commands.size() )
    {
        return;
    }

    FreeIndicies.push( Id );
    Commands[Id] = T();
    MarkChanged( Id );
}

template<typename T>
T* CommandCache<T>::Get( unsigned int Id )
{
    if( Id >= Commands.size() )
    {
        return nullptr;
    }

    return &Commands[Id];
}
