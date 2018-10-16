#include "stdafx.h"
#include "App.h"
#include "MitchGame.h"

using namespace Windows::ApplicationModel::Core;

ref class GameApp sealed
	: public App
{
public:
	GameApp()
		: App(std::make_unique<MitchGame>())
	{
	}
};

ref class Direct3DApplicationSource sealed : Windows::ApplicationModel::Core::IFrameworkViewSource
{
public:
	virtual Windows::ApplicationModel::Core::IFrameworkView^ CreateView()
	{
		return ref new GameApp();
	}
};

[Platform::MTAThread]
int main(Platform::Array<Platform::String^>^)
{
	auto direct3DApplicationSource = ref new Direct3DApplicationSource();
	CoreApplication::Run(direct3DApplicationSource);
	return 0;
}