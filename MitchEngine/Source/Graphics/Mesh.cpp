#include <iostream>
#include <assert.h>
#include "Graphics/Mesh.h"
#include "Utility/Logger.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <stb_image.h>

Mesh::Mesh()
{

}

Mesh::~Mesh()
{
	// TODO: Unload textures
}

Mesh* Mesh::Load(const std::string& InFilePath)
{
	Mesh* LoadedTexture = new Mesh();
	return LoadedTexture;
}
