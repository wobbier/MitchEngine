#define SDL_MAIN_HANDLED
#include "ShaderEditor.h"

int main( int argc, char** argv )
{
    ToolCreationFlags flags;
    flags.isBorderless = false;
    flags.isDockingEnabled = true;
    flags.toolName = "Shader Editor";

    CLog::GetInstance().SetLogVerbosity( CLog::LogType::Warning );
    ShaderEditor tool( flags );
    tool.Start();

    return 0;
}
