#pragma once
#include <Components/UI/BasicUIView.h>
#include "Dementia.h"

class ExampleMenuController final
	: public BasicUIView
{
public:
	ExampleMenuController();

#if USING( ME_UI )
	void OnUILoad(ultralight::JSObject& GlobalWindow, ultralight::View* Caller) final;

	void LoadScene(const ultralight::JSObject& thisObject, const ultralight::JSArgs& args);
#endif
};

ME_REGISTER_COMPONENT(ExampleMenuController);