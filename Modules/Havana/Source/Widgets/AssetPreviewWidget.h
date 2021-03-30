#pragma once
#include <HavanaWidget.h>
#include <Events/EventReceiver.h>
#include <Pointers.h>

namespace Moonlight { class Texture; }

class AssetPreviewWidget
	: public HavanaWidget
	, public EventReceiver
{
public:
	AssetPreviewWidget();

	void Init() override;
	void Destroy() override;

	bool OnEvent(const BaseEvent& evt) final;

	void Update() override;
	void Render() override;

private:
	SharedPtr<Moonlight::Texture> ViewTexture;
};