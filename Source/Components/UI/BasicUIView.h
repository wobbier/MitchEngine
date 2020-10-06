#pragma once
#include "ECS/Core.h"
#include "ECS/Component.h"
#include "ECS/ComponentDetail.h"
#include "File.h"
#include "Path.h"
#include "Ultralight/Listener.h"
#include "UI/JSHelpers.h"
#include "Ultralight/View.h"

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

	virtual void OnUpdateHistory(ultralight::View* caller) override;

	virtual void OnDOMReady(ultralight::View* caller,
		uint64_t frame_id,
		bool is_main_frame,
		const ultralight::String& url) final;

	virtual void OnUILoad(ultralight::JSObject& GlobalWindow, ultralight::View* Caller);

	void ExecuteScript(const std::string& Script);

	Path FilePath;

protected:
	bool IsInitialized = false;
	size_t Index;
	File SourceFile;

	void PlaySound(const ultralight::JSObject& thisObject, const ultralight::JSArgs& args);
private:
	virtual void OnSerialize(json& outJson) override;
	virtual void OnDeserialize(const json& inJson) override;

	ultralight::RefPtr<ultralight::View> ViewRef;
};
ME_REGISTER_COMPONENT_FOLDER(BasicUIView, "UI")
