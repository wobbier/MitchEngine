#pragma once
#include <string>

#include "ShaderStructures.h"
#include "bgfx/bgfx.h"
#include "Pointers.h"

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

        const bgfx::ProgramHandle& GetProgram() const {
            return Program;
        }

        const bool IsLoaded() const {
            return isLoaded;
        };

    private:
        // Owns one bgfx reference to the program. Shared so material copies
        // reuse it, and the program is destroyed when the last copy goes away.
        struct ProgramRef
        {
            explicit ProgramRef( bgfx::ProgramHandle inHandle );
            ~ProgramRef();
            bgfx::ProgramHandle Handle;
        };

        void SetProgram( bgfx::ProgramHandle inProgram );

        SharedPtr<ProgramRef> m_programRef;
        bgfx::ProgramHandle Program;
        bool isLoaded = false;
    };
}
