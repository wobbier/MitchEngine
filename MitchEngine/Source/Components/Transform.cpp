#include "Transform.h"

Transform::Transform() : Position(glm::vec3(0.f, 0.f, 0.f)), Rotation(0.f,0.f,0.f,1.f), Scale(glm::vec3(1.f, 1.f, 1.f))
{
}

Transform::~Transform()
{
}

void Transform::Init()
{
}