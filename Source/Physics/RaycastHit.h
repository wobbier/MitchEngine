#pragma once
#include "Math/Line.h"

struct RaycastHit
{
	RaycastHit() = default;
	Vector3 Position;
	Vector3 Normal;
	Line Ray;

	class Rigidbody* What = nullptr;
};
