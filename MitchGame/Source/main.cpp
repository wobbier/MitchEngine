#include "MitchGame.h"
#include "JobScheduler.h"

void RunJob(JobScheduler* manager, void* game)
{
	Game* gamePtr = reinterpret_cast<Game*>(game);
	gamePtr->Run();
}

int main()
{
	JobScheduler scheduler;

	MitchGame* game = new MitchGame();
	game->Start();

	scheduler.Run(10, RunJob, game);
	return 0;
}