#pragma once
#include <HavanaWidget.h>
#include "Events/EventReceiver.h"
#include <ECS/EntityHandle.h>
#include <Path.h>
#include <Resource/MetaFile.h>

class Transform;
class BaseCore;
class BaseComponent;

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

	void DrawComponentProperties(BaseComponent* comp, EntityHandle entity);

	void ClearSelection();
	void AddComponentPopup(EntityHandle inSelectedEntity);

	EntityHandle SelectedEntity;
	WeakPtr<Transform> SelectedTransform;
	class BaseCore* SelectedCore = nullptr;
};

#endif