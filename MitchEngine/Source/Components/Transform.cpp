#include "Transform.h"

Transform::Transform() : Position(glm::vec3(0, 0, 0)), Rotation(), Scale(glm::vec3(1, 1, 1))
{
}

Transform::~Transform()
{
}

void Transform::Init()
{
}