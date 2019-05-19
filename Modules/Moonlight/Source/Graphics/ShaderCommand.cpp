#include "ShaderCommand.h"

#include "Utils/DirectXHelper.h"
#include "Renderer.h"
#include "Game.h"

#include <VertexTypes.h>
#include "Texture.h"
#include "optick.h"
#include "Content/ShaderFile.h"
#include "Resource/ResourceCache.h"

namespace Moonlight
{
	//hlsl files - ShaderFile
	//shader asset - shader
	//shader backend - shaderdata
	//
	//shader has a shader file and shader data
	//
	//shader has shader type
	//
	//editor loads mesh with default shader
	//	shader stuff exported on mesh component
	//
	//mesh loads, created default shader
	//, editor loads on top points to new shader

	ShaderCommand::ShaderCommand(const std::string& InVertexPath, const std::string& InPixelPath)
	{
		// I should compile the hlsl at runtime in debug

		// Retrieve the shader source code from paths
		FilePath vPath(InVertexPath);
		FilePath fPath(InPixelPath);
		std::vector<char> VertexSource = ResourceCache::GetInstance().Get<ShaderFile>(vPath)->Data;
		std::vector<char> FragSource = ResourceCache::GetInstance().Get<ShaderFile>(fPath)->Data;
		
		//try
		//{
		//	// Open files
		//	std::ifstream vShaderFile(vPath.FullPath);
		//	std::ifstream fShaderFile(fPath.FullPath);
		//	std::stringstream vShaderStream, fShaderStream;

		//	// Read file's buffer contents into streams
		//	vShaderStream << vShaderFile.rdbuf();
		//	fShaderStream << fShaderFile.rdbuf();

		//	// close file handlers
		//	vShaderFile.close();
		//	fShaderFile.close();

		//	// Convert stream into string
		//	VertexSource = vShaderStream.str();
		//	FragSource = fShaderStream.str();
		//}
		//catch (std::exception e)
		//{
		//	std::cout << "ERROR::SHADER::FILE_NOT_SUCCESFULLY_READ" << std::endl;
		//}

		//const char* vShaderCode = VertexSource.c_str();
		//const char * fShaderCode = FragSource.c_str();

		auto& dxDevice = static_cast<D3D12Device&>(Game::GetEngine().GetRenderer().GetDevice());

		DX::ThrowIfFailed(
			dxDevice.GetD3DDevice()->CreateVertexShader(
				VertexSource.data(),
				VertexSource.size(),
				nullptr,
				&m_vertexShader
			)
		);

		// Wrap this and subscribe to 
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
				VertexSource.data(),
				VertexSource.size(),
				&m_inputLayout
			)
		);

		DX::ThrowIfFailed(
			dxDevice.GetD3DDevice()->CreatePixelShader(
				FragSource.data(),
				FragSource.size(),
				nullptr,
				&m_pixelShader
			)
		);

		isLoaded = true;
	}

	ShaderCommand::~ShaderCommand()
	{
		m_vertexShader.Reset();
		m_inputLayout.Reset();
		m_pixelShader.Reset();
	}

	void ShaderCommand::Use()
	{
		OPTICK_EVENT("Shader::Use", Optick::Category::Rendering)

		if (!IsLoaded())
		{
			return;
		}
		auto& dxDevice = static_cast<D3D12Device&>(Game::GetEngine().GetRenderer().GetDevice());
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

	const unsigned int ShaderCommand::GetProgram() const
	{
		return Program;
	}

	void ShaderCommand::SetMat4(const std::string &name, const Matrix4& mat) const
	{
	}

	void ShaderCommand::SetInt(const std::string &name, int value) const
	{
	}

	void ShaderCommand::SetVec3(const std::string &name, const Vector3& value) const
	{
	}

	void ShaderCommand::SetVec3(const std::string &name, float x, float y, float z) const
	{
	}

	void ShaderCommand::SetFloat(const std::string &name, float value) const
	{
	}
}