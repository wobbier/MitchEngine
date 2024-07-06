#define SDL_MAIN_HANDLED
#include "ShaderEditor.h"
#include "Math/Matrix4.h"

int main( int argc, char** argv )
{
    ToolCreationFlags flags;
    flags.isBorderless = false;
    flags.isDockingEnabled = true;
    flags.toolName = "Shader Editor";

    CLog::GetInstance().SetLogVerbosity( CLog::LogType::Warning );
    BRUH( std::to_string( sizeof( int ) ) );
    BRUH( std::to_string( sizeof( bool ) ) );
    BRUH( std::to_string( sizeof( float ) ) );
    BRUH( std::to_string( sizeof( Vector2 ) ) );
    BRUH( std::to_string( sizeof( Vector3 ) ) );
    BRUH( std::to_string( sizeof( Vector4 ) ) );
    BRUH( std::to_string( sizeof( SharedPtr<Moonlight::Texture> ) ) );
    BRUH( std::to_string( sizeof( float[4] ) ) );
    BRUH( std::to_string( sizeof( Matrix4 ) ) );

    ShaderEditor tool( flags );
    tool.Start();

    return 0;
}
