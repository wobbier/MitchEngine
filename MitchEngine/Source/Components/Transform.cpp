#include "Transform.h"

using namespace MAN;

Transform::Transform() : Position(glm::vec3(0, 0, 0)), Rotation(glm::vec3(0, 0, 0)), Scale(glm::vec3(1, 1, 1))
{
}

Transform::~Transform()
{
}

void Transform::Init()
{
}