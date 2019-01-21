#include "Shader.h"

#include "Utils/DirectXHelper.h"
#include "Renderer.h"
#include "Game.h"

#include <VertexTypes.h>

namespace Moonlight
{
	Shader::Shader(const std::string& InVertexPath, const std::string& InFragPath)
	{
		// Retrieve the shader source code from paths
		std::string VertexSource;
		std::string FragSource;

		try
		{
			// Open files
			std::ifstream vShaderFile("Assets/Shaders/SampleVertexShader.cso");
			std::ifstream fShaderFile("Assets/Shaders/SamplePixelShader.cso");
			std::stringstream vShaderStream, fShaderStream;

			// Read file's buffer contents into streams
			vShaderStream << vShaderFile.rdbuf();
			fShaderStream << fShaderFile.rdbuf();

			// close file handlers
			vShaderFile.close();
			fShaderFile.close();

			// Convert stream into string
			VertexSource = vShaderStream.str();
			FragSource = fShaderStream.str();
		}
		catch (std::exception e)
		{
			std::cout << "ERROR::SHADER::FILE_NOT_SUCCESFULLY_READ" << std::endl;
		}

		const char* vShaderCode = VertexSource.c_str();
		const char * fShaderCode = FragSource.c_str();

		auto dxDevice = static_cast<D3D12Device&>(Game::GetEngine().GetRenderer().GetDevice());

#if ME_PLATFORM_UWP
		// Load shaders asynchronously.
		auto loadVSTask = DX::ReadDataAsync(L"SampleVertexShader.cso");
		auto loadPSTask = DX::ReadDataAsync(L"SamplePixelShader.cso");

		// After the vertex shader file is loaded, create the shader and input layout.
		auto createVSTask = loadVSTask.then([this, dxDevice](const std::vector<byte>& fileData)
		{
#endif
			DX::ThrowIfFailed(
				dxDevice.GetD3DDevice()->CreateVertexShader(
#if ME_PLATFORM_UWP
					&fileData[0],
					fileData.size(),
#elif ME_DIRECTX
					&vShaderCode[0],
					VertexSource.size(),
#endif
					nullptr,
					&m_vertexShader
				)
			);

			/*{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "COLOR", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "TEXCOORD", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 },*/

			static const D3D11_INPUT_ELEMENT_DESC vertexDesc[] =
			{
				{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
				{ "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT,    0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
				{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			};

			DX::ThrowIfFailed(
				dxDevice.GetD3DDevice()->CreateInputLayout(
					vertexDesc,
					ARRAYSIZE(vertexDesc),
#if ME_PLATFORM_UWP
					&fileData[0],
					fileData.size(),
#elif ME_DIRECTX
					&vShaderCode[0],
					VertexSource.size(),
#endif
					&m_inputLayout
				)
			);
#if ME_PLATFORM_UWP
		});

		// After the pixel shader file is loaded, create the shader and constant buffer.
		auto createPSTask = loadPSTask.then([this, dxDevice](const std::vector<byte>& fileData)
		{
#endif
			DX::ThrowIfFailed(
				dxDevice.GetD3DDevice()->CreatePixelShader(
#if ME_PLATFORM_UWP
					&fileData[0],
					fileData.size(),
#elif ME_DIRECTX
					&fShaderCode[0],
					FragSource.size(),
#endif
					nullptr,
					&m_pixelShader
				)
			);
			isLoaded = true;
#if ME_PLATFORM_UWP
		});
#endif
	}

	Shader::~Shader()
	{
		m_vertexShader.Reset();
		m_inputLayout.Reset();
		m_pixelShader.Reset();
	}

	void Shader::Use()
	{
		if (!IsLoaded())
		{
			return;
		}
		auto dxDevice = static_cast<D3D12Device&>(Game::GetEngine().GetRenderer().GetDevice());
		dxDevice.GetD3DDeviceContext()->IASetInputLayout(m_inputLayout.Get());

		// Attach our vertex shader.
		dxDevice.GetD3DDeviceContext()->VSSetShader(
			m_vertexShader.Get(),
			nullptr,
			0
		);
		// Attach our pixel shader.
		dxDevice.GetD3DDeviceContext()->PSSetShader(
			m_pixelShader.Get(),
			nullptr,
			0
		);
	}

	const unsigned int Shader::GetProgram() const
	{
		return Program;
	}

	void Shader::SetMat4(const std::string &name, const glm::mat4 &mat) const
	{
	}

	void Shader::SetInt(const std::string &name, int value) const
	{
	}

	void Shader::SetVec3(const std::string &name, const glm::vec3 &value) const
	{
	}

	void Shader::SetVec3(const std::string &name, float x, float y, float z) const
	{
	}

	void Shader::SetFloat(const std::string &name, float value) const
	{
	}
}