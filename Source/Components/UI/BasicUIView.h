#pragma once
#include "ECS/Core.h"
#include "ECS/Component.h"
#include "ECS/ComponentDetail.h"
#include "File.h"
#include "Path.h"
#include "Ultralight/Listener.h"
#include "UI/JSHelpers.h"

class BasicUIView
	: public Component<BasicUIView>
	, public ultralight::LoadListener
{
	friend class UICore;
public:
	BasicUIView();
	BasicUIView(const char* Name);

	virtual void Init() override;


#if ME_EDITOR
	virtual void OnEditorInspect() override;
#endif

	virtual void OnBeginLoading(ultralight::View* caller) override;


	virtual void OnFinishLoading(ultralight::View* caller) override;


	virtual void OnUpdateHistory(ultralight::View* caller) override;

	virtual void OnDOMReady(ultralight::View* caller) final;

	virtual void OnUILoad(ultralight::JSObject& GlobalWindow, ultralight::View* Caller);
	Path FilePath;

protected:
	bool IsInitialized = false;
	size_t Index;
	File SourceFile;

	void PlaySound(const ultralight::JSObject& thisObject, const ultralight::JSArgs& args);
private:
	virtual void OnSerialize(json& outJson) override;
	virtual void OnDeserialize(const json& inJson) override;
};
ME_REGISTER_COMPONENT_FOLDER(BasicUIView, "UI")
