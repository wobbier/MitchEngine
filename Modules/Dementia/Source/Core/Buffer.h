#pragma once

#include <stdint.h>
#include <memory>
#include <assert.h>

// An owned byte array. Move-only: a copy of the old raw-pointer version shared its allocation and
// the pair would double free (or neither would free) on Release.
class Buffer
{
public:
    uint64_t Size = 0;

    Buffer() = default;
    explicit Buffer( uint64_t size )
    {
        Allocate( size );
    }
    Buffer( const Buffer& ) = delete;
    Buffer& operator=( const Buffer& ) = delete;
    Buffer( Buffer&& ) noexcept = default;
    Buffer& operator=( Buffer&& ) noexcept = default;
    ~Buffer() = default;

    void Allocate( uint64_t size )
    {
        assert( !m_data );
        m_data = std::make_unique<uint8_t[]>( size );
        Size = size;
    }

    void Release()
    {
        m_data.reset();
        Size = 0;
    }

    uint8_t* Data()
    {
        return m_data.get();
    }

    const uint8_t* Data() const
    {
        return m_data.get();
    }

    explicit operator bool() const
    {
        return Size > 0 && m_data != nullptr;
    }

private:
    std::unique_ptr<uint8_t[]> m_data;
};
