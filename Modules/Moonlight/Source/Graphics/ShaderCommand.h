#pragma once


#include "Device/DX11Device.h"
#include "Math/Matirx4.h"
#include "Math/Vector3.h"
#include <string>

namespace Moonlight
{
	class ShaderCommand
	{
	public:
		ShaderCommand() = default;

		// Constructor generates the shader on the fly
		ShaderCommand(const std::string& InVertexPath, const std::string& InPixelPath);
		ShaderCommand(const std::string& InShaderFile);

		~ShaderCommand();

		// Uses the current shader
		void Use();

		const ShaderProgram& GetProgram() const;

		void SetMat4(const std::string& name, const Matrix4& mat) const;
		void SetInt(const std::string& name, int value) const;
		void SetVec3(const std::string& name, const Vector3& value) const;
		void SetVec3(const std::string& name, float x, float y, float z) const;
		void SetFloat(const std::string& name, float value) const;
		const bool IsLoaded() const { return isLoaded; };

	private:
		ShaderProgram Program;
		bool isLoaded = false;
	};
}