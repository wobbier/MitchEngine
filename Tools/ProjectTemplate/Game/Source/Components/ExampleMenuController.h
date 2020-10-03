#pragma once
#include <Components/UI/BasicUIView.h>

class ExampleMenuController final
	: public BasicUIView
{
public:
	ExampleMenuController();

	void OnUILoad(ultralight::JSObject& GlobalWindow, ultralight::View* Caller) final;

	void LoadScene(const ultralight::JSObject& thisObject, const ultralight::JSArgs& args);
};

ME_REGISTER_COMPONENT(ExampleMenuController);