#pragma once
#include "Singleton.h"
#include <map>
#include "Math/Vector2.h"

#if ME_PLATFORM_UWP
using namespace Windows::UI::Core;
using namespace Windows::System;
#define KeyCode VirtualKey
#else
#define KeyCode KeyCodeDef::Type
#endif

namespace KeyCodeDef
{
	enum Type : int
	{
		LeftButton = 0x01,
		RightButton = 0x02,
		Enter = 0x0D,
		Space = 0x20,
		Number0 = 0x30,
		Number1 = 0x31,
		Number2 = 0x32,
		Number3 = 0x33,
		Number4 = 0x34,
		Number5 = 0x35,
		Number6 = 0x36,
		Number7 = 0x37,
		Number8 = 0x38,
		Number9 = 0x39,
		// 0x3A - 0x40 Undefined
		A = 0x41,
		B = 0x42,
		C = 0x43,
		D = 0x44,
		E = 0x45,
		F = 0x46,
		G = 0x47,
		H = 0x48,
		I = 0x49,
		J = 0x4A,
		K = 0x4B,
		L = 0x4C,
		M = 0x4D,
		N = 0x4E,
		O = 0x4F,
		P = 0x50,
		Q = 0x51,
		R = 0x52,
		S = 0x53,
		T = 0x54,
		U = 0x55,
		V = 0x56,
		W = 0x57,
		X = 0x58,
		Y = 0x59,
		Z = 0x5A,
		LeftShift = 0XA0
	};
}

class Input : public Singleton<Input>
{
	friend class Singleton<Input>;
	friend class Window;
public:
	struct Key
	{
		int Id;
		int Scancode;
		int Action;
		int Mode;
	};

	struct MouseInfo
	{
		Vector2 Position;
		Vector2 Scroll;
	};

	bool IsKeyDown(KeyCode key);

	bool IsKeyUp(KeyCode key);
	Vector2 GetMousePosition();
	Vector2 GetMouseScrollOffset();

#if ME_PLATFORM_UWP
	static void KeyCallback(VirtualKey key);
#endif

private:
	Input() = default;
	~Input() {}
	std::map<int, Key> Keys;
	MouseInfo Mouse;

};
