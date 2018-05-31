#include "Collider2D.h"

namespace MAN
{
	Collider2D::Collider2D()
	{
	}

	Collider2D::~Collider2D()
	{
	}

	void Collider2D::Init()
	{
		ShapeDefinition.SetAsBox(1, 1);
		SetBodyType(b2_dynamicBody);
	}

	void Collider2D::SetBodyType(b2BodyType InBodyType)
	{
		BodyDefinition.type = InBodyType;
	}
}