#include "EditorApp.h"
#include "Engine/Engine.h"

int main()
{
	EditorApp* app = new EditorApp();
	app->LoadGameDLL();
	GetEngine().Init(app);
	GetEngine().Run();

	return 0;
}