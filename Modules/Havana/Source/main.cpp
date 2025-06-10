#include "EditorApp.h"
#include "Engine/Engine.h"
#include <Widgets/AssetBrowser.h>
#include <Core/Memory.h>

int main( int argc, char** argv )
{
#if USING( ME_MEMORY_LEAK_TRACKER )
    int dbgFlags = _CrtSetDbgFlag( _CRTDBG_REPORT_FLAG );
    dbgFlags |= _CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF;
    _CrtSetDbgFlag( dbgFlags );
    _CrtSetBreakAlloc(162);
#endif  /* ME_MEMORY_LEAK_TRACKER */

    {
        std::string* lmaooooo = ME_NEW std::string( "lmfaoooooo" );

        for( int i = 0; i < argc; ++i )
        {
            std::printf( "argv[%d]: %s\n", i, argv[i] );
            if( std::string( argv[i] ) == "-CompileAssets" )
            {
                bgfx::Init init;
                init.resolution.width = static_cast<uint32_t>( 0 );
                init.resolution.height = static_cast<uint32_t>( 0 );
                init.resolution.reset = BGFX_RESET_VSYNC;

                bgfx::init( init );
                AssetBrowserWidget browser = AssetBrowserWidget( nullptr );
                browser.BuildAssets();
                return 0;
            }
        }
        EditorApp app( argc, argv );
        GetEngine().Init( &app );
        GetEngine().Run();
    }

#if USING( ME_MEMORY_LEAK_TRACKER )
    _CrtDumpMemoryLeaks();
#endif  /* ME_MEMORY_LEAK_TRACKER */

    return 0;
}
