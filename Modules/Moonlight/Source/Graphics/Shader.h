#pragma once

#include <string>
#include <fstream>
#include <sstream>
#include <iostream>

#include "Device/D3D12Device.h"
#include "Math/Matirx4.h"
#include "Math/Vector3.h"

namespace Moonlight
{
	class Shader
	{
	public:
		Shader() = default;

		// Constructor generates the shader on the fly
		Shader(const std::string& InVertexPath, const std::string& InPixelPath);

		~Shader();

		// Uses the current shader
		void Use();

		const unsigned int GetProgram() const;

		void SetMat4(const std::string& name, const Matrix4& mat) const;
		void SetInt(const std::string& name, int value) const;
		void SetVec3(const std::string& name, const Vector3& value) const;
		void SetVec3(const std::string& name, float x, float y, float z) const;
		void SetFloat(const std::string& name, float value) const;
		const bool IsLoaded() const { return isLoaded; };

	private:
#if ME_DIRECTX
		Microsoft::WRL::ComPtr<ID3D11InputLayout>	m_inputLayout;
		Microsoft::WRL::ComPtr<ID3D11VertexShader>	m_vertexShader;
		Microsoft::WRL::ComPtr<ID3D11PixelShader>	m_pixelShader;
#endif
		unsigned int Program;
		bool isLoaded = false;
	};
}