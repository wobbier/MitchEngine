#include "Shader.h"

#if ME_PLATFORM_WIN64
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#endif
#include "Utils/DirectXHelper.h"
#include "Renderer.h"

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
			std::ifstream vShaderFile(InVertexPath.c_str());
			std::ifstream fShaderFile(InFragPath.c_str());
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

		// Load shaders asynchronously.
		auto loadVSTask = DX::ReadDataAsync(L"SampleVertexShader.cso");
		auto loadPSTask = DX::ReadDataAsync(L"SamplePixelShader.cso");

		auto dxDevice = static_cast<D3D12Device&>(Renderer::GetInstance().GetDevice());
		// After the vertex shader file is loaded, create the shader and input layout.
		auto createVSTask = loadVSTask.then([this, dxDevice](const std::vector<byte>& fileData)
		{
			DX::ThrowIfFailed(
				dxDevice.GetD3DDevice()->CreateVertexShader(
					&fileData[0],
					fileData.size(),
					nullptr,
					&m_vertexShader
				)
			);

			static const D3D11_INPUT_ELEMENT_DESC vertexDesc[] =
			{
				{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
				{ "COLOR", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			};

			DX::ThrowIfFailed(
				dxDevice.GetD3DDevice()->CreateInputLayout(
					vertexDesc,
					ARRAYSIZE(vertexDesc),
					&fileData[0],
					fileData.size(),
					&m_inputLayout
				)
			);
		});

		// After the pixel shader file is loaded, create the shader and constant buffer.
		auto createPSTask = loadPSTask.then([this, dxDevice](const std::vector<byte>& fileData)
		{
			DX::ThrowIfFailed(
				dxDevice.GetD3DDevice()->CreatePixelShader(
					&fileData[0],
					fileData.size(),
					nullptr,
					&m_pixelShader
				)
			);
			isLoaded = true;
		});

#if ME_PLATFORM_WIN64
		// Variables for our compiled shaders
		unsigned int vertex, fragment;
		int success;
		char infoLog[512];

		// Vertex Shader
		vertex = glCreateShader(GL_VERTEX_SHADER);
		glShaderSource(vertex, 1, &vShaderCode, NULL);
		glCompileShader(vertex);

		// Print compile errors if any
		glGetShaderiv(vertex, GL_COMPILE_STATUS, &success);
		if (!success)
		{
			glGetShaderInfoLog(vertex, 512, NULL, infoLog);
			std::cout << "ERROR::SHADER::VERTEX::COMPILATION_FAILED\n" << infoLog << std::endl;
		}

		// Fragment Shader
		fragment = glCreateShader(GL_FRAGMENT_SHADER);
		glShaderSource(fragment, 1, &fShaderCode, NULL);
		glCompileShader(fragment);

		// Print compile errors if any
		glGetShaderiv(fragment, GL_COMPILE_STATUS, &success);
		if (!success)
		{
			glGetShaderInfoLog(fragment, 512, NULL, infoLog);
			std::cout << "ERROR::SHADER::FRAGMENT::COMPILATION_FAILED\n" << infoLog << std::endl;
		}

		// Shader Program
		Program = glCreateProgram();
		glAttachShader(Program, vertex);
		glAttachShader(Program, fragment);
		glLinkProgram(Program);

		// Print linking errors if any
		glGetProgramiv(this->Program, GL_LINK_STATUS, &success);
		if (!success)
		{
			glGetProgramInfoLog(Program, 512, NULL, infoLog);
			std::cout << "ERROR::SHADER::PROGRAM::LINKING_FAILED\n" << infoLog << std::endl;
		}

		// Delete the shaders as they're linked into our program now and no longer necessary
		glDeleteShader(vertex);
		glDeleteShader(fragment);
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
		auto dxDevice = static_cast<D3D12Device&>(Renderer::GetInstance().GetDevice());
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
#if ME_PLATFORM_WIN64
		glUseProgram(Program);
#endif
	}

	const unsigned int Shader::GetProgram() const
	{
		return Program;
	}

	void Shader::SetMat4(const std::string &name, const glm::mat4 &mat) const
	{
#if ME_PLATFORM_WIN64
		glUniformMatrix4fv(glGetUniformLocation(Program, name.c_str()), 1, GL_FALSE, &mat[0][0]);
#endif
	}

	void Shader::SetInt(const std::string &name, int value) const
	{
#if ME_PLATFORM_WIN64
		glUniform1i(glGetUniformLocation(Program, name.c_str()), value);
#endif
	}

	void Shader::SetVec3(const std::string &name, const glm::vec3 &value) const
	{
#if ME_PLATFORM_WIN64
		glUniform3fv(glGetUniformLocation(Program, name.c_str()), 1, &value[0]);
#endif
	}

	void Shader::SetVec3(const std::string &name, float x, float y, float z) const
	{
#if ME_PLATFORM_WIN64
		glUniform3f(glGetUniformLocation(Program, name.c_str()), x, y, z);
#endif
	}

	void Shader::SetFloat(const std::string &name, float value) const
	{
#if ME_PLATFORM_WIN64
		glUniform1f(glGetUniformLocation(Program, name.c_str()), value);
#endif
	}
}