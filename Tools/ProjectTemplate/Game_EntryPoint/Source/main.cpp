#include "stdafx.h"
#include "App.h"

using namespace Windows::ApplicationModel::Core;

ref class GameApplicationSource sealed : Windows::ApplicationModel::Core::IFrameworkViewSource
{
public:
	virtual Windows::ApplicationModel::Core::IFrameworkView^ CreateView()
	{
		return ref new App();
	}
};

[Platform::MTAThread]
int main(Platform::Array<Platform::String^>^)
{
	auto applicationSource = ref new GameApplicationSource();
	CoreApplication::Run(applicationSource);
	return 0;
}