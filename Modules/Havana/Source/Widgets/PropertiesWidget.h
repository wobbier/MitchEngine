#pragma once
#include <HavanaWidget.h>
#include "Events/EventReceiver.h"
#include <ECS/EntityHandle.h>
#include <Path.h>
#include <Resource/MetaFile.h>

class Transform;
class BaseCore;

#if ME_EDITOR

class PropertiesWidget
	: public HavanaWidget
	, public EventReceiver
{
public:
	PropertiesWidget();

	void Init() override;
	void Destroy() override;

	bool OnEvent(const BaseEvent& evt) override;

	void Update() override;
	void Render() override;

	void ClearSelection();
	void AddComponentPopup(EntityHandle inSelectedEntity);

	EntityHandle SelectedEntity;
	class Transform* SelectedTransform = nullptr;
	class BaseCore* SelectedCore = nullptr;
	Path AssetBrowserPath;
	MetaBase* metafile = nullptr;
};

#endif