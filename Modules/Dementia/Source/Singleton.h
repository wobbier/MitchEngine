#pragma once
#define ME_SINGLETON_DEFINITION(T)\
public:\
	static T& GetInstance(void)\
	{\
	return instance;\
	}\
	static T * GetInstancePtr(void)\
	{\
	return &instance;\
	}\
protected:\
	static T instance;\
private:\
	T(const T&) = delete;\
	T& operator=(const T&) = delete;\
	T(T&&) = delete;\
	T& operator=(T&&) = delete;

#define ME_SINGLETON_IMPLEMENTATION(T)\
		T T::instance = T();


//
//template <class T>
//class Singleton
//{
//public:
//	// Gets the class instance associated with the singleton.
//	static T& GetInstance();
//
//	virtual ~Singleton();
//
//protected:
//	// Use default empty constructor
//	Singleton() = default;
//};
//
//template <class T>
//T& Singleton<T>::GetInstance()
//{
//	static T singleton;
//	return singleton;
//}
//
//template<class T>
//Singleton<T>::~Singleton()
//{
//}
