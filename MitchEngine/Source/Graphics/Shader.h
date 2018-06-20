#pragma once

#include <string>
#include <fstream>
#include <sstream>
#include <iostream>
using namespace std;

//#include <GL/glew.h>

class Shader
{
public:
	unsigned int Program;
	Shader() = default;

	// Constructor generates the shader on the fly
	Shader(const char* InVertexPath, const char* InFragPath);
	// Uses the current shader
	void Use();
};
