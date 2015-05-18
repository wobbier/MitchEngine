// 2015 Mitchell Andrews
#pragma once
#include "Entity.h"

namespace ma {
	class Component {
	public:
		Component();
		~Component();

		ma::Entity* Entity;

		unsigned int Handle;
	};
}