#pragma once
#include "ECS/Component.h"
#include "ECS/ComponentDetail.h"
#include <string>

class Text
	: public Component<Text>
{
	friend class UICore;
public:
	Text() = default;
	virtual void Init() override;

	virtual void Serialize(json& outJson) override;
	virtual void Deserialize(const json& inJson) override;

#if ME_EDITOR
	virtual void OnEditorInspect() override;
#endif
	void SetText(const std::string& NewText);

	Vector2 Anchor;
private:
	unsigned int RenderId = 0;
	bool HasChanged = false;

	std::string SourceText;
};

ME_REGISTER_COMPONENT(Text)