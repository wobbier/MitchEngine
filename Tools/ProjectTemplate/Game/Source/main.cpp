#include "MitchGame.h"
#include "Engine/Engine.h"

int main()
{
	GetEngine().Init(new MitchGame());
	GetEngine().Run();

	return 0;
}