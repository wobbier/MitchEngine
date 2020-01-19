#pragma once
#include "Math/Vector3.h"

struct RaycastHit
{
	Vector3 Position;
	Vector3 Normal;

	class Rigidbody* What = nullptr;
};
