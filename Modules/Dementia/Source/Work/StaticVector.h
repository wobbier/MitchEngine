#pragma once
#include <vector>

template<typename T>
class StaticVector
{
public:
    StaticVector() = delete;
    StaticVector( std::size_t InSize )
        : Count( InSize * sizeof( T ) )
        , End( 0 )
    {
        Vector.resize( Count );
    }

    T& operator[]( std::size_t i )
    {
        return *reinterpret_cast<T*>( &Vector[sizeof( T ) * i] );
    }

    std::size_t CurrentSize()
    {
        return End;
    }
    std::size_t Capacity()
    {
        return Count;
    }

    template<typename... Args>
    T& EmplaceBack( Args&&... args )
    {
        T* ele = reinterpret_cast<T*>( &Vector[sizeof( T ) * End++] );
        new( ele ) T { std::forward<Args>( args )... };
        return *ele;
    }

private:
    std::vector<char> Vector;
    std::size_t End;
    std::size_t Count;
};