#define SDL_MAIN_HANDLED
#include "ShaderEditor.h"
#include "Math/Matrix4.h"

int main( int argc, char** argv )
{
    ToolCreationFlags flags;
    flags.isBorderless = false;
    flags.isDockingEnabled = true;
    flags.toolName = "Shader Editor";

    CLog::GetInstance().SetLogVerbosity( CLog::LogType::Debug );
    BRUH(sizeof(int) );
    BRUH(sizeof(bool) );
    BRUH(sizeof(float) );
    BRUH(sizeof(Vector2) );
    BRUH(sizeof(Vector3) );
    BRUH(sizeof(Vector4) );
    BRUH(sizeof(SharedPtr<Moonlight::Texture>) );
    BRUH(sizeof(float[4]) );
    BRUH(sizeof(Matrix4) );

    ShaderEditor tool( flags );
    tool.Start();

    return 0;
}
