#include "ShaderCommand.h"

#include "Utils/DirectXHelper.h"
#include "Renderer.h"
#include "Game.h"

#include <VertexTypes.h>
#include "Texture.h"
#include "optick.h"
#include "Graphics/ShaderFile.h"
#include "Resource/ResourceCache.h"
#include "Engine/Engine.h"
#include "Pointers.h"

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

	//ShaderCommand::ShaderCommand(const std::string& InVertexPath, const std::string& InPixelPath)
	//{
	//	// I should compile the hlsl at runtime in debug

	//	// Retrieve the shader source code from paths
	//	Path vPath(InVertexPath);
	//	Path fPath(InPixelPath);
	//	SharedPtr<ShaderFile> vertShader = ResourceCache::GetInstance().Get<ShaderFile>(vPath);
	//	SharedPtr<ShaderFile> fragShader = ResourceCache::GetInstance().Get<ShaderFile>(fPath);
	//	if (!vertShader || !fragShader)
	//	{
	//		YIKES("Failed to load shader: " + vPath.LocalPath + " / " + fPath.LocalPath);
	//		return;
	//	}

	//	std::vector<char> VertexSource = vertShader->Data;
	//	std::vector<char> FragSource = fragShader->Data;
	//	
	//	//try
	//	//{
	//	//	// Open files
	//	//	std::ifstream vShaderFile(vPath.FullPath);
	//	//	std::ifstream fShaderFile(fPath.FullPath);
	//	//	std::stringstream vShaderStream, fShaderStream;

	//	//	// Read file's buffer contents into streams
	//	//	vShaderStream << vShaderFile.rdbuf();
	//	//	fShaderStream << fShaderFile.rdbuf();

	//	//	// close file handlers
	//	//	vShaderFile.close();
	//	//	fShaderFile.close();

	//	//	// Convert stream into string
	//	//	VertexSource = vShaderStream.str();
	//	//	FragSource = fShaderStream.str();
	//	//}
	//	//catch (std::exception e)
	//	//{
	//	//	std::cout << "ERROR::SHADER::FILE_NOT_SUCCESFULLY_READ" << std::endl;
	//	//}

	//	//const char* vShaderCode = VertexSource.c_str();
	//	//const char * fShaderCode = FragSource.c_str();

	//	auto& dxDevice = static_cast<DX11Device&>(GetEngine().GetRenderer().GetDevice());

	//	DX::ThrowIfFailed(
	//		dxDevice.GetD3DDevice()->CreateVertexShader(
	//			VertexSource.data(),
	//			VertexSource.size(),
	//			nullptr,
	//			&Program.VertexShader
	//		)
	//	);

	//	// Wrap this and subscribe to 
	//	static const D3D11_INPUT_ELEMENT_DESC vertexDesc[] =
	//	{
	//		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
	//		{ "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT,    0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
	//		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
	//	};

	//	DX::ThrowIfFailed(
	//		dxDevice.GetD3DDevice()->CreateInputLayout(
	//			vertexDesc,
	//			ARRAYSIZE(vertexDesc),
	//			VertexSource.data(),
	//			VertexSource.size(),
	//			&Program.InputLayout
	//		)
	//	);

	//	DX::ThrowIfFailed(
	//		dxDevice.GetD3DDevice()->CreatePixelShader(
	//			FragSource.data(),
	//			FragSource.size(),
	//			nullptr,
	//			&Program.PixelShader
	//		)
	//	);

	//	isLoaded = true;
	//}

	ShaderCommand::ShaderCommand(const std::string& InShaderFile)
	{
		OPTICK_EVENT("ShaderCommand(string)");
		auto& dxDevice = static_cast<DX11Device&>(GetEngine().GetRenderer().GetDevice());

		if (!dxDevice.FindShader(InShaderFile, Program))
		{
			auto vs = dxDevice.CompileShader(Path(InShaderFile), "main_vs", "vs_4_0_level_9_3");
			auto ps = dxDevice.CompileShader(Path(InShaderFile), "main_ps", "ps_4_0_level_9_3");

			// Wrap this and subscribe to 
			static const std::vector<D3D11_INPUT_ELEMENT_DESC> vertexDesc =
			{
				{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
				{ "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT,    0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
				{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
				{ "TANGENT",   0, DXGI_FORMAT_R32G32B32_FLOAT,    0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			};

			Program = dxDevice.CreateShaderProgram(InShaderFile, vs, ps, &vertexDesc);
		}

		isLoaded = true;
	}

	ShaderCommand::~ShaderCommand()
	{
	}

	void ShaderCommand::Use()
	{
		OPTICK_EVENT("Shader::Use", Optick::Category::Rendering)

		if (!IsLoaded())
		{
			return;
		}
		auto& dxDevice = static_cast<DX11Device&>(GetEngine().GetRenderer().GetDevice());
		dxDevice.GetD3DDeviceContext()->IASetInputLayout(Program.InputLayout.Get());

		// Attach our vertex shader.
		dxDevice.GetD3DDeviceContext()->VSSetShader(
			Program.VertexShader.Get(),
			nullptr,
			0
		);
		// Attach our pixel shader.
		dxDevice.GetD3DDeviceContext()->PSSetShader(
			Program.PixelShader.Get(),
			nullptr,
			0
		);
	}

	const ShaderProgram& ShaderCommand::GetProgram() const
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