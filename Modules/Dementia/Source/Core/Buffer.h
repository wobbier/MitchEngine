#pragma once

#include <stdint.h>
#include <memory>
#include <assert.h>
#include <Core/Memory.h>

class Buffer
{
public:
    uint8_t* Data = nullptr;
    uint64_t Size = 0;

    Buffer() = default;
    Buffer( const Buffer& buffer ) = default;

    Buffer( uint64_t size )
        : Size( size )
    {
        Allocate( Size );
    }

    void Allocate( uint64_t size )
    {
        assert( !Data );
        Data = ME_NEW uint8_t[size];
        Size = size;
    }

    void Release()
    {
        delete[] Data;
        Data = nullptr;
        Size = 0;
    }

    operator bool()
    {
        return Size > 0;
    }
};


class ScopedBuffer
{
    Buffer Buf;

    ScopedBuffer() = delete;
    ScopedBuffer( uint64_t size )
        : Buf( size )
    {
    }

    ScopedBuffer( const Buffer& buffer )
        : Buf( buffer )
    {
    }

    ~ScopedBuffer()
    {
        Buf.Release();
    }
};