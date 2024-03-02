#define SDL_MAIN_HANDLED
#include "MitchHub.h"

int main(int argc, char** argv)
{
    ToolCreationFlags flags;
	flags.isBorderless = true;
	flags.isDockingEnabled = false;
	flags.toolName = "ME HUB";

	MitchHub tool( flags );
	tool.Start();

	return 0;
}
