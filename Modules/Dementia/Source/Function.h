#pragma once

#include <functional>

template <typename TReturnType, class... Args>
class Function
{
public:
	Function() = default;
	Function(std::function<TReturnType(Args...)> callback)
		: mCallback(std::move(callback))
	{
	}

	Function(const Function& other)
		: mCallback(other.mCallback)
	{
	}

	Function(Function&& other)
		: mCallback(std::move(other.mCallback))
	{
	}

	template <class... CallArgs>
	TReturnType Call(CallArgs&&... args) const
	{
		return mCallback(std::forward<CallArgs>(args)...);
	}

	void Set(std::function<TReturnType(Args...)> callback)
	{
		mCallback = callback;
	}

	// 	std::function<TReturnType>(Args...) Get() const
	// 	{
	// 		return mCallback;
	// 	}

	TReturnType operator()(Args... args) const
	{
		return Call(args...);
	}

	void operator=(const Function<TReturnType, Args...> other)
	{
		mCallback = other.mCallback;
	}

	bool operator!=(nullptr_t)
	{
		return mCallback != nullptr;
	}

private:
	std::function<TReturnType(Args...)> mCallback;
};
