#pragma once
#include <memory>

namespace ma {

	template<typename TBase>
	class ClassTypeId {
	public:
		template<typename T>
		static std::size_t GetTypeId() {
			static const std::size_t Id = NextTypeId++;
			return Id;
		}
	private:
		static std::size_t NextTypeId;
	};

	template<typename TBase>
	std::size_t ClassTypeId<TBase>::NextTypeId = 0;
}