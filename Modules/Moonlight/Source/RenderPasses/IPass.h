#pragma once

namespace Moonlight
{
    class IPass
    {
    public:
        IPass() = default;

        virtual bool IsSupported() = 0;
    };
}