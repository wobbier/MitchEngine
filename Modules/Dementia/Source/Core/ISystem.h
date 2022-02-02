#pragma once
#include "Hash.h"
#include <stdint.h>

class ISystem
{
public:
    virtual uint32_t GetSystemID() const = 0;
};

#define ME_SYSTEM_ID(Name)\
constexpr static uint32_t const sSystemID = Hash::FNV1a::GetHash32(#Name);\
virtual uint32_t GetSystemID() const final { return Name::sSystemID; }