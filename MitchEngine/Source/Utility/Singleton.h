#pragma once

namespace MAN
{
	template <class T>
	class Singleton
	{
	public:
		// Gets the class instance associated with the singleton.
		static T& Get();

		virtual ~Singleton();

		// Singleton can't be copy constructed.
		Singleton(const Singleton&) = delete;

		// Singleton can't be copied.
		Singleton& operator=(const Singleton&) = delete;

		// Singleton can't be move constructed.
		Singleton(Singleton&&) = delete;

		// Singleton can't be moved.
		Singleton& operator=(Singleton&&) = delete;
	protected:
		// Use default empty constructor
		Singleton() = default;
	};

	template <class T>
	T& Singleton<T>::Get()
	{
		static T singleton;
		return singleton;
	}

	template<class T>
	Singleton<T>::~Singleton()
	{
	}
}