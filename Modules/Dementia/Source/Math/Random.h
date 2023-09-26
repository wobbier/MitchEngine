#pragma once
#include <random>

class Random64
{
public:
    Random64()
        : Twister( std::random_device {}( ) )
    {
    }

    unsigned long long operator()( unsigned long long Min, unsigned long long Max )
    {
        std::uniform_int_distribution<unsigned long long> distribution( Min, Max );
        return distribution( Twister );
    }

private:
    std::mt19937_64 Twister;
};