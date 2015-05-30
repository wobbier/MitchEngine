#pragma once
#include <string>
#include <GL/glew.h>
#include "SOIL.h"
#include "Util.h"

namespace ma {
	class Texture {
	public:
		GLuint Id;
		std::string Type;
		std::string Directory;

		Texture() = delete;
		Texture(std::string InPath);
		~Texture();

		// Textures should not be copied around in memory
		MA_NONCOPYABLE(Texture);
	};
}