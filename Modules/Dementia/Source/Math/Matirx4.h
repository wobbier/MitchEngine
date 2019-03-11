#pragma once
#include <glm.hpp>

class Matrix4
{
public:
	Matrix4(glm::mat4 mat)
		: m_matrix(mat)
	{

	}

private:
	glm::mat4 m_matrix { 1.0f };
};