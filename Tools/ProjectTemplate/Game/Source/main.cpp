#include <GameApp.h>
#include <Engine/Engine.h>

int main()
{
	GetEngine().Init(new GameApp());
	GetEngine().Run();

	return 0;
}