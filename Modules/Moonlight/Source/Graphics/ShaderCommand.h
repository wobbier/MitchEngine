#pragma once
#include <string>

#include "ShaderStructures.h"
#include "bgfx/bgfx.h"

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
        ~ShaderCommand();

        const bgfx::ProgramHandle& GetProgram() const {
            return Program;
        }

        const bool IsLoaded() const {
            return isLoaded;
        };

    private:
        bgfx::ProgramHandle Program;
        bool isLoaded = false;
    };
}
