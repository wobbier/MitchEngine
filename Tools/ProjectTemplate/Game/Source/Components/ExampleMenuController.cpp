#include <Components/ExampleMenuController.h>

#include <Events/SceneEvents.h>

ExampleMenuController::ExampleMenuController()
	: BasicUIView("MenuController")
{
	FilePath = Path("Assets/UI/ExampleMenu.html");
}

void ExampleMenuController::OnUILoad(ultralight::JSObject& GlobalWindow, ultralight::View* Caller)
{
	BasicUIView::OnUILoad(GlobalWindow, Caller);

	GlobalWindow["LoadScene"] = BindJSCallback(&ExampleMenuController::LoadScene);
}

void ExampleMenuController::LoadScene(const ultralight::JSObject& thisObject, const ultralight::JSArgs& args)
{
	LoadSceneEvent evt;
	ultralight::String path = args[0].ToString();
	evt.Level = std::string(path.utf8().data());
	evt.Fire();
}