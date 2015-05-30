#pragma once

#include <string>
#include <fstream>
#include <sstream>
#include <iostream>
using namespace std;

#include <GL/glew.h>

namespace ma {
	class Shader {
	public:
		GLuint Program;
		Shader() = default;

		// Constructor generates the shader on the fly
		Shader(const GLchar* InVertexPath, const GLchar* InFragPath) {

			// Retrieve the shader source code from paths
			string VertexSource;
			string FragSource;

			try {
				// Open files
				ifstream vShaderFile(InVertexPath);
				ifstream fShaderFile(InFragPath);
				stringstream vShaderStream, fShaderStream;

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
			catch (exception e) {
				cout << "ERROR::SHADER::FILE_NOT_SUCCESFULLY_READ" << endl;
			}

			const GLchar* vShaderCode = VertexSource.c_str();
			const GLchar * fShaderCode = FragSource.c_str();

			// Variables for our compiled shaders
			GLuint vertex, fragment;
			GLint success;
			GLchar infoLog[512];

			// Vertex Shader
			vertex = glCreateShader(GL_VERTEX_SHADER);
			glShaderSource(vertex, 1, &vShaderCode, NULL);
			glCompileShader(vertex);

			// Print compile errors if any
			glGetShaderiv(vertex, GL_COMPILE_STATUS, &success);
			if (!success) {
				glGetShaderInfoLog(vertex, 512, NULL, infoLog);
				cout << "ERROR::SHADER::VERTEX::COMPILATION_FAILED\n" << infoLog << endl;
			}

			// Fragment Shader
			fragment = glCreateShader(GL_FRAGMENT_SHADER);
			glShaderSource(fragment, 1, &fShaderCode, NULL);
			glCompileShader(fragment);

			// Print compile errors if any
			glGetShaderiv(fragment, GL_COMPILE_STATUS, &success);
			if (!success) {
				glGetShaderInfoLog(fragment, 512, NULL, infoLog);
				cout << "ERROR::SHADER::FRAGMENT::COMPILATION_FAILED\n" << infoLog << endl;
			}

			// Shader Program
			this->Program = glCreateProgram();
			glAttachShader(this->Program, vertex);
			glAttachShader(this->Program, fragment);
			glLinkProgram(this->Program);

			// Print linking errors if any
			glGetProgramiv(this->Program, GL_LINK_STATUS, &success);
			if (!success) {
				glGetProgramInfoLog(this->Program, 512, NULL, infoLog);
				cout << "ERROR::SHADER::PROGRAM::LINKING_FAILED\n" << infoLog << endl;
			}
			// Delete the shaders as they're linked into our program now and no longer necessary
			glDeleteShader(vertex);
			glDeleteShader(fragment);
		}
		// Uses the current shader
		void Use() { glUseProgram(this->Program); }
	};
}