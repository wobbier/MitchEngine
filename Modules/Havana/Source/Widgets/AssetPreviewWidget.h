#pragma once
#include <HavanaWidget.h>
#include <Events/EventReceiver.h>
#include <Pointers.h>
#include <Math/Vector2.h>

namespace Moonlight { class Texture; }

class AssetPreviewWidget
	: public HavanaWidget
	, public EventReceiver
{
	struct DisplayParams
	{
		std::string Name;
		Vector2 Extents;
	};
public:
	AssetPreviewWidget();

	void Init() override;
	void Destroy() override;

	bool OnEvent(const BaseEvent& evt) final;

	void Update() override;
	void Render() override;

private:
	SharedPtr<Moonlight::Texture> ViewTexture;
	Vector2 SceneViewRenderSize;
	Vector2 SceneViewRenderLocation;
	DisplayParams CurrentDisplayParams;
};