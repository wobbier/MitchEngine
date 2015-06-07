#pragma once
#include "Component.h"
#include <glm.hpp>

namespace ma {
	class Transform :
		public Component<Transform> {
	public:
		glm::vec3 Position;
		glm::vec3 Scale;
		glm::vec3 Rotation;

		Transform();
		virtual ~Transform();

		// Separate init from construction code.
		virtual void Init() final;

	};
}