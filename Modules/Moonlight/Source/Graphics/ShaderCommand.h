#pragma once
#include <string>

#include "ShaderStructures.h"
#include "bgfx/bgfx.h"
#include "Pointers.h"
#include "Utils/BGFXUtils.h"

namespace Moonlight
{
    class ShaderCommand
    {
        friend class BGFXRenderer;
    public:
        ShaderCommand();

        // Constructor generates the shader on the fly
        ShaderCommand( const std::string& InShaderFile );
        ShaderCommand( const std::string& InVertexShaderPath, const std::string& InFragShaderPath );

        // Always current: shader hot reloads rebuild the shared program in place, so read this at
        // submit time rather than caching the handle.
        const bgfx::ProgramHandle& GetProgram() const
        {
            static const bgfx::ProgramHandle kInvalid = BGFX_INVALID_HANDLE;
            return m_programRef ? m_programRef->Handle : kInvalid;
        }

        const bool IsLoaded() const
        {
            return m_programRef && bgfx::isValid( m_programRef->Handle );
        };

    private:
        // Shared by every material copy (and every user of the same shader pair); the program is
        // destroyed when the last reference goes away.
        SharedPtr<ProgramRef> m_programRef;
    };
}
