// 2015 Mitchell Andrews
#pragma once
#include <string>
#include <vector>

namespace ma {
	class BaseComponent {
	public:
		BaseComponent() = default;
		~BaseComponent() = default;
	};

	template<typename T>
	class Component
		: BaseComponent {
	public:
		Component();
		~Component();

		typedef std::vector<std::reference_wrapper<BaseComponent>> ComponentArray;
	};
}