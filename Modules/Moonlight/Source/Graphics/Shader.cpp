#include "Shader.h"

#if ME_PLATFORM_WIN64
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#endif

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

void Shader::Use()
{
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
