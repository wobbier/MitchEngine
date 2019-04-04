#pragma once
#include <memory>

template<class T>
using UniquePtr = std::unique_ptr<T>;

template<class T>
using SharedPtr = std::shared_ptr<T>;

template<class T>
using WeakPtr = std::weak_ptr<T>;

//
//template<class T>
//class WeakPtr
//	: public std::weak_ptr<T>
//{
//public:
//	T* operator->() { return get(); }
//};
//
//namespace std
//{
//	template<class T>
//	bool operator==(std::weak_ptr<T> left,
//		std::weak_ptr<T> right)
//	{
//		return left.get() == right.get();
//	}
//
//	template<class T>
//	T* operator->() (WeakPtr<T> in)
//	{
//		return in.get();
//	}
//
//}