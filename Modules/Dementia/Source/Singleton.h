#pragma once
#define ME_SINGLETON_DEFINITION(T)\
public:\
	static T& GetInstance()\
	{\
		static T instance;\
		return instance;\
	}\
	static T * GetInstancePtr()\
	{\
		return &GetInstance();\
	}\
private:\
	T( const T& ) = delete;\
	T& operator=( const T& ) = delete;\
	T( T&& ) = delete;\
	T& operator=( T&& ) = delete;