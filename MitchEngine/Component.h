// 2015 Mitchell Andrews
#pragma once
#include <string>
#include <vector>
#include "ClassTypeId.h"

namespace ma {
	class BaseComponent {
	public:
		~BaseComponent() = default;

		virtual void Init() = 0;
	};

	template<typename T>
	class Component
		: public BaseComponent {
	public:
		static TypeId GetTypeId() {
			return ClassTypeId<BaseComponent>::GetTypeId<T>();
		}
	};
	typedef std::vector<std::reference_wrapper<BaseComponent>> ComponentArray;
}