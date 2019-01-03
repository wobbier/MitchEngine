#pragma once
#include <string>
#include "Utility/Util.h"
#include "Engine/Resource.h"
#include "Texture.h"
#include "Shader.h"

struct Vertex
{
	glm::vec3 Position;
	glm::vec3 Normal;
	glm::vec2 TexCoords;
	glm::vec3 Tangent;
	glm::vec3 Bitangent;
};

class Mesh
{
public:
	Mesh() = delete;
	~Mesh();

	std::vector<Vertex> vertices;
	std::vector<unsigned int> indices;
	std::vector<Texture*> textures;
	unsigned int VAO;

	Mesh(std::vector<Vertex> vertices, std::vector<unsigned int> indices, std::vector<Texture*> textures);

	// render the mesh
	void Draw(Shader shader);

private:
	unsigned int VBO, EBO;

	void InitMesh();

};
