#include <cstddef>

namespace ma {
	typedef std::size_t Type;

	template <typename T>
	class TypeId {
	public:
		template <typename T>
		static Type GetTypeId() {
			static const Type Id = NextTypeId++;
			return Id;
		}
	protected:
	private:
		static Type NextTypeId;
	};

	template<typename T>
	Type TypeId<T>::NextTypeId = 0;
}