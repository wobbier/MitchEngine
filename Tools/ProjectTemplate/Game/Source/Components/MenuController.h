#pragma once
#include "Components/UI/BasicUIView.h"

class MenuController
	: public BasicUIView
{
public:
	MenuController();
	void OnUILoad(ultralight::JSObject& GlobalWindow, ultralight::View* Caller) override;

	void LoadScene(const ultralight::JSObject& thisObject, const ultralight::JSArgs& args);

};

ME_REGISTER_COMPONENT(MenuController);