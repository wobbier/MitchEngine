#pragma once

#include <string>
#include <fstream>
#include <sstream>
#include <iostream>

#include <glm.hpp>

class Shader
{
public:
	Shader() = default;

	// Constructor generates the shader on the fly
	Shader(const std::string& InVertexPath, const std::string& InFragPath);

	// Uses the current shader
	void Use();

	const unsigned int GetProgram() const;

	void SetMat4(const std::string &name, const glm::mat4 &mat) const;
	void SetInt(const std::string &name, int value) const;
private:
	unsigned int Program;
};
