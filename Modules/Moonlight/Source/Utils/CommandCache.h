#pragma once
#include <vector>
#include <queue>
#include <mutex>

template<typename T>
struct CommandCache
{
    unsigned int Push( const T& inCommand );
    void Update( unsigned int Id, T& inCommand );
    void Pop( unsigned int Id );

    T* Get( unsigned int Id );

    std::vector<T> Commands;
    std::queue<unsigned int> FreeIndicies;
    std::mutex CommandMutex;
};

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
}

template<typename T>
void CommandCache<T>::Pop( unsigned int Id )
{
    if( Id > Commands.size() )
    {
        return;
    }

    FreeIndicies.push( Id );
    Commands[Id] = T();
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