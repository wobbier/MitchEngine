#include "stdafx.h"
#include "App.h"
#include "MitchGame.h"

using namespace Windows::ApplicationModel::Core;

ref class GameApp
	: public App
{
public:
	virtual void Init() override
	{
		if (!m_game)
		{
			m_game = std::make_unique<MitchGame>();
			m_game->Start();
		}
	}

	virtual void Tick() override
	{
		m_game->Tick();
	}

private:
	std::unique_ptr<MitchGame> m_game;
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