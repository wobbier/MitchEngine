// 2015 Mitchell Andrews
#pragma once

namespace ma {
	class Component {
	public:
		Component();
		~Component();

		class Entity* Object;

		unsigned int Handle;
	};
}