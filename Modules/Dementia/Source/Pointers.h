#pragma once
#include <memory>

template <typename T, typename TDeleter = std::default_delete<T>>
using UniquePtr = std::unique_ptr<T, TDeleter>;

template<class T>
using SharedPtr = std::shared_ptr<T>;

template<class T>
using WeakPtr = std::weak_ptr<T>;

template<typename T, typename... Args>
inline SharedPtr<T> MakeShared( Args&&... InArgs )
{
    return std::make_shared<T, Args...>( std::forward<Args>( InArgs )... );
}

template<typename T, typename... Args>
inline UniquePtr<T> MakeUnique( Args&&... InArgs )
{
    return std::make_unique<T, Args...>( std::forward<Args>( InArgs )... );
}