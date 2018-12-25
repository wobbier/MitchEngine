#include "MitchGame.h"

int main()
{
	MitchGame* game = new MitchGame();
	game->Start();
	game->Tick();

	return 0;
}