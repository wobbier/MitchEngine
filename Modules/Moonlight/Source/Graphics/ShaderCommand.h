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
		ShaderCommand() = default;

		// Constructor generates the shader on the fly
		ShaderCommand(const std::string& InShaderFile);
		~ShaderCommand();

		const ShaderProgram& GetProgram() const;

		const bool IsLoaded() const { return isLoaded; };

	private:
		bgfx::ProgramHandle Program;
		bool isLoaded = false;
	};
}