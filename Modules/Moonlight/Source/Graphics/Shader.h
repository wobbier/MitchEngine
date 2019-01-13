#pragma once

#include <string>
#include <fstream>
#include <sstream>
#include <iostream>

#include <glm.hpp>
#include "Device/D3D12Device.h"

namespace Moonlight
{
	class Shader
	{
	public:
		Shader() = default;

		// Constructor generates the shader on the fly
		Shader(const std::string& InVertexPath, const std::string& InFragPath);

		~Shader();

		// Uses the current shader
		void Use();

		const unsigned int GetProgram() const;

		void SetMat4(const std::string &name, const glm::mat4 &mat) const;
		void SetInt(const std::string &name, int value) const;
		void SetVec3(const std::string &name, const glm::vec3 &value) const;
		void SetVec3(const std::string &name, float x, float y, float z) const;
		void SetFloat(const std::string &name, float value) const;
		const bool IsLoaded() const { return isLoaded; };

	private:
#if ME_PLATFORM_UWP
		Microsoft::WRL::ComPtr<ID3D11InputLayout>	m_inputLayout;
		Microsoft::WRL::ComPtr<ID3D11VertexShader>	m_vertexShader;
		Microsoft::WRL::ComPtr<ID3D11PixelShader>	m_pixelShader;
#endif
		unsigned int Program;
		bool isLoaded = false;
	};
}