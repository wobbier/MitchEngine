#include "Collider2D.h"

namespace ma {
	Collider2D::Collider2D() {
	}


	Collider2D::~Collider2D() {
	}

	void Collider2D::Init() {
		ShapeDefinition.SetAsBox(1, 1);
		SetBodyType(b2_dynamicBody);
	}

	void Collider2D::Update(float dt) {

	}

	void Collider2D::SetBodyType(b2BodyType InBodyType) {
		BodyDefinition.type = InBodyType;
	}

}