#pragma once
#include "ECS/Core.h"
#include "ECS/Component.h"
#include "ECS/ComponentDetail.h"
#include "File.h"
#include "Path.h"

class BasicUIView
	: public Component<BasicUIView>
{
	friend class UICore;
public:
	BasicUIView();
	BasicUIView(const char* Name);

	virtual void Init() override;

	virtual void Serialize(json& outJson) override;
	virtual void Deserialize(const json& inJson) override;

#if ME_EDITOR
	virtual void OnEditorInspect() override;
#endif
protected:
	bool IsInitialized = false;
	size_t Index;
	File SourceFile;
	Path FilePath;
};
ME_REGISTER_COMPONENT(BasicUIView)
