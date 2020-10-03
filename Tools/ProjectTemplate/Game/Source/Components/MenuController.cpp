#include "MenuController.h"
#include "UI/JSHelpers.h"
#include "Events/SceneEvents.h"

MenuController::MenuController()
	: BasicUIView("MenuController")
{
	FilePath = Path("Assets/UI/Main.html");
}

void MenuController::OnUILoad(ultralight::JSObject& GlobalWindow, ultralight::View* Caller)
{
	BasicUIView::OnUILoad(GlobalWindow, Caller);

	GlobalWindow["LoadScene"] = BindJSCallback(&MenuController::LoadScene);
}

void MenuController::LoadScene(const ultralight::JSObject& thisObject, const ultralight::JSArgs& args)
{
	LoadSceneEvent evt;
	ultralight::String thing = args[0].ToString();
	std::string news = std::string(thing.utf8().data());
	evt.Level = news;
	evt.Fire();
}