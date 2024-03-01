#define SDL_MAIN_HANDLED
#include "MitchHub.h"

int main(int argc, char** argv)
{
    ToolCreationFlags flags;
	flags.isBorderless = true;
	MitchHub tool( flags );
	tool.Start();

	return 0;
}
